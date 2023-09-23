/*
 *x window.h -x window handling functions header
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

#pragma once

#include "color.h"
#include "globalconf.h"

#include <xcb/shape.h>

void xwindow_set_state(xcb_window_t, uint32_t);
xcb_get_property_cookie_t xwindow_get_state_unchecked(xcb_window_t);
uint32_t xwindow_get_state_reply(xcb_get_property_cookie_t);
void xwindow_configure(xcb_window_t, area_t, int);
void xwindow_buttons_grab(xcb_window_t, const std::vector<button_t*>&);
xcb_get_property_cookie_t xwindow_get_opacity_unchecked(xcb_window_t);
double xwindow_get_opacity(xcb_window_t);
double xwindow_get_opacity_from_cookie(xcb_get_property_cookie_t);
void xwindow_set_opacity(xcb_window_t, double);
void xwindow_grabkeys(xcb_window_t, const std::vector<keyb_t*>&);
void xwindow_takefocus(xcb_window_t);
void xwindow_set_cursor(xcb_window_t, xcb_cursor_t);
void xwindow_set_border_color(xcb_window_t, color_t*);
cairo_surface_t* xwindow_get_shape(xcb_window_t, xcb_shape_sk_t);
void xwindow_set_shape(xcb_window_t, int, int, xcb_shape_sk_t, cairo_surface_t*, int);
void xwindow_translate_for_gravity(
  xcb_gravity_t, int16_t, int16_t, int16_t, int16_t, int16_t*, int16_t*);

#define xwindow_set_name_static(win, name) \
    xcb_icccm_set_wm_name(getGlobals().connection, win, XCB_ATOM_STRING, 8, sizeof(name) - 1, name)
#define xwindow_set_class_instance(win) xwindow_set_class_instance_static(win, "awesome", "awesome")
#define xwindow_set_class_instance_static(win, instance, class) \
    _xwindow_set_class_instance_static(win, instance "\0" class)
#define _xwindow_set_class_instance_static(win, instance_class) \
    xcb_icccm_set_wm_class(getGlobals().connection, win, sizeof(instance_class), instance_class)

// vim: filetype=c:expandtab:shiftwidth=4:tabstop=8:softtabstop=4:textwidth=80
