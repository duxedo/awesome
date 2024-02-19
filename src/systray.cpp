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
#include "xcbcpp/xcb.h"
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
    xcb_screen_t* xscreen = Manager::get().screen;

    Manager::get().systray.window = getConnection().generate_id();
    Manager::get().systray.background_pixel = xscreen->black_pixel;
    getConnection().create_window(
      xscreen->root_depth,
      Manager::get().systray.window,
      xscreen->root,
      {-1, -1, 1, 1},
      0,
      XCB_COPY_FROM_PARENT,
      xscreen->root_visual,
      XCB_CW_BACK_PIXEL | XCB_CW_EVENT_MASK,
      std::array<uint32_t, 2>{xscreen->black_pixel, XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT});
    xwindow_set_class_instance(Manager::get().systray.window);
    xwindow_set_name_static(Manager::get().systray.window, "Awesome systray window");

    auto atom_name = xcb_atom_name_by_screen("_NET_SYSTEM_TRAY", Manager::get().x.default_screen);
    if (!atom_name) {
        log_fatal("error getting systray atom name");
    }
    auto atom_systray_q =
      getConnection().intern_atom_unchecked(false, strlen(atom_name), atom_name);

    p_delete(&atom_name);

    auto atom_systray_r = getConnection().intern_atom_reply(atom_systray_q);
    if (!atom_systray_r) {
        log_fatal("error getting systray atom");
    }

    Manager::get().systray.atom = atom_systray_r->atom;
}

/** Register systray in X.
 */
static void systray_register(void) {

    xcb_screen_t* xscreen = Manager::get().screen;

    if (Manager::get().systray.registered) {
        return;
    }

    Manager::get().systray.registered = true;

    xcb_client_message_event_t ev{.response_type = XCB_CLIENT_MESSAGE,
                                  .format = 32,
                                  .sequence = 0,
                                  .window = xscreen->root,
                                  .type = MANAGER,
                                  .data = {.data32 = {Manager::get().x.get_timestamp(),
                                                      Manager::get().systray.atom,
                                                      Manager::get().systray.window,
                                                      0,
                                                      0}}};
    getConnection().set_selection_owner(
      Manager::get().systray.window, Manager::get().systray.atom, Manager::get().x.get_timestamp());

    getConnection().send_event(false, xscreen->root, 0xFFFFFF, reinterpret_cast<const char*>(&ev));
}

/** Remove systray information in X.
 */
void systray_cleanup(void) {
    if (!Manager::get().systray.registered) {
        return;
    }

    Manager::get().systray.registered = false;

    getConnection().set_selection_owner(
      XCB_NONE, Manager::get().systray.atom, Manager::get().x.get_timestamp());

    getConnection().unmap_window(Manager::get().systray.window);
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
    auto it = std::find_if(Manager::get().embedded.begin(),
                           Manager::get().embedded.end(),
                           [embed_win](const auto& window) { return window.win == embed_win; });
    if (it != Manager::get().embedded.end()) {
        return -1;
    }

    p_clear(&em_cookie, 1);

    em_cookie = XEmbed::info_get_unchecked(&getConnection(), embed_win);
    getConnection().change_attributes(embed_win, XCB_CW_EVENT_MASK, select_input_val);

    /* we grab the window, but also make sure it's automatically reparented back
     * to the root window if we should die.
     */
    getConnection().change_save_set(XCB_SET_MODE_INSERT, embed_win);
    getConnection().reparent_window(embed_win, Manager::get().systray.window, 0, 0);

    em.win = embed_win;

    auto info =
      XEmbed::xembed_info_get_reply(&getConnection(), em_cookie)
        .value_or(XEmbed::info{.version = XEMBED_VERSION,
                               .flags = static_cast<uint32_t>(XEmbed::InfoFlags::MAPPED)});
    em.info = info;

    XEmbed::xembed_embedded_notify(getConnection(),
                                   em.win,
                                   Manager::get().x.get_timestamp(),
                                   Manager::get().systray.window,
                                   MIN(XEMBED_VERSION, em.info.version));

    Manager::get().embedded.push_back(em);
    Lua::systray_invalidate();

    return 0;
}

/** Handle systray message.
 * \param ev The event.
 * \return 0 on no error.
 */
int systray_process_client_message(xcb_client_message_event_t* ev) {
    if (ev->data.data32[1] != SYSTEM_TRAY_REQUEST_DOCK) {
        return 0;
    }
    auto geom_c = getConnection().get_geometry_unchecked(ev->window);
    auto geom_r = getConnection().get_geometry_reply(geom_c);

    if (!geom_r) {
        return -1;
    }

    if (Manager::get().screen->root != geom_r->root) {
        return 0;
    }

    return systray_request_handle(ev->data.data32[2]);
}

/** Check if a window is a KDE tray.
 * \param w The window to check.
 * \return True if it is, false otherwise.
 */
bool systray_iskdedockapp(xcb_window_t w) {
    /* Check if that is a KDE tray because it does not respect fdo standards,
     * thanks KDE. */

    auto kde_check_q = getConnection().get_property_unchecked(
      false, w, _KDE_NET_WM_SYSTEM_TRAY_WINDOW_FOR, XCB_ATOM_WINDOW, 0, 1);

    auto kde_check = getConnection().get_property_reply(kde_check_q);

    /* it's a KDE systray ?*/
    return (kde_check && kde_check->value_len);
}

/** Handle xembed client message.
 * \param ev The event.
 * \return 0 on no error.
 */
int xembed_process_client_message(xcb_client_message_event_t* ev) {
    switch (XEmbed::from_native(ev->data.data32[1])) {
    case XEmbed::Message::REQUEST_FOCUS:
        xembed_focus_in(getConnection(),
                        ev->window,
                        Manager::get().x.get_timestamp(),
                        XEmbed::Focus::CURRENT);
    default: break;
    }
    return 0;
}

static int systray_num_visible_entries(void) {
    return std::ranges::count_if(Manager::get().embedded, [](const auto& em) {
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
        getConnection().unmap_window(Manager::get().systray.window);
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
    getConnection().configure_window(Manager::get().systray.window,
                                     XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT,
                                     config_vals);

    /* Now resize each embedded window */
    config_vals[0] = config_vals[1] = 0;
    config_vals[2] = config_vals[3] = base_size;
    for (size_t i = 0; i < Manager::get().embedded.size(); i++) {
        decltype(Manager::get().embedded)::iterator em;

        if (reverse) {
            em = Manager::get().embedded.begin() + (Manager::get().embedded.size() - i - 1);
        } else {
            em = Manager::get().embedded.begin() + i;
        }

        if (!(em->info.flags & static_cast<uint32_t>(XEmbed::InfoFlags::MAPPED))) {
            getConnection().unmap_window(em->win);
            continue;
        }

        getConnection().configure_window(em->win,
                                         XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y |
                                           XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT,
                                         config_vals);
        getConnection().map_window(em->win);

        if (force_redraw) {
            getConnection().clear_area(1, em->win);
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
        auto w = drawin_class.checkudata<drawin_t>(L, 1);
        int x = round(Lua::checknumber_range(L, 2, MIN_X11_COORDINATE, MAX_X11_COORDINATE));
        int y = round(Lua::checknumber_range(L, 3, MIN_X11_COORDINATE, MAX_X11_COORDINATE));
        int base_size = ceil(Lua::checknumber_range(L, 4, MIN_X11_SIZE, MAX_X11_SIZE));
        bool horiz = lua_toboolean(L, 5);
        const char* bg = luaL_checklstring(L, 6, &bg_len);
        bool revers = lua_toboolean(L, 7);
        int spacing = ceil(Lua::checknumber_range(L, 8, 0, MAX_X11_COORDINATE));
        int rows = ceil(Lua::checknumber_range(L, 9, 1, INT16_MAX));
        color_t bg_color;
        bool force_redraw = false;

        if (color_init_reply(
              color_init_unchecked(&bg_color, bg, bg_len, Manager::get().default_visual)) &&
            Manager::get().systray.background_pixel != bg_color.pixel) {
            Manager::get().systray.background_pixel = bg_color.pixel;
            getConnection().change_attributes(
              Manager::get().systray.window, XCB_CW_BACK_PIXEL, std::array{bg_color.pixel});
            getConnection().clear_area(1, Manager::get().systray.window);
            force_redraw = true;
        }

        if (Manager::get().systray.parent != w) {
            getConnection().reparent_window(Manager::get().systray.window, w->window, x, y);
        } else {
            getConnection().configure_window(Manager::get().systray.window,
                                             XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y,
                                             std::array{(uint32_t)x, (uint32_t)y});
        }

        Manager::get().systray.parent = w;

        if (systray_num_visible_entries() != 0) {
            systray_update(base_size, horiz, revers, spacing, force_redraw, rows);
            getConnection().map_window(Manager::get().systray.window);
        }
    }

    lua_pushinteger(L, systray_num_visible_entries());
    luaA_object_push(L, Manager::get().systray.parent);
    return 2;
}

// vim: filetype=c:expandtab:shiftwidth=4:tabstop=8:softtabstop=4:textwidth=80
