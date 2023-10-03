/*
 * button.h - button header
 *
 * Copyright © 2007-2009 Julien Danjou <julien@danjou.info>
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

#include "common/luaclass.h"
#include "common/luaobject.h"
#include "globalconf.h"

#include <stdint.h>
#include <vector>
#include <xcb/xcb.h>

/** Mouse buttons bindings */
struct button_t {
    LUA_OBJECT_HEADER
    /** Key modifiers */
    uint16_t modifiers;
    /** Mouse button number */
    xcb_button_t button;

    button_t() = default;
    button_t(button_t&&) = default;
    button_t& operator=(button_t&&) = default;
    button_t(const button_t&) = delete;
    button_t& operator=(const button_t&) = delete;
};

extern lua_class_t button_class;
LUA_OBJECT_FUNCS(button_class, button_t, button)

int luaA_button_array_get(lua_State*, int, const std::vector<button_t*>&);
void luaA_button_array_set(lua_State*, int, int, std::vector<button_t*>*);
void button_class_setup(lua_State*);