/*
 * selection_transfer.c - objects for selection transfer header
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

#include "objects/selection_transfer.h"

#include "common/atoms.h"
#include "common/luaclass.h"
#include "common/lualib.h"
#include "common/luaobject.h"
#include "common/util.h"
#include "globalconf.h"
#include "lua.h"

#include <cstdint>

#define REGISTRY_TRANSFER_TABLE_INDEX "awesome_selection_transfers"
#define TRANSFER_DATA_INDEX "data_for_next_chunk"

enum transfer_state {
    TRANSFER_WAIT_FOR_DATA,
    TRANSFER_INCREMENTAL_SENDING,
    TRANSFER_INCREMENTAL_DONE,
    TRANSFER_DONE
};

struct selection_transfer_t: public lua_object_t {
    /** Reference in the special table to this object */
    int ref;
    /* Information from the xcb_selection_request_event_t */
    xcb_window_t requestor;
    xcb_atom_t selection;
    xcb_atom_t target;
    xcb_atom_t property;
    xcb_timestamp_t time;
    /* Current state of the transfer */
    enum transfer_state state;
    /* Offset into TRANSFER_DATA_INDEX for the next chunk of data */
    size_t offset;
    /* Can there be more data coming from Lua? */
    bool more_data;
};

static inline selection_transfer_t* selection_transfer_new(lua_State*);

static bool selection_transfer_checker(selection_transfer_t* transfer) {
    return transfer->state != TRANSFER_DONE;
}

static lua_class_t selection_transfer_class{
  "selection_transfer",
  NULL,
  {
    [](auto* state) { return static_cast<lua_object_t*>(selection_transfer_new(state)); },
    destroyObject<selection_transfer_t>,
    [](auto* obj) { return selection_transfer_checker(static_cast<selection_transfer_t*>(obj)); },
    Lua::class_index_miss_property,
    Lua::class_newindex_miss_property,
    }
};

LUA_OBJECT_FUNCS(selection_transfer_class, selection_transfer_t, selection_transfer)

static size_t max_property_length(void) {
    uint32_t max_request_length = xcb_get_maximum_request_length(getGlobals().connection);
    max_request_length = MIN(max_request_length, (1 << 16) - 1);
    return max_request_length * 4 - sizeof(xcb_change_property_request_t);
}

static void selection_transfer_notify(xcb_window_t requestor,
                                      xcb_atom_t selection,
                                      xcb_atom_t target,
                                      xcb_atom_t property,
                                      xcb_timestamp_t time) {
    xcb_selection_notify_event_t ev;

    p_clear(&ev, 1);
    ev.response_type = XCB_SELECTION_NOTIFY;
    ev.requestor = requestor;
    ev.selection = selection;
    ev.target = target;
    ev.property = property;
    ev.time = time;

    xcb_send_event(getGlobals().connection, false, requestor, XCB_EVENT_MASK_NO_EVENT, (char*)&ev);
}

void selection_transfer_reject(xcb_window_t requestor,
                               xcb_atom_t selection,
                               xcb_atom_t target,
                               xcb_timestamp_t time) {
    selection_transfer_notify(requestor, selection, target, XCB_NONE, time);
}

static void transfer_done(lua_State* L, selection_transfer_t* transfer) {
    transfer->state = TRANSFER_DONE;

    lua_pushliteral(L, REGISTRY_TRANSFER_TABLE_INDEX);
    lua_rawget(L, LUA_REGISTRYINDEX);
    luaL_unref(L, -1, transfer->ref);
    transfer->ref = LUA_NOREF;
    lua_pop(L, 1);
}

static void transfer_continue_incremental(lua_State* L, int ud) {
    const char* data;
    size_t data_length;
    auto transfer = selection_transfer_class.checkudata<selection_transfer_t>(L, ud);

    ud = Lua::absindex(L, ud);

    /* Get the data that is to be sent next */
    Lua::getuservalue(L, ud);
    lua_pushliteral(L, TRANSFER_DATA_INDEX);
    lua_rawget(L, -2);
    lua_remove(L, -2);

    data = luaL_checklstring(L, -1, &data_length);
    if (transfer->offset == data_length) {
        if (transfer->more_data) {
            /* Request the next piece of data from Lua */
            transfer->state = TRANSFER_INCREMENTAL_DONE;
            luaA_object_emit_signal(L, ud, "continue", 0);
            if (transfer->state != TRANSFER_INCREMENTAL_DONE) {
                /* Lua gave us more data to send. */
                lua_pop(L, 1);
                return;
            }
        }
        /* End of transfer */
        getConnection().replace_property(
          transfer->requestor, transfer->property, UTF8_STRING, std::span("", 0));
        getConnection().clear_attributes(transfer->requestor, XCB_CW_EVENT_MASK);
        transfer_done(L, transfer);
    } else {
        /* Send next piece of data */
        assert(transfer->offset < data_length);
        size_t next_length = MIN(data_length - transfer->offset, max_property_length());
        getConnection().replace_property(transfer->requestor,
                                         transfer->property,
                                         UTF8_STRING,
                                         std::span(data + transfer->offset, next_length));
        transfer->offset += next_length;
    }
    lua_pop(L, 1);
}

void selection_transfer_begin(lua_State* L,
                              int ud,
                              xcb_window_t requestor,
                              xcb_atom_t selection,
                              xcb_atom_t target,
                              xcb_atom_t property,
                              xcb_timestamp_t time) {
    ud = Lua::absindex(L, ud);

    /* Allocate a transfer object */
    auto transfer = (selection_transfer_t*)selection_transfer_class.alloc_object(L);
    transfer->requestor = requestor;
    transfer->selection = selection;
    transfer->target = target;
    transfer->property = property;
    transfer->time = time;
    transfer->state = TRANSFER_WAIT_FOR_DATA;

    /* Save the object in the registry */
    lua_pushliteral(L, REGISTRY_TRANSFER_TABLE_INDEX);
    lua_rawget(L, LUA_REGISTRYINDEX);
    lua_pushvalue(L, -2);
    transfer->ref = luaL_ref(L, -2);
    lua_pop(L, 1);

    /* Get the atom name */
    xcb_get_atom_name_reply_t* reply = xcb_get_atom_name_reply(
      getGlobals().connection, xcb_get_atom_name_unchecked(getGlobals().connection, target), NULL);
    if (reply) {
        lua_pushlstring(L, xcb_get_atom_name_name(reply), xcb_get_atom_name_name_length(reply));
        p_delete(&reply);
    } else {
        lua_pushnil(L);
    }

    /* Emit the request signal with target and transfer object */
    lua_pushvalue(L, -2);
    luaA_object_emit_signal(L, ud, "request", 2);

    /* Reject the transfer if Lua did not do anything */
    if (transfer->state == TRANSFER_WAIT_FOR_DATA) {
        selection_transfer_reject(requestor, selection, target, time);
        transfer_done(L, transfer);
    }

    /* Remove the transfer object from the stack */
    lua_pop(L, 1);
}

static int luaA_selection_transfer_send(lua_State* L) {
    size_t data_length;
    bool incr = false;
    uint32_t incr_size = 0;

    auto transfer = selection_transfer_class.checkudata<selection_transfer_t>(L, 1);
    if (transfer->state != TRANSFER_WAIT_FOR_DATA && transfer->state != TRANSFER_INCREMENTAL_DONE) {
        luaL_error(L, "Transfer object is not ready for more data to be sent");
    }

    Lua::checktable(L, 2);

    lua_pushliteral(L, "continue");
    lua_rawget(L, 2);
    transfer->more_data = incr = lua_toboolean(L, -1);
    if (incr && lua_isnumber(L, -1)) {
        incr_size = lua_tonumber(L, -1);
    }
    lua_pop(L, 1);

    if (transfer->state == TRANSFER_INCREMENTAL_DONE) {
        /* Save the data on the transfer object */
        lua_pushliteral(L, "data");
        lua_rawget(L, 2);

        Lua::getuservalue(L, 1);
        lua_pushliteral(L, TRANSFER_DATA_INDEX);
        lua_pushvalue(L, -3);
        lua_rawset(L, -3);
        lua_pop(L, 1);

        /* Continue the incremental transfer */
        transfer->state = TRANSFER_INCREMENTAL_SENDING;
        transfer->offset = 0;

        transfer_continue_incremental(L, 1);

        return 0;
    }

    /* Get format and data from the table */
    lua_pushliteral(L, "format");
    lua_rawget(L, 2);
    lua_pushliteral(L, "data");
    lua_rawget(L, 2);

    if (lua_isstring(L, -2)) {
        auto format_string = Lua::checkstring(L, -2);
        if (format_string != "atom") {
            luaL_error(L, "Unknown format '%s'", format_string);
        }
        if (incr) {
            luaL_error(L, "Cannot transfer atoms in pieces");
        }

        /* 'data' is a table with strings */
        size_t len = Lua::rawlen(L, -1);

        /* Get an array with atoms */
        auto* atom_lengths = p_alloca(size_t, len);
        auto* atom_strings = p_alloca(const char*, len);
        for (size_t i = 0; i < len; i++) {
            lua_rawgeti(L, -1, i + 1);
            atom_strings[i] = luaL_checklstring(L, -1, &atom_lengths[i]);
            lua_pop(L, 1);
        }

        auto* cookies = p_alloca(xcb_intern_atom_cookie_t, len);
        auto* atoms = p_alloca(xcb_atom_t, len);
        for (size_t i = 0; i < len; i++) {
            cookies[i] = xcb_intern_atom_unchecked(
              getGlobals().connection, false, atom_lengths[i], atom_strings[i]);
        }
        for (size_t i = 0; i < len; i++) {
            xcb_intern_atom_reply_t* reply =
              xcb_intern_atom_reply(getGlobals().connection, cookies[i], NULL);
            atoms[i] = reply ? reply->atom : XCB_NONE;
            p_delete(&reply);
        }
        getConnection().replace_property(
          transfer->requestor, transfer->property, XCB_ATOM_ATOM, std::span(atoms, len));
    } else {
        /* 'data' is a string with the data to transfer */
        const char* data = luaL_checklstring(L, -1, &data_length);

        if (!incr) {
            incr_size = data_length;
        }

        if (data_length >= max_property_length()) {
            incr = true;
        }

        if (incr) {
            getConnection().change_attributes(
              transfer->requestor, XCB_CW_EVENT_MASK, std::array{XCB_EVENT_MASK_PROPERTY_CHANGE});
            getConnection().replace_property(
              transfer->requestor, transfer->property, INCR, incr_size);

            /* Save the data on the transfer object */
            Lua::getuservalue(L, 1);
            lua_pushliteral(L, TRANSFER_DATA_INDEX);
            lua_pushvalue(L, -3);
            lua_rawset(L, -3);
            lua_pop(L, 1);

            transfer->state = TRANSFER_INCREMENTAL_SENDING;
            transfer->offset = 0;
        } else {
            getConnection().replace_property(
              transfer->requestor, transfer->property, UTF8_STRING, std::span(data, data_length));
        }
    }

    selection_transfer_notify(transfer->requestor,
                              transfer->selection,
                              transfer->target,
                              transfer->property,
                              transfer->time);
    if (!incr) {
        transfer_done(L, transfer);
    }

    return 0;
}

void selection_transfer_handle_propertynotify(xcb_property_notify_event_t* ev) {
    lua_State* L = globalconf_get_lua_State();

    if (ev->state != XCB_PROPERTY_DELETE) {
        return;
    }

    /* Iterate over all active selection acquire objects */
    lua_pushliteral(L, REGISTRY_TRANSFER_TABLE_INDEX);
    lua_rawget(L, LUA_REGISTRYINDEX);
    lua_pushnil(L);
    while (lua_next(L, -2) != 0) {
        if (lua_type(L, -1) == LUA_TUSERDATA) {
            auto transfer = (selection_transfer_t*)lua_touserdata(L, -1);
            if (transfer->state == TRANSFER_INCREMENTAL_SENDING &&
                transfer->requestor == ev->window && transfer->property == ev->atom) {
                transfer_continue_incremental(L, -1);
                /* Remove table, key and transfer object */
                lua_pop(L, 3);
                return;
            }
        }
        /* Remove the value, leaving only the key */
        lua_pop(L, 1);
    }
    /* Remove the table */
    lua_pop(L, 1);
}

void selection_transfer_class_setup(lua_State* L) {
    static const struct luaL_Reg methods[] = {
      {NULL, NULL}
    };

    static constexpr auto meta = DefineObjectMethods({
      {"send", luaA_selection_transfer_send},
    });

    /* Store a table in the registry that tracks active selection_transfer_t. */
    lua_pushliteral(L, REGISTRY_TRANSFER_TABLE_INDEX);
    lua_newtable(L);
    lua_rawset(L, LUA_REGISTRYINDEX);

    selection_transfer_class.setup(L, methods, meta.data());
}

// vim: filetype=c:expandtab:shiftwidth=4:tabstop=8:softtabstop=4:textwidth=80
