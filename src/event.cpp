/*
 * event.c - event handlers
 *
 * Copyright © 2007-2008 Julien Danjou <julien@danjou.info>
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

#include "event.h"

#include "awesome.h"
#include "common/atoms.h"
#include "common/xembed.h"
#include "common/xutil.h"
#include "ewmh.h"
#include "globalconf.h"
#include "keygrabber.h"
#include "luaa.h"
#include "mousegrabber.h"
#include "objects/client.h"
#include "objects/drawin.h"
#include "objects/screen.h"
#include "objects/selection_acquire.h"
#include "objects/selection_getter.h"
#include "objects/selection_watcher.h"
#include "objects/tag.h"
#include "property.h"
#include "systray.h"
#include "xkb.h"
#include "xwindow.h"

#include <map>
#include <set>
#include <vector>
#include <xcb/randr.h>
#include <xcb/shape.h>
#include <xcb/xcb.h>
#include <xcb/xcb_atom.h>
#include <xcb/xcb_event.h>
#include <xcb/xcb_icccm.h>
#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wkeyword-macro"
#endif
#define explicit explicit_
#ifdef __clang__
#pragma clang diagnostic pop
#endif
#include <xcb/xkb.h>
#undef explicit
#include <xcb/xfixes.h>

#define DO_EVENT_HOOK_CALLBACK(type, xcbtype, xcbeventprefix, arraytype, match) \
    static void event_##xcbtype##_callback(xcb_##xcbtype##_press_event_t* ev,   \
                                           const arraytype& arr,                \
                                           lua_State* L,                        \
                                           int oud,                             \
                                           int nargs,                           \
                                           void* data) {                        \
        int abs_oud = oud < 0 ? ((lua_gettop(L) + 1) + oud) : oud;              \
        int item_matching = 0;                                                  \
        for (auto* item : arr)                                                  \
            if (match(ev, item, data)) {                                        \
                if (oud)                                                        \
                    luaA_object_push_item(L, abs_oud, item);                    \
                else                                                            \
                    luaA_object_push(L, item);                                  \
                item_matching++;                                                \
            }                                                                   \
        for (; item_matching > 0; item_matching--) {                            \
            switch (ev->response_type) {                                        \
            case xcbeventprefix##_PRESS:                                        \
                for (int i = 0; i < nargs; i++)                                 \
                    lua_pushvalue(L, -nargs - item_matching);                   \
                luaA_object_emit_signal(L, -nargs - 1, "press", nargs);         \
                break;                                                          \
            case xcbeventprefix##_RELEASE:                                      \
                for (int i = 0; i < nargs; i++)                                 \
                    lua_pushvalue(L, -nargs - item_matching);                   \
                luaA_object_emit_signal(L, -nargs - 1, "release", nargs);       \
                break;                                                          \
            }                                                                   \
            lua_pop(L, 1);                                                      \
        }                                                                       \
        lua_pop(L, nargs);                                                      \
    }

static bool event_key_match(xcb_key_press_event_t* ev, keyb_t* k, void* data) {
    assert(data);
    xcb_keysym_t keysym = *(xcb_keysym_t*)data;
    return (((k->keycode && ev->detail == k->keycode) || (k->keysym && keysym == k->keysym)) &&
            (k->modifiers == XCB_BUTTON_MASK_ANY || k->modifiers == ev->state));
}

static bool event_button_match(xcb_button_press_event_t* ev, button_t* b, void* data) {
    return ((!b->button() || ev->detail == b->button()) &&
            (b->modifiers() == XCB_BUTTON_MASK_ANY || b->modifiers() == ev->state));
}

DO_EVENT_HOOK_CALLBACK(button_t, button, XCB_BUTTON, std::vector<button_t*>, event_button_match)
DO_EVENT_HOOK_CALLBACK(keyb_t, key, XCB_KEY, std::vector<keyb_t*>, event_key_match)

/** Handle an event with mouse grabber if needed
 * \param x The x coordinate.
 * \param y The y coordinate.
 * \param mask The mask buttons.
 * \return True if the event was handled.
 */
static bool event_handle_mousegrabber(int x, int y, uint16_t mask) {
    if (Manager::get().mousegrabber) {
        lua_State* L = globalconf_get_lua_State();
        mousegrabber_handleevent(L, x, y, mask);
        lua_rawgeti(L, LUA_REGISTRYINDEX, Manager::get().mousegrabber.idx.idx);
        if (!Lua::dofunction(L, 1, 1)) {
            log_warn("Stopping mousegrabber.");
            luaA_mousegrabber_stop(L);
        } else {
            if (!lua_isboolean(L, -1) || !lua_toboolean(L, -1)) {
                luaA_mousegrabber_stop(L);
            }
            lua_pop(L, 1); /* pop returned value */
        }
        return true;
    }
    return false;
}

/** Emit a button signal.
 * The top of the lua stack has to be the object on which to emit the event.
 * \param L The Lua VM state.
 * \param ev The event to handle.
 */
static void event_emit_button(lua_State* L, xcb_button_press_event_t* ev) {
    const char* name;
    switch (XCB_EVENT_RESPONSE_TYPE(ev)) {
    case XCB_BUTTON_PRESS: name = "button::press"; break;
    case XCB_BUTTON_RELEASE: name = "button::release"; break;
    default: log_fatal("Invalid event type");
    }

    /* Push the event's info */
    lua_pushinteger(L, ev->event_x);
    lua_pushinteger(L, ev->event_y);
    lua_pushinteger(L, ev->detail);
    luaA_pushmodifiers(L, ev->state);
    /* And emit the signal */
    luaA_object_emit_signal(L, -5, name, 4);
}

/** The button press event handler.
 * \param ev The event.
 */
static void event_handle_button(xcb_button_press_event_t* ev) {
    lua_State* L = globalconf_get_lua_State();
    client* c;
    drawin_t* drawin;

    Manager::get().x.update_timestamp(ev);

    {
        /* ev->state contains the state before the event. Compute the state
         * after the event for the mousegrabber.
         */
        uint16_t state = ev->state, change = 1 << (ev->detail - 1 + 8);
        if (XCB_EVENT_RESPONSE_TYPE(ev) == XCB_BUTTON_PRESS) {
            state |= change;
        } else {
            state &= ~change;
        }
        if (event_handle_mousegrabber(ev->root_x, ev->root_y, state)) {
            return;
        }
    }

    /* ev->state is
     * button status (8 bits) + modifiers status (8 bits)
     * we don't care for button status that we get, especially on release, so
     * drop them */
    ev->state &= 0x00ff;

    if ((drawin = drawin_getbywin(ev->event)) || (drawin = drawin_getbywin(ev->child))) {
        /* If the drawin is child, then x,y are
         * relative to root window */
        if (drawin->window == ev->child) {
            ev->event_x -= drawin->geometry.top_left.x + drawin->border_width;
            ev->event_y -= drawin->geometry.top_left.y + drawin->border_width;
        }

        /* Push the drawable */
        luaA_object_push(L, drawin);
        luaA_object_push_item(L, -1, drawin->drawable);
        /* and handle the button raw button event */
        event_emit_button(L, ev);
        lua_pop(L, 1);
        /* check if any button object matches */
        event_button_callback(ev, drawin->buttons, L, -1, 1, NULL);
        /* Either we are receiving this due to ButtonPress/Release on the root
         * window or because we grabbed the button on the window. In the later
         * case we have to call AllowEvents.
         * Use AsyncPointer instead of ReplayPointer so that the event is
         * "eaten" instead of being handled again on the root window.
         */
        if (ev->child == XCB_NONE) {
            getConnection().allow_events(XCB_ALLOW_ASYNC_POINTER, ev->time);
        }
    } else if ((c = client_getbyframewin(ev->event)) || (c = client_getbywin(ev->event))) {
        /* For clicks inside of c->window, we get two events. Once because of a
         * passive grab on c->window and then again for c->frame_window.
         * Ignore the second event (identifiable by ev->child != XCB_NONE).
         */
        if (ev->event != c->frame_window || ev->child == XCB_NONE) {
            luaA_object_push(L, c);
            if (c->window == ev->event) {
                /* Button event into the client itself (not titlebar), translate
                 * into the frame window.
                 */
                ev->event_x += c->titlebar[CLIENT_TITLEBAR_LEFT].size;
                ev->event_y += c->titlebar[CLIENT_TITLEBAR_TOP].size;
            }
            /* And handle the button raw button event */
            event_emit_button(L, ev);
            /* then check if a titlebar was "hit" */
            if (c->frame_window == ev->event) {
                point p = {ev->event_x, ev->event_y};
                drawable_t* d = client_get_drawable_offset(c, &p);
                if (d) {
                    /* Copy the event so that we can fake x/y */
                    xcb_button_press_event_t event = *ev;
                    event.event_x = p.x;
                    event.event_y = p.y;
                    luaA_object_push_item(L, -1, d);
                    event_emit_button(L, &event);
                    lua_pop(L, 1);
                }
            }
            /* then check if any button objects match */
            event_button_callback(ev, c->buttons, L, -1, 1, NULL);
        }
        getConnection().allow_events(XCB_ALLOW_REPLAY_POINTER, ev->time);
    } else if (ev->child == XCB_NONE && Manager::get().screen->root == ev->event) {
        event_button_callback(ev, Manager::get().buttons, L, 0, 0, NULL);
        return;
    }
}

static void event_handle_configurerequest_configure_window(xcb_configure_request_event_t* ev) {
    uint16_t config_win_mask = 0;
    std::array<uint32_t, 7> config_win_vals;
    unsigned short i = 0;

    if (ev->value_mask & XCB_CONFIG_WINDOW_X) {
        config_win_mask |= XCB_CONFIG_WINDOW_X;
        config_win_vals[i++] = ev->x;
    }
    if (ev->value_mask & XCB_CONFIG_WINDOW_Y) {
        config_win_mask |= XCB_CONFIG_WINDOW_Y;
        config_win_vals[i++] = ev->y;
    }
    if (ev->value_mask & XCB_CONFIG_WINDOW_WIDTH) {
        config_win_mask |= XCB_CONFIG_WINDOW_WIDTH;
        config_win_vals[i++] = ev->width;
    }
    if (ev->value_mask & XCB_CONFIG_WINDOW_HEIGHT) {
        config_win_mask |= XCB_CONFIG_WINDOW_HEIGHT;
        config_win_vals[i++] = ev->height;
    }
    if (ev->value_mask & XCB_CONFIG_WINDOW_BORDER_WIDTH) {
        config_win_mask |= XCB_CONFIG_WINDOW_BORDER_WIDTH;
        config_win_vals[i++] = ev->border_width;
    }
    if (ev->value_mask & XCB_CONFIG_WINDOW_SIBLING) {
        config_win_mask |= XCB_CONFIG_WINDOW_SIBLING;
        config_win_vals[i++] = ev->sibling;
    }
    if (ev->value_mask & XCB_CONFIG_WINDOW_STACK_MODE) {
        config_win_mask |= XCB_CONFIG_WINDOW_STACK_MODE;
        config_win_vals[i++] = ev->stack_mode;
    }

    getConnection().configure_window(ev->window, config_win_mask, config_win_vals);
}

/** The configure event handler.
 * \param ev The event.
 */
static void event_handle_configurerequest(xcb_configure_request_event_t* ev) {
    client* c;

    if ((c = client_getbywin(ev->window))) {
        lua_State* L = globalconf_get_lua_State();
        if (ev->value_mask & XCB_CONFIG_WINDOW_BORDER_WIDTH) {
            luaA_object_push(L, c);
            window_set_border_width(L, -1, ev->border_width);
            lua_pop(L, 1);
        }

        area_t geometry = c->geometry;
        uint16_t bw = c->border_width;
        uint16_t tb_left = c->titlebar[CLIENT_TITLEBAR_LEFT].size;
        uint16_t tb_right = c->titlebar[CLIENT_TITLEBAR_RIGHT].size;
        uint16_t tb_top = c->titlebar[CLIENT_TITLEBAR_TOP].size;
        uint16_t tb_bottom = c->titlebar[CLIENT_TITLEBAR_BOTTOM].size;
        uint16_t deco_left = bw + tb_left;
        uint16_t deco_right = bw + tb_right;
        uint16_t deco_top = bw + tb_top;
        uint16_t deco_bottom = bw + tb_bottom;
        int16_t diff_w = 0, diff_h = 0;

        if (ev->value_mask & XCB_CONFIG_WINDOW_WIDTH) {
            uint16_t old_w = geometry.width;
            geometry.width = ev->width;
            /* The ConfigureRequest specifies the size of the client window, we want the frame */
            geometry.width += tb_left + tb_right;
            diff_w = geometry.width - old_w;
        }
        if (ev->value_mask & XCB_CONFIG_WINDOW_HEIGHT) {
            uint16_t old_h = geometry.height;
            geometry.height = ev->height;
            /* The ConfigureRequest specifies the size of the client window, we want the frame */
            geometry.height += tb_top + tb_bottom;
            diff_h = geometry.height - old_h;
        }

        /* If the client resizes without moving itself, apply window gravity */
        if (c->size_hints.flags & XCB_ICCCM_SIZE_HINT_P_WIN_GRAVITY) {
            xwindow_translate_for_gravity((xcb_gravity_t)c->size_hints.win_gravity,
                                          0,
                                          0,
                                          diff_w,
                                          diff_h,
                                          &geometry.top_left.x,
                                          &geometry.top_left.y);
        }
        if (ev->value_mask & XCB_CONFIG_WINDOW_X) {
            geometry.top_left.x = ev->x;
            if (c->size_hints.flags & XCB_ICCCM_SIZE_HINT_P_WIN_GRAVITY) {
                xwindow_translate_for_gravity((xcb_gravity_t)c->size_hints.win_gravity,
                                              deco_left,
                                              0,
                                              deco_right,
                                              0,
                                              &geometry.top_left.x,
                                              NULL);
            }
        }
        if (ev->value_mask & XCB_CONFIG_WINDOW_Y) {
            geometry.top_left.y = ev->y;
            if (c->size_hints.flags & XCB_ICCCM_SIZE_HINT_P_WIN_GRAVITY) {
                xwindow_translate_for_gravity((xcb_gravity_t)c->size_hints.win_gravity,
                                              0,
                                              deco_top,
                                              0,
                                              deco_bottom,
                                              NULL,
                                              &geometry.top_left.y);
            }
        }

        c->got_configure_request = true;

        /* Request the changes to be applied */
        luaA_object_push(L, c);
        lua_pushstring(L, "ewmh"); /* context */
        lua_newtable(L);           /* props */

        /* area, it needs to be directly in the `hints` table to comply with
           the "protocol"
         */
        lua_pushstring(L, "x");
        lua_pushinteger(L, geometry.top_left.x);
        lua_rawset(L, -3);

        lua_pushstring(L, "y");
        lua_pushinteger(L, geometry.top_left.y);
        lua_rawset(L, -3);

        lua_pushstring(L, "width");
        lua_pushinteger(L, geometry.width);
        lua_rawset(L, -3);

        lua_pushstring(L, "height");
        lua_pushinteger(L, geometry.height);
        lua_rawset(L, -3);

        luaA_object_emit_signal(L, -3, "request::geometry", 2);
        lua_pop(L, 1);
    } else if (std::find_if(Manager::get().embedded.begin(),
                            Manager::get().embedded.end(),
                            [xwin = ev->window](const auto& win) { return win.win == xwin; }) !=
               Manager::get().embedded.end()) {
        /* Ignore this so that systray icons cannot resize themselves.
         * We decide their size!
         * However, Xembed says that we act like a WM to the embedded window and
         * thus we have to send a synthetic configure notify informing the
         * window that its configure request was denied.
         */
        auto geom_cookie = getConnection().get_geometry_unchecked(ev->window);
        xcb_translate_coordinates_cookie_t coords_cookie =
          getConnection().translate_coordinates_unchecked(
            ev->window, Manager::get().screen->root, {0, 0});
        auto geom = getConnection().get_geometry_reply(geom_cookie);
        auto coords = getConnection().translate_coordinates_reply(coords_cookie);

        if (geom && coords) {
            xwindow_configure(ev->window,
                              (area_t){
                                {coords->dst_x, coords->dst_y},
                                geom->width, geom->height
            },
                              0);
        }
    } else {
        event_handle_configurerequest_configure_window(ev);
    }
}

/** The configure notify event handler.
 * \param ev The event.
 */
static void event_handle_configurenotify(xcb_configure_notify_event_t* ev) {
    xcb_screen_t* screen = Manager::get().screen;

    if (ev->window == screen->root) {
        screen_schedule_refresh();
    }

    /* Copy what XRRUpdateConfiguration() would do: Update the configuration */
    if (ev->window == screen->root) {
        screen->width_in_pixels = ev->width;
        screen->height_in_pixels = ev->height;
    }
}

/** The destroy notify event handler.
 * \param ev The event.
 */
static void event_handle_destroynotify(xcb_destroy_notify_event_t* ev) {
    client* c;

    if ((c = client_getbywin(ev->window))) {
        client_unmanage(c, CLIENT_UNMANAGE_DESTROYED);
    } else if (std::erase_if(Manager::get().embedded,
                             [xwin = ev->window](const auto& win) { return win.win == xwin; })) {
        Lua::systray_invalidate();
    }
}

/** Record that the given drawable contains the pointer.
 */
void event_drawable_under_mouse(lua_State* L, int ud) {
    void* d;

    lua_pushvalue(L, ud);
    d = luaA_object_ref(L, -1);

    if (d == Manager::get().drawable_under_mouse) {
        /* Nothing to do */
        luaA_object_unref(L, d);
        return;
    }

    if (Manager::get().drawable_under_mouse != NULL) {
        /* Emit leave on previous drawable */
        luaA_object_push(L, Manager::get().drawable_under_mouse);
        luaA_object_emit_signal(L, -1, "mouse::leave", 0);
        lua_pop(L, 1);

        /* Unref the previous drawable */
        luaA_object_unref(L, Manager::get().drawable_under_mouse);
        Manager::get().drawable_under_mouse = NULL;
    }
    if (d != NULL) {
        /* Reference the drawable for leave event later */
        Manager::get().drawable_under_mouse = (drawable_t*)d;

        /* Emit enter */
        luaA_object_emit_signal(L, ud, "mouse::enter", 0);
    }
}

/** The motion notify event handler.
 * \param ev The event.
 */
static void event_handle_motionnotify(xcb_motion_notify_event_t* ev) {
    lua_State* L = globalconf_get_lua_State();
    drawin_t* w;
    client* c;

    Manager::get().x.update_timestamp(ev);

    if (event_handle_mousegrabber(ev->root_x, ev->root_y, ev->state)) {
        return;
    }

    if ((c = client_getbyframewin(ev->event))) {
        luaA_object_push(L, c);
        lua_pushinteger(L, ev->event_x);
        lua_pushinteger(L, ev->event_y);
        luaA_object_emit_signal(L, -3, "mouse::move", 2);

        /* now check if a titlebar was "hit" */
        point pt{ev->event_x, ev->event_y};
        drawable_t* d = client_get_drawable_offset(c, &pt);
        if (d) {
            luaA_object_push_item(L, -1, d);
            event_drawable_under_mouse(L, -1);
            lua_pushinteger(L, pt.x);
            lua_pushinteger(L, pt.y);
            luaA_object_emit_signal(L, -3, "mouse::move", 2);
            lua_pop(L, 1);
        }
        lua_pop(L, 1);
    }

    if ((w = drawin_getbywin(ev->event))) {
        luaA_object_push(L, w);
        luaA_object_push_item(L, -1, w->drawable);
        event_drawable_under_mouse(L, -1);
        lua_pushinteger(L, ev->event_x);
        lua_pushinteger(L, ev->event_y);
        luaA_object_emit_signal(L, -3, "mouse::move", 2);
        lua_pop(L, 2);
    }
}

/** The leave notify event handler.
 * \param ev The event.
 */
static void event_handle_leavenotify(xcb_leave_notify_event_t* ev) {
    lua_State* L = globalconf_get_lua_State();
    client* c;

    Manager::get().x.update_timestamp(ev);

    /*
     * Ignore events with non-normal modes. Those are because a grab
     * activated/deactivated. Everything will be "back to normal" after the
     * grab.
     */
    if (ev->mode != XCB_NOTIFY_MODE_NORMAL) {
        return;
    }

    if ((c = client_getbyframewin(ev->event))) {
        /* The window was left in some way, so definitely no titlebar has the
         * mouse cursor.
         */
        lua_pushnil(L);
        event_drawable_under_mouse(L, -1);
        lua_pop(L, 1);

        /* If detail is inferior, it means that the cursor is now in some child
         * window of our window. Thus, the titlebar was left, but now the cursor
         * is in the actual child window. Thus, ignore detail=Inferior for
         * leaving client windows.
         */
        if (ev->detail != XCB_NOTIFY_DETAIL_INFERIOR) {
            luaA_object_push(L, c);
            luaA_object_emit_signal(L, -1, "mouse::leave", 0);
            lua_pop(L, 1);
        }
    } else if (ev->detail != XCB_NOTIFY_DETAIL_INFERIOR) {
        /* Some window was left. This must be a drawin. Ignore detail=Inferior,
         * because this means that some child window now contains the mouse
         * cursor, i.e. a systray window. Everything else is a real 'leave'.
         */
        lua_pushnil(L);
        event_drawable_under_mouse(L, -1);
        lua_pop(L, 1);
    }
}

/** The enter notify event handler.
 * \param ev The event.
 */
static void event_handle_enternotify(xcb_enter_notify_event_t* ev) {
    lua_State* L = globalconf_get_lua_State();
    client* c;
    drawin_t* drawin;

    Manager::get().x.update_timestamp(ev);

    /*
     * Ignore events with non-normal modes. Those are because a grab
     * activated/deactivated. Everything will be "back to normal" after the
     * grab.
     */
    if (ev->mode != XCB_NOTIFY_MODE_NORMAL) {
        return;
    }

    /*
     * We ignore events with detail "inferior".  This detail means that the
     * cursor was previously inside of a child window and now left that child
     * window. For our purposes, the cursor was already inside our window
     * before.
     * One exception are titlebars: They are not their own window, but are
     * "outside of the actual client window".
     */

    if (ev->detail != XCB_NOTIFY_DETAIL_INFERIOR && (drawin = drawin_getbywin(ev->event))) {
        luaA_object_push(L, drawin);
        luaA_object_push_item(L, -1, drawin->drawable);
        event_drawable_under_mouse(L, -1);
        lua_pop(L, 2);
    }

    if ((c = client_getbyframewin(ev->event))) {
        luaA_object_push(L, c);
        /* Detail=Inferior means that a child of the frame window now contains
         * the mouse cursor, i.e. the actual client now has the cursor. All
         * other details mean that the client itself was really left.
         */
        if (ev->detail != XCB_NOTIFY_DETAIL_INFERIOR) {
            luaA_object_emit_signal(L, -1, "mouse::enter", 0);
        }

        drawable_t* d = client_get_drawable(c, {ev->event_x, ev->event_y});
        if (d) {
            luaA_object_push_item(L, -1, d);
        } else {
            lua_pushnil(L);
        }
        event_drawable_under_mouse(L, -1);
        lua_pop(L, 2);
    } else if (ev->detail != XCB_NOTIFY_DETAIL_INFERIOR &&
               ev->event == Manager::get().screen->root) {
        /* When there are multiple X screens with awesome running separate
         * instances, reset focus.
         */
        Manager::get().focus.need_update = true;
    }
}

/** The focus in event handler.
 * \param ev The event.
 */
static void event_handle_focusin(xcb_focus_in_event_t* ev) {
    if (ev->event == Manager::get().screen->root) {
        /* Received focus in for root window, refocusing the focused window */
        Manager::get().focus.need_update = true;
    }

    if (ev->mode == XCB_NOTIFY_MODE_GRAB || ev->mode == XCB_NOTIFY_MODE_UNGRAB) {
        /* Ignore focus changes due to keyboard grabs */
        return;
    }

    /* Events that we are interested in: */
    switch (ev->detail) {
    /* These are events that jump between root windows.
     */
    case XCB_NOTIFY_DETAIL_ANCESTOR:
    case XCB_NOTIFY_DETAIL_INFERIOR:

    /* These are events that jump between clients.
     * Virtual events ensure we always get an event on our top-level window.
     */
    case XCB_NOTIFY_DETAIL_NONLINEAR_VIRTUAL:
    case XCB_NOTIFY_DETAIL_NONLINEAR: {
        client* c;

        if ((c = client_getbywin(ev->event))) {
            /* If there is still a pending focus change, do it now. */
            client_focus_refresh();
            client_focus_update(c);
        }
    }
    /* all other events are ignored */
    default: break;
    }
}

/** The expose event handler.
 * \param ev The event.
 */
static void event_handle_expose(xcb_expose_event_t* ev) {
    if (drawin_t* drawin = drawin_getbywin(ev->window)) {
        drawin_refresh_pixmap_partial(drawin, ev->x, ev->y, ev->width, ev->height);
    }
    if (client* client = client_getbyframewin(ev->window)) {
        client_refresh_partial(client, ev->x, ev->y, ev->width, ev->height);
    }
}

/** The key press event handler.
 * \param ev The event.
 */
static void event_handle_key(xcb_key_press_event_t* ev) {
    lua_State* L = globalconf_get_lua_State();
    Manager::get().x.update_timestamp(ev);

    if (Manager::get().keygrabber) {
        if (keygrabber_handlekpress(L, ev)) {
            lua_rawgeti(L, LUA_REGISTRYINDEX, Manager::get().keygrabber.idx.idx);

            if (!Lua::dofunction(L, 3, 0)) {
                log_warn("Stopping keygrabber.");
                luaA_keygrabber_stop(L);
            }
        }
    } else {
        /* get keysym ignoring all modifiers */
        xcb_keysym_t keysym = Manager::get().input.keysyms.get_keysym(ev->detail, 0);
        client* c;
        if ((c = client_getbywin(ev->event)) || (c = client_getbynofocuswin(ev->event))) {
            luaA_object_push(L, c);
            event_key_callback(ev, c->keys, L, -1, 1, &keysym);
        } else {
            event_key_callback(ev, Manager::get().keys, L, 0, 0, &keysym);
        }
    }
}

/** The map request event handler.
 * \param ev The event.
 */
static void event_handle_maprequest(xcb_map_request_event_t* ev) {
    client* c;
    auto wa_c = getConnection().get_window_attributes_unchecked(ev->window);

    auto wa_r = getConnection().get_window_attributes_reply(wa_c);
    if (!wa_r || wa_r->override_redirect) {
        return;
    }

    if (auto em =
          std::ranges::find_if(Manager::get().embedded,
                               [xwin = ev->window](const auto& win) { return win.win == xwin; });
        em != Manager::get().embedded.end()) {
        getConnection().map_window(ev->window);
        XEmbed::xembed_window_activate(
          getConnection(), ev->window, Manager::get().x.get_timestamp());
        /* The correct way to set this is via the _XEMBED_INFO property. Neither
         * of the XEMBED not the systray spec talk about mapping windows.
         * Apparently, Qt doesn't care and does not set an _XEMBED_INFO
         * property. Let's simulate the XEMBED_MAPPED bit.
         */
        em->info.flags |= static_cast<uint32_t>(XEmbed::InfoFlags::MAPPED);
        Lua::systray_invalidate();
    } else if ((c = client_getbywin(ev->window))) {
        /* Check that it may be visible, but not asked to be hidden */
        if (client_on_selected_tags(c) && !c->hidden) {
            lua_State* L = globalconf_get_lua_State();
            luaA_object_push(L, c);
            client_set_minimized(L, -1, false);
            lua_pop(L, 1);
            /* it will be raised, so just update ourself */
            client_raise(c);
        }
    } else {
        auto geom_c = getConnection().get_geometry_unchecked(ev->window);
        auto geom_r = getConnection().get_geometry_reply(geom_c);
        if (!geom_r) {
            return;
        }

        client_manage(ev->window, geom_r.get(), wa_r.get());
    }
}

/** The unmap notify event handler.
 * \param ev The event.
 */
static void event_handle_unmapnotify(xcb_unmap_notify_event_t* ev) {
    client* c;

    if ((c = client_getbywin(ev->window))) {
        client_unmanage(c, CLIENT_UNMANAGE_UNMAP);
    }
}

/** The randr screen change notify event handler.
 * \param ev The event.
 */
static void event_handle_randr_screen_change_notify(xcb_randr_screen_change_notify_event_t* ev) {
    /* Ignore events for other roots (do we get them at all?) */
    if (ev->root != Manager::get().screen->root) {
        return;
    }

    /* Do (part of) what XRRUpdateConfiguration() would do (update our state) */
    if (ev->rotation & (XCB_RANDR_ROTATION_ROTATE_90 | XCB_RANDR_ROTATION_ROTATE_270)) {
        Manager::get().screen->width_in_pixels = ev->height;
        Manager::get().screen->width_in_millimeters = ev->mheight;
        Manager::get().screen->height_in_pixels = ev->width;
        Manager::get().screen->height_in_millimeters = ev->mwidth;
    } else {
        Manager::get().screen->width_in_pixels = ev->width;
        Manager::get().screen->width_in_millimeters = ev->mwidth;
        Manager::get().screen->height_in_pixels = ev->height;
        Manager::get().screen->height_in_millimeters = ev->mheight;
        ;
    }

    screen_schedule_refresh();
}

/** XRandR event handler for RRNotify subtype XRROutputChangeNotifyEvent
 */
static void event_handle_randr_output_change_notify(xcb_randr_notify_event_t* ev) {
    if (ev->subCode != XCB_RANDR_NOTIFY_OUTPUT_CHANGE) {
        return;
    }
    xcb_randr_output_t output = ev->u.oc.output;
    uint8_t connection = ev->u.oc.connection;
    const char* connection_str = NULL;
    lua_State* L = globalconf_get_lua_State();

    /* The following explicitly uses XCB_CURRENT_TIME since we want to know
     * the final state of the connection. There could be more notification
     * events underway and using some "old" timestamp causes problems.
     */
    auto info = getConnection().randr().get_output_info_reply(
      getConnection().randr().get_output_info_unchecked(output, XCB_CURRENT_TIME));
    if (!info) {
        return;
    }

    switch (connection) {
    case XCB_RANDR_CONNECTION_CONNECTED: connection_str = "Connected"; break;
    case XCB_RANDR_CONNECTION_DISCONNECTED: connection_str = "Disconnected"; break;
    default: connection_str = "Unknown"; break;
    }

    lua_pushlstring(L,
                    (char*)xcb_randr_get_output_info_name(info.get()),
                    xcb_randr_get_output_info_name_length(info.get()));
    lua_pushstring(L, connection_str);
    signal_object_emit(L, &Lua::global_signals, "screen::change", 2);

    /* The docs for RRSetOutputPrimary say we get this signal */
    screen_update_primary();
}

/** The shape notify event handler.
 * \param ev The event.
 */
static void event_handle_shape_notify(xcb_shape_notify_event_t* ev) {
    client* c = client_getbywin(ev->affected_window);
    if (!c) {
        return;
    }
    lua_State* L = globalconf_get_lua_State();
    luaA_object_push(L, c);
    if (ev->shape_kind == XCB_SHAPE_SK_BOUNDING) {
        luaA_object_emit_signal(L, -1, "property::shape_client_bounding", 0);
    }
    if (ev->shape_kind == XCB_SHAPE_SK_CLIP) {
        luaA_object_emit_signal(L, -1, "property::shape_client_clip", 0);
    }
    lua_pop(L, 1);
}

/** The client message event handler.
 * \param ev The event.
 */
static void event_handle_clientmessage(xcb_client_message_event_t* ev) {
    /* check for startup notification messages */
    if (sn_xcb_display_process_event(Manager::get().sndisplay, (xcb_generic_event_t*)ev)) {
        return;
    }

    if (ev->type == WM_CHANGE_STATE) {
        client* c;
        if ((c = client_getbywin(ev->window)) && ev->format == 32 &&
            ev->data.data32[0] == XCB_ICCCM_WM_STATE_ICONIC) {
            lua_State* L = globalconf_get_lua_State();
            luaA_object_push(L, c);
            client_set_minimized(L, -1, true);
            lua_pop(L, 1);
        }
    } else if (ev->type == _XEMBED) {
        xembed_process_client_message(ev);
    } else if (ev->type == _NET_SYSTEM_TRAY_OPCODE) {
        systray_process_client_message(ev);
    } else {
        ewmh_process_client_message(ev);
    }
}

static void event_handle_reparentnotify(xcb_reparent_notify_event_t* ev) {
    client* c;

    if ((c = client_getbywin(ev->window)) && c->frame_window != ev->parent) {
        /* Ignore reparents to the root window, they *might* be caused by
         * ourselves if a client quickly unmaps and maps itself again. */
        if (ev->parent != Manager::get().screen->root) {
            client_unmanage(c, CLIENT_UNMANAGE_REPARENT);
        }
    } else if (ev->parent != Manager::get().systray.window) {
        /* Embedded window moved elsewhere, end of embedding */
        if (std::erase_if(Manager::get().embedded,
                          [xwin = ev->window](const auto& win) { return win.win == xwin; })) {
            getConnection().change_save_set(XCB_SET_MODE_DELETE, ev->window);
            Lua::systray_invalidate();
        }
    }
}

static void event_handle_selectionclear(xcb_selection_clear_event_t* ev) {
    if (ev->selection == Manager::get().x.selection_atom) {
        log_warn("Lost WM_Sn selection, exiting...");
        g_main_loop_quit(Manager::get().loop);
    } else {
        selection_handle_selectionclear(ev);
    }
}

/** \brief awesome xerror function.
 * There's no way to check accesses to destroyed windows, thus those cases are
 * ignored (especially on UnmapNotify's).
 * \param e The error event.
 */
static void xerror(xcb_generic_error_t* e) {
    /* ignore this */
    if (e->error_code == XCB_WINDOW ||
        (e->error_code == XCB_MATCH && e->major_code == XCB_SET_INPUT_FOCUS) ||
        (e->error_code == XCB_VALUE && e->major_code == XCB_KILL_CLIENT) ||
        (e->error_code == XCB_MATCH && e->major_code == XCB_CONFIGURE_WINDOW)) {
        return;
    }

#ifdef WITH_XCB_ERRORS
    const char* major =
      xcb_errors_get_name_for_major_code(Manager::get().x.errors_ctx, e->major_code);
    const char* minor =
      xcb_errors_get_name_for_minor_code(Manager::get().x.errors_ctx, e->major_code, e->minor_code);
    const char* extension = NULL;
    const char* error =
      xcb_errors_get_name_for_error(Manager::get().x.errors_ctx, e->error_code, &extension);
#else
    const char* major = xcb_event_get_request_label(e->major_code);
    const char* minor = NULL;
    const char* extension = NULL;
    const char* error = xcb_event_get_error_label(e->error_code);
#endif
    log_warn("X error: request={}{}{} (major {}, minor {}), error={}{}{} ({})",
             major,
             minor == NULL ? "" : "-",
             NONULL(minor),
             e->major_code,
             e->minor_code,
             NONULL(extension),
             extension == NULL ? "" : "-",
             error,
             e->error_code);

    return;
}

static bool should_ignore(xcb_generic_event_t* event) {
    uint8_t response_type = XCB_EVENT_RESPONSE_TYPE(event);

    /* Remove completed sequences */
    uint32_t sequence = event->full_sequence;
    while (Manager::get().ignore_enter_leave_events.size() > 0) {
        uint32_t end = Manager::get().ignore_enter_leave_events[0].end.sequence;
        /* Do if (end >= sequence) break;, but handle wrap-around: The above is
         * equivalent to end-sequence > 0 (assuming unlimited precision). With
         * int32_t, this would mean that the sign bit is cleared, which means:
         */
        if (end - sequence < UINT32_MAX / 2) {
            break;
        }
        Manager::get().ignore_enter_leave_events.erase(
          Manager::get().ignore_enter_leave_events.begin());
    }

    /* Check if this event should be ignored */
    if ((response_type == XCB_ENTER_NOTIFY || response_type == XCB_LEAVE_NOTIFY) &&
        Manager::get().ignore_enter_leave_events.size() > 0) {
        uint32_t begin = Manager::get().ignore_enter_leave_events[0].begin.sequence;
        uint32_t end = Manager::get().ignore_enter_leave_events[0].end.sequence;
        if (sequence >= begin && sequence <= end) {
            return true;
        }
    }

    return false;
}

template <typename ArgT>
ArgT get_argt(void (*fun)(ArgT)) {
    return nullptr;
}

void event_handle(xcb_generic_event_t* event) {
    uint8_t response_type = XCB_EVENT_RESPONSE_TYPE(event);

    if (should_ignore(event)) {
        return;
    }

    if (response_type == 0) {
        /* This is an error, not a event */
        xerror((xcb_generic_error_t*)event);
        return;
    }

    switch (response_type) {
#define EVENT(type, callback) \
    case type: callback((decltype(get_argt(callback)))event); return
        EVENT(XCB_BUTTON_PRESS, event_handle_button);
        EVENT(XCB_BUTTON_RELEASE, event_handle_button);
        EVENT(XCB_CONFIGURE_REQUEST, event_handle_configurerequest);
        EVENT(XCB_CONFIGURE_NOTIFY, event_handle_configurenotify);
        EVENT(XCB_DESTROY_NOTIFY, event_handle_destroynotify);
        EVENT(XCB_ENTER_NOTIFY, event_handle_enternotify);
        EVENT(XCB_CLIENT_MESSAGE, event_handle_clientmessage);
        EVENT(XCB_EXPOSE, event_handle_expose);
        EVENT(XCB_FOCUS_IN, event_handle_focusin);
        EVENT(XCB_KEY_PRESS, event_handle_key);
        EVENT(XCB_KEY_RELEASE, event_handle_key);
        EVENT(XCB_LEAVE_NOTIFY, event_handle_leavenotify);
        EVENT(XCB_MAP_REQUEST, event_handle_maprequest);
        EVENT(XCB_MOTION_NOTIFY, event_handle_motionnotify);
        EVENT(XCB_PROPERTY_NOTIFY, property_handle_propertynotify);
        EVENT(XCB_REPARENT_NOTIFY, event_handle_reparentnotify);
        EVENT(XCB_UNMAP_NOTIFY, event_handle_unmapnotify);
        EVENT(XCB_SELECTION_CLEAR, event_handle_selectionclear);
        EVENT(XCB_SELECTION_NOTIFY, event_handle_selectionnotify);
        EVENT(XCB_SELECTION_REQUEST, selection_handle_selectionrequest);
#undef EVENT
    }

#define EXTENSION_EVENT(base, offset, callback)                         \
    if (Manager::get().x.event_base_##base != 0 &&                      \
        response_type == Manager::get().x.event_base_##base + (offset)) \
    callback((decltype(get_argt(callback)))event)
    EXTENSION_EVENT(randr, XCB_RANDR_SCREEN_CHANGE_NOTIFY, event_handle_randr_screen_change_notify);
    EXTENSION_EVENT(randr, XCB_RANDR_NOTIFY, event_handle_randr_output_change_notify);
    EXTENSION_EVENT(shape, XCB_SHAPE_NOTIFY, event_handle_shape_notify);
    EXTENSION_EVENT(xkb, 0, event_handle_xkb_notify);
    EXTENSION_EVENT(xfixes, XCB_XFIXES_SELECTION_NOTIFY, event_handle_xfixes_selection_notify);
#undef EXTENSION_EVENT
}

void event_init(void) {
    const xcb_query_extension_reply_t* reply;

    reply = getConnection().get_extension_data(&xcb_randr_id);
    if (reply && reply->present) {
        Manager::get().x.event_base_randr = reply->first_event;
    }

    reply = getConnection().get_extension_data(&xcb_shape_id);
    if (reply && reply->present) {
        Manager::get().x.event_base_shape = reply->first_event;
    }

    reply = getConnection().get_extension_data(&xcb_xkb_id);
    if (reply && reply->present) {
        Manager::get().x.event_base_xkb = reply->first_event;
    }

    reply = getConnection().get_extension_data(&xcb_xfixes_id);
    if (reply && reply->present) {
        Manager::get().x.event_base_xfixes = reply->first_event;
    }
}

// vim: filetype=c:expandtab:shiftwidth=4:tabstop=8:softtabstop=4:textwidth=80
