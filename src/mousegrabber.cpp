/*
 * mousegrabber.c - mouse pointer grabbing
 *
 * Copyright © 2008-2009 Julien Danjou <julien@danjou.info>
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

/** Set a callback to process all mouse events.
 * @author Julien Danjou &lt;julien@danjou.info&gt;
 * @copyright 2008-2009 Julien Danjou
 * @coreclassmod mousegrabber
 */

#include "mousegrabber.h"

#include "common/xcursor.h"
#include "globalconf.h"
#include "globals.h"
#include "mouse.h"

#include <stdbool.h>
#include <unistd.h>

/** Grab the mouse.
 * \param cursor The cursor to use while grabbing.
 * \return True if mouse was grabbed.
 */
static bool mousegrabber_grab(xcb_cursor_t cursor) {
    xcb_window_t root = Manager::get().screen->root;

    for (int i = 1000; i; i--) {
        auto grab_ptr_c = Manager::get().x.connection.grab_pointer_unchecked(
          false,
          root,
          XCB_EVENT_MASK_BUTTON_PRESS | XCB_EVENT_MASK_BUTTON_RELEASE |
            XCB_EVENT_MASK_POINTER_MOTION,
          XCB_GRAB_MODE_ASYNC,
          XCB_GRAB_MODE_ASYNC,
          root,
          cursor,
          XCB_CURRENT_TIME);

        if (Manager::get().x.connection.grab_pointer_reply(grab_ptr_c)) {
            return true;
        }
        usleep(1000);
    }
    return false;
}

/** Handle mouse motion events.
 * \param L Lua stack to push the pointer motion.
 * \param x The received mouse event x component.
 * \param y The received mouse event y component.
 * \param mask The received mouse event bit mask.
 */
void mousegrabber_handleevent(lua_State* L, int x, int y, uint16_t mask) {
    luaA_mouse_pushstatus(L, x, y, mask);
}

/** Grab the mouse pointer and list motions, calling callback function at each
 * motion. The callback function must return a boolean value: true to
 * continue grabbing, false to stop.
 * The function is called with one argument:
 * a table containing modifiers pointer coordinates.
 *
 * The list of valid cursors is:
 *
 *@DOC_cursor_c_COMMON@
 *
 *
 * @tparam function func A callback function as described above.
 * @tparam string|nil cursor The name of an X cursor to use while grabbing or `nil`
 * to not change the cursor.
 * @noreturn
 * @staticfct run
 */
static int luaA_mousegrabber_run(lua_State* L) {
    if (Manager::get().mousegrabber) {
        luaL_error(L, "mousegrabber already running");
    }

    xcb_cursor_t cursor = XCB_NONE;

    if (!lua_isnil(L, 2)) {
        uint16_t cfont = xcursor_font_fromstr(luaL_checkstring(L, 2));
        if (!cfont) {
            Lua::warn(L, "invalid cursor");
            return 0;
        }

        cursor = xcursor_new(Manager::get().x.cursor_ctx, cfont);
    }

    Lua::registerfct(L, 1, &(Manager::get().mousegrabber));

    if (!mousegrabber_grab(cursor)) {
        Lua::unregister(L, &(Manager::get().mousegrabber.idx));
        luaL_error(L, "unable to grab mouse pointer");
    }

    return 0;
}

/** Stop grabbing the mouse pointer.
 *
 * @staticfct stop
 * @noreturn
 */
int luaA_mousegrabber_stop(lua_State* L) {
    Manager::get().x.connection.ungrab_pointer();
    Lua::unregister(L, &Manager::get().mousegrabber);
    return 0;
}

/** Check if mousegrabber is running.
 *
 * @treturn boolean True if running, false otherwise.
 * @staticfct isrunning
 */
static int luaA_mousegrabber_isrunning(lua_State* L) {
    lua_pushboolean(L, Manager::get().mousegrabber.hasRef());
    return 1;
}

const struct luaL_Reg awesome_mousegrabber_lib[] = {
  {       "run",       luaA_mousegrabber_run},
  {      "stop",      luaA_mousegrabber_stop},
  { "isrunning", luaA_mousegrabber_isrunning},
  {   "__index",          Lua::default_index},
  {"__newindex",       Lua::default_newindex},
  {        NULL,                        NULL}
};

// vim: filetype=c:expandtab:shiftwidth=4:tabstop=8:softtabstop=4:textwidth=80
