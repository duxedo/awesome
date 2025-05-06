/*
 * common/xutil.h - X-related useful functions header
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

#include "common/atoms.h"
#include "common/util.h"

#include <string>
#include <string_view>
#include "globalconf.h"
#include "xcbcpp/xcb.h"
#include <xcb/xcb_keysyms.h>

#define MAX_X11_COORDINATE INT16_MAX
#define MIN_X11_COORDINATE INT16_MIN
#define MAX_X11_SIZE UINT16_MAX
#define MIN_X11_SIZE 1

static inline std::string xutil_get_text_property_from_reply(const XCB::reply<xcb_get_property_reply_t>& reply) {
    if (!reply ||
        (reply->type != XCB_ATOM_STRING && reply->type != UTF8_STRING &&
         reply->type != COMPOUND_TEXT) || reply->format != 8 ) {
        return "";
        /* We need to copy it that way since the string may not be
         * NULL-terminated */
    }
    int len = xcb_get_property_value_length(reply.get());
    if(!len) {
        return "";
    }
    auto data = (char*)xcb_get_property_value(reply.get());
    return std::string {data, data + len};
}

static inline void xutil_ungrab_server() {
    /* XCB's output buffer might have filled up between the GrabServer request
     * and now. Thus, the GrabServer might already have been sent and the
     * following UngrabServer would sit around in the output buffer for an
     * indeterminate time and might cause problems. We cannot detect this
     * situation, so just always flush directly after UngrabServer.
     */
    getConnection().ungrab_server();
    getConnection().flush();
}
#define xcb_ungrab_server do_not_use_xcb_ungrab_server_directly

uint16_t xutil_key_mask_fromstr(const std::string_view);
void xutil_key_mask_tostr(uint16_t, const char**, size_t*);
