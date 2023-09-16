/*
 * xcursor.c - X cursors management
 *
 * Copyright © 2008 Julien Danjou <julien@danjou.info>
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

#include "common/xcursor.h"

/** Equivalent to 'XCreateFontCursor()', error are handled by the
 * default current error handler.
 * \param ctx The xcb-cursor context.
 * \param cursor_font Type of cursor to use.
 * \return Allocated cursor font.
 */
xcb_cursor_t
xcursor_new(CursorMap *map, xcb_cursor_context_t *ctx, const char *cursor_name)
{
    auto it = map->find(cursor_name);

    if (it!=map->end())
    {
        return it->second;
    }

    auto cursor = xcb_cursor_load_cursor(ctx, cursor_name);
    map->insert_or_assign(cursor_name, cursor);

    return cursor;
}


// vim: filetype=c:expandtab:shiftwidth=4:tabstop=8:softtabstop=4:textwidth=80
