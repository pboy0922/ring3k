/*
 * GDI region objects. Shamelessly ripped out from the X11 distribution
 * Thanks for the nice licence.
 *
 * Copyright 1993, 1994, 1995 Alexandre Julliard
 * Modifications and additions: Copyright 1998 Huw Davies
 *					  1999 Alex Korobka
 *                              Copyright 2009 Mike McCormack
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

/************************************************************************

Copyright (c) 1987, 1988  X Consortium

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
X CONSORTIUM BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN
AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

Except as contained in this notice, the name of the X Consortium shall not be
used in advertising or otherwise to promote the sale, use or other dealings
in this Software without prior written authorization from the X Consortium.


Copyright 1987, 1988 by Digital Equipment Corporation, Maynard, Massachusetts.

			All Rights Reserved

Permission to use, copy, modify, and distribute this software and its
documentation for any purpose and without fee is hereby granted,
provided that the above copyright notice appear in all copies and that
both that copyright notice and this permission notice appear in
supporting documentation, and that the name of Digital not be
used in advertising or publicity pertaining to distribution of the
software without specific, written prior permission.

DIGITAL DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE, INCLUDING
ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO EVENT SHALL
DIGITAL BE LIABLE FOR ANY SPECIAL, INDIRECT OR CONSEQUENTIAL DAMAGES OR
ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS,
WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION,
ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS
SOFTWARE.

************************************************************************/
/*
 * The functions in this file implement the Region abstraction, similar to one
 * used in the X11 sample server. A Region is simply an area, as the name
 * implies, and is implemented as a "y-x-banded" array of rectangles. To
 * explain: Each Region is made up of a certain number of rectangles sorted
 * by y coordinate first, and then by x coordinate.
 *
 * Furthermore, the rectangles are banded such that every rectangle with a
 * given upper-left y coordinate (y1) will have the same lower-right y
 * coordinate (y2) and vice versa. If a rectangle has scanlines in a band, it
 * will span the entire vertical distance of the band. This means that some
 * areas that could be merged into a taller rectangle will be represented as
 * several shorter rectangles to account for shorter rectangles to its left
 * or right but within its "vertical scope".
 *
 * An added constraint on the rectangles is that they must cover as much
 * horizontal area as possible. E.g. no two rectangles in a band are allowed
 * to touch.
 *
 * Whenever possible, bands will be merged together to cover a greater vertical
 * distance (and thus reduce the number of rectangles). Two bands can be merged
 * only if the bottom of one touches the top of the other and they have
 * rectangles in the same places (of the same width, of course). This maintains
 * the y-x-banding that's so nice to have...
 */


#include "config.h"

#include <stdarg.h>
#include "ntstatus.h"
#define WIN32_NO_STATUS
#include "windef.h"
#include "winternl.h"

#include "ntcall.h"
#include "ntwin32.h"
#include "debug.h"
#include "win32mgr.h"

#define TRACE dprintf

#define RGN_DEFAULT_RECTS	2

struct WINEREGION {
    INT size;
    INT numRects;
    RECT *rects;
    RECT extents;
};

static inline void EMPTY_REGION(WINEREGION *pReg) {
    (pReg)->numRects = 0;
    (pReg)->extents.left = (pReg)->extents.top = 0;
    (pReg)->extents.right = (pReg)->extents.bottom = 0;
}

/***********************************************************************
 *            REGION_AllocWineRegion
 *            Create a new empty WINEREGION.
 */
static WINEREGION *REGION_AllocWineRegion( INT n )
{
    WINEREGION *pReg;

    if ((pReg = new WINEREGION))
    {
        if ((pReg->rects = new RECT[n]))
        {
            pReg->size = n;
            EMPTY_REGION(pReg);
            return pReg;
        }
        delete pReg;
    }
    return NULL;
}

static void REGION_DestroyWineRegion( WINEREGION* pReg )
{
    delete[] pReg->rects;
    delete pReg;
}

class region_tt : public gdi_object_t
{
	WINEREGION *rgn;
public:
	region_tt();
	~region_tt();
	static region_tt* alloc( INT n );
	void set_rect( int left, int top, int right, int bottom );
	INT get_region_box( RECT* rect );
	INT get_region_type();
	BOOL rect_equal( PRECT r1, PRECT r2 );
	BOOL equal( region_tt *other );
	INT offset( INT x, INT y );
};

region_tt::region_tt()
{
}

region_tt::~region_tt()
{
	REGION_DestroyWineRegion( rgn );
}

region_tt* region_tt::alloc( INT n )
{
	region_tt* region = new region_tt;
	if (!region)
		return NULL;
	region->rgn = REGION_AllocWineRegion( n );
	region->handle = alloc_gdi_handle( FALSE, GDI_OBJECT_REGION, 0, region );
	if (!region->handle)
	{
		delete region;
		return 0;
	}
	return region;
}

region_tt* region_from_handle( HGDIOBJ handle )
{
	gdi_handle_table_entry *entry = get_handle_table_entry( handle );
	if (!entry)
		return FALSE;
	if (entry->Type != GDI_OBJECT_REGION)
		return FALSE;
	assert( entry->kernel_info );
	return (region_tt*) entry->kernel_info;
}

void region_tt::set_rect( int left, int top, int right, int bottom )
{
    if (left > right) { INT tmp = left; left = right; right = tmp; }
    if (top > bottom) { INT tmp = top; top = bottom; bottom = tmp; }

    if((left != right) && (top != bottom))
    {
        rgn->rects->left = rgn->extents.left = left;
        rgn->rects->top = rgn->extents.top = top;
        rgn->rects->right = rgn->extents.right = right;
        rgn->rects->bottom = rgn->extents.bottom = bottom;
        rgn->numRects = 1;
    }
    else
	EMPTY_REGION(rgn);
}

INT region_tt::get_region_type()
{
	switch(rgn->numRects)
	{
	case 0:  return NULLREGION;
	case 1:  return SIMPLEREGION;
	default: return COMPLEXREGION;
	}
}

INT region_tt::get_region_box( RECT* rect )
{
	rect->left = rgn->extents.left;
	rect->top = rgn->extents.top;
	rect->right = rgn->extents.right;
	rect->bottom = rgn->extents.bottom;
	TRACE("%p (%ld,%ld-%ld,%ld)\n", get_handle(),
		rect->left, rect->top, rect->right, rect->bottom);
	return get_region_type();
}

BOOL region_tt::rect_equal( PRECT r1, PRECT r2 )
{
	return (r1->left == r2->left) &&
		(r1->right == r2->right) &&
		(r1->top == r2->top) &&
		(r1->bottom == r2->bottom);
}

BOOL region_tt::equal( region_tt *other )
{
	if (rgn->numRects != other->rgn->numRects)
		return FALSE;

	if (rgn->numRects == 0)
		return TRUE;

	if (!rect_equal( &rgn->extents, &other->rgn->extents ))
		return FALSE;

	for (int i = 0; i < rgn->numRects; i++ )
		if (!rect_equal(&rgn->rects[i], &other->rgn->rects[i]))
			return FALSE;

	return TRUE;
}

INT region_tt::offset( INT x, INT y )
{
	int nbox = rgn->numRects;
	RECT *pbox = rgn->rects;

	while (nbox--)
	{
		pbox->left += x;
		pbox->right += x;
		pbox->top += y;
		pbox->bottom += y;
		pbox++;
	}
	rgn->extents.left += x;
	rgn->extents.right += x;
	rgn->extents.top += y;
	rgn->extents.bottom += y;
	return get_region_type();
}

HRGN NTAPI NtGdiCreateRectRgn( int left, int top, int right, int bottom )
{
	region_tt* region = region_tt::alloc( RGN_DEFAULT_RECTS );
	if (!region)
		return 0;
	region->set_rect( left, top, right, bottom );
	return (HRGN) region->get_handle();
}

HRGN NTAPI NtGdiCreateEllipticRgn( int left, int top, int right, int bottom )
{
	return 0;
}

int NTAPI NtGdiGetRgnBox( HRGN Region, PRECT Rect )
{
	region_tt* region = region_from_handle( Region );
	if (!region)
		return ERROR;

	RECT box;
	int region_type = region->get_region_box( &box );

	NTSTATUS r;
	r = copy_to_user( Rect, &box, sizeof box );
	if (r < STATUS_SUCCESS)
		return ERROR;

	return region_type;
}

int NTAPI NtGdiCombineRgn( HRGN Dest, HRGN Source1, HRGN Source2, int CombineMode )
{
	return 0;
}

BOOL NTAPI NtGdiEqualRgn( HRGN Source1, HRGN Source2 )
{
	region_tt* rgn1 = region_from_handle( Source1 );
	if (!rgn1)
		return ERROR;

	region_tt* rgn2 = region_from_handle( Source2 );
	if (!rgn2)
		return ERROR;

	return rgn1->equal( rgn2 );
}

int NTAPI NtGdiOffsetRgn( HRGN Region, int x, int y )
{
	region_tt* region = region_from_handle( Region );
	if (!region)
		return ERROR;
	return region->offset( x, y );
}

BOOL NTAPI NtGdiSetRectRgn( HRGN Region, int left, int top, int right, int bottom )
{
	region_tt* region = region_from_handle( Region );
	if (!region)
		return ERROR;
	region->set_rect( left, top, right, bottom );
	return TRUE;
}