/*
 * selection_watcher.h - selection change watcher
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

#include "objects/selection_watcher.h"

#include "common/luaclass.h"
#include "common/luaobject.h"
#include "globalconf.h"
#include "lua.h"

#include <xcb/xfixes.h>

#define REGISTRY_WATCHER_TABLE_INDEX "awesome_selection_watchers"

struct selection_watcher_t: public lua_object_t {
    /** Is this watcher currently active and watching? Used as reference with luaL_ref */
    int active_ref;
    /** Atom identifying the selection to watch */
    xcb_atom_t selection;
    /** Window used for watching */
    xcb_window_t window;
};

static inline selection_watcher_t* selection_watcher_new(lua_State*);

static lua_class_t selection_watcher_class{
  "selection_watcher",
  NULL,
  {[](auto* state) { return static_cast<lua_object_t*>(selection_watcher_new(state)); },
    destroyObject<selection_watcher_t>,
    nullptr, Lua::class_index_miss_property,
    Lua::class_newindex_miss_property},
};

LUA_OBJECT_FUNCS(selection_watcher_class, selection_watcher_t, selection_watcher)

void event_handle_xfixes_selection_notify(xcb_generic_event_t* ev) {
    auto e = (xcb_xfixes_selection_notify_event_t*)ev;
    lua_State* L = globalconf_get_lua_State();

    /* Iterate over all active selection watchers */
    lua_pushliteral(L, REGISTRY_WATCHER_TABLE_INDEX);
    lua_rawget(L, LUA_REGISTRYINDEX);
    lua_pushnil(L);
    while (lua_next(L, -2) != 0) {
        if (lua_type(L, -1) == LUA_TUSERDATA) {
            selection_watcher_t* selection =
              reinterpret_cast<selection_watcher_t*>(lua_touserdata(L, -1));

            if (selection->selection == e->selection && selection->window == e->window) {
                lua_pushboolean(L, e->owner != XCB_NONE);
                luaA_object_emit_signal(L, -2, "selection_changed", 1);
            }
        }
        /* Remove the watcher */
        lua_pop(L, 1);
    }
    /* Remove watcher table */
    lua_pop(L, 1);
}

/** Create a new selection watcher object.
 * \param L The Lua VM state.
 * \return The number of elements pushed on the stack.
 */
static int luaA_selection_watcher_new(lua_State* L) {
    size_t name_length;
    const char* name;
    xcb_intern_atom_reply_t* reply;
    selection_watcher_t* selection;

    name = luaL_checklstring(L, 2, &name_length);
    selection = reinterpret_cast<selection_watcher_t*>(selection_watcher_class.alloc_object(L));
    selection->active_ref = LUA_NOREF;
    selection->window = XCB_NONE;

    /* Get the atom identifying the selection to watch */
    reply = xcb_intern_atom_reply(
      getGlobals().connection,
      xcb_intern_atom_unchecked(getGlobals().connection, false, name_length, name),
      NULL);
    if (reply) {
        selection->selection = reply->atom;
        p_delete(&reply);
    }

    return 1;
}

static int luaA_selection_watcher_set_active(lua_State* L, selection_watcher_t* selection) {
    bool b = Lua::checkboolean(L, -1);
    bool is_active = selection->active_ref != LUA_NOREF;
    if (b != is_active) {
        if (b) {
            /* Selection becomes active */

            /* Create a window for it */
            if (selection->window == XCB_NONE) {
                selection->window = getConnection().generate_id();
            }
            xcb_create_window(getGlobals().connection,
                              getGlobals().screen->root_depth,
                              selection->window,
                              getGlobals().screen->root,
                              -1,
                              -1,
                              1,
                              1,
                              0,
                              XCB_COPY_FROM_PARENT,
                              getGlobals().screen->root_visual,
                              0,
                              NULL);

            /* Start watching for selection changes */
            if (getGlobals().have_xfixes) {
                xcb_xfixes_select_selection_input(
                  getGlobals().connection,
                  selection->window,
                  selection->selection,
                  XCB_XFIXES_SELECTION_EVENT_MASK_SET_SELECTION_OWNER |
                    XCB_XFIXES_SELECTION_EVENT_MASK_SELECTION_WINDOW_DESTROY |
                    XCB_XFIXES_SELECTION_EVENT_MASK_SELECTION_CLIENT_CLOSE);
            } else {
                Lua::warn(
                  L, "X11 server does not support the XFixes extension; cannot watch selections");
            }

            /* Reference the selection watcher. For this, first get the tracking
             * table out of the registry. */
            lua_pushliteral(L, REGISTRY_WATCHER_TABLE_INDEX);
            lua_rawget(L, LUA_REGISTRYINDEX);

            /* Then actually get the reference */
            lua_pushvalue(L, -3 - 1);
            selection->active_ref = luaL_ref(L, -2);

            /* And pop the tracking table again */
            lua_pop(L, 1);
        } else {
            /* Stop watching and destroy the window */
            if (getGlobals().have_xfixes) {
                xcb_xfixes_select_selection_input(
                  getGlobals().connection, selection->window, selection->selection, 0);
            }
            xcb_destroy_window(getGlobals().connection, selection->window);

            /* Unreference the selection object */
            lua_pushliteral(L, REGISTRY_WATCHER_TABLE_INDEX);
            lua_rawget(L, LUA_REGISTRYINDEX);
            luaL_unref(L, -1, selection->active_ref);
            lua_pop(L, 1);

            selection->active_ref = LUA_NOREF;
        }
        luaA_object_emit_signal(L, -3, "property::active", 0);
    }
    return 0;
}

static int luaA_selection_watcher_get_active(lua_State* L, selection_watcher_t* selection) {
    lua_pushboolean(L, selection->active_ref != LUA_NOREF);
    return 1;
}

void selection_watcher_class_setup(lua_State* L) {
    static constexpr auto methods = DefineClassMethods<&selection_watcher_class>({
      {"__call", luaA_selection_watcher_new},
    });

    static constexpr auto meta = DefineObjectMethods();

    /* Reference a table in the registry that tracks active watchers. This code
     * does debug.getregistry()[REGISTRY_WATCHER_TABLE_INDEX] = {}
     */
    lua_pushliteral(L, REGISTRY_WATCHER_TABLE_INDEX);
    lua_newtable(L);
    lua_rawset(L, LUA_REGISTRYINDEX);

    selection_watcher_class.setup(L, methods.data(), meta.data());
    selection_watcher_class.add_property("active",
                                         (lua_class_propfunc_t)luaA_selection_watcher_set_active,
                                         (lua_class_propfunc_t)luaA_selection_watcher_get_active,
                                         (lua_class_propfunc_t)luaA_selection_watcher_set_active);
}

// vim: filetype=c:expandtab:shiftwidth=4:tabstop=8:softtabstop=4:textwidth=80
