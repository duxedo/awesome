/*
 * common/xembed.h - XEMBED functions header
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
#pragma once

#include "xcbcpp/xcb.h"

namespace XEmbed {
enum class InfoFlags : uint32_t { UNMAPPED = 0, MAPPED = (1 << 0), FLAGS_ALL = 1 };
struct info {
    unsigned long version = 0;
    uint32_t flags = static_cast<uint32_t>(InfoFlags::UNMAPPED);
};

struct window {
    xcb_window_t win;
    struct info info;
};

/** The version of the XEMBED protocol that this library supports.  */
#define XEMBED_VERSION 0

/** XEMBED messages */
enum class Message {
    EMBEDDED_NOTIFY = 0,
    WINDOW_ACTIVATE = 1,
    WINDOW_DEACTIVATE = 2,
    REQUEST_FOCUS = 3,
    FOCUS_IN = 4,
    FOCUS_OUT = 5,
    FOCUS_NEXT = 6,
    FOCUS_PREV = 7,
    /* 8-9 were used for XEMBED_GRAB_KEY/XEMBED_UNGRAB_KEY */
    MODALITY_ON = 10,
    MODALITY_OFF = 11,
    REGISTER_ACCELERATOR = 12,
    UNREGISTER_ACCELERATOR = 13,
    ACTIVATE_ACCELERATOR = 14
};

inline constexpr uint32_t to_native(Message m) { return static_cast<uint32_t>(m); }
inline constexpr Message from_native(uint32_t m) { return static_cast<Message>(m); }

enum Focus : uint32_t { CURRENT = 0, FIRST = 1, LAST = 2 };

/** Modifiers field for XEMBED_REGISTER_ACCELERATOR */

enum class Modifier {
    SHIFT = (1 << 0),
    CONTROL = (1 << 1),
    ALT = (1 << 2),
    SUPER = (1 << 3),
    HYPER = (1 << 4),
};

/** Flags for XEMBED_ACTIVATE_ACCELERATOR */
#define XEMBED_ACCELERATOR_OVERLOADED (1 << 0)

void xembed_message_send(
  xcb_connection_t*, xcb_window_t, xcb_timestamp_t, Message, uint32_t, uint32_t, uint32_t);

void xembed_property_update(XCB::Connection* connection,
                            window& emwin,
                            xcb_timestamp_t timestamp,
                            const XCB::reply<xcb_get_property_reply_t>& reply);

xcb_get_property_cookie_t info_get_unchecked(XCB::Connection* connection, xcb_window_t win);
std::optional<info> xembed_info_get_reply(XCB::Connection* conn, xcb_get_property_cookie_t cookie);

/** Indicate to an embedded window that it has focus.
 * \param c The X connection.
 * \param client The client.
 * \param timestamp The timestamp.
 * \param focus_type The type of focus.
 */
static inline void xembed_focus_in(xcb_connection_t* c,
                                   xcb_window_t client,
                                   xcb_timestamp_t timestamp,
                                   Focus focus_type) {
    xembed_message_send(
      c, client, timestamp, Message::FOCUS_IN, static_cast<uint32_t>(focus_type), 0, 0);
}

/** Notify a window that it has become active.
 * \param c The X connection.
 * \param client The window to notify.
 * \param timestamp The timestamp.
 */
static inline void
xembed_window_activate(xcb_connection_t* c, xcb_window_t client, xcb_timestamp_t timestamp) {
    xembed_message_send(c, client, timestamp, Message::WINDOW_ACTIVATE, 0, 0, 0);
}

/** Notify a window that it has become inactive.
 * \param c The X connection.
 * \param client The window to notify.
 * \param timestamp The timestamp.
 */
static inline void
xembed_window_deactivate(xcb_connection_t* c, xcb_window_t client, xcb_timestamp_t timestamp) {
    xembed_message_send(c, client, timestamp, Message::WINDOW_DEACTIVATE, 0, 0, 0);
}

/** Notify a window that its embed request has been received and accepted.
 * \param c The X connection.
 * \param client The client to send message to.
 * \param timestamp The timestamp.
 * \param embedder The embedder window.
 * \param version The version.
 */
static inline void xembed_embedded_notify(xcb_connection_t* c,
                                          xcb_window_t client,
                                          xcb_timestamp_t timestamp,
                                          xcb_window_t embedder,
                                          uint32_t version) {
    xembed_message_send(c, client, timestamp, Message::EMBEDDED_NOTIFY, 0, embedder, version);
}

/** Have the embedder end XEMBED protocol communication with a child.
 * \param connection The X connection.
 * \param child The window to unembed.
 * \param root The root window to reparent to.
 */
static inline void
xembed_window_unembed(xcb_connection_t* connection, xcb_window_t child, xcb_window_t root) {
    xcb_reparent_window(connection, child, root, 0, 0);
}

/** Indicate to an embedded window that it has lost focus.
 * \param c The X connection.
 * \param client The client to send message to.
 * \param timestamp The timestamp.
 */
static inline void
xembed_focus_out(xcb_connection_t* c, xcb_window_t client, xcb_timestamp_t timestamp) {
    xembed_message_send(c, client, timestamp, Message::FOCUS_OUT, 0, 0, 0);
}

} // namespace XEmbed
