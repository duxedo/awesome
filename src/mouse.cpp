/*
 * mouse.c - mouse managing
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

/** Manipulate and inspect the mouse cursor.
 *
 * The mouse buttons are represented as index. The common ones are:
 *
 * ![Client geometry](../images/mouse.svg)
 *
 * It is possible to be notified of mouse events by connecting to various
 * `client`, `widget`s and `wibox` signals:
 *
 *  * `mouse::enter`
 *  * `mouse::leave`
 *  * `mouse::press`
 *  * `mouse::release`
 *  * `mouse::move`
 *
 * It is also possible to add generic mouse button callbacks for `client`s,
 * `wiboxe`s and the `root` window. Those are set in the default `rc.lua` as such:
 *
 * **root**:
 *
 *    root.buttons(awful.util.table.join(
 *        awful.button({ }, 3, function () mymainmenu:toggle() end),
 *        awful.button({ }, 4, awful.tag.viewnext),
 *        awful.button({ }, 5, awful.tag.viewprev)
 *    ))
 *
 * **client**:
 *
 *    clientbuttons = awful.util.table.join(
 *        awful.button({ }, 1, function (c) client.focus = c; c:raise() end),
 *        awful.button({ modkey }, 1, awful.mouse.client.move),
 *        awful.button({ modkey }, 3, awful.mouse.client.resize)
 *    )
 *
 * See also `mousegrabber`
 *
 * @author Julien Danjou &lt;julien@danjou.info&gt;
 * @copyright 2008-2009 Julien Danjou
 * @inputmodule mouse
 */

#include "mouse.h"

#include "common/luaclass.h"
#include "common/lualib.h"
#include "common/util.h"
#include "common/xutil.h"
#include "globalconf.h"
#include "globals.h"
#include "math.h"
#include "objects/client.h"
#include "objects/drawin.h"
#include "objects/screen.h"

static Lua::FunctionRegistryIdx miss_index_handler;
static Lua::FunctionRegistryIdx miss_newindex_handler;

/**
 * The `screen` under the cursor
 * @property screen
 * @tparam screen|nil screen
 * @propertytype nil This will only happen if `screen` is set to `off` in the
 *  modeline or command line options. It happens very early in the initialization
 *  before the screens are created. If you check the screen from a signal, then
 *  you should never have to worry about this. Another corner case where this
 *  *might* happen is if you use `fake_resize` to have a smaller area than the
 *  physical screen.
 * @propertydefault It checks where the cursor is and match it to one of the
 *  screen `geometry`.
 */

/** Get the pointer position.
 * \param window The window to get position on.
 * \param x will be set to the Pointer-x-coordinate relative to window
 * \param y will be set to the Pointer-y-coordinate relative to window
 * \param child Will be set to the window under the pointer.
 * \param mask will be set to the current buttons state
 * \return true on success, false if an error occurred
 **/
bool mouse_query_pointer(
  xcb_window_t window, int16_t* x, int16_t* y, xcb_window_t* child, uint16_t* mask) {
    xcb_query_pointer_cookie_t query_ptr_c = getConnection().query_pointer_unchecked(window);
    auto query_ptr_r = getConnection().query_pointer_reply(query_ptr_c, NULL);

    if (!query_ptr_r || !query_ptr_r->same_screen) {
        return false;
    }

    *x = query_ptr_r->win_x;
    *y = query_ptr_r->win_y;

    if (mask) {
        *mask = query_ptr_r->mask;
    }
    if (child) {
        *child = query_ptr_r->child;
    }

    return true;
}

/** Get the pointer position on the screen.
 * \param x This will be set to the Pointer-x-coordinate relative to window.
 * \param y This will be set to the Pointer-y-coordinate relative to window.
 * \param child This will be set to the window under the pointer.
 * \param mask This will be set to the current buttons state.
 * \return True on success, false if an error occurred.
 */
static bool mouse_query_pointer_root(int16_t* x, int16_t* y, xcb_window_t* child, uint16_t* mask) {
    xcb_window_t root = Manager::get().screen->root;

    return mouse_query_pointer(root, x, y, child, mask);
}

/** Mouse library.
 * \param L The Lua VM state.
 * \return The number of elements pushed on stack.
 * \luastack
 * \lfield coords Mouse coordinates.
 * \lfield screen Mouse screen.
 */
static int luaA_mouse_index(lua_State* L) {
    auto attr = Lua::checkstring(L, 2);
    int16_t mouse_x, mouse_y;

    /* attr is not "screen"?! */
    if (attr != "screen") {
        if (miss_index_handler) {
            return Lua::call_handler(L, miss_index_handler);
        } else {
            return Lua::default_index(L);
        }
    }

    if (!mouse_query_pointer_root(&mouse_x, &mouse_y, NULL, NULL)) {
        /* Nothing ever handles mouse.screen being nil. Lying is better than
         * having lots of lua errors in this case.
         */
        if (Manager::get().focus.client) {
            luaA_object_push(L, Manager::get().focus.client->screen);
        } else {
            luaA_object_push(L, screen_get_primary());
        }
        return 1;
    }

    luaA_object_push(L, screen_getbycoord({mouse_x, mouse_y}));
    return 1;
}

/** Newindex for mouse.
 * \param L The Lua VM state.
 * \return The number of elements pushed on stack.
 */
static int luaA_mouse_newindex(lua_State* L) {
    auto attr = Lua::checkstring(L, 2);
    screen_t* screen;

    if (attr != "screen") {
        /* Call the lua mouse property handler */
        if (miss_newindex_handler) {
            return Lua::call_handler(L, miss_newindex_handler);
        } else {
            return Lua::default_newindex(L);
        }
    }

    screen = luaA_checkscreen(L, 3);
    getConnection().warp_pointer(Manager::get().screen->root, screen->geometry.top_left);
    return 0;
}

/** Push a table with mouse status.
 * \param L The Lua VM state.
 * \param x The x coordinate.
 * \param y The y coordinate.
 * \param mask The button mask.
 */
int luaA_mouse_pushstatus(lua_State* L, int x, int y, uint16_t mask) {
    lua_createtable(L, 0, 2);
    lua_pushinteger(L, x);
    lua_setfield(L, -2, "x");
    lua_pushinteger(L, y);
    lua_setfield(L, -2, "y");

    lua_createtable(L, 5, 0);

    int i = 1;

    for (uint16_t maski = XCB_BUTTON_MASK_1; maski <= XCB_BUTTON_MASK_5; maski <<= 1) {
        if (mask & maski) {
            lua_pushboolean(L, true);
        } else {
            lua_pushboolean(L, false);
        }
        lua_rawseti(L, -2, i++);
    }
    lua_setfield(L, -2, "buttons");
    return 1;
}

/* documented in lib/awful/mouse/init.lua */
static int luaA_mouse_coords(lua_State* L) {
    uint16_t mask;
    int x, y;
    int16_t mouse_x, mouse_y;

    if (lua_gettop(L) >= 1) {
        Lua::checktable(L, 1);
        bool ignore_enter_notify = (lua_gettop(L) == 2 && Lua::checkboolean(L, 2));

        if (!mouse_query_pointer_root(&mouse_x, &mouse_y, NULL, &mask)) {
            return 0;
        }

        x = round(
          Lua::getopt_number_range(L, 1, "x", mouse_x, MIN_X11_COORDINATE, MAX_X11_COORDINATE));
        y = round(
          Lua::getopt_number_range(L, 1, "y", mouse_y, MIN_X11_COORDINATE, MAX_X11_COORDINATE));

        if (ignore_enter_notify) {
            client_ignore_enterleave_events();
        }

        getConnection().warp_pointer(Manager::get().screen->root,
                                     {static_cast<int16_t>(x), static_cast<int16_t>(y)});

        if (ignore_enter_notify) {
            client_restore_enterleave_events();
        }

        lua_pop(L, 1);
    }

    if (!mouse_query_pointer_root(&mouse_x, &mouse_y, NULL, &mask)) {
        return 0;
    }

    return luaA_mouse_pushstatus(L, mouse_x, mouse_y, mask);
}

/** Get the client or any object which is under the pointer.
 *
 * @treturn client|wibox|nil A client, wibox or nil.
 * @staticfct object_under_pointer
 */
static int luaA_mouse_object_under_pointer(lua_State* L) {
    int16_t mouse_x, mouse_y;
    xcb_window_t child;

    if (!mouse_query_pointer_root(&mouse_x, &mouse_y, &child, NULL)) {
        return 0;
    }

    drawin_t* drawin;
    client* client;

    if ((drawin = drawin_getbywin(child))) {
        return luaA_object_push(L, drawin);
    }

    if ((client = client_getbyframewin(child))) {
        return luaA_object_push(L, client);
    }

    return 0;
}

/**
 * Add a custom property handler (getter).
 */
static int luaA_mouse_set_index_miss_handler(lua_State* L) {
    return Lua::registerfct(L, 1, &miss_index_handler);
}

/**
 * Add a custom property handler (setter).
 */
static int luaA_mouse_set_newindex_miss_handler(lua_State* L) {
    return Lua::registerfct(L, 1, &miss_newindex_handler);
}

const struct luaL_Reg awesome_mouse_methods[] = {
  {                  "__index",                     luaA_mouse_index},
  {               "__newindex",                  luaA_mouse_newindex},
  {                   "coords",                    luaA_mouse_coords},
  {     "object_under_pointer",      luaA_mouse_object_under_pointer},
  {   "set_index_miss_handler",    luaA_mouse_set_index_miss_handler},
  {"set_newindex_miss_handler", luaA_mouse_set_newindex_miss_handler},
  {                       NULL,                                 NULL}
};
const struct luaL_Reg awesome_mouse_meta[] = {
  {NULL, NULL}
};

// vim: filetype=c:expandtab:shiftwidth=4:tabstop=8:softtabstop=4:textwidth=80
