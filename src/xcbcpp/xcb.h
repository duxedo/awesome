#pragma once
#include <array>
#include <bits/iterator_concepts.h>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <memory>
#include <span>
#include <type_traits>
#include <xcb/bigreq.h>
#include <xcb/randr.h>
#include <xcb/xcb.h>
#include <xcb/xcb_atom.h>
#include <xcb/xcb_aux.h>
#include <xcb/xcb_event.h>
#include <xcb/xinerama.h>
#include <xcb/xproto.h>
#include <xcb/xtest.h>
#include <xcb/shape.h>
#include <xcb/xfixes.h>
#include <xcb/xcb_icccm.h>
#include <xcb/xcb_keysyms.h>
#include <xcb/xcb_cursor.h>
#include <xcb/xcb_xrm.h>

namespace XCB {
/*
 *
XCB_PROP_MODE_REPLACE AWESOME_CLIENT_ORDER, XCB_ATOM_WINDOW, 32, n, wins);
XCB_PROP_MODE_REPLACE WM_STATE, WM_STATE, 32, 2, data);
XCB_PROP_MODE_REPLACE _NET_WM_WINDOW_OPACITY, XCB_ATOM_CARDINAL, 32, 1L, &real_opacity);
XCB_PROP_MODE_REPLACE _XROOTPMAP_ID, XCB_ATOM_PIXMAP, 32, 1, &p);
XCB_PROP_MODE_REPLACE ESETROOT_PMAP_ID, XCB_ATOM_PIXMAP, 32, 1, &p);
XCB_PROP_MODE_REPLACE _NET_WM_STATE, XCB_ATOM_ATOM, 32, i, state);
XCB_PROP_MODE_REPLACE _NET_ACTIVE_WINDOW, XCB_ATOM_WINDOW, 32, 1, &win);
XCB_PROP_MODE_REPLACE _NET_CLIENT_LIST, XCB_ATOM_WINDOW, 32, n, wins);
XCB_PROP_MODE_REPLACE _NET_FRAME_EXTENTS, XCB_ATOM_CARDINAL, 32, 4, extents);
XCB_PROP_MODE_REPLACE _NET_SUPPORTED, XCB_ATOM_ATOM, 32, countof(atom), atom);
XCB_PROP_MODE_REPLACE _NET_SUPPORTING_WM_CHECK, XCB_ATOM_WINDOW, 32, 1, &father);
XCB_PROP_MODE_REPLACE _NET_SUPPORTING_WM_CHECK, XCB_ATOM_WINDOW, 32, 1, &father);
XCB_PROP_MODE_REPLACE _NET_WM_NAME, UTF8_STRING, 8, 7, "awesome");
XCB_PROP_MODE_REPLACE _NET_WM_PID, XCB_ATOM_CARDINAL, 32, 1, &i);
XCB_PROP_MODE_REPLACE _NET_CLIENT_LIST_STACKING, XCB_ATOM_WINDOW, 32, n, wins);
XCB_PROP_MODE_REPLACE _NET_NUMBER_OF_DESKTOPS, XCB_ATOM_CARDINAL, 32, 1, &count);
XCB_PROP_MODE_REPLACE _NET_CURRENT_DESKTOP, XCB_ATOM_CARDINAL, 32, 1, &idx);
XCB_PROP_MODE_REPLACE _NET_DESKTOP_NAMES, UTF8_STRING, 8, buf.len, buf.s);
XCB_PROP_MODE_REPLACE _NET_WM_DESKTOP, XCB_ATOM_CARDINAL, 32, 1, &desktops);
XCB_PROP_MODE_REPLACE _NET_WM_DESKTOP, XCB_ATOM_CARDINAL, 32, 1, &i);
XCB_PROP_MODE_REPLACE _NET_WM_STRUT_PARTIAL, XCB_ATOM_CARDINAL, 32, countof(state), state);
XCB_PROP_MODE_REPLACE _NET_WM_WINDOW_TYPE, XCB_ATOM_ATOM, 32, 1, &type);
XCB_PROP_MODE_REPLACE XCB_ATOM_WM_NAME, XCB_ATOM_STRING, 8, strlen(name), name);
XCB_PROP_MODE_REPLACE _XEMBED_INFO, _XEMBED_INFO, 32, 2, (uint32_t[]) { 0, 1 });
XCB_PROP_MODE_REPLACE transfer->property, UTF8_STRING, 8, 0, NULL);
XCB_PROP_MODE_REPLACE transfer->property, UTF8_STRING, 8, next_length, &data[transfer->offset]);
XCB_PROP_MODE_REPLACE transfer->property, XCB_ATOM_ATOM, 32, len, &atoms[0]);
XCB_PROP_MODE_REPLACE transfer->property, INCR, 32, 1, &incr_size );
XCB_PROP_MODE_REPLACE transfer->property, UTF8_STRING, 8, data_length, data);
XCB_PROP_MODE_REPLACE prop->atom, type, format, len, data);
*/

template<typename T>
concept XCBDataBlock = requires(T a)
{
    { std::begin(a) } -> std::contiguous_iterator;
    { std::size(a) } -> std::convertible_to<size_t>;
};
template<typename T>
concept XCBDataVal = !std::is_pointer_v<T> && std::is_trivial_v<T> && !std::is_array_v<T>;

template<typename T>
concept XCBData = XCBDataBlock<T> || XCBDataVal<T> || std::is_array_v<T>;

class Connection {
public:
    xcb_void_cookie_t reparent_window(xcb_window_t window, xcb_window_t parent, int16_t x, int16_t y) {
        return xcb_reparent_window(connection, window, parent, x, y);
    }
    template<typename Data, size_t N>
    xcb_void_cookie_t change_property(uint8_t mode, xcb_window_t window, xcb_atom_t property, xcb_atom_t type, const Data (&data)[N]) {
        return change_property(mode, window, property, type, std::span<const Data, N>(data, N));
    }
    template<XCBDataBlock Data>
    xcb_void_cookie_t change_property(uint8_t mode, xcb_window_t window, xcb_atom_t property, xcb_atom_t type, const Data& data) {
        constexpr uint8_t format = sizeof(decltype(*std::begin(data))) * 8;
        return xcb_change_property(connection, mode, window, property, type, format, std::size(data), std::to_address(std::begin(data)));
    }
    template<XCBDataVal Data>
    xcb_void_cookie_t change_property(uint8_t mode, xcb_window_t window, xcb_atom_t property, xcb_atom_t type, const Data& data) {
        constexpr uint8_t format = sizeof(Data) * 8;
        return xcb_change_property(connection, mode, window, property, type, format, 1, &data);
    }
    template<XCBData Data>
    xcb_void_cookie_t replace_property(xcb_window_t window, xcb_atom_t property, xcb_atom_t type, const Data& data) {
        return change_property(XCB_PROP_MODE_REPLACE, window, property, type, data);
    }
    template<XCBData Data>
    xcb_void_cookie_t prepend_property(xcb_window_t window, xcb_atom_t property, xcb_atom_t type, const Data& data) {
        return change_property(XCB_PROP_MODE_PREPEND, window, property, type, data);
    }
    template<XCBData Data>
    xcb_void_cookie_t append_property(xcb_window_t window, xcb_atom_t property, xcb_atom_t type, const Data& data) {
        return change_property(XCB_PROP_MODE_APPEND, window, property, type, data);
    }

    template<typename T, size_t N>
    xcb_void_cookie_t change_attributes(xcb_window_t w, uint32_t mask, const std::array<T, N>& arr) {
        return xcb_change_window_attributes(connection, w, mask, arr.data());
    }

    xcb_void_cookie_t change_attributes(xcb_window_t w, uint32_t mask, const void * data) {
        return xcb_change_window_attributes(connection, w, mask, data);
    }
    xcb_void_cookie_t clear_attributes(xcb_window_t w, uint32_t mask) {
        uint32_t none[] = { 0 };
        return xcb_change_window_attributes(connection, w, mask, none);
    }

    template<size_t N>
    xcb_void_cookie_t configure_window(xcb_window_t window, uint16_t value_mask, const std::array<uint32_t, N>& value_list){
        return xcb_configure_window(connection, window, value_mask, &value_list[0]);
    }
    xcb_void_cookie_t configure_window(xcb_window_t window, uint16_t value_mask, uint32_t val){
        return configure_window(window, value_mask, std::array<uint32_t, 1>{val});
    }

//private:
    xcb_connection_t * connection = nullptr;

    xcb_connection_t * getConnection() { return connection; }
};
}
