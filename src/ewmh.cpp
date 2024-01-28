/*
 * ewmh.c - EWMH support functions
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

#include "ewmh.h"

#include "common/atoms.h"
#include "draw.h"
#include "globalconf.h"
#include "objects/client.h"
#include "objects/tag.h"
#include "xwindow.h"

#include <sys/types.h>
#include <unistd.h>
#include <xcb/xcb.h>
#include <xcb/xcb_atom.h>

#define _NET_WM_STATE_REMOVE 0
#define _NET_WM_STATE_ADD 1
#define _NET_WM_STATE_TOGGLE 2

#define ALL_DESKTOPS 0xffffffff

/** Update client EWMH hints.
 * \param L The Lua VM state.
 */
static int ewmh_client_update_hints(lua_State* L) {
    auto c = client_class.checkudata<client>(L, 1);
    xcb_atom_t state[10]; /* number of defined state atoms */
    size_t i = 0;

    if (c->modal) {
        state[i++] = _NET_WM_STATE_MODAL;
    }
    if (c->fullscreen) {
        state[i++] = _NET_WM_STATE_FULLSCREEN;
    }
    if (c->maximized_vertical || c->maximized) {
        state[i++] = _NET_WM_STATE_MAXIMIZED_VERT;
    }
    if (c->maximized_horizontal || c->maximized) {
        state[i++] = _NET_WM_STATE_MAXIMIZED_HORZ;
    }
    if (c->sticky) {
        state[i++] = _NET_WM_STATE_STICKY;
    }
    if (c->skip_taskbar) {
        state[i++] = _NET_WM_STATE_SKIP_TASKBAR;
    }
    if (c->above) {
        state[i++] = _NET_WM_STATE_ABOVE;
    }
    if (c->below) {
        state[i++] = _NET_WM_STATE_BELOW;
    }
    if (c->minimized) {
        state[i++] = _NET_WM_STATE_HIDDEN;
    }
    if (c->urgent) {
        state[i++] = _NET_WM_STATE_DEMANDS_ATTENTION;
    }

    getConnection().replace_property(c->window, _NET_WM_STATE, XCB_ATOM_ATOM, std::span{state, i});

    return 0;
}

static int ewmh_update_net_active_window(lua_State* L) {
    xcb_window_t win;

    if (getGlobals().focus.client) {
        win = getGlobals().focus.client->window;
    } else {
        win = XCB_NONE;
    }

    getConnection().replace_property(
      getGlobals().screen->root, _NET_ACTIVE_WINDOW, XCB_ATOM_WINDOW, win);
    return 0;
}

static int ewmh_update_net_client_list(lua_State* L) {
    xcb_window_t* wins = p_alloca(xcb_window_t, getGlobals().clients.size());

    int n = 0;
    for (auto* client : getGlobals().clients) {
        wins[n++] = (client)->window;
    }

    getConnection().replace_property(
      getGlobals().screen->root, _NET_CLIENT_LIST, XCB_ATOM_WINDOW, std::span(wins, n));

    return 0;
}

static int ewmh_client_update_frame_extents(lua_State* L) {
    auto c = client_class.checkudata<client>(L, 1);
    uint32_t extents[4];

    extents[0] = c->border_width + c->titlebar[CLIENT_TITLEBAR_LEFT].size;
    extents[1] = c->border_width + c->titlebar[CLIENT_TITLEBAR_RIGHT].size;
    extents[2] = c->border_width + c->titlebar[CLIENT_TITLEBAR_TOP].size;
    extents[3] = c->border_width + c->titlebar[CLIENT_TITLEBAR_BOTTOM].size;

    getConnection().replace_property(c->window, _NET_FRAME_EXTENTS, XCB_ATOM_CARDINAL, extents);

    return 0;
}

void ewmh_init(void) {
    xcb_atom_t atom[] = {_NET_SUPPORTED,
                         _NET_SUPPORTING_WM_CHECK,
                         _NET_STARTUP_ID,
                         _NET_CLIENT_LIST,
                         _NET_CLIENT_LIST_STACKING,
                         _NET_NUMBER_OF_DESKTOPS,
                         _NET_CURRENT_DESKTOP,
                         _NET_DESKTOP_NAMES,
                         _NET_ACTIVE_WINDOW,
                         _NET_CLOSE_WINDOW,
                         _NET_FRAME_EXTENTS,
                         _NET_WM_NAME,
                         _NET_WM_STRUT_PARTIAL,
                         _NET_WM_ICON_NAME,
                         _NET_WM_VISIBLE_ICON_NAME,
                         _NET_WM_DESKTOP,
                         _NET_WM_WINDOW_TYPE,
                         _NET_WM_WINDOW_TYPE_DESKTOP,
                         _NET_WM_WINDOW_TYPE_DOCK,
                         _NET_WM_WINDOW_TYPE_TOOLBAR,
                         _NET_WM_WINDOW_TYPE_MENU,
                         _NET_WM_WINDOW_TYPE_UTILITY,
                         _NET_WM_WINDOW_TYPE_SPLASH,
                         _NET_WM_WINDOW_TYPE_DIALOG,
                         _NET_WM_WINDOW_TYPE_DROPDOWN_MENU,
                         _NET_WM_WINDOW_TYPE_POPUP_MENU,
                         _NET_WM_WINDOW_TYPE_TOOLTIP,
                         _NET_WM_WINDOW_TYPE_NOTIFICATION,
                         _NET_WM_WINDOW_TYPE_COMBO,
                         _NET_WM_WINDOW_TYPE_DND,
                         _NET_WM_WINDOW_TYPE_NORMAL,
                         _NET_WM_ICON,
                         _NET_WM_PID,
                         _NET_WM_STATE,
                         _NET_WM_STATE_STICKY,
                         _NET_WM_STATE_SKIP_TASKBAR,
                         _NET_WM_STATE_FULLSCREEN,
                         _NET_WM_STATE_MAXIMIZED_HORZ,
                         _NET_WM_STATE_MAXIMIZED_VERT,
                         _NET_WM_STATE_ABOVE,
                         _NET_WM_STATE_BELOW,
                         _NET_WM_STATE_MODAL,
                         _NET_WM_STATE_HIDDEN,
                         _NET_WM_STATE_DEMANDS_ATTENTION};

    xcb_screen_t* xscreen = getGlobals().screen;

    getConnection().replace_property(xscreen->root, _NET_SUPPORTED, XCB_ATOM_ATOM, atom);

    /* create our own window */
    xcb_window_t father = getConnection().generate_id();

    xcb_create_window(getGlobals().x.connection,
                      xscreen->root_depth,
                      father,
                      xscreen->root,
                      -1,
                      -1,
                      1,
                      1,
                      0,
                      XCB_COPY_FROM_PARENT,
                      xscreen->root_visual,
                      0,
                      NULL);

    getConnection().replace_property(
      xscreen->root, _NET_SUPPORTING_WM_CHECK, XCB_ATOM_WINDOW, father);

    getConnection().replace_property(father, _NET_SUPPORTING_WM_CHECK, XCB_ATOM_WINDOW, father);

    /* set the window manager name */
    getConnection().replace_property(father, _NET_WM_NAME, UTF8_STRING, "awesome");

    /* Set an instance, just because we can */
    xwindow_set_class_instance(father);

    /* set the window manager PID */
    int32_t i = getpid();
    getConnection().replace_property(father, _NET_WM_PID, XCB_ATOM_CARDINAL, i);
}

static void ewmh_update_maximize(bool h, bool status, bool toggle) {
    lua_State* L = globalconf_get_lua_State();

    if (h) {
        lua_pushstring(L, "client_maximize_horizontal");
    } else {
        lua_pushstring(L, "client_maximize_vertical");
    }

    /* Create table argument with raise=true. */
    lua_newtable(L);
    lua_pushstring(L, "toggle");
    lua_pushboolean(L, toggle);
    lua_settable(L, -3);
    lua_pushstring(L, "status");
    lua_pushboolean(L, status);
    lua_settable(L, -3);

    luaA_object_emit_signal(L, -3, "request::geometry", 2);
}

void ewmh_init_lua(void) {
    lua_State* L = globalconf_get_lua_State();

    client_class.connect_signal(L, "focus", ewmh_update_net_active_window);
    client_class.connect_signal(L, "unfocus", ewmh_update_net_active_window);
    client_class.connect_signal(L, "request::manage", ewmh_update_net_client_list);
    client_class.connect_signal(L, "request::unmanage", ewmh_update_net_client_list);
    client_class.connect_signal(L, "property::modal", ewmh_client_update_hints);
    client_class.connect_signal(L, "property::fullscreen", ewmh_client_update_hints);
    client_class.connect_signal(L, "property::maximized_horizontal", ewmh_client_update_hints);
    client_class.connect_signal(L, "property::maximized_vertical", ewmh_client_update_hints);
    client_class.connect_signal(L, "property::maximized", ewmh_client_update_hints);
    client_class.connect_signal(L, "property::sticky", ewmh_client_update_hints);
    client_class.connect_signal(L, "property::skip_taskbar", ewmh_client_update_hints);
    client_class.connect_signal(L, "property::above", ewmh_client_update_hints);
    client_class.connect_signal(L, "property::below", ewmh_client_update_hints);
    client_class.connect_signal(L, "property::minimized", ewmh_client_update_hints);
    client_class.connect_signal(L, "property::urgent", ewmh_client_update_hints);
    client_class.connect_signal(L, "property::titlebar_top", ewmh_client_update_frame_extents);
    client_class.connect_signal(L, "property::titlebar_bottom", ewmh_client_update_frame_extents);
    client_class.connect_signal(L, "property::titlebar_right", ewmh_client_update_frame_extents);
    client_class.connect_signal(L, "property::titlebar_left", ewmh_client_update_frame_extents);
    client_class.connect_signal(L, "property::border_width", ewmh_client_update_frame_extents);
    client_class.connect_signal(L, "request::manage", ewmh_client_update_frame_extents);
    /* NET_CURRENT_DESKTOP handling */
    client_class.connect_signal(L, "focus", ewmh_update_net_current_desktop);
    client_class.connect_signal(L, "unfocus", ewmh_update_net_current_desktop);
    client_class.connect_signal(L, "tagged", ewmh_update_net_current_desktop);
    client_class.connect_signal(L, "untagged", ewmh_update_net_current_desktop);
    tag_class.connect_signal(L, "property::selected", ewmh_update_net_current_desktop);
}

/** Set the client list in stacking order, bottom to top.
 */
void ewmh_update_net_client_list_stacking(void) {
    size_t n = 0;
    std::vector<xcb_window_t> wins(getGlobals().getStack().size());

    for (auto* client : getGlobals().getStack()) {
        wins[n++] = (client)->window;
    }

    getConnection().replace_property(
      getGlobals().screen->root, _NET_CLIENT_LIST_STACKING, XCB_ATOM_WINDOW, std::span{wins});
}

void ewmh_update_net_numbers_of_desktop(void) {
    uint32_t count = getGlobals().tags.size();

    getConnection().replace_property(
      getGlobals().screen->root, _NET_NUMBER_OF_DESKTOPS, XCB_ATOM_CARDINAL, count);
}

int ewmh_update_net_current_desktop(lua_State* L) {
    uint32_t idx = tags_get_current_or_first_selected_index();

    getConnection().replace_property(
      getGlobals().screen->root, _NET_CURRENT_DESKTOP, XCB_ATOM_CARDINAL, idx);
    return 0;
}

void ewmh_update_net_desktop_names(void) {
    std::vector<char> buf;

    for (const auto& tag : getGlobals().tags) {
        auto tagname = tag.get()->name;
        buf.insert(buf.begin() + buf.size(), tagname.data(), tagname.data() + (tagname.size()) + 1);
    }

    getConnection().replace_property(
      getGlobals().screen->root, _NET_DESKTOP_NAMES, UTF8_STRING, buf);
}

static void ewmh_process_state_atom(client* c, xcb_atom_t state, int set) {
    lua_State* L = globalconf_get_lua_State();
    luaA_object_push(L, c);

    if (state == _NET_WM_STATE_STICKY) {
        if (set == _NET_WM_STATE_REMOVE) {
            client_set_sticky(L, -1, false);
        } else if (set == _NET_WM_STATE_ADD) {
            client_set_sticky(L, -1, true);
        } else if (set == _NET_WM_STATE_TOGGLE) {
            client_set_sticky(L, -1, !c->sticky);
        }
    } else if (state == _NET_WM_STATE_SKIP_TASKBAR) {
        if (set == _NET_WM_STATE_REMOVE) {
            client_set_skip_taskbar(L, -1, false);
        } else if (set == _NET_WM_STATE_ADD) {
            client_set_skip_taskbar(L, -1, true);
        } else if (set == _NET_WM_STATE_TOGGLE) {
            client_set_skip_taskbar(L, -1, !c->skip_taskbar);
        }
    } else if (state == _NET_WM_STATE_FULLSCREEN) {
        if (set == _NET_WM_STATE_REMOVE) {
            client_set_fullscreen(L, -1, false);
        } else if (set == _NET_WM_STATE_ADD) {
            client_set_fullscreen(L, -1, true);
        } else if (set == _NET_WM_STATE_TOGGLE) {
            client_set_fullscreen(L, -1, !c->fullscreen);
        }
    } else if (state == _NET_WM_STATE_MAXIMIZED_HORZ) {
        if (set == _NET_WM_STATE_REMOVE) {
            ewmh_update_maximize(true, false, false);
        } else if (set == _NET_WM_STATE_ADD) {
            ewmh_update_maximize(true, true, false);
        } else if (set == _NET_WM_STATE_TOGGLE) {
            ewmh_update_maximize(true, false, true);
        }
    } else if (state == _NET_WM_STATE_MAXIMIZED_VERT) {
        if (set == _NET_WM_STATE_REMOVE) {
            ewmh_update_maximize(false, false, false);
        } else if (set == _NET_WM_STATE_ADD) {
            ewmh_update_maximize(false, true, false);
        } else if (set == _NET_WM_STATE_TOGGLE) {
            ewmh_update_maximize(false, false, true);
        }
    } else if (state == _NET_WM_STATE_ABOVE) {
        if (set == _NET_WM_STATE_REMOVE) {
            client_set_above(L, -1, false);
        } else if (set == _NET_WM_STATE_ADD) {
            client_set_above(L, -1, true);
        } else if (set == _NET_WM_STATE_TOGGLE) {
            client_set_above(L, -1, !c->above);
        }
    } else if (state == _NET_WM_STATE_BELOW) {
        if (set == _NET_WM_STATE_REMOVE) {
            client_set_below(L, -1, false);
        } else if (set == _NET_WM_STATE_ADD) {
            client_set_below(L, -1, true);
        } else if (set == _NET_WM_STATE_TOGGLE) {
            client_set_below(L, -1, !c->below);
        }
    } else if (state == _NET_WM_STATE_MODAL) {
        if (set == _NET_WM_STATE_REMOVE) {
            client_set_modal(L, -1, false);
        } else if (set == _NET_WM_STATE_ADD) {
            client_set_modal(L, -1, true);
        } else if (set == _NET_WM_STATE_TOGGLE) {
            client_set_modal(L, -1, !c->modal);
        }
    } else if (state == _NET_WM_STATE_HIDDEN) {
        if (set == _NET_WM_STATE_REMOVE) {
            client_set_minimized(L, -1, false);
        } else if (set == _NET_WM_STATE_ADD) {
            client_set_minimized(L, -1, true);
        } else if (set == _NET_WM_STATE_TOGGLE) {
            client_set_minimized(L, -1, !c->minimized);
        }
    } else if (state == _NET_WM_STATE_DEMANDS_ATTENTION) {
        if (set == _NET_WM_STATE_REMOVE) {
            lua_pushboolean(L, false);
            /*TODO v5: Add a context */
            luaA_object_emit_signal(L, -2, "request::urgent", 1);
        } else if (set == _NET_WM_STATE_ADD) {
            lua_pushboolean(L, true);
            /*TODO v5: Add a context */
            luaA_object_emit_signal(L, -2, "request::urgent", 1);
        } else if (set == _NET_WM_STATE_TOGGLE) {
            lua_pushboolean(L, !c->urgent);
            /*TODO v5: Add a context */
            luaA_object_emit_signal(L, -2, "request::urgent", 1);
        }
    }

    lua_pop(L, 1);
}

static void ewmh_process_desktop(client* c, uint32_t desktop) {
    lua_State* L = globalconf_get_lua_State();
    int idx = desktop;
    if (desktop == ALL_DESKTOPS) {
        luaA_object_push(L, c);
        lua_pushboolean(L, true);
        /*TODO v5: Move the context argument to arg1 */
        luaA_object_emit_signal(L, -2, "request::tag", 1);
        /* Pop the client, arguments are already popped */
        lua_pop(L, 1);
    } else if (idx >= 0 && idx < (int)getGlobals().tags.size()) {
        luaA_object_push(L, c);
        luaA_object_push(L, getGlobals().tags[idx].get());
        /*TODO v5: Move the context argument to arg1 */
        luaA_object_emit_signal(L, -2, "request::tag", 1);
        /* Pop the client, arguments are already popped */
        lua_pop(L, 1);
    }
}

int ewmh_process_client_message(xcb_client_message_event_t* ev) {
    client* c;

    if (ev->type == _NET_CURRENT_DESKTOP) {
        int idx = ev->data.data32[0];
        if (idx >= 0 && idx < (int)getGlobals().tags.size()) {
            lua_State* L = globalconf_get_lua_State();
            luaA_object_push(L, getGlobals().tags[idx].get());
            lua_pushstring(L, "ewmh");
            luaA_object_emit_signal(L, -2, "request::select", 1);
            lua_pop(L, 1);
        }
    } else if (ev->type == _NET_CLOSE_WINDOW) {
        if ((c = client_getbywin(ev->window))) {
            client_kill(c);
        }
    } else if (ev->type == _NET_WM_DESKTOP) {
        if ((c = client_getbywin(ev->window))) {
            ewmh_process_desktop(c, ev->data.data32[0]);
        }
    } else if (ev->type == _NET_WM_STATE) {
        if ((c = client_getbywin(ev->window))) {
            ewmh_process_state_atom(c, (xcb_atom_t)ev->data.data32[1], ev->data.data32[0]);
            if (ev->data.data32[2]) {
                ewmh_process_state_atom(c, (xcb_atom_t)ev->data.data32[2], ev->data.data32[0]);
            }
        }
    } else if (ev->type == _NET_ACTIVE_WINDOW) {
        if ((c = client_getbywin(ev->window))) {
            lua_State* L = globalconf_get_lua_State();
            luaA_object_push(L, c);
            lua_pushstring(L, "ewmh");

            /* Create table argument with raise=true. */
            lua_newtable(L);
            lua_pushstring(L, "raise");
            lua_pushboolean(L, true);
            lua_settable(L, -3);

            luaA_object_emit_signal(L, -3, "request::activate", 2);
            lua_pop(L, 1);
        }
    }

    return 0;
}

/** Update the client active desktop.
 * This is "wrong" since it can be on several tags, but EWMH has a strict view
 * of desktop system so just take the first tag.
 * \param c The client.
 */
void ewmh_client_update_desktop(client* c) {
    uint32_t i;

    if (c->sticky) {
        static uint32_t desktops = ALL_DESKTOPS;
        getConnection().replace_property(c->window, _NET_WM_DESKTOP, XCB_ATOM_CARDINAL, desktops);
        return;
    }
    for (i = 0; i < (size_t)getGlobals().tags.size(); i++) {
        if (is_client_tagged(c, getGlobals().tags[i].get())) {
            getConnection().replace_property(c->window, _NET_WM_DESKTOP, XCB_ATOM_CARDINAL, i);
            return;
        }
    }
    /* It doesn't have any tags, remove the property */
    xcb_delete_property(getGlobals().x.connection, c->window, _NET_WM_DESKTOP);
}

/** Update the client struts.
 * \param window The window to update the struts for.
 * \param strut The strut type to update the window with.
 */
void ewmh_update_strut(xcb_window_t window, strut_t* strut) {
    if (window) {
        const uint32_t state[] = {strut->left,
                                  strut->right,
                                  strut->top,
                                  strut->bottom,
                                  strut->left_start_y,
                                  strut->left_end_y,
                                  strut->right_start_y,
                                  strut->right_end_y,
                                  strut->top_start_x,
                                  strut->top_end_x,
                                  strut->bottom_start_x,
                                  strut->bottom_end_x};

        getConnection().replace_property(window, _NET_WM_STRUT_PARTIAL, XCB_ATOM_CARDINAL, state);
    }
}

/** Update the window type.
 * \param window The window to update.
 * \param type The new type to set.
 */
void ewmh_update_window_type(xcb_window_t window, uint32_t type) {
    getConnection().replace_property(window, _NET_WM_WINDOW_TYPE, XCB_ATOM_ATOM, type);
}

void ewmh_client_check_hints(client* c) {
    xcb_atom_t* state;
    void* data = NULL;
    xcb_get_property_cookie_t c0, c1, c2;
    xcb_get_property_reply_t* reply;
    bool is_h_max = false;
    bool is_v_max = false;

    /* Send the GetProperty requests which will be processed later */
    c0 = xcb_get_property_unchecked(getGlobals().x.connection,
                                    false,
                                    c->window,
                                    _NET_WM_DESKTOP,
                                    XCB_GET_PROPERTY_TYPE_ANY,
                                    0,
                                    1);

    c1 = xcb_get_property_unchecked(
      getGlobals().x.connection, false, c->window, _NET_WM_STATE, XCB_ATOM_ATOM, 0, UINT32_MAX);

    c2 = xcb_get_property_unchecked(getGlobals().x.connection,
                                    false,
                                    c->window,
                                    _NET_WM_WINDOW_TYPE,
                                    XCB_ATOM_ATOM,
                                    0,
                                    UINT32_MAX);

    reply = xcb_get_property_reply(getGlobals().x.connection, c0, NULL);
    if (reply && reply->value_len && (data = xcb_get_property_value(reply))) {
        ewmh_process_desktop(c, *(uint32_t*)data);
    }

    p_delete(&reply);

    reply = xcb_get_property_reply(getGlobals().x.connection, c1, NULL);
    if (reply && (data = xcb_get_property_value(reply))) {
        state = (xcb_atom_t*)data;
        for (int i = 0; i < xcb_get_property_value_length(reply) / (int)sizeof(xcb_atom_t); i++) {
            if (state[i] == _NET_WM_STATE_MAXIMIZED_HORZ) {
                is_h_max = true;
            } else if (state[i] == _NET_WM_STATE_MAXIMIZED_VERT) {
                is_v_max = true;
            } else {
                ewmh_process_state_atom(c, state[i], _NET_WM_STATE_ADD);
            }
        }
    }

    /* Check maximization manually */
    if (is_h_max && is_v_max) {
        lua_State* L = globalconf_get_lua_State();
        luaA_object_push(L, c);
        client_set_maximized(L, -1, true);
        lua_pop(L, 1);
    } else if (is_h_max) {
        lua_State* L = globalconf_get_lua_State();
        luaA_object_push(L, c);
        client_set_maximized_horizontal(L, -1, true);
        lua_pop(L, 1);
    } else if (is_v_max) {
        lua_State* L = globalconf_get_lua_State();
        luaA_object_push(L, c);
        client_set_maximized_vertical(L, -1, true);
        lua_pop(L, 1);
    }

    p_delete(&reply);

    reply = xcb_get_property_reply(getGlobals().x.connection, c2, NULL);
    if (reply && (data = xcb_get_property_value(reply))) {
        c->has_NET_WM_WINDOW_TYPE = true;
        state = (xcb_atom_t*)data;
        for (int i = 0; i < xcb_get_property_value_length(reply) / (int)sizeof(xcb_atom_t); i++) {
            if (state[i] == _NET_WM_WINDOW_TYPE_DESKTOP) {
                c->type = MAX(c->type, WINDOW_TYPE_DESKTOP);
            } else if (state[i] == _NET_WM_WINDOW_TYPE_DIALOG) {
                c->type = MAX(c->type, WINDOW_TYPE_DIALOG);
            } else if (state[i] == _NET_WM_WINDOW_TYPE_SPLASH) {
                c->type = MAX(c->type, WINDOW_TYPE_SPLASH);
            } else if (state[i] == _NET_WM_WINDOW_TYPE_DOCK) {
                c->type = MAX(c->type, WINDOW_TYPE_DOCK);
            } else if (state[i] == _NET_WM_WINDOW_TYPE_MENU) {
                c->type = MAX(c->type, WINDOW_TYPE_MENU);
            } else if (state[i] == _NET_WM_WINDOW_TYPE_TOOLBAR) {
                c->type = MAX(c->type, WINDOW_TYPE_TOOLBAR);
            } else if (state[i] == _NET_WM_WINDOW_TYPE_UTILITY) {
                c->type = MAX(c->type, WINDOW_TYPE_UTILITY);
            }
        }
    } else {
        c->has_NET_WM_WINDOW_TYPE = false;
    }

    p_delete(&reply);
}

/** Process the WM strut of a client.
 * \param c The client.
 */
void ewmh_process_client_strut(client* c) {
    void* data;
    xcb_get_property_reply_t* strut_r;

    xcb_get_property_cookie_t strut_q = xcb_get_property_unchecked(
      getGlobals().x.connection, false, c->window, _NET_WM_STRUT_PARTIAL, XCB_ATOM_CARDINAL, 0, 12);
    strut_r = xcb_get_property_reply(getGlobals().x.connection, strut_q, NULL);

    if (strut_r && strut_r->value_len && (data = xcb_get_property_value(strut_r))) {
        auto strut = (uint32_t*)data;

        if (c->strut.left != strut[0] || c->strut.right != strut[1] || c->strut.top != strut[2] ||
            c->strut.bottom != strut[3] || c->strut.left_start_y != strut[4] ||
            c->strut.left_end_y != strut[5] || c->strut.right_start_y != strut[6] ||
            c->strut.right_end_y != strut[7] || c->strut.top_start_x != strut[8] ||
            c->strut.top_end_x != strut[9] || c->strut.bottom_start_x != strut[10] ||
            c->strut.bottom_end_x != strut[11]) {
            c->strut.left = strut[0];
            c->strut.right = strut[1];
            c->strut.top = strut[2];
            c->strut.bottom = strut[3];
            c->strut.left_start_y = strut[4];
            c->strut.left_end_y = strut[5];
            c->strut.right_start_y = strut[6];
            c->strut.right_end_y = strut[7];
            c->strut.top_start_x = strut[8];
            c->strut.top_end_x = strut[9];
            c->strut.bottom_start_x = strut[10];
            c->strut.bottom_end_x = strut[11];

            lua_State* L = globalconf_get_lua_State();
            luaA_object_push(L, c);
            luaA_object_emit_signal(L, -1, "property::struts", 0);
            lua_pop(L, 1);
        }
    }

    p_delete(&strut_r);
}

/** Send request to get NET_WM_ICON (EWMH)
 * \param w The window.
 * \return The cookie associated with the request.
 */
xcb_get_property_cookie_t ewmh_window_icon_get_unchecked(xcb_window_t w) {
    return xcb_get_property_unchecked(
      getGlobals().x.connection, false, w, _NET_WM_ICON, XCB_ATOM_CARDINAL, 0, UINT32_MAX);
}

static cairo_surface_t* ewmh_window_icon_from_reply_next(uint32_t** data, uint32_t* data_end) {
    uint32_t width, height;
    uint64_t data_len;
    uint32_t* icon_data;

    if (data_end - *data <= 2) {
        return NULL;
    }

    width = (*data)[0];
    height = (*data)[1];

    /* Check that we have enough data, handling overflow */
    data_len = width * (uint64_t)height;
    if (width < 1 || height < 1 || data_len > (uint64_t)(data_end - *data) - 2) {
        return NULL;
    }

    icon_data = *data + 2;
    *data += 2 + data_len;
    return draw_surface_from_data(width, height, icon_data);
}

static std::vector<cairo_surface_handle> ewmh_window_icon_from_reply(xcb_get_property_reply_t* r) {
    uint32_t *data, *data_end;
    std::vector<cairo_surface_handle> result;
    cairo_surface_t* s;

    if (!r || r->type != XCB_ATOM_CARDINAL || r->format != 32) {
        return result;
    }

    data = (uint32_t*)xcb_get_property_value(r);
    data_end = &data[r->length];
    if (!data) {
        return result;
    }

    while ((s = ewmh_window_icon_from_reply_next(&data, data_end)) != NULL) {
        result.emplace_back(s);
    }

    return result;
}

/** Get NET_WM_ICON.
 * \param cookie The cookie.
 * \return An array of icons.
 */
std::vector<cairo_surface_handle> ewmh_window_icon_get_reply(xcb_get_property_cookie_t cookie) {
    xcb_get_property_reply_t* r = xcb_get_property_reply(getGlobals().x.connection, cookie, NULL);
    std::vector<cairo_surface_handle> result = ewmh_window_icon_from_reply(r);
    p_delete(&r);
    return result;
}

// vim: filetype=c:expandtab:shiftwidth=4:tabstop=8:softtabstop=4:textwidth=80
