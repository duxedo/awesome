/*
 * xwindow.c - X window handling functions
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

#include "xwindow.h"

#include "common/atoms.h"
#include "globalconf.h"
#include "objects/button.h"

#include <cairo-xcb.h>
#include <cstdint>
#include <xcb/shape.h>
#include <xcb/xcb.h>
#include <xcb/xproto.h>

/** Mask shorthands */
#define BUTTONMASK (XCB_EVENT_MASK_BUTTON_PRESS | XCB_EVENT_MASK_BUTTON_RELEASE)

/** Set client state (WM_STATE) property.
 * \param win The window to set state.
 * \param state The state to set.
 */
void xwindow_set_state(xcb_window_t win, uint32_t state) {
    uint32_t data[] = {state, XCB_NONE};
    getConnection().replace_property(win, WM_STATE, WM_STATE, data);
}

/** Send request to get a window state (WM_STATE).
 * \param w A client window.
 * \return The cookie associated with the request.
 */
xcb_get_property_cookie_t xwindow_get_state_unchecked(xcb_window_t w) {
    return getConnection().get_property_unchecked(false, w, WM_STATE, WM_STATE, 0L, 2L);
}

/** Get a window state (WM_STATE).
 * \param cookie The cookie.
 * \return The current state of the window, or 0 on error.
 */
uint32_t xwindow_get_state_reply(xcb_get_property_cookie_t cookie) {
    auto& conn = getConnection();
    if (auto prop_r = conn.get_property_reply(cookie, NULL)) {
        if (auto res = conn.get_property_value<uint32_t>(prop_r)) {
            return res.value();
        }
    }

    return XCB_ICCCM_WM_STATE_NORMAL;
}

/** Configure a window with its new geometry and border size.
 * \param win The X window id to configure.
 * \param geometry The new window geometry.
 * \param border The new border size.
 */
void xwindow_configure(xcb_window_t win, area_t geometry, int border) {
    xcb_configure_notify_event_t ce;

    ce.response_type = XCB_CONFIGURE_NOTIFY;
    ce.event = win;
    ce.window = win;
    ce.x = geometry.top_left.x + border;
    ce.y = geometry.top_left.y + border;
    ce.width = geometry.width;
    ce.height = geometry.height;
    ce.border_width = border;
    ce.above_sibling = XCB_NONE;
    ce.override_redirect = false;
    Manager::get().x.connection.send_event(false, win, XCB_EVENT_MASK_STRUCTURE_NOTIFY, (char*)&ce);
}

/** Grab or ungrab buttons on a window.
 * \param win The window.
 * \param buttons The buttons to grab.
 */
void xwindow_buttons_grab(xcb_window_t win, const std::vector<button_t*>& buttons) {
    if (win == XCB_NONE) {
        return;
    }

    /* Ungrab everything first */
    Manager::get().x.connection.ungrab_button(XCB_BUTTON_INDEX_ANY, win, XCB_BUTTON_MASK_ANY);

    for (auto each : buttons) {
        each->grab(win);
    }
}

/** Grab key on a window.
 * \param win The window.
 * \param k The key.
 */
static void xwindow_grabkey(xcb_window_t win, const keyb_t* k) {
    if (k->keycode) {
        Manager::get().x.connection.grab_key(
          true, win, k->modifiers, k->keycode, XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC);
    } else if (k->keysym) {
        auto keycodes = Manager::get().input.keysyms.get_keycode(k->keysym);
        if (!keycodes) {
            return;
        }
        for (xcb_keycode_t* kc = keycodes.get(); *kc; kc++) {
            getConnection().grab_key(
              true, win, k->modifiers, *kc, XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC);
        }
    }
}

void xwindow_grabkeys(xcb_window_t win, const std::vector<keyb_t*>& keys) {
    /* Ungrab everything first */
    getConnection().ungrab_key(XCB_GRAB_ANY, win, XCB_BUTTON_MASK_ANY);

    for (auto k : keys) {
        xwindow_grabkey(win, k);
    }
}

/** Send a request for a window's opacity.
 * \param win The window
 * \return A cookie for xwindow_get_opacity_from_reply().
 */
xcb_get_property_cookie_t xwindow_get_opacity_unchecked(xcb_window_t win) {
    return getConnection().get_property_unchecked(
      false, win, _NET_WM_WINDOW_OPACITY, XCB_ATOM_CARDINAL, 0L, 1L);
}

/** Get the opacity of a window.
 * \param win The window.
 * \return The opacity, between 0 and 1 or -1 or no opacity set.
 */
double xwindow_get_opacity(xcb_window_t win) {
    xcb_get_property_cookie_t prop_c = xwindow_get_opacity_unchecked(win);
    return xwindow_get_opacity_from_cookie(prop_c);
}

/** Get the opacity of a window.
 * \param cookie A cookie for a reply to a get property request for _NET_WM_WINDOW_OPACITY.
 * \return The opacity, between 0 and 1.
 */
double xwindow_get_opacity_from_cookie(xcb_get_property_cookie_t cookie) {
    auto prop_r = getConnection().get_property_reply(cookie, NULL);

    if (prop_r && prop_r->value_len && prop_r->format == 32) {
        auto val = getConnection().get_property_value<uint32_t>(prop_r);
        return (double)val.value() / (double)0xffffffff;
    }

    return -1;
}

/** Set opacity of a window.
 * \param win The window.
 * \param opacity Opacity of the window, between 0 and 1.
 */
void xwindow_set_opacity(xcb_window_t win, double opacity) {
    if (win) {
        if (opacity >= 0 && opacity <= 1) {
            uint32_t real_opacity = opacity * 0xffffffff;
            getConnection().replace_property(
              win, _NET_WM_WINDOW_OPACITY, XCB_ATOM_CARDINAL, real_opacity);
        } else {
            getConnection().delete_property(win, _NET_WM_WINDOW_OPACITY);
        }
    }
}

/** Send WM_TAKE_FOCUS client message to window
 * \param win destination window
 */
void xwindow_takefocus(xcb_window_t win) {
    xcb_client_message_event_t ev;

    /* Initialize all of event's fields first */
    p_clear(&ev, 1);

    ev.response_type = XCB_CLIENT_MESSAGE;
    ev.window = win;
    ev.format = 32;
    ev.data.data32[1] = Manager::get().x.get_timestamp();
    ev.type = WM_PROTOCOLS;
    ev.data.data32[0] = WM_TAKE_FOCUS;

    getConnection().send_event(false, win, XCB_EVENT_MASK_NO_EVENT, (char*)&ev);
}

/** Set window cursor.
 * \param w The window.
 * \param c The cursor.
 */
void xwindow_set_cursor(xcb_window_t w, xcb_cursor_t c) {
    const uint32_t values[] = {c};
    getConnection().change_attributes(w, XCB_CW_CURSOR, values);
}

/** Set a window border color.
 * \param w The window.
 * \param color The color.
 */
void xwindow_set_border_color(xcb_window_t w, color_t* color) {
    if (w) {
        getConnection().change_attributes(w, XCB_CW_BORDER_PIXEL, &color->pixel);
    }
}

/** Get one of a window's shapes as a cairo surface */
cairo_surface_t* xwindow_get_shape(xcb_window_t win, enum xcb_shape_sk_t kind) {
    if (!Manager::get().x.caps.have_shape) {
        return NULL;
    }
    if (kind == XCB_SHAPE_SK_INPUT && !Manager::get().x.caps.have_input_shape) {
        return NULL;
    }

    int16_t x, y;
    uint16_t width, height;
    xcb_shape_get_rectangles_cookie_t rcookie = getConnection().shape().get_rectangles(win, kind);
    if (kind == XCB_SHAPE_SK_INPUT) {
        /* We cannot query the size/existence of an input shape... */
        auto geom = getConnection().get_geometry_reply(getConnection().get_geometry(win));
        if (!geom) {
            getConnection().discard_reply(rcookie.sequence);
            /* Create a cairo surface in an error state */
            return cairo_image_surface_create(CAIRO_FORMAT_INVALID, -1, -1);
        }
        x = 0;
        y = 0;
        width = geom->width;
        height = geom->height;
    } else {
        xcb_shape_query_extents_cookie_t ecookie = getConnection().shape().query_extents(win);
        auto extents = getConnection().shape().query_extents_reply(ecookie);
        bool shaped;

        if (!extents) {
            getConnection().discard_reply(rcookie.sequence);
            /* Create a cairo surface in an error state */
            return cairo_image_surface_create(CAIRO_FORMAT_INVALID, -1, -1);
        }

        if (kind == XCB_SHAPE_SK_BOUNDING) {
            x = extents->bounding_shape_extents_x;
            y = extents->bounding_shape_extents_y;
            width = extents->bounding_shape_extents_width;
            height = extents->bounding_shape_extents_height;
            shaped = extents->bounding_shaped;
        } else {
            awsm_check(kind == XCB_SHAPE_SK_CLIP);
            x = extents->clip_shape_extents_x;
            y = extents->clip_shape_extents_y;
            width = extents->clip_shape_extents_width;
            height = extents->clip_shape_extents_height;
            shaped = extents->clip_shaped;
        }

        if (!shaped) {
            getConnection().discard_reply(rcookie.sequence);
            return NULL;
        }
    }

    auto rects_reply = getConnection().shape().get_rectangles_reply(rcookie);
    if (!rects_reply) {
        /* Create a cairo surface in an error state */
        return cairo_image_surface_create(CAIRO_FORMAT_INVALID, -1, -1);
    }

    cairo_surface_t* surface = cairo_image_surface_create(CAIRO_FORMAT_A1, width, height);
    cairo_t* cr = cairo_create(surface);
    int num_rects = xcb_shape_get_rectangles_rectangles_length(rects_reply.get());
    xcb_rectangle_t* rects = xcb_shape_get_rectangles_rectangles(rects_reply.get());

    cairo_surface_set_device_offset(surface, -x, -y);
    cairo_set_fill_rule(cr, CAIRO_FILL_RULE_WINDING);

    for (int i = 0; i < num_rects; i++) {
        cairo_rectangle(cr, rects[i].x, rects[i].y, rects[i].width, rects[i].height);
    }
    cairo_fill(cr);

    cairo_destroy(cr);
    return surface;
}

/** Turn a cairo surface into a pixmap with depth 1 */
static xcb_pixmap_t xwindow_shape_pixmap(int width, int height, cairo_surface_t* surf) {
    xcb_pixmap_t pixmap = getConnection().generate_id();
    cairo_surface_t* dest;
    cairo_t* cr;

    if (width <= 0 || height <= 0) {
        return XCB_NONE;
    }

    getConnection().create_pixmap(
      1, pixmap, Manager::get().screen->root, {(uint16_t)(width), (uint16_t)height});
    dest = cairo_xcb_surface_create_for_bitmap(
      getConnection().getConnection(), Manager::get().screen, pixmap, width, height);

    cr = cairo_create(dest);
    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    cairo_set_source_surface(cr, surf, 0, 0);
    cairo_paint(cr);

    cairo_destroy(cr);
    cairo_surface_flush(dest);
    cairo_surface_finish(dest);
    cairo_surface_destroy(dest);

    return pixmap;
}

/** Set one of a window's shapes */
void xwindow_set_shape(xcb_window_t win,
                       int width,
                       int height,
                       enum xcb_shape_sk_t kind,
                       cairo_surface_t* surf,
                       int offset) {
    if (!Manager::get().x.caps.have_shape) {
        return;
    }
    if (kind == XCB_SHAPE_SK_INPUT && !Manager::get().x.caps.have_input_shape) {
        return;
    }

    xcb_pixmap_t pixmap = XCB_NONE;
    if (surf) {
        pixmap = xwindow_shape_pixmap(width, height, surf);
    }

    getConnection().shape().mask(XCB_SHAPE_SO_SET, kind, win, offset, offset, pixmap);

    if (pixmap != XCB_NONE) {
        getConnection().free_pixmap(pixmap);
    }
}

/** Calculate the position change that a window needs applied.
 * \param gravity The window gravity that should be used.
 * \param change_width_before The window width difference that will be applied.
 * \param change_height_before The window height difference that will be applied.
 * \param change_width_after The window width difference that will be applied.
 * \param change_height_after The window height difference that will be applied.
 * \param dx On return, this will be changed by the amount the pixel has to be moved.
 * \param dy On return, this will be changed by the amount the pixel has to be moved.
 */
void xwindow_translate_for_gravity(xcb_gravity_t gravity,
                                   int16_t change_width_before,
                                   int16_t change_height_before,
                                   int16_t change_width_after,
                                   int16_t change_height_after,
                                   int32_t* dx,
                                   int32_t* dy) {
    int16_t x = 0, y = 0;
    int16_t change_height = change_height_before + change_height_after;
    int16_t change_width = change_width_before + change_width_after;

    switch (gravity) {
    case XCB_GRAVITY_WIN_UNMAP:
    case XCB_GRAVITY_NORTH_WEST: break;
    case XCB_GRAVITY_NORTH: x = -change_width / 2; break;
    case XCB_GRAVITY_NORTH_EAST: x = -change_width; break;
    case XCB_GRAVITY_WEST: y = -change_height / 2; break;
    case XCB_GRAVITY_CENTER:
        x = -change_width / 2;
        y = -change_height / 2;
        break;
    case XCB_GRAVITY_EAST:
        x = -change_width;
        y = -change_height / 2;
        break;
    case XCB_GRAVITY_SOUTH_WEST: y = -change_height; break;
    case XCB_GRAVITY_SOUTH:
        x = -change_width / 2;
        y = -change_height;
        break;
    case XCB_GRAVITY_SOUTH_EAST:
        x = -change_width;
        y = -change_height;
        break;
    case XCB_GRAVITY_STATIC:
        x = -change_width_before;
        y = -change_height_before;
        break;
    }

    if (dx) {
        *dx += x;
    }
    if (dy) {
        *dy += y;
    }
}

// vim: filetype=c:expandtab:shiftwidth=4:tabstop=8:softtabstop=4:textwidth=80
