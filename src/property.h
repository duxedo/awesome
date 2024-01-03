/*
 * property.h - property handlers header
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

#pragma once

#include <xcb/xproto.h>
extern "C" {
#include <lua.h>
}
#include <compare>
#include <string>
struct client;

#define PROPERTY(funcname)                                        \
    xcb_get_property_cookie_t property_get_##funcname(client* c); \
    void property_update_##funcname(client* c, xcb_get_property_cookie_t cookie)

PROPERTY(wm_name);
PROPERTY(net_wm_name);
PROPERTY(wm_icon_name);
PROPERTY(net_wm_icon_name);
PROPERTY(wm_client_machine);
PROPERTY(wm_window_role);
PROPERTY(wm_transient_for);
PROPERTY(wm_client_leader);
PROPERTY(wm_normal_hints);
PROPERTY(wm_hints);
PROPERTY(wm_class);
PROPERTY(wm_protocols);
PROPERTY(net_wm_pid);
PROPERTY(net_wm_icon);
PROPERTY(motif_wm_hints);

#undef PROPERTY

void property_handle_propertynotify(xcb_property_notify_event_t* ev);
int luaA_register_xproperty(lua_State* L);
int luaA_set_xproperty(lua_State* L);
int luaA_get_xproperty(lua_State* L);

struct xproperty {
    xcb_atom_t atom;
    std::string name;
    enum {
        /* UTF8_STRING */
        PROP_STRING,
        /* CARDINAL */
        PROP_NUMBER,
        /* CARDINAL with values 0 and 1 (or "0 and != 0") */
        PROP_BOOLEAN
    } type;
    auto operator<=>(const xproperty& rhs) const { return atom <=> rhs.atom; }
};
