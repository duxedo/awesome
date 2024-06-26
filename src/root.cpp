/*
 * root.c - root window management
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

/** APIs to interact with the root window.
 *
 * X11 windows (clients) are stored in a tree. Each window
 * can have children. A common example of this are modal
 * dialog windows.
 *
 * This tree goes beyond a process. The root window is where
 * the wallpaper is drawn. It span the combined geometry of
 * every screen. It also persist when AwesomeWM is restarted.
 *
 * @author Julien Danjou &lt;julien@danjou.info&gt;
 * @copyright 2008-2009 Julien Danjou
 * @coreclassmod root
 */

#include "common/atoms.h"
#include "common/lualib.h"
#include "common/xcursor.h"
#include "common/xutil.h"
#include "globalconf.h"
#include "globals.h"
#include "math.h"
#include "objects/button.h"
#include "objects/drawin.h"
#include "objects/tag.h"
#include "xcbcpp/xcb.h"
#include "xwindow.h"

#include <cairo-xcb.h>
#include <xcb/xcb_aux.h>
#include <xcb/xtest.h>
#include <xkbcommon/xkbcommon.h>

static Lua::FunctionRegistryIdx miss_index_handler;
static Lua::FunctionRegistryIdx miss_newindex_handler;
static Lua::FunctionRegistryIdx miss_call_handler;

static void root_set_wallpaper_pixmap(XCB::Connection& c, xcb_pixmap_t p) {
    xcb_get_property_cookie_t prop_c;
    xcb_get_property_reply_t* prop_r;
    const xcb_screen_t* screen = Manager::get().screen;

    /* We now have the pattern painted to the pixmap p. Now turn p into the root
     * window's background pixmap.
     */
    c.change_attributes(screen->root, XCB_CW_BACK_PIXMAP, &p);
    xcb_clear_area(c.getConnection(), 0, screen->root, 0, 0, 0, 0);

    prop_c = xcb_get_property_unchecked(
      c.connection, false, screen->root, ESETROOT_PMAP_ID, XCB_ATOM_PIXMAP, 0, 1);

    /* Theoretically, this should be enough to set the wallpaper. However, to
     * make pseudo-transparency work, clients need a way to get the wallpaper.
     * You can't query a window's back pixmap, so properties are (ab)used.
     */
    c.replace_property(screen->root, _XROOTPMAP_ID, XCB_ATOM_PIXMAP, p);
    c.replace_property(screen->root, ESETROOT_PMAP_ID, XCB_ATOM_PIXMAP, p);

    /* Now make sure that the old wallpaper is freed (but only do this for ESETROOT_PMAP_ID) */
    prop_r = xcb_get_property_reply(c.connection, prop_c, NULL);
    if (prop_r && prop_r->value_len) {
        xcb_pixmap_t* rootpix = (xcb_pixmap_t*)xcb_get_property_value(prop_r);
        if (rootpix) {
            xcb_kill_client(c.getConnection(), *rootpix);
        }
    }
    p_delete(&prop_r);
}

static bool root_set_wallpaper(cairo_pattern_t* pattern) {
    lua_State* L = globalconf_get_lua_State();
    xcb_connection_t* c = xcb_connect(NULL, NULL);
    xcb_pixmap_t p = xcb_generate_id(c);
    /* globalconf.connection should be connected to the same X11 server, so we
     * can just use the info from that other connection.
     */
    const xcb_screen_t* screen = Manager::get().screen;
    uint16_t width = screen->width_in_pixels;
    uint16_t height = screen->height_in_pixels;
    bool result = false;
    cairo_surface_t* surface;
    cairo_t* cr;

    auto disconnect = [c]() {
        xcb_aux_sync(c);
        xcb_disconnect(c);
    };

    if (xcb_connection_has_error(c)) {
        disconnect();
        return false;
    }

    /* Create a pixmap and make sure it is already created, because we are going
     * to use it from the other X11 connection (Juggling with X11 connections
     * is a really, really bad idea).
     */
    xcb_create_pixmap(c, screen->root_depth, p, screen->root, width, height);
    xcb_aux_sync(c);

    /* Now paint to the picture from the main connection so that cairo sees that
     * it can tell the X server to copy between the (possible) old pixmap and
     * the new one directly and doesn't need GetImage and PutImage.
     */
    surface = cairo_xcb_surface_create(
      getConnection().getConnection(), p, draw_default_visual(screen), width, height);
    cr = cairo_create(surface);
    /* Paint the pattern to the surface */
    cairo_set_source(cr, pattern);
    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    cairo_paint(cr);
    cairo_destroy(cr);
    cairo_surface_flush(surface);
    getConnection().aux_sync();

    /* Change the wallpaper, without sending us a PropertyNotify event */
    getConnection().grab_server();
    getConnection().clear_attributes(Manager::get().screen->root, XCB_CW_EVENT_MASK);
    root_set_wallpaper_pixmap(getConnection(), p);
    getConnection().change_attributes(
      Manager::get().screen->root, XCB_CW_EVENT_MASK, ROOT_WINDOW_EVENT_MASK);
    xutil_ungrab_server();

    /* Make sure our pixmap is not destroyed when we disconnect. */
    xcb_set_close_down_mode(c, XCB_CLOSE_DOWN_RETAIN_PERMANENT);

    /* Tell Lua that the wallpaper changed */
    cairo_surface_destroy(Manager::get().wallpaper);
    Manager::get().wallpaper = surface;
    signal_object_emit(L, &Lua::global_signals, "wallpaper_changed", 0);

    result = true;
    disconnect();
    return result;
}

void root_update_wallpaper(void) {
    xcb_pixmap_t* rootpix;

    cairo_surface_destroy(Manager::get().wallpaper);
    Manager::get().wallpaper = NULL;
    auto prop_c = getConnection().get_property_unchecked(
      false, Manager::get().screen->root, _XROOTPMAP_ID, XCB_ATOM_PIXMAP, 0, 1);
    auto prop_r = getConnection().get_property_reply(prop_c);

    if (!prop_r || !prop_r->value_len) {
        return;
    }

    rootpix = (xcb_pixmap_t*)xcb_get_property_value(prop_r.get());
    if (!rootpix) {
        return;
    }

    auto geom_c = getConnection().get_geometry_unchecked(*rootpix);
    auto geom_r = getConnection().get_geometry_reply(geom_c);
    if (!geom_r) {
        return;
    }

    /* Only the default visual makes sense, so just the default depth */
    if (geom_r->depth !=
        draw_visual_depth(Manager::get().screen, Manager::get().default_visual->visual_id)) {
        log_warn(
          "Got a pixmap with depth {}, but the default depth is {}, continuing anyway",
          geom_r->depth,
          draw_visual_depth(Manager::get().screen, Manager::get().default_visual->visual_id));
    }

    Manager::get().wallpaper = cairo_xcb_surface_create(getConnection().getConnection(),
                                                        *rootpix,
                                                        Manager::get().default_visual,
                                                        geom_r->width,
                                                        geom_r->height);
}

static xcb_keycode_t _string_to_key_code(const char* s) {
    xcb_keysym_t keysym;

    keysym = xkb_keysym_from_name(s, XKB_KEYSYM_NO_FLAGS);
    auto keycodes = Manager::get().input.keysyms.get_keycode(keysym);

    if (keycodes) {
        return keycodes.get()[0]; /* XXX only returning the first is probably not
                                   * the best */
    } else {
        return 0;
    }
}

/** Send fake keyboard or mouse events.
 *
 * Usually the currently focused client or the keybindings will receive those
 * events. If a `keygrabber` or `mousegrabber` is running, then it will get them.
 *
 * Some keys have different names compared to the ones generally used in
 * Awesome. For example, Awesome uses "modifier keys" for keybindings using
 * their X11 names such as "Control" or "Mod1" (for "Alt"). These are not the
 * name of the key but is only the name of the modifier they represent. Some
 * modifiers are even present twice on some keyboard like the left and right
 * "Shift". Here is a list of the "real" key names matching the modifiers in
 * `fake_input`:
 *
 * <table class='widget_list' border=1>
 *  <tr style='font-weight: bold;'>
 *   <th align='center'>Modifier name </th>
 *   <th align='center'>Key name</th>
 *   <th align='center'>Other key name</th>
 *  </tr>
 *  <tr><td> Mod4</td><td align='center'> Super_L </td><td align='center'> Super_R </td></tr>
 *  <tr><td> Control </td><td align='center'> Control_L </td><td align='center'> Control_R
 * </td></tr> <tr><td> Shift </td><td align='center'> Shift_L </td><td align='center'> Shift_R
 * </td></tr> <tr><td> Mod1</td><td align='center'> Alt_L </td><td align='center'> Alt_R </td></tr>
 * </table>
 *
 * Note that this is valid for most of the modern "western" keyboard layouts.
 * Some older, custom or foreign layouts may break this convention.
 *
 * This function is very low level, to be more useful, it can be wrapped into
 * higher level constructs such as:
 *
 * **Sending strings:**
 *
 * @DOC_text_root_fake_string_EXAMPLE@
 *
 * Note that this example works for most ASCII inputs but may fail depending on
 * how the string is encoded. Some multi-byte characters may not represent
 * keys and some UTF-8 encoding format create characters by combining multiple
 * elements such as accent + base character or various escape sequences. If you
 * wish to use this example for "real world" i18n use cases, learning about
 * XKB event and UTF-8 encoding is a prerequisites.
 *
 * **Clicking:**
 *
 * ![Client geometry](../images/mouse.svg)
 *
 * @DOC_text_root_fake_click_EXAMPLE@
 *
 * @param event_type The event type: key\_press, key\_release, button\_press,
 *  button\_release or motion\_notify.
 * @param detail The detail: in case of a key event, this is the keycode
 *  to send, in case of a button event this is the number of the button. In
 *  case of a motion event, this is a boolean value which if true makes the
 *  coordinates relatives.
 * @param x In case of a motion event, this is the X coordinate.
 * @param y In case of a motion event, this is the Y coordinate.
 * @noreturn
 * @staticfct fake_input
 */
static int luaA_root_fake_input(lua_State* L) {
    if (!Manager::get().x.caps.have_xtest) {
        Lua::warn(L, "XTest extension is not available, cannot fake input.");
        return 0;
    }

    auto stype = Lua::checkstring(L, 1);
    uint8_t type, detail;
    int x = 0, y = 0;

    if (stype == "key_press") {
        type = XCB_KEY_PRESS;
        if (lua_type(L, 2) == LUA_TSTRING) {
            detail = _string_to_key_code(lua_tostring(L, 2)); /* keysym */
        } else {
            detail = luaL_checkinteger(L, 2); /* keycode */
        }
    } else if (stype == "key_release") {
        type = XCB_KEY_RELEASE;
        if (lua_type(L, 2) == LUA_TSTRING) {
            detail = _string_to_key_code(lua_tostring(L, 2)); /* keysym */
        } else {
            detail = luaL_checkinteger(L, 2); /* keycode */
        }
    } else if (stype == "button_press") {
        type = XCB_BUTTON_PRESS;
        detail = luaL_checkinteger(L, 2); /* button number */
    } else if (stype == "button_release") {
        type = XCB_BUTTON_RELEASE;
        detail = luaL_checkinteger(L, 2); /* button number */
    } else if (stype == "motion_notify") {
        type = XCB_MOTION_NOTIFY;
        detail = Lua::checkboolean(L, 2); /* relative to the current position or not */
        x = round(Lua::checknumber_range(L, 3, MIN_X11_COORDINATE, MAX_X11_COORDINATE));
        y = round(Lua::checknumber_range(L, 4, MIN_X11_COORDINATE, MAX_X11_COORDINATE));
    } else {
        return 0;
    }

    getConnection().test_fake_input(type,
                                    detail,
                                    0, /* This is a delay, not a timestamp! */
                                    XCB_NONE,
                                    {static_cast<int16_t>(x), static_cast<int16_t>(y)},
                                    0);
    return 0;
}

/** Get or set global key bindings.
 * These bindings will be available when you press keys on the root window
 * (the wallpaper).
 *
 * @property keys
 * @tparam[opt={}] table keys
 * @tablerowtype A list of `awful.key` objects.
 * @see awful.key
 */
static int luaA_root_keys(lua_State* L) {
    if (lua_gettop(L) == 1) {
        Lua::checktable(L, 1);

        for (auto* key : Manager::get().keys) {
            luaA_object_unref(L, key);
        }

        Manager::get().keys.clear();

        lua_pushnil(L);
        while (lua_next(L, 1)) {
            Manager::get().keys.push_back((keyb_t*)luaA_object_ref_class(L, -1, &key_class));
        }

        xcb_screen_t* s = Manager::get().screen;
        xwindow_grabkeys(s->root, Manager::get().keys);

        return 1;
    }

    lua_createtable(L, Manager::get().keys.size(), 0);
    for (size_t i = 0; i < Manager::get().keys.size(); i++) {
        luaA_object_push(L, Manager::get().keys[i]);
        lua_rawseti(L, -2, i + 1);
    }

    return 1;
}

/**
 * Store the list of mouse buttons to be applied on the wallpaper (also
 * known as root window).
 *
 * @property buttons
 * @tparam[opt={}] table buttons
 * @tablerowtype A list of `awful.button` objects.
 * @see awful.button
 *
 * @usage
 * root.buttons = {
 *     awful.button({ }, 3, function () mymainmenu:toggle() end),
 *     awful.button({ }, 4, awful.tag.viewnext),
 *     awful.button({ }, 5, awful.tag.viewprev),
 * }
 */

static int luaA_root_buttons(lua_State* L) {
    auto& buttons = Manager::get().buttons;
    if (lua_gettop(L) == 1) {
        Lua::checktable(L, 1);

        for (auto* button : buttons) {
            luaA_object_unref(L, button);
        }

        buttons.clear();

        lua_pushnil(L);
        while (lua_next(L, 1)) {
            buttons.push_back((button_t*)luaA_object_ref(L, -1));
        }

        return 1;
    }

    lua_createtable(L, buttons.size(), 0);
    for (size_t i = 0; i < buttons.size(); i++) {
        luaA_object_push(L, buttons[i]);
        lua_rawseti(L, -2, i + 1);
    }

    return 1;
}

/** Set the root cursor
 *
 * The possible values are:
 *
 *@DOC_cursor_c_COMMON@
 *
 * @tparam string cursor_name A X cursor name.
 * @noreturn
 * @staticfct cursor
 */
static int luaA_root_cursor(lua_State* L) {
    const char* cursor_name = luaL_checkstring(L, 1);
    uint16_t cursor_font = xcursor_font_fromstr(cursor_name);

    if (cursor_font) {
        uint32_t change_win_vals[] = {xcursor_new(Manager::get().x.cursor_ctx, cursor_font)};

        getConnection().change_attributes(
          Manager::get().screen->root, XCB_CW_CURSOR, change_win_vals);
    } else {
        Lua::warn(L, "invalid cursor %s", cursor_name);
    }

    return 0;
}

/** Get the drawins attached to a screen.
 *
 * @treturn table A table with all drawins.
 * @staticfct drawins
 */
static int luaA_root_drawins(lua_State* L) {
    lua_createtable(L, Manager::get().drawins.size(), 0);

    for (size_t i = 0; i < Manager::get().drawins.size(); i++) {
        luaA_object_push(L, Manager::get().drawins[i]);
        lua_rawseti(L, -2, i + 1);
    }

    return 1;
}

/** Get the wallpaper as a cairo surface or set it as a cairo pattern.
 *
 * @param pattern A cairo pattern as light userdata
 * @return A cairo surface or nothing.
 * @deprecated wallpaper
 * @see awful.wallpaper
 */
static int luaA_root_wallpaper(lua_State* L) {
    if (lua_gettop(L) == 1) {
        /* Avoid `error()s` down the line. If this happens during
         * initialization, AwesomeWM can be stuck in an infinite loop */
        if (lua_isnil(L, -1)) {
            return 0;
        }

        cairo_pattern_t* pattern = (cairo_pattern_t*)lua_touserdata(L, -1);
        lua_pushboolean(L, root_set_wallpaper(pattern));
        /* Don't return the wallpaper, it's too easy to get memleaks */
        return 1;
    }

    if (Manager::get().wallpaper == NULL) {
        return 0;
    }

    /* lua has to make sure this surface gets destroyed */
    lua_pushlightuserdata(L, cairo_surface_reference(Manager::get().wallpaper));
    return 1;
}

/** Get the content of the root window as a cairo surface.
 *
 * @property content
 * @tparam raw_surface content A cairo surface with the root window content (aka the whole surface
 * from every screens).
 * @propertydefault This is the live content. Use `gears.surface(root.content)` to
 *  take a screenshot.
 * @see gears.surface
 */
static int luaA_root_get_content(lua_State* L) {
    cairo_surface_t* surface;

    surface = cairo_xcb_surface_create(getConnection().getConnection(),
                                       Manager::get().screen->root,
                                       Manager::get().default_visual,
                                       Manager::get().screen->width_in_pixels,
                                       Manager::get().screen->height_in_pixels);

    lua_pushlightuserdata(L, surface);
    return 1;
}

/** Get the size of the root window.
 *
 * @treturn integer Width of the root window.
 * @treturn integer height of the root window.
 * @staticfct size
 */
static int luaA_root_size(lua_State* L) {
    lua_pushinteger(L, Manager::get().screen->width_in_pixels);
    lua_pushinteger(L, Manager::get().screen->height_in_pixels);
    return 2;
}

/** Get the physical size of the root window, in millimeter.
 *
 * @treturn integer Width of the root window, in millimeters.
 * @treturn integer height of the root window, in millimeters.
 * @staticfct size_mm
 */
static int luaA_root_size_mm(lua_State* L) {
    lua_pushinteger(L, Manager::get().screen->width_in_millimeters);
    lua_pushinteger(L, Manager::get().screen->height_in_millimeters);
    return 2;
}

/** Get the attached tags.
 * @treturn table A table with all tags.
 * @staticfct tags
 */
static int luaA_root_tags(lua_State* L) {
    lua_createtable(L, Manager::get().tags.size(), 0);
    for (int i = 0; i < (int)Manager::get().tags.size(); i++) {
        luaA_object_push(L, Manager::get().tags[i].get());
        lua_rawseti(L, -2, i + 1);
    }

    return 1;
}

/**
 * Add a custom call handler.
 */
static int luaA_root_set_call_handler(lua_State* L) {
    return Lua::registerfct(L, 1, &miss_call_handler);
}

/**
 * Add a custom property handler (getter).
 */
static int luaA_root_set_index_miss_handler(lua_State* L) {
    return Lua::registerfct(L, 1, &miss_index_handler);
}

/**
 * Add a custom property handler (setter).
 */
static int luaA_root_set_newindex_miss_handler(lua_State* L) {
    return Lua::registerfct(L, 1, &miss_newindex_handler);
}

/** Root library.
 * \param L The Lua VM state.
 * \return The number of elements pushed on stack.
 * \luastack
 */
static int luaA_root_index(lua_State* L) {
    if (miss_index_handler) {
        return Lua::call_handler(L, miss_index_handler);
    }

    return Lua::default_index(L);
}

/** Newindex for root.
 * \param L The Lua VM state.
 * \return The number of elements pushed on stack.
 */
static int luaA_root_newindex(lua_State* L) {
    /* Call the lua root property handler */
    if (miss_newindex_handler) {
        return Lua::call_handler(L, miss_newindex_handler);
    }

    return Lua::default_newindex(L);
}
[[gnu::used]] const struct luaL_Reg awesome_root_methods[] = {
  {                 "_buttons",                   luaA_root_buttons},
  {                    "_keys",                      luaA_root_keys},
  {                   "cursor",                    luaA_root_cursor},
  {               "fake_input",                luaA_root_fake_input},
  {                  "drawins",                   luaA_root_drawins},
  {               "_wallpaper",                 luaA_root_wallpaper},
  {                  "content",               luaA_root_get_content},
  {                     "size",                      luaA_root_size},
  {                  "size_mm",                   luaA_root_size_mm},
  {                     "tags",                      luaA_root_tags},
  {                  "__index",                     luaA_root_index},
  {               "__newindex",                  luaA_root_newindex},
  {   "set_index_miss_handler",    luaA_root_set_index_miss_handler},
  {         "set_call_handler",          luaA_root_set_call_handler},
  {"set_newindex_miss_handler", luaA_root_set_newindex_miss_handler},

  {                       NULL,                                NULL}
};
[[gnu::used]] const struct luaL_Reg awesome_root_meta[] = {
  {NULL, NULL}
};
// vim: filetype=c:expandtab:shiftwidth=4:tabstop=8:softtabstop=4:textwidth=80
