/*
 * draw.h - draw functions header
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

#include "common/luahdr.h"
#include "common/util.h"
#include "xcbcpp/xcb.h"

#include <cairo.h>
#include <glib.h> /* for GError */
#include <memory>

/* Forward definition */
typedef struct _GdkPixbuf GdkPixbuf;

struct point {
    int x, y;

    point& operator-=(const point& rhs) {
        x -= rhs.x;
        y -= rhs.y;
        return *this;
    }
    point& operator+=(const point& rhs) {
        x += rhs.x;
        y += rhs.y;
        return *this;
    }
    point operator-(const point& rhs) const { return {x - rhs.x, y - rhs.y}; }
    point operator+(const point& rhs) const { return {x + rhs.x, y + rhs.y}; }
    bool operator<=>(const point&) const = default;
    operator XCB::Pos() const { return {(int16_t)x, (int16_t)y}; }
};

struct area_t {
    /** Co-ords of upper left corner */
    point top_left;
    uint16_t width;
    uint16_t height;

    bool operator<=>(const area_t&) const = default;

    auto left() const { return top_left.x; }
    auto top() const { return top_left.y; }
    auto bottom() const { return top_left.y + height; }
    auto right() const { return top_left.x + width; }

    point bottom_right() const { return {top_left.x + width, top_left.y + height}; }

    bool inside(point p) const {
        return (left() > p.x || right() <= p.x) && (top() > p.y || bottom() <= p.y);
    }
    operator XCB::Rect() const { return {(int16_t)left(), (int16_t)top(), width, height}; }
};

struct CairoDeleter {
    void operator()(cairo_surface_t* ptr) const { cairo_surface_destroy(ptr); }
};

using cairo_surface_handle = std::unique_ptr<cairo_surface_t, CairoDeleter>;

cairo_surface_t* draw_surface_from_data(int width, int height, uint32_t* data);
cairo_surface_t* draw_dup_image_surface(cairo_surface_t* surface);
cairo_surface_t* draw_load_image(lua_State* L, const char* path, GError** error);
cairo_surface_t* draw_surface_from_pixbuf(GdkPixbuf* buf);

xcb_visualtype_t* draw_find_visual(const xcb_screen_t* s, xcb_visualid_t visual);
xcb_visualtype_t* draw_default_visual(const xcb_screen_t* s);
xcb_visualtype_t* draw_argb_visual(const xcb_screen_t* s);
uint8_t draw_visual_depth(const xcb_screen_t* s, xcb_visualid_t vis);

void draw_test_cairo_xcb(void);
