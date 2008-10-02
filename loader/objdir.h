/*
 * nt loader
 *
 * Copyright 2006-2008 Mike McCormack
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

#ifndef __NTNATIVE_OBJDIR_H__
#define __NTNATIVE_OBJDIR_H__

#include "object.h"

class object_dir_t : public object_t {
protected:
	friend class object_t;
public:
	object_dir_t();
	virtual ~object_dir_t();
	virtual bool access_allowed( ACCESS_MASK access, ACCESS_MASK handle_access ) = 0;
};

#endif // __NTNATIVE_OBJDIR_H__
