/*
 * systray.c - systray handling
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

#include "systray.h"

#include "common/atoms.h"
#include "common/xembed.h"
#include "common/xutil.h"
#include "globalconf.h"
#include "objects/drawin.h"
#include "xwindow.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <xcb/xcb.h>
#include <xcb/xcb_atom.h>
#include <xcb/xcb_icccm.h>

#define SYSTEM_TRAY_REQUEST_DOCK 0 /* Begin icon docking */

/** Initialize systray information in X.
 */
void systray_init(void) {
    xcb_intern_atom_cookie_t atom_systray_q;
    xcb_intern_atom_reply_t* atom_systray_r;
    char* atom_name;
    xcb_screen_t* xscreen = getGlobals().screen;

    getGlobals().systray.window = getConnection().generate_id();
    getGlobals().systray.background_pixel = xscreen->black_pixel;
    const uint32_t values[] = {xscreen->black_pixel, XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT};
    xcb_create_window(getGlobals().connection,
                      xscreen->root_depth,
                      getGlobals().systray.window,
                      xscreen->root,
                      -1,
                      -1,
                      1,
                      1,
                      0,
                      XCB_COPY_FROM_PARENT,
                      xscreen->root_visual,
                      XCB_CW_BACK_PIXEL | XCB_CW_EVENT_MASK,
                      values);
    xwindow_set_class_instance(getGlobals().systray.window);
    xwindow_set_name_static(getGlobals().systray.window, "Awesome systray window");

    atom_name = xcb_atom_name_by_screen("_NET_SYSTEM_TRAY", getGlobals().default_screen);
    if (!atom_name) {
        fatal("error getting systray atom name");
    }

    atom_systray_q =
      xcb_intern_atom_unchecked(getGlobals().connection, false, a_strlen(atom_name), atom_name);

    p_delete(&atom_name);

    atom_systray_r = xcb_intern_atom_reply(getGlobals().connection, atom_systray_q, NULL);
    if (!atom_systray_r) {
        fatal("error getting systray atom");
    }

    getGlobals().systray.atom = atom_systray_r->atom;
    p_delete(&atom_systray_r);
}

/** Register systray in X.
 */
static void systray_register(void) {
    xcb_client_message_event_t ev;

    xcb_screen_t* xscreen = getGlobals().screen;

    if (getGlobals().systray.registered) {
        return;
    }

    getGlobals().systray.registered = true;

    /* Fill event */
    p_clear(&ev, 1);
    ev.response_type = XCB_CLIENT_MESSAGE;
    ev.window = xscreen->root;
    ev.format = 32;
    ev.type = MANAGER;
    ev.data.data32[0] = getGlobals().get_timestamp();
    ev.data.data32[1] = getGlobals().systray.atom;
    ev.data.data32[2] = getGlobals().systray.window;
    ev.data.data32[3] = ev.data.data32[4] = 0;

    xcb_set_selection_owner(getGlobals().connection,
                            getGlobals().systray.window,
                            getGlobals().systray.atom,
                            getGlobals().get_timestamp());

    xcb_send_event(
      getGlobals().connection, false, xscreen->root, 0xFFFFFF, reinterpret_cast<const char*>(&ev));
}

/** Remove systray information in X.
 */
void systray_cleanup(void) {
    if (!getGlobals().systray.registered) {
        return;
    }

    getGlobals().systray.registered = false;

    xcb_set_selection_owner(
      getGlobals().connection, XCB_NONE, getGlobals().systray.atom, getGlobals().get_timestamp());

    xcb_unmap_window(getGlobals().connection, getGlobals().systray.window);
}

/** Handle a systray request.
 * \param embed_win The window to embed.
 * \return 0 on no error.
 */
int systray_request_handle(xcb_window_t embed_win) {
    XEmbed::window em;
    xcb_get_property_cookie_t em_cookie;
    const uint32_t select_input_val[] = {XCB_EVENT_MASK_STRUCTURE_NOTIFY |
                                         XCB_EVENT_MASK_PROPERTY_CHANGE |
                                         XCB_EVENT_MASK_ENTER_WINDOW};

    /* check if not already trayed */
    auto it = std::find_if(getGlobals().embedded.begin(),
                           getGlobals().embedded.end(),
                           [embed_win](const auto& window) { return window.win == embed_win; });
    if (it != getGlobals().embedded.end()) {
        return -1;
    }

    p_clear(&em_cookie, 1);

    em_cookie = XEmbed::info_get_unchecked(&getConnection(), embed_win);

    xcb_change_window_attributes(
      getGlobals().connection, embed_win, XCB_CW_EVENT_MASK, select_input_val);

    /* we grab the window, but also make sure it's automatically reparented back
     * to the root window if we should die.
     */
    xcb_change_save_set(getGlobals().connection, XCB_SET_MODE_INSERT, embed_win);
    xcb_reparent_window(getGlobals().connection, embed_win, getGlobals().systray.window, 0, 0);

    em.win = embed_win;

    auto info = XEmbed::xembed_info_get_reply(&getConnection(), em_cookie).
        value_or(XEmbed::info{.version = XEMBED_VERSION, .flags = static_cast<uint32_t>(XEmbed::InfoFlags::MAPPED)});
    em.info = info;

    XEmbed::xembed_embedded_notify(getGlobals().connection,
                                   em.win,
                                   getGlobals().get_timestamp(),
                                   getGlobals().systray.window,
                                   MIN(XEMBED_VERSION, em.info.version));

    getGlobals().embedded.push_back(em);
    Lua::systray_invalidate();

    return 0;
}

/** Handle systray message.
 * \param ev The event.
 * \return 0 on no error.
 */
int systray_process_client_message(xcb_client_message_event_t* ev) {
    int ret = 0;
    xcb_get_geometry_cookie_t geom_c;
    xcb_get_geometry_reply_t* geom_r;

    switch (ev->data.data32[1]) {
    case SYSTEM_TRAY_REQUEST_DOCK:
        geom_c = xcb_get_geometry_unchecked(getGlobals().connection, ev->window);

        if (!(geom_r = xcb_get_geometry_reply(getGlobals().connection, geom_c, NULL))) {
            return -1;
        }

        if (getGlobals().screen->root == geom_r->root) {
            ret = systray_request_handle(ev->data.data32[2]);
        }

        p_delete(&geom_r);
        break;
    }

    return ret;
}

/** Check if a window is a KDE tray.
 * \param w The window to check.
 * \return True if it is, false otherwise.
 */
bool systray_iskdedockapp(xcb_window_t w) {
    xcb_get_property_cookie_t kde_check_q;
    xcb_get_property_reply_t* kde_check;
    bool ret;

    /* Check if that is a KDE tray because it does not respect fdo standards,
     * thanks KDE. */
    kde_check_q = xcb_get_property_unchecked(
      getGlobals().connection, false, w, _KDE_NET_WM_SYSTEM_TRAY_WINDOW_FOR, XCB_ATOM_WINDOW, 0, 1);

    kde_check = xcb_get_property_reply(getGlobals().connection, kde_check_q, NULL);

    /* it's a KDE systray ?*/
    ret = (kde_check && kde_check->value_len);

    p_delete(&kde_check);

    return ret;
}

/** Handle xembed client message.
 * \param ev The event.
 * \return 0 on no error.
 */
int xembed_process_client_message(xcb_client_message_event_t* ev) {
    switch (XEmbed::from_native(ev->data.data32[1])) {
    case XEmbed::Message::REQUEST_FOCUS:
        xembed_focus_in(getGlobals().connection,
                        ev->window,
                        getGlobals().get_timestamp(),
                        XEmbed::Focus::CURRENT);
    default: break;
    }
    return 0;
}

static int systray_num_visible_entries(void) {
    return std::ranges::count_if(getGlobals().embedded, [](const auto& em) {
        return em.info.flags & static_cast<uint32_t>(XEmbed::InfoFlags::MAPPED);
    });
}
namespace Lua {
/** Inform lua that the systray needs to be updated.
 */
void systray_invalidate(void) {
    lua_State* L = globalconf_get_lua_State();
    signal_object_emit(L, &global_signals, "systray::update", 0);

    /* Unmap now if the systray became empty */
    if (systray_num_visible_entries() == 0) {
        xcb_unmap_window(getGlobals().connection, getGlobals().systray.window);
    }
}
}
static void systray_update(
  int base_size, bool horizontal, bool reverse, int spacing, bool force_redraw, int rows) {
    if (base_size <= 0) {
        return;
    }

    /* Give the systray window the correct size */
    int num_entries = systray_num_visible_entries();
    int cols = (num_entries + rows - 1) / rows;
    std::array<uint32_t, 4> config_vals = {0, 0, 0, 0};
    if (horizontal) {
        config_vals[0] = base_size * cols + spacing * (cols - 1);
        config_vals[1] = base_size * rows + spacing * (rows - 1);
    } else {
        config_vals[0] = base_size * rows + spacing * (rows - 1);
        config_vals[1] = base_size * cols + spacing * (cols - 1);
    }
    getConnection().configure_window(
      getGlobals().systray.window, XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT, config_vals);

    /* Now resize each embedded window */
    config_vals[0] = config_vals[1] = 0;
    config_vals[2] = config_vals[3] = base_size;
    for (size_t i = 0; i < getGlobals().embedded.size(); i++) {
        decltype(getGlobals().embedded)::iterator em;

        if (reverse) {
            em = getGlobals().embedded.begin() + (getGlobals().embedded.size() - i - 1);
        } else {
            em = getGlobals().embedded.begin() + i;
        }

        if (!(em->info.flags & static_cast<uint32_t>(XEmbed::InfoFlags::MAPPED))) {
            xcb_unmap_window(getGlobals().connection, em->win);
            continue;
        }

        getConnection().configure_window(em->win,
                                         XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y |
                                           XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT,
                                         config_vals);
        xcb_map_window(getGlobals().connection, em->win);
        if (force_redraw) {
            xcb_clear_area(getGlobals().connection, 1, em->win, 0, 0, 0, 0);
        }
        if (int(i % rows) == rows - 1) {
            if (horizontal) {
                config_vals[0] += base_size + spacing;
                config_vals[1] = 0;
            } else {
                config_vals[0] = 0;
                config_vals[1] += base_size + spacing;
            }
        } else {
            if (horizontal) {
                config_vals[1] += base_size + spacing;
            } else {
                config_vals[0] += base_size + spacing;
            }
        }
    }
}

/** Update the systray
 * \param L The Lua VM state.
 * \return The number of elements pushed on stack.
 * \luastack
 * \lparam The drawin to display the systray in.
 * \lparam x X position for the systray.
 * \lparam y Y position for the systray.
 * \lparam base_size The size (width and height) each systray item gets.
 * \lparam horiz If true, the systray is horizontal, else vertical.
 * \lparam bg Color of the systray background.
 * \lparam revers If true, the systray icon order will be reversed, else default.
 * \lparam spacing The size of the spacing between icons.
 * \lparam rows Number of rows to display.
 */
int luaA_systray(lua_State* L) {
    systray_register();

    if (lua_gettop(L) == 1) {
        luaA_drawin_systray_kickout(L);
    }

    if (lua_gettop(L) > 1) {
        size_t bg_len;
        drawin_t* w = (drawin_t*)luaA_checkudata(L, 1, &drawin_class);
        int x = round(luaA_checknumber_range(L, 2, MIN_X11_COORDINATE, MAX_X11_COORDINATE));
        int y = round(luaA_checknumber_range(L, 3, MIN_X11_COORDINATE, MAX_X11_COORDINATE));
        int base_size = ceil(luaA_checknumber_range(L, 4, MIN_X11_SIZE, MAX_X11_SIZE));
        bool horiz = lua_toboolean(L, 5);
        const char* bg = luaL_checklstring(L, 6, &bg_len);
        bool revers = lua_toboolean(L, 7);
        int spacing = ceil(luaA_checknumber_range(L, 8, 0, MAX_X11_COORDINATE));
        int rows = ceil(luaA_checknumber_range(L, 9, 1, INT16_MAX));
        color_t bg_color;
        bool force_redraw = false;

        if (color_init_reply(
              color_init_unchecked(&bg_color, bg, bg_len, getGlobals().default_visual)) &&
            getGlobals().systray.background_pixel != bg_color.pixel) {
            getGlobals().systray.background_pixel = bg_color.pixel;
            getConnection().change_attributes(
              getGlobals().systray.window, XCB_CW_BACK_PIXEL, std::array{bg_color.pixel});
            xcb_clear_area(getGlobals().connection, 1, getGlobals().systray.window, 0, 0, 0, 0);
            force_redraw = true;
        }

        if (getGlobals().systray.parent != w) {
            xcb_reparent_window(
              getGlobals().connection, getGlobals().systray.window, w->window, x, y);
        } else {
            getConnection().configure_window(getGlobals().systray.window,
                                             XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y,
                                             std::array<uint32_t, 2>{(uint32_t)x, (uint32_t)y});
        }

        getGlobals().systray.parent = w;

        if (systray_num_visible_entries() != 0) {
            systray_update(base_size, horiz, revers, spacing, force_redraw, rows);
            xcb_map_window(getGlobals().connection, getGlobals().systray.window);
        }
    }

    lua_pushinteger(L, systray_num_visible_entries());
    luaA_object_push(L, getGlobals().systray.parent);
    return 2;
}

// vim: filetype=c:expandtab:shiftwidth=4:tabstop=8:softtabstop=4:textwidth=80
