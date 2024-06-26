/*
 * ewmh.h - EWMH header
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

#include "draw.h"
#include "strut.h"

#include <cairo.h>
#include <vector>
#include <xcb/xcb.h>

struct client;

void ewmh_init(void);
void ewmh_init_lua(void);
void ewmh_update_net_numbers_of_desktop(void);
int ewmh_update_net_current_desktop(lua_State*);
void ewmh_update_net_desktop_names(void);
int ewmh_process_client_message(xcb_client_message_event_t*);
void ewmh_update_net_client_list_stacking(void);
void ewmh_client_check_hints(client*);
void ewmh_client_update_desktop(client*);
void ewmh_process_client_strut(client*);
void ewmh_update_strut(xcb_window_t, strut_t*);
void ewmh_update_window_type(xcb_window_t window, uint32_t type);
xcb_get_property_cookie_t ewmh_window_icon_get_unchecked(xcb_window_t);
std::vector<cairo_surface_handle> ewmh_window_icon_get_reply(xcb_get_property_cookie_t);
