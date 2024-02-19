/*
 * xkb.c - keyboard layout control functions
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

/**
 * @module awesome
 */

#include "xkb.h"

#include "common/atoms.h"
#include "common/util.h"
#include "globalconf.h"
#include "objects/client.h"
#include "xwindow.h"

#include <xkbcommon/xkbcommon-x11.h>
#include <xkbcommon/xkbcommon.h>

/**
 * Switch keyboard layout.
 *
 * @staticfct xkb_set_layout_group
 * @tparam integer num Keyboard layout number, integer from 0 to 3
 * @noreturn
 */
int luaA_xkb_set_layout_group(lua_State* L) {
    unsigned group = luaL_checkinteger(L, 1);
    getConnection().xkb().latch_lock_state(XCB_XKB_ID_USE_CORE_KBD, 0, 0, true, group, 0, 0, 0);
    return 0;
}

/**
 * Get current layout number.
 *
 * @staticfct xkb_get_layout_group
 * @treturn integer num Current layout number, integer from 0 to 3.
 */
int luaA_xkb_get_layout_group(lua_State* L) {
    auto state_c = getConnection().xkb().get_state_unchecked(XCB_XKB_ID_USE_CORE_KBD);
    auto state_r = getConnection().xkb().get_state_reply(state_c);
    if (!state_r) {
        return 0;
    }
    lua_pushinteger(L, state_r->group);
    return 1;
}

/**
 * Get layout short names.
 *
 * @staticfct xkb_get_group_names
 * @treturn string A string describing the current layout settings,
 *   e.g.: 'pc+us+de:2+inet(evdev)+group(alt_shift_toggle)+ctrl(nocaps)'
 */
int luaA_xkb_get_group_names(lua_State* L) {
    auto name_c = getConnection().xkb().get_names_unchecked(XCB_XKB_ID_USE_CORE_KBD,
                                                            XCB_XKB_NAME_DETAIL_SYMBOLS);
    auto name_r = getConnection().xkb().get_names_reply(name_c);

    if (!name_r) {
        Lua::warn(L, "Failed to get xkb symbols name");
        return 0;
    }

    xcb_xkb_get_names_value_list_t name_list;
    void* buffer = xcb_xkb_get_names_value_list(name_r.get());
    xcb_xkb_get_names_value_list_unpack(buffer,
                                        name_r->nTypes,
                                        name_r->indicators,
                                        name_r->virtualMods,
                                        name_r->groupNames,
                                        name_r->nKeys,
                                        name_r->nKeyAliases,
                                        name_r->nRadioGroups,
                                        name_r->which,
                                        &name_list);

    auto atom_name_c = getConnection().get_atom_name_unchecked(name_list.symbolsName);
    auto atom_name_r = getConnection().get_atom_name_reply(atom_name_c);
    if (!atom_name_r) {
        Lua::warn(L, "Failed to get atom symbols name");
        return 0;
    }

    const char* name = xcb_get_atom_name_name(atom_name_r.get());
    size_t name_len = xcb_get_atom_name_name_length(atom_name_r.get());
    lua_pushlstring(L, name, name_len);

    return 1;
}

static bool fill_rmlvo_from_root(struct xkb_rule_names* xkb_names) {
    auto prop_reply = getConnection().get_property_reply(
      getConnection().get_property_unchecked(false,
                                             Manager::get().screen->root,
                                             _XKB_RULES_NAMES,
                                             XCB_GET_PROPERTY_TYPE_ANY,
                                             0,
                                             UINT_MAX));
    if (!prop_reply) {
        return false;
    }

    if (prop_reply->value_len == 0) {
        return false;
    }

    const char* walk = (const char*)xcb_get_property_value(prop_reply.get());
    unsigned int remaining = xcb_get_property_value_length(prop_reply.get());
    for (int i = 0; i < 5 && remaining > 0; i++) {
        const int len = strnlen(walk, remaining);
        switch (i) {
        case 0: xkb_names->rules = strndup(walk, len); break;
        case 1: xkb_names->model = strndup(walk, len); break;
        case 2: xkb_names->layout = strndup(walk, len); break;
        case 3: xkb_names->variant = strndup(walk, len); break;
        case 4: xkb_names->options = strndup(walk, len); break;
        }
        remaining -= len + 1;
        walk = &walk[len + 1];
    }

    return true;
}

/** Fill globalconf.xkb_state based on connection and context
 */
static void xkb_fill_state(void) {
    xcb_connection_t* conn = getConnection().getConnection();

    int32_t device_id = xkb_x11_get_core_keyboard_device_id(conn);

    if (device_id != -1) {
        struct xkb_keymap* xkb_keymap = xkb_x11_keymap_new_from_device(
          Manager::get().xkb_ctx, conn, device_id, XKB_KEYMAP_COMPILE_NO_FLAGS);

        if (!xkb_keymap) {
            log_fatal("Failed while getting XKB keymap from device");
        }

        Manager::get().xkb_state = xkb_x11_state_new_from_device(xkb_keymap, conn, device_id);
        if (!Manager::get().xkb_state) {
            log_fatal("Failed while getting XKB state from device");
        }

        /* xkb_keymap is no longer referenced directly; decreasing refcount */
        xkb_keymap_unref(xkb_keymap);
    } else {
        log_warn("Failed while getting XKB device id");
        struct xkb_rule_names names = {NULL, NULL, NULL, NULL, NULL};
        if (!fill_rmlvo_from_root(&names)) {
            log_warn("Could not get _XKB_RULES_NAMES from root window, falling back to defaults.");
        }

        struct xkb_keymap* xkb_keymap = xkb_keymap_new_from_names(
          Manager::get().xkb_ctx, &names, (enum xkb_keymap_compile_flags)0);

        Manager::get().xkb_state = xkb_state_new(xkb_keymap);
        if (!Manager::get().xkb_state) {
            log_fatal("Failed while creating XKB state");
        }

        /* xkb_keymap is no longer referenced directly; decreasing refcount */
        xkb_keymap_unref(xkb_keymap);
        p_delete(&names.rules);
        p_delete(&names.model);
        p_delete(&names.layout);
        p_delete(&names.variant);
        p_delete(&names.options);
    }
}

/** Loads xkb context, state and keymap to globalconf.
 * These variables should be freed by xkb_free_keymap() afterwards
 */
static void xkb_init_keymap(void) {
    Manager::get().xkb_ctx = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    if (!Manager::get().xkb_ctx) {
        log_fatal("Failed while getting XKB context");
    }

    xkb_fill_state();
}

/** Frees xkb context, state and keymap from globalconf.
 * This should be used when these variables will not be used anymore
 */
static void xkb_free_keymap(void) {
    xkb_state_unref(Manager::get().xkb_state);
    xkb_context_unref(Manager::get().xkb_ctx);
}

/** Rereads the state of keyboard from X.
 * This call should be used after receiving NewKeyboardNotify or MapNotify,
 * as written in http://xkbcommon.org/doc/current/group__x11.html
 */
static void xkb_reload_keymap(void) {
    xkb_state_unref(Manager::get().xkb_state);
    xkb_fill_state();

    /* Free and then allocate the key symbols */
    Manager::get().input.keysyms = getConnection().key_symbols_alloc();

    /* Regrab key bindings on the root window */
    xcb_screen_t* s = Manager::get().screen;
    xwindow_grabkeys(s->root, Manager::get().keys);

    /* Regrab key bindings on clients */
    for (auto* c : Manager::get().clients) {
        xwindow_grabkeys(c->window, c->keys);
        if (c->nofocus_window) {
            xwindow_grabkeys(c->nofocus_window, c->keys);
        }
    }
}

static gboolean xkb_refresh(gpointer unused) {
    lua_State* L = globalconf_get_lua_State();

    Manager::get().xkb_update_pending = false;
    if (Manager::get().xkb_reload_keymap) {
        xkb_reload_keymap();
    }
    if (Manager::get().xkb_map_changed) {
        signal_object_emit(L, &Lua::global_signals, "xkb::map_changed", 0);
    }
    if (Manager::get().xkb_group_changed) {
        signal_object_emit(L, &Lua::global_signals, "xkb::group_changed", 0);
    }

    Manager::get().xkb_reload_keymap = false;
    Manager::get().xkb_map_changed = false;
    Manager::get().xkb_group_changed = false;

    return G_SOURCE_REMOVE;
}

static void xkb_schedule_refresh(void) {
    if (Manager::get().xkb_update_pending) {
        return;
    }
    Manager::get().xkb_update_pending = true;
    g_idle_add_full(G_PRIORITY_LOW, xkb_refresh, NULL, NULL);
}

/** The xkb notify event handler.
 * \param event The event.
 */
void event_handle_xkb_notify(xcb_generic_event_t* event) {
    /* The pad0 field of xcb_generic_event_t contains the event sub-type,
     * unfortunately xkb doesn't provide a usable struct for getting this in a
     * nicer way*/
    switch (event->pad0) {
    case XCB_XKB_NEW_KEYBOARD_NOTIFY: {
        auto new_keyboard_event = (xcb_xkb_new_keyboard_notify_event_t*)event;

        Manager::get().xkb_reload_keymap = true;

        if (new_keyboard_event->changed & XCB_XKB_NKN_DETAIL_KEYCODES) {
            Manager::get().xkb_map_changed = true;
        }
        xkb_schedule_refresh();
        break;
    }
    case XCB_XKB_MAP_NOTIFY: {
        Manager::get().xkb_reload_keymap = true;
        Manager::get().xkb_map_changed = true;
        xkb_schedule_refresh();
        break;
    }
    case XCB_XKB_STATE_NOTIFY: {
        xcb_xkb_state_notify_event_t* state_notify_event = (xcb_xkb_state_notify_event_t*)event;

        xkb_state_update_mask(Manager::get().xkb_state,
                              state_notify_event->baseMods,
                              state_notify_event->latchedMods,
                              state_notify_event->lockedMods,
                              state_notify_event->baseGroup,
                              state_notify_event->latchedGroup,
                              state_notify_event->lockedGroup);

        if (state_notify_event->changed & XCB_XKB_STATE_PART_GROUP_STATE) {
            Manager::get().xkb_group_changed = true;
            xkb_schedule_refresh();
        }

        break;
    }
    }
}

/** Initialize XKB support
 * This call allocates resources, that should be freed by calling xkb_free()
 */
void xkb_init(void) {
    Manager::get().xkb_update_pending = false;
    Manager::get().xkb_reload_keymap = false;
    Manager::get().xkb_map_changed = false;
    Manager::get().xkb_group_changed = false;

    int success_xkb = xkb_x11_setup_xkb_extension(getConnection().getConnection(),
                                                  XKB_X11_MIN_MAJOR_XKB_VERSION,
                                                  XKB_X11_MIN_MINOR_XKB_VERSION,
                                                  XKB_X11_SETUP_XKB_EXTENSION_NO_FLAGS,
                                                  NULL,
                                                  NULL,
                                                  NULL,
                                                  NULL);

    if (!success_xkb) {
        log_fatal("XKB not found or not supported");
        xkb_init_keymap();
        return;
    }

    uint16_t map = XCB_XKB_EVENT_TYPE_STATE_NOTIFY | XCB_XKB_EVENT_TYPE_MAP_NOTIFY |
                   XCB_XKB_EVENT_TYPE_NEW_KEYBOARD_NOTIFY;

    //
    // These maps are provided to allow key remapping,
    // that could be used in awesome
    //
    uint16_t map_parts = XCB_XKB_MAP_PART_KEY_TYPES | XCB_XKB_MAP_PART_KEY_SYMS |
                         XCB_XKB_MAP_PART_MODIFIER_MAP | XCB_XKB_MAP_PART_EXPLICIT_COMPONENTS |
                         XCB_XKB_MAP_PART_KEY_ACTIONS | XCB_XKB_MAP_PART_KEY_BEHAVIORS |
                         XCB_XKB_MAP_PART_VIRTUAL_MODS | XCB_XKB_MAP_PART_VIRTUAL_MOD_MAP;

    /* Enable detectable auto-repeat, but ignore failures */
    getConnection().discard_reply(
      getConnection()
        .xkb()
        .per_client_flags(XCB_XKB_ID_USE_CORE_KBD,
                          XCB_XKB_PER_CLIENT_FLAG_DETECTABLE_AUTO_REPEAT,
                          XCB_XKB_PER_CLIENT_FLAG_DETECTABLE_AUTO_REPEAT,
                          0,
                          0,
                          0)
        .sequence);

    getConnection().xkb().select_events(
      XCB_XKB_ID_USE_CORE_KBD, map, 0, map, map_parts, map_parts, 0);

    /* load keymap to use when resolving keypresses */
    xkb_init_keymap();
}

/** Frees resources allocated by xkb_init()
 */
void xkb_free(void) {
    getConnection().xkb().select_events(XCB_XKB_ID_USE_CORE_KBD, 0, 0, 0, 0, 0, 0);
    xkb_free_keymap();
}

// vim: filetype=c:expandtab:shiftwidth=4:tabstop=8:softtabstop=4:textwidth=80
