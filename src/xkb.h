/*
 * xkb.h - Keyboard layout manager header
 *
 * Copyright © 2015 Aleksey Fedotov <lexa@cfotr.com>
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

#include "common/luahdr.h"

#include <xcb/xcb.h>

void event_handle_xkb_notify(xcb_generic_event_t* event);
void xkb_init(void);
void xkb_free(void);

extern "C" int luaA_xkb_set_layout_group(lua_State* L);
extern "C" int luaA_xkb_get_layout_group(lua_State* L);
extern "C" int luaA_xkb_get_group_names(lua_State* L);
