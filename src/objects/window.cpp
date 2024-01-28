/*
 * window.c - window object
 *
 * Copyright © 2009 Julien Danjou <julien@danjou.info>
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

/** Handling of X properties.
 *
 * This can not be used as a standalone class, but is instead referenced
 * explicitely in the classes, where it can be used. In the respective
 * classes,it then can be used via `classname:get_xproperty(...)` etc.
 * @classmod xproperties
 */

/**
 * @signal property::border_color
 */

/**
 * @signal property::border_width
 */

/**
 * @signal property::buttons
 */

/**
 * @signal property::opacity
 */

/**
 * @signal property::struts
 */

/**
 * @signal property::type
 */

#include "objects/window.h"

#include "common/atoms.h"
#include "common/luaclass.h"
#include "common/luaobject.h"
#include "common/xutil.h"
#include "ewmh.h"
#include "globalconf.h"
#include "objects/screen.h"
#include "property.h"
#include "xwindow.h"

#include <algorithm>
#include <fmt/core.h>
#include <span>

lua_class_t window_class{
  "window",
  nullptr,
  {
    [](auto* state) -> lua_object_t* {
    assert(false);
    return nullptr;
    }, [](auto* obj) { assert(false); },
    nullptr, Lua::class_index_miss_property,
    Lua::class_newindex_miss_property,
    }
};

static xcb_window_t window_get(window_t* window) {
    if (window->frame_window != XCB_NONE) {
        return window->frame_window;
    }
    return window->window;
}

/** Get or set mouse buttons bindings on a window.
 * \param L The Lua VM state.
 * \return The number of elements pushed on the stack.
 */
static int luaA_window_buttons(lua_State* L) {
    auto window = window_class.checkudata<window_t>(L, 1);

    if (lua_gettop(L) == 2) {
        luaA_button_array_set(L, 1, 2, &window->buttons);
        luaA_object_emit_signal(L, 1, "property::buttons", 0);
        xwindow_buttons_grab(window->window, window->buttons);
    }

    return luaA_button_array_get(L, 1, window->buttons);
}

/** Return window struts (reserved space at the edge of the screen).
 * \param L The Lua VM state.
 * \return The number of elements pushed on stack.
 */
static int luaA_window_struts(lua_State* L) {
    auto window = window_class.checkudata<window_t>(L, 1);

    if (lua_gettop(L) == 2) {
        luaA_tostrut(L, 2, &window->strut);
        ewmh_update_strut(window->window, &window->strut);
        luaA_object_emit_signal(L, 1, "property::struts", 0);
        /* We don't know the correct screen, update them all */
        for (auto* s : getGlobals().screens) {
            screen_update_workarea(s);
        }
    }

    return luaA_pushstrut(L, window->strut);
}

/** Set a window opacity.
 * \param L The Lua VM state.
 * \param idx The index of the window on the stack.
 * \param opacity The opacity value.
 */
void window_set_opacity(lua_State* L, int idx, double opacity) {
    auto window = window_class.checkudata<window_t>(L, idx);

    if (window->opacity != opacity) {
        window->opacity = opacity;
        xwindow_set_opacity(window_get(window), opacity);
        luaA_object_emit_signal(L, idx, "property::opacity", 0);
    }
}

/** Set a window opacity.
 * \param L The Lua VM state.
 * \param window The window object.
 * \return The number of elements pushed on stack.
 */
static int luaA_window_set_opacity(lua_State* L, lua_object_t* o) {
    if (lua_isnil(L, -1)) {
        window_set_opacity(L, -3, -1);
    } else {
        double d = luaL_checknumber(L, -1);
        if (d >= 0 && d <= 1) {
            window_set_opacity(L, -3, d);
        }
    }
    return 0;
}

/** Get the window opacity.
 * \param L The Lua VM state.
 * \param window The window object.
 * \return The number of elements pushed on stack.
 */
static int luaA_window_get_opacity(lua_State* L, lua_object_t* o) {
    auto window = static_cast<window_t*>(o);
    if (window->opacity >= 0) {
        lua_pushnumber(L, window->opacity);
    } else {
        /* Let's always return some "good" value */
        lua_pushnumber(L, 1);
    }
    return 1;
}

void window_border_refresh(window_t* window) {
    if (!window->border_need_update) {
        return;
    }
    window->border_need_update = false;
    xwindow_set_border_color(window_get(window), &window->border_color);
    if (window->window) {
        getConnection().configure_window(
          window_get(window), XCB_CONFIG_WINDOW_BORDER_WIDTH, window->border_width);
    }
}

/** Set the window border color.
 * \param L The Lua VM state.
 * \param window The window object.
 * \return The number of elements pushed on stack.
 */
static int luaA_window_set_border_color(lua_State* L, lua_object_t* o) {
    auto window = static_cast<window_t*>(o);
    size_t len = 0;
    const char* color_name = luaL_checklstring(L, -1, &len);

    if (color_name && color_init_reply(color_init_unchecked(
                        &window->border_color, color_name, len, getGlobals().visual))) {
        window->border_need_update = true;
        luaA_object_emit_signal(L, -3, "property::border_color", 0);
    }

    return 0;
}

/** Set a window border width.
 * \param L The Lua VM state.
 * \param idx The window index.
 * \param width The border width.
 */
void window_set_border_width(lua_State* L, int idx, int width) {
    auto window = window_class.checkudata<window_t>(L, idx);
    uint16_t old_width = window->border_width;

    if (width == window->border_width || width < 0) {
        return;
    }

    window->border_need_update = true;
    window->border_width = width;

    if (window->border_width_callback) {
        (*window->border_width_callback)(window, old_width, width);
    }

    luaA_object_emit_signal(L, idx, "property::border_width", 0);
}

/** Push window type to stack.
 * \param L The Lua VM state.
 * \param window The window object.
 * \return The number of elements pushed on stack.
 */

namespace Lua {
int Pusher<window_type_t>::push(State& L, const window_type_t t) {
    switch (t) {
    case WINDOW_TYPE_DESKTOP: L.push("desktop"); break;
    case WINDOW_TYPE_DOCK: L.push("dock"); break;
    case WINDOW_TYPE_SPLASH: L.push("splash"); break;
    case WINDOW_TYPE_DIALOG: L.push("dialog"); break;
    case WINDOW_TYPE_MENU: L.push("menu"); break;
    case WINDOW_TYPE_TOOLBAR: L.push("toolbar"); break;
    case WINDOW_TYPE_UTILITY: L.push("utility"); break;
    case WINDOW_TYPE_DROPDOWN_MENU: L.push("dropdown_menu"); break;
    case WINDOW_TYPE_POPUP_MENU: L.push("popup_menu"); break;
    case WINDOW_TYPE_TOOLTIP: L.push("tooltip"); break;
    case WINDOW_TYPE_NOTIFICATION: L.push("notification"); break;
    case WINDOW_TYPE_COMBO: L.push("combo"); break;
    case WINDOW_TYPE_DND: L.push("dnd"); break;
    case WINDOW_TYPE_NORMAL: L.push("normal"); break;
    default: return 0;
    }
    return 1;
}
} // namespace Lua
/** Set the window type.
 * \param L The Lua VM state.
 * \param window The window object.
 * \return The number of elements pushed on stack.
 */
int luaA_window_set_type(lua_State* L, window_t* w) {
    window_type_t type;
    auto buf = Lua::checkstring(L, -1);

    if (buf == "desktop") {
        type = WINDOW_TYPE_DESKTOP;
    } else if (buf == "dock") {
        type = WINDOW_TYPE_DOCK;
    } else if (buf == "splash") {
        type = WINDOW_TYPE_SPLASH;
    } else if (buf == "dialog") {
        type = WINDOW_TYPE_DIALOG;
    } else if (buf == "menu") {
        type = WINDOW_TYPE_MENU;
    } else if (buf == "toolbar") {
        type = WINDOW_TYPE_TOOLBAR;
    } else if (buf == "utility") {
        type = WINDOW_TYPE_UTILITY;
    } else if (buf == "dropdown_menu") {
        type = WINDOW_TYPE_DROPDOWN_MENU;
    } else if (buf == "popup_menu") {
        type = WINDOW_TYPE_POPUP_MENU;
    } else if (buf == "tooltip") {
        type = WINDOW_TYPE_TOOLTIP;
    } else if (buf == "notification") {
        type = WINDOW_TYPE_NOTIFICATION;
    } else if (buf == "combo") {
        type = WINDOW_TYPE_COMBO;
    } else if (buf == "dnd") {
        type = WINDOW_TYPE_DND;
    } else if (buf == "normal") {
        type = WINDOW_TYPE_NORMAL;
    } else {
        Lua::warn(L, "Unknown window type '%s'", buf->data());
        return 0;
    }

    if (w->type != type) {
        w->type = type;
        if (w->window != XCB_WINDOW_NONE) {
            ewmh_update_window_type(w->window, window_translate_type(w->type));
        }
        luaA_object_emit_signal(L, -3, "property::type", 0);
    }

    return 0;
}

static const xproperty* luaA_find_xproperty(lua_State* L, int idx) {
    const char* name = luaL_checkstring(L, idx);

    auto it = std::ranges::find_if(getGlobals().xproperties,
                                   [name](const auto& prop) { return prop.name == name; });
    if (it != getGlobals().xproperties.end()) {
        return &(*it);
    }
    luaL_argerror(L, idx, "Unknown xproperty");
    return NULL;
}

int window_set_xproperty(lua_State* L, xcb_window_t window, int prop_idx, int value_idx) {
    const xproperty* prop = luaA_find_xproperty(L, prop_idx);

    if (lua_isnil(L, value_idx)) {
        xcb_delete_property(getGlobals().x.connection, window, prop->atom);
        return 0;
    }
    if (prop->type == xproperty::PROP_STRING) {
        size_t len = 0;
        const char* data = luaL_checklstring(L, value_idx, &len);
        getConnection().replace_property(window, prop->atom, UTF8_STRING, std::span(data, len));
    } else if (prop->type == xproperty::PROP_NUMBER || prop->type == xproperty::PROP_BOOLEAN) {
        uint32_t data = (prop->type == xproperty::PROP_NUMBER)
                          ? Lua::checkinteger_range(L, value_idx, 0, UINT32_MAX)
                          : Lua::checkboolean(L, value_idx);
        getConnection().replace_property(window, prop->atom, XCB_ATOM_CARDINAL, data);
    } else {
        log_fatal("Got an xproperty with invalid type");
    }
    return 0;
}

int window_get_xproperty(lua_State* L, xcb_window_t window, int prop_idx) {
    const xproperty* prop = luaA_find_xproperty(L, prop_idx);
    xcb_atom_t type;
    const char* data;
    xcb_get_property_reply_t* reply;
    uint32_t length;

    type = prop->type == xproperty::PROP_STRING ? UTF8_STRING : XCB_ATOM_CARDINAL;
    length = prop->type == xproperty::PROP_STRING ? UINT32_MAX : 1;
    reply = xcb_get_property_reply(
      getGlobals().x.connection,
      xcb_get_property_unchecked(
        getGlobals().x.connection, false, window, prop->atom, type, 0, length),
      NULL);
    if (!reply) {
        return 0;
    }

    data = (const char*)xcb_get_property_value(reply);

    if (prop->type == xproperty::PROP_STRING) {
        lua_pushlstring(L, data, reply->value_len);
    } else {
        if (reply->value_len <= 0) {
            p_delete(&reply);
            return 0;
        }
        if (prop->type == xproperty::PROP_NUMBER) {
            lua_pushinteger(L, *(uint32_t*)data);
        } else {
            lua_pushboolean(L, *(uint32_t*)data);
        }
    }

    p_delete(&reply);
    return 1;
}

/** Change a xproperty.
 *
 * @param name The name of the X11 property
 * @param value The new value for the property
 * @function set_xproperty
 */
static int luaA_window_set_xproperty(lua_State* L) {
    auto w = window_class.checkudata<window_t>(L, 1);
    return window_set_xproperty(L, w->window, 2, 3);
}

/** Get the value of a xproperty.
 *
 * @param name The name of the X11 property
 * @function get_xproperty
 */
static int luaA_window_get_xproperty(lua_State* L) {
    auto w = window_class.checkudata<window_t>(L, 1);
    return window_get_xproperty(L, w->window, 2);
}

/* Translate a window_type_t into the corresponding EWMH atom.
 * @param type The type to return.
 * @return The EWMH atom for this type.
 */
uint32_t window_translate_type(window_type_t type) {
    switch (type) {
    case WINDOW_TYPE_NORMAL: return _NET_WM_WINDOW_TYPE_NORMAL;
    case WINDOW_TYPE_DESKTOP: return _NET_WM_WINDOW_TYPE_DESKTOP;
    case WINDOW_TYPE_DOCK: return _NET_WM_WINDOW_TYPE_DOCK;
    case WINDOW_TYPE_SPLASH: return _NET_WM_WINDOW_TYPE_SPLASH;
    case WINDOW_TYPE_DIALOG: return _NET_WM_WINDOW_TYPE_DIALOG;
    case WINDOW_TYPE_MENU: return _NET_WM_WINDOW_TYPE_MENU;
    case WINDOW_TYPE_TOOLBAR: return _NET_WM_WINDOW_TYPE_TOOLBAR;
    case WINDOW_TYPE_UTILITY: return _NET_WM_WINDOW_TYPE_UTILITY;
    case WINDOW_TYPE_DROPDOWN_MENU: return _NET_WM_WINDOW_TYPE_DROPDOWN_MENU;
    case WINDOW_TYPE_POPUP_MENU: return _NET_WM_WINDOW_TYPE_POPUP_MENU;
    case WINDOW_TYPE_TOOLTIP: return _NET_WM_WINDOW_TYPE_TOOLTIP;
    case WINDOW_TYPE_NOTIFICATION: return _NET_WM_WINDOW_TYPE_NOTIFICATION;
    case WINDOW_TYPE_COMBO: return _NET_WM_WINDOW_TYPE_COMBO;
    case WINDOW_TYPE_DND: return _NET_WM_WINDOW_TYPE_DND;
    }
    return _NET_WM_WINDOW_TYPE_NORMAL;
}

static int luaA_window_set_border_width(lua_State* L, lua_object_t*) {
    window_set_border_width(L, -3, round(Lua::checknumber_range(L, -1, 0, MAX_X11_SIZE)));
    return 0;
}

void window_class_setup(lua_State* L) {
    static const struct luaL_Reg window_methods[] = {
      {NULL, NULL}
    };

    static const struct luaL_Reg window_meta[] = {
      {       "struts",        luaA_window_struts},
      {     "_buttons",       luaA_window_buttons},
      {"set_xproperty", luaA_window_set_xproperty},
      {"get_xproperty", luaA_window_get_xproperty},
      {           NULL,                      NULL}
    };

    window_class.setup(L, window_methods, window_meta);

    window_class.add_property("window", nullptr, exportProp<&window_t::window>(), nullptr);
    window_class.add_property(
      "_opacity", luaA_window_set_opacity, luaA_window_get_opacity, luaA_window_set_opacity);
    window_class.add_property("_border_color",
                              luaA_window_set_border_color,
                              exportProp<&window_t::border_color>(),
                              luaA_window_set_border_color);
    window_class.add_property("_border_width",
                              luaA_window_set_border_width,
                              exportProp<&window_t::border_width>(),
                              luaA_window_set_border_width);
}

// vim: filetype=c:expandtab:shiftwidth=4:tabstop=8:softtabstop=4:textwidth=80
