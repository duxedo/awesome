/*
 * window.h - window object header
 *
 * Copyright © 2009 Julien Danjou <julien@danjou.info>
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

#include "color.h"
#include "common/luaclass.h"
#include "math.h"
#include "objects/button.h"
#include "strut.h"

/** Windows type */
typedef enum {
    WINDOW_TYPE_NORMAL = 0,
    WINDOW_TYPE_DESKTOP,
    WINDOW_TYPE_DOCK,
    WINDOW_TYPE_SPLASH,
    WINDOW_TYPE_DIALOG,
    /* The ones below may have TRANSIENT_FOR, but are not plain dialogs.
     * They were purposefully placed below DIALOG.
     */
    WINDOW_TYPE_MENU,
    WINDOW_TYPE_TOOLBAR,
    WINDOW_TYPE_UTILITY,
    /* This ones are usually set on override-redirect windows. */
    WINDOW_TYPE_DROPDOWN_MENU,
    WINDOW_TYPE_POPUP_MENU,
    WINDOW_TYPE_TOOLTIP,
    WINDOW_TYPE_NOTIFICATION,
    WINDOW_TYPE_COMBO,
    WINDOW_TYPE_DND
} window_type_t;

/** Window structure */
struct window_t: lua_object_t {
    /** The X window number */
    xcb_window_t window;
    /** The frame window, might be XCB_NONE */
    xcb_window_t frame_window;
    /** Opacity */
    double opacity = 1.0;
    /** Strut */
    strut_t strut;
    /** Button bindings */
    std::vector<button_t*> buttons;
    /** Do we have pending border changes? */
    bool border_need_update;
    /** Border color */
    color_t border_color;
    /** Border width */
    uint16_t border_width;
    /** The window type */
    window_type_t type;
    /** The border width callback */
    void (*border_width_callback)(void*, uint16_t old, uint16_t new_width);
};

void window_class_setup(lua_State*);

void window_set_opacity(lua_State*, int, double);
void window_set_border_width(lua_State*, int, int);
void window_border_refresh(window_t*);
int luaA_window_set_type(lua_State*, window_t*);
uint32_t window_translate_type(window_type_t);
int window_set_xproperty(lua_State*, xcb_window_t, int, int);
int window_get_xproperty(lua_State*, xcb_window_t, int);

namespace Lua {
template <>
struct Pusher<window_type_t> {
    int push(State& L, const window_type_t t);
};
}
