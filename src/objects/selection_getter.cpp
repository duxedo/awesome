/*
 * selection_getter.c - selection content getter
 *
 * Copyright © 2019 Uli Schlachter <psychon@znc.in>
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

#include "objects/selection_getter.h"

#include "common/atoms.h"
#include "common/luaclass.h"
#include "common/luaobject.h"
#include "globalconf.h"
#include "lua.h"

#define REGISTRY_GETTER_TABLE_INDEX "awesome_selection_getters"

struct selection_getter_t: public lua_object_t {
    /** Reference in the special table to this object */
    int ref;
    /** Window used for the transfer */
    xcb_window_t window;

    ~selection_getter_t() { getConnection().destroy_window(window); }
};

static lua_class_t selection_getter_class{
  "selection_getter",
  NULL,
  {[](auto* state) {
return static_cast<lua_object_t*>(newobj<selection_getter_t, selection_getter_class>(state));
}, destroyObject<selection_getter_t>,
    nullptr, Lua::class_index_miss_property,
    Lua::class_newindex_miss_property}
};

static int luaA_selection_getter_new(lua_State* L) {
    size_t name_length, target_length;
    selection_getter_t* selection;
    xcb_atom_t name_atom, target_atom;

    Lua::checktable(L, 2);
    lua_pushliteral(L, "selection");
    lua_gettable(L, 2);
    lua_pushliteral(L, "target");
    lua_gettable(L, 2);

    auto name = luaL_checklstring(L, -2, &name_length);
    auto target = luaL_checklstring(L, -1, &target_length);

    /* Create a selection object */
    selection = reinterpret_cast<selection_getter_t*>(selection_getter_class.alloc_object(L));
    selection->window = getConnection().generate_id();
    getConnection().create_window(Manager::get().screen->root_depth,
                                  selection->window,
                                  Manager::get().screen->root,
                                  {-1, -1, 1, 1},
                                  0,
                                  XCB_COPY_FROM_PARENT,
                                  Manager::get().screen->root_visual,
                                  0);

    /* Save it in the registry */
    lua_pushliteral(L, REGISTRY_GETTER_TABLE_INDEX);
    lua_rawget(L, LUA_REGISTRYINDEX);
    lua_pushvalue(L, -2);
    selection->ref = luaL_ref(L, -2);
    lua_pop(L, 1);

    /* Get the atoms identifying the request */
    auto c1 = getConnection().intern_atom_unchecked(false, name_length, name);
    auto c2 = getConnection().intern_atom_unchecked(false, target_length, target);

    auto reply = getConnection().intern_atom_reply(c1);
    name_atom = reply ? reply->atom : XCB_NONE;

    reply = getConnection().intern_atom_reply(c2);
    target_atom = reply ? reply->atom : XCB_NONE;

    getConnection().convert_selection(selection->window,
                                      name_atom,
                                      target_atom,
                                      AWESOME_SELECTION_ATOM,
                                      Manager::get().x.get_timestamp());

    return 1;
}

static void selection_transfer_finished(lua_State* L, int ud) {
    selection_getter_t* selection = reinterpret_cast<selection_getter_t*>(lua_touserdata(L, ud));

    /* Unreference the selection object; it's dead */
    lua_pushliteral(L, REGISTRY_GETTER_TABLE_INDEX);
    lua_rawget(L, LUA_REGISTRYINDEX);
    luaL_unref(L, -1, selection->ref);
    lua_pop(L, 1);

    selection->ref = LUA_NOREF;

    luaA_object_emit_signal(L, ud, "data_end", 0);
}

static void selection_push_data(lua_State* L, xcb_get_property_reply_t* property) {
    if (property->type == XCB_ATOM_ATOM && property->format == 32) {
        size_t num_atoms = xcb_get_property_value_length(property) / 4;
        xcb_atom_t* atoms = (xcb_atom_t*)xcb_get_property_value(property);
        xcb_get_atom_name_cookie_t cookies[num_atoms];

        for (size_t i = 0; i < num_atoms; i++) {
            cookies[i] = getConnection().get_atom_name_unchecked(atoms[i]);
        }

        lua_newtable(L);
        for (size_t i = 0; i < num_atoms; i++) {
            auto reply = getConnection().get_atom_name_reply(cookies[i]);
            if (reply) {
                lua_pushlstring(L,
                                xcb_get_atom_name_name(reply.get()),
                                xcb_get_atom_name_name_length(reply.get()));
                lua_rawseti(L, -2, i + 1);
            }
        }
    } else {
        lua_pushlstring(L,
                        (const char*)xcb_get_property_value(property),
                        xcb_get_property_value_length(property));
    }
}

static void selection_handle_selectionnotify(lua_State* L, int ud, xcb_atom_t property) {
    selection_getter_t* selection;

    ud = Lua::absindex(L, ud);
    selection = (selection_getter_t*)lua_touserdata(L, ud);

    if (property != XCB_NONE) {
        selection_transfer_finished(L, ud);
        return;
    }

    getConnection().change_attributes(
      selection->window, XCB_CW_EVENT_MASK, std::array{XCB_EVENT_MASK_PROPERTY_CHANGE});

    auto property_r = getConnection().get_property_reply(getConnection().get_property(
      true, selection->window, AWESOME_SELECTION_ATOM, XCB_GET_PROPERTY_TYPE_ANY, 0, 0xffffffff));

    if (!property_r) {
        selection_transfer_finished(L, ud);
        return;
    }

    if (property_r->type == INCR) {
        /* This is an incremental transfer. The above GetProperty had
         * delete=true. This indicates to the other end that the
         * transfer should start now. Right now we only get an estimate
         * of the size of the data to be transferred, which we ignore.
         */
        return;
    }
    selection_push_data(L, property_r.get());
    luaA_object_emit_signal(L, ud, "data", 1);
    selection_transfer_finished(L, ud);
}

static int selection_getter_find_by_window(lua_State* L, xcb_window_t window) {
    /* Iterate over all active selection getters */
    lua_pushliteral(L, REGISTRY_GETTER_TABLE_INDEX);
    lua_rawget(L, LUA_REGISTRYINDEX);
    lua_pushnil(L);
    while (lua_next(L, -2) != 0) {
        if (lua_type(L, -1) == LUA_TUSERDATA) {
            auto selection = (selection_getter_t*)lua_touserdata(L, -1);
            if (selection->window == window) {
                /* Found the right selection, remove table and key */
                lua_remove(L, -2);
                lua_remove(L, -2);
                return 1;
            }
        }
        /* Remove the value, leaving only the key */
        lua_pop(L, 1);
    }
    /* Remove the getter table */
    lua_pop(L, 1);

    return 0;
}

void property_handle_awesome_selection_atom(uint8_t state, xcb_window_t window) {
    lua_State* L = globalconf_get_lua_State();

    if (state != XCB_PROPERTY_NEW_VALUE) {
        return;
    }

    if (selection_getter_find_by_window(L, window) == 0) {
        return;
    }

    selection_getter_t* selection = (selection_getter_t*)lua_touserdata(L, -1);

    auto property_r = getConnection().get_property_reply(getConnection().get_property(
      true, selection->window, AWESOME_SELECTION_ATOM, XCB_GET_PROPERTY_TYPE_ANY, 0, 0xffffffff));

    if (property_r) {
        if (property_r->value_len > 0) {
            selection_push_data(L, property_r.get());
            luaA_object_emit_signal(L, -2, "data", 1);
        } else {
            /* Transfer finished */
            selection_transfer_finished(L, -1);
        }
    }

    lua_pop(L, 1);
}

void event_handle_selectionnotify(xcb_selection_notify_event_t* ev) {
    lua_State* L = globalconf_get_lua_State();

    if (selection_getter_find_by_window(L, ev->requestor) == 0) {
        return;
    }

    selection_handle_selectionnotify(L, -1, ev->property);
    lua_pop(L, 1);
}

void selection_getter_class_setup(lua_State* L) {
    static const struct luaL_Reg selection_getter_methods[] = {
      {"__call", luaA_selection_getter_new},
      {    NULL,                      NULL}
    };

    static constexpr auto meta = DefineObjectMethods();
    /* Store a table in the registry that tracks active getters. This code does
     * debug.getregistry(){REGISTRY_GETTER_TABLE_INDEX] = {}
     */
    lua_pushliteral(L, REGISTRY_GETTER_TABLE_INDEX);
    lua_newtable(L);
    lua_rawset(L, LUA_REGISTRYINDEX);

    selection_getter_class.setup(L, selection_getter_methods, meta.data());
}

// vim: filetype=c:expandtab:shiftwidth=4:tabstop=8:softtabstop=4:textwidth=80
