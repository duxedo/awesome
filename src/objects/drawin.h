/*
 * drawin.h - drawin functions header
 *
 * Copyright © 2007-2009 Julien Danjou <julien@danjou.info>
 * Copyright ©      2010 Uli Schlachter <psychon@znc.in>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 */
#pragma once

#include "objects/drawable.h"
#include "objects/window.h"

/** Drawin type */
struct drawin_t: public window_t {
    /** Ontop */
    bool ontop;
    /** Visible */
    bool visible;
    /** Cursor */
    std::string cursor;
    /** The drawable for this drawin. */
    drawable_t* drawable;
    /** The window geometry. */
    area_t geometry;
    /** Do we have a pending geometry change that still needs to be applied? */
    bool geometry_dirty;

    auto x() const { return geometry.left(); }
    auto y() const { return geometry.top(); }
    auto w() const { return geometry.width; }
    auto h() const { return geometry.height; }

    ~drawin_t();
};

drawin_t* drawin_getbywin(xcb_window_t);
void drawin_refresh_pixmap_partial(drawin_t*, int16_t, int16_t, uint16_t, uint16_t);
void luaA_drawin_systray_kickout(lua_State*);

void drawin_class_setup(lua_State*);

extern lua_class_t drawin_class;
