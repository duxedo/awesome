/*
 * common/xembed.c - XEMBED functions
 *
 * Copyright © 2007-2008 Julien Danjou <julien@danjou.info>
 * Copyright © 2004 Matthew Reppert
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

#include "common/xembed.h"

#include "atoms-extern.h"
#include "common/atoms.h"
#include "common/util.h"
#include "globalconf.h"
#include "luaa.h"
#include "xcbcpp/xcb.h"

#include <xcb/xproto.h>
/* I should really include the correct header instead... */
namespace XEmbed {
/** Send an XEMBED message to a window.
 * \param connection Connection to the X server.
 * \param towin Destination window
 * \param message The message.
 * \param d1 Element 3 of message.
 * \param d2 Element 4 of message.
 * \param d3 Element 5 of message.
 */
#define IFLAG(x) static_cast<uint32_t>(InfoFlags::x)
void xembed_message_send(XCB::Connection& connection,
                         xcb_window_t towin,
                         xcb_timestamp_t timestamp,
                         Message message,
                         uint32_t d1,
                         uint32_t d2,
                         uint32_t d3) {
    xcb_client_message_event_t ev{.response_type = XCB_CLIENT_MESSAGE,
                                  .format = 32,
                                  .sequence = 0,
                                  .window = towin,
                                  .type = _XEMBED,
                                  .data = {.data32 = {timestamp, to_native(message), d1, d2, d3}}};
    connection.send_event(false, towin, XCB_EVENT_MASK_NO_EVENT, (char*)&ev);
}

/** Deliver a request to get XEMBED info for a window.
 * \param connection The X connection.
 * \param win The window.
 * \return A cookie.
 */
xcb_get_property_cookie_t info_get_unchecked(XCB::Connection* connection, xcb_window_t win) {
    return connection->get_property_unchecked(
      false, win, _XEMBED_INFO, XCB_GET_PROPERTY_TYPE_ANY, 0L, 2);
}

static std::optional<info>
xembed_info_from_reply(const XCB::reply<xcb_get_property_reply_t>& prop_r) {
    struct Data {
        uint32_t data[2];
    };

    auto data = getConnection().get_property_value<Data>(prop_r);
    if (!data) {
        return {};
    }

    info info;

    info.version = data->data[0];
    info.flags = data->data[1] & IFLAG(FLAGS_ALL);

    return info;
}

/** Get the XEMBED info for a window.
 * \param connection The X connection.
 * \param cookie The cookie of the request.
 * \param info The xembed_info_t structure to fill.
 */
std::optional<info> xembed_info_get_reply(XCB::Connection* conn, xcb_get_property_cookie_t cookie) {
    return xembed_info_from_reply(conn->get_property_reply(cookie));
}

/** Update embedded window properties.
 * \param connection The X connection.
 * \param emwin The embedded window.
 * \param timestamp The timestamp.
 * \param reply The property reply to handle.
 */
void xembed_property_update(XCB::Connection& connection,
                            window& emwin,
                            xcb_timestamp_t timestamp,
                            const XCB::reply<xcb_get_property_reply_t>& reply) {
    int flags_changed;
    info _info = xembed_info_from_reply(reply).value_or<info>(info{});

    /* test if it changed */
    if (!(flags_changed =
            static_cast<int32_t>(_info.flags) ^ static_cast<uint32_t>(emwin.info.flags))) {
        return;
    }

    emwin.info.flags = _info.flags;
    if (flags_changed & IFLAG(MAPPED)) {
        if (_info.flags & IFLAG(MAPPED)) {
            getConnection().map_window(emwin.win);
            xembed_window_activate(connection, emwin.win, timestamp);
        } else {
            getConnection().unmap_window(emwin.win);
            xembed_window_deactivate(connection, emwin.win, timestamp);
            xembed_focus_out(connection, emwin.win, timestamp);
        }
        Lua::systray_invalidate();
    }
}
} // namespace XEmbed

// vim: filetype=c:expandtab:shiftwidth=4:tabstop=8:softtabstop=4:textwidth=80
