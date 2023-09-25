
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <iterator>
#include <memory>
#include <optional>
#include <span>
#include <type_traits>
#include <xcb/bigreq.h>
#include <xcb/randr.h>
#include <xcb/shape.h>
#include <xcb/xcb.h>
#include <xcb/xcb_atom.h>
#include <xcb/xcb_aux.h>
#include <xcb/xcb_cursor.h>
#include <xcb/xcb_event.h>
#include <xcb/xcb_icccm.h>
#include <xcb/xcb_keysyms.h>
#include <xcb/xcb_xrm.h>
#include <xcb/xfixes.h>
#include <xcb/xinerama.h>
#include <xcb/xproto.h>
#include <xcb/xtest.h>

namespace XCB {

template <typename T>
concept XCBDataBlock = requires(T a) {
    { std::begin(a) } -> std::contiguous_iterator;
    { std::size(a) } -> std::convertible_to<size_t>;
};
template <typename T>
concept XCBDataVal = !std::is_pointer_v<T> && std::is_trivial_v<T> && !std::is_array_v<T>;

template <typename T>
concept XCBData = XCBDataBlock<T> || XCBDataVal<T> || std::is_array_v<T>;

struct ReplyDeleter {
    template <typename T>
    void operator()(T* arg) {
        free(arg);
    }
};

template <typename T>
using reply = std::unique_ptr<T, ReplyDeleter>;

class Connection {
  public:
    xcb_void_cookie_t
    reparent_window(xcb_window_t window, xcb_window_t parent, int16_t x, int16_t y) {
        return xcb_reparent_window(connection, window, parent, x, y);
    }
    template <typename Data, size_t N>
    xcb_void_cookie_t change_property(uint8_t mode,
                                      xcb_window_t window,
                                      xcb_atom_t property,
                                      xcb_atom_t type,
                                      const Data (&data)[N]) {
        return change_property(mode, window, property, type, std::span<const Data, N>(data, N));
    }
    template <XCBDataBlock Data>
    xcb_void_cookie_t change_property(
      uint8_t mode, xcb_window_t window, xcb_atom_t property, xcb_atom_t type, const Data& data) {
        constexpr uint8_t format = sizeof(decltype(*std::begin(data))) * 8;
        return xcb_change_property(connection,
                                   mode,
                                   window,
                                   property,
                                   type,
                                   format,
                                   std::size(data),
                                   std::to_address(std::begin(data)));
    }
    template <XCBDataVal Data>
    xcb_void_cookie_t change_property(
      uint8_t mode, xcb_window_t window, xcb_atom_t property, xcb_atom_t type, const Data& data) {
        constexpr uint8_t format = sizeof(Data) * 8;
        return xcb_change_property(connection, mode, window, property, type, format, 1, &data);
    }
    template <XCBData Data>
    xcb_void_cookie_t
    replace_property(xcb_window_t window, xcb_atom_t property, xcb_atom_t type, const Data& data) {
        return change_property(XCB_PROP_MODE_REPLACE, window, property, type, data);
    }
    template <XCBData Data>
    xcb_void_cookie_t
    prepend_property(xcb_window_t window, xcb_atom_t property, xcb_atom_t type, const Data& data) {
        return change_property(XCB_PROP_MODE_PREPEND, window, property, type, data);
    }
    template <XCBData Data>
    xcb_void_cookie_t
    append_property(xcb_window_t window, xcb_atom_t property, xcb_atom_t type, const Data& data) {
        return change_property(XCB_PROP_MODE_APPEND, window, property, type, data);
    }

    template <typename T, size_t N>
    xcb_void_cookie_t
    change_attributes(xcb_window_t w, uint32_t mask, const std::array<T, N>& arr) {
        return xcb_change_window_attributes(connection, w, mask, arr.data());
    }

    xcb_void_cookie_t change_attributes(xcb_window_t w, uint32_t mask, const void* data) {
        return xcb_change_window_attributes(connection, w, mask, data);
    }
    xcb_void_cookie_t clear_attributes(xcb_window_t w, uint32_t mask) {
        uint32_t none[] = {0};
        return xcb_change_window_attributes(connection, w, mask, none);
    }

    template <size_t N>
    xcb_void_cookie_t configure_window(xcb_window_t window,
                                       uint16_t value_mask,
                                       const std::array<uint32_t, N>& value_list) {
        return xcb_configure_window(connection, window, value_mask, &value_list[0]);
    }
    xcb_void_cookie_t configure_window(xcb_window_t window, uint16_t value_mask, uint32_t val) {
        return configure_window(window, value_mask, std::array<uint32_t, 1>{val});
    }

    xcb_query_tree_cookie_t query_tree_unckecked(xcb_window_t window) {
        return xcb_query_tree_unchecked(connection, window);
    }

    reply<xcb_query_tree_reply_t> query_tree_reply(xcb_query_tree_cookie_t cookie,
                                                   xcb_generic_error_t** err = nullptr) {
        return reply<xcb_query_tree_reply_t>{xcb_query_tree_reply(connection, cookie, err)};
    }

    std::optional<std::span<xcb_window_t, std::dynamic_extent>>
    query_tree_children(const reply<xcb_query_tree_reply_t>& reply) {
        auto wins = xcb_query_tree_children(reply.get());
        if (!wins) {
            return {};
        }
        size_t len = xcb_query_tree_children_length(reply.get());
        return {
          std::span{wins, len}
        };
    }

    xcb_get_window_attributes_cookie_t get_window_attributes_unckecked(xcb_window_t window) {
        return xcb_get_window_attributes_unchecked(connection, window);
    }
    xcb_get_geometry_cookie_t get_geometry_unchecked(xcb_window_t window) {
        return xcb_get_geometry_unchecked(connection, window);
    }

    xcb_get_property_cookie_t get_property_unchecked(bool _delete,
                                                     xcb_window_t window,
                                                     xcb_atom_t property,
                                                     xcb_atom_t type,
                                                     uint32_t long_offset,
                                                     uint32_t long_length) {
        return xcb_get_property_unchecked(
          connection, _delete, window, property, type, long_offset, long_length);
    }

    reply<xcb_get_window_attributes_reply_t>
    get_window_attributes_reply(xcb_get_window_attributes_cookie_t cookie,
                                xcb_generic_error_t** err = nullptr) {
        return reply<xcb_get_window_attributes_reply_t>{
          xcb_get_window_attributes_reply(connection, cookie, err)};
    }
    reply<xcb_get_geometry_reply_t> get_geometry_reply(xcb_get_geometry_cookie_t cookie,
                                                       xcb_generic_error_t** err = nullptr) {
        return reply<xcb_get_geometry_reply_t>{xcb_get_geometry_reply(connection, cookie, err)};
    }

    reply<xcb_get_property_reply_t> get_property_reply(xcb_get_property_cookie_t cookie,
                                                       xcb_generic_error_t** err = nullptr) {
        return reply<xcb_get_property_reply_t>{xcb_get_property_reply(connection, cookie, err)};
    }

    std::optional<std::span<uint8_t, std::dynamic_extent>>
    get_property_value(const reply<xcb_get_property_reply_t>& reply) {
        if (size_t len = xcb_get_property_value_length(reply.get())) {
            return std::span{(uint8_t*)xcb_get_property_value(reply.get()), len};
        }
        return {};
    }
    template <typename T>
    std::optional<T> get_property_value(const reply<xcb_get_property_reply_t>& reply) {
        if (size_t len = xcb_get_property_value_length(reply.get())) {
            if (len > sizeof(T)) {
                std::cerr << "wrong property length requested" << std::endl;
                return {};
            }
            T ret;
            memcpy(&ret, xcb_get_property_value(reply.get()), len);
            return ret;
        }
        return {};
    }
    uint32_t generate_id() { return xcb_generate_id(connection); }

    // private:
    xcb_connection_t* connection = nullptr;

    xcb_connection_t* getConnection() { return connection; }
};
} // namespace XCB
