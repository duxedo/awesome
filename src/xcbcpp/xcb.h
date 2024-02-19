
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

#include <xcb/xcb_atom.h>
#include <xcb/xcb_aux.h>
#include <xcb/xcb_cursor.h>
#include <xcb/xcb_errors.h>
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
namespace internal {

struct FreeDeleter {
    template <typename T>
    void operator()(T* arg) {
        free(arg);
    }
};

struct KeySymsDeleter {
    template <typename T>
    void operator()(T* arg) {
        xcb_key_symbols_free(arg);
    }
};
}
template <typename T>
using reply = std::unique_ptr<T, internal::FreeDeleter>;

template <typename T>
using event = std::unique_ptr<T, internal::FreeDeleter>;

using keycodes = std::unique_ptr<xcb_keycode_t, internal::FreeDeleter>;

class KeySyms {
    std::unique_ptr<xcb_key_symbols_t, internal::KeySymsDeleter> keysyms = nullptr;

  public:
    KeySyms() = default;
    KeySyms(xcb_key_symbols_t* syms)
        : keysyms(syms) {}
    KeySyms(KeySyms&&) = default;
    KeySyms(const KeySyms&) = delete;
    KeySyms& operator=(KeySyms&&) = default;
    KeySyms& operator=(const KeySyms&) = delete;

    explicit operator bool() const { return static_cast<bool>(keysyms); }

    xcb_keysym_t get_keysym(xcb_keycode_t keycode, int col) {
        return xcb_key_symbols_get_keysym(keysyms.get(), keycode, col);
    }

    keycodes get_keycode(xcb_keysym_t keysym) {
        return keycodes{xcb_key_symbols_get_keycode(keysyms.get(), keysym)};
    }
};

struct Size {
    uint16_t width = 0, height = 0;
};

struct Pos {
    int16_t x = 0, y = 0;
};

struct Color {
    uint16_t red = 0, green = 0, blue = 0;
};

struct Rect {
    int16_t x = 0, y = 0;
    uint16_t width = 0, height = 0;
};

class Connection {
  public:
    xcb_get_atom_name_cookie_t get_atom_name_unchecked(xcb_atom_t atom) {
        return xcb_get_atom_name_unchecked(connection, atom);
    }
    reply<xcb_get_atom_name_reply_t> get_atom_name_reply(xcb_get_atom_name_cookie_t cookie,
                                                         xcb_generic_error_t** e = nullptr) {
        return reply<xcb_get_atom_name_reply_t>{xcb_get_atom_name_reply(connection, cookie, e)};
    };
    xcb_intern_atom_cookie_t
    intern_atom_unchecked(uint8_t only_if_exists, uint16_t name_len, const char* name) {
        return xcb_intern_atom_unchecked(connection, only_if_exists, name_len, name);
    }

    reply<xcb_intern_atom_reply_t> intern_atom_reply(xcb_intern_atom_cookie_t cookie,
                                                     xcb_generic_error_t** e = nullptr) {
        return reply<xcb_intern_atom_reply_t>{xcb_intern_atom_reply(connection, cookie, e)};
    }
    xcb_void_cookie_t
    reparent_window(xcb_window_t window, xcb_window_t parent, int16_t x, int16_t y) {
        return xcb_reparent_window(connection, window, parent, x, y);
    }
    xcb_void_cookie_t
    reparent_window_checked(xcb_window_t window, xcb_window_t parent, int16_t x, int16_t y) {
        return xcb_reparent_window_checked(connection, window, parent, x, y);
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

    xcb_void_cookie_t delete_property(xcb_window_t window, xcb_atom_t property) {
        return xcb_delete_property(connection, window, property);
    }
    template <typename T, size_t N>
    xcb_void_cookie_t
    change_attributes(xcb_window_t w, uint32_t mask, const std::array<T, N>& arr) {
        return xcb_change_window_attributes(connection, w, mask, arr.data());
    }

    xcb_void_cookie_t change_attributes(xcb_window_t w, uint32_t mask, const void* data) {
        return xcb_change_window_attributes(connection, w, mask, data);
    }
    xcb_void_cookie_t change_save_set(uint8_t mode, xcb_window_t window) {
        return xcb_change_save_set(connection, mode, window);
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

    xcb_query_tree_cookie_t query_tree_unchecked(xcb_window_t window) {
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

    xcb_get_window_attributes_cookie_t get_window_attributes_unchecked(xcb_window_t window) {
        return xcb_get_window_attributes_unchecked(connection, window);
    }
    xcb_get_geometry_cookie_t get_geometry_unchecked(xcb_drawable_t window) {
        return xcb_get_geometry_unchecked(connection, window);
    }

    xcb_get_geometry_cookie_t get_geometry(xcb_window_t window) {
        return xcb_get_geometry(connection, window);
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

    xcb_get_property_cookie_t get_property(bool _delete,
                                           xcb_window_t window,
                                           xcb_atom_t property,
                                           xcb_atom_t type,
                                           uint32_t long_offset,
                                           uint32_t long_length) {
        return xcb_get_property(
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
        size_t len = xcb_get_property_value_length(reply.get());
        if (!len) {
            return {};
        }
        if (len > sizeof(T)) {
            std::cerr << "incorrect property length requested" << std::endl;
            return {};
        }
        T ret;
        memcpy(&ret, xcb_get_property_value(reply.get()), len);
        return ret;
    }
    uint32_t generate_id() { return xcb_generate_id(connection); }

    template <size_t N>
    xcb_void_cookie_t create_window(uint8_t depth,
                                    xcb_window_t wid,
                                    xcb_window_t parent,
                                    Rect rect,
                                    uint16_t border_width,
                                    uint16_t _class,
                                    xcb_visualid_t visual,
                                    uint32_t value_mask,
                                    const std::array<uint32_t, N>& value_list) {
        return xcb_create_window(connection,
                                 depth,
                                 wid,
                                 parent,
                                 rect.x,
                                 rect.y,
                                 rect.width,
                                 rect.height,
                                 border_width,
                                 _class,
                                 visual,
                                 value_mask,
                                 value_list.data());
    }
    template <size_t N>
    xcb_void_cookie_t create_window(uint8_t depth,
                                    xcb_window_t wid,
                                    xcb_window_t parent,
                                    Rect rect,
                                    uint16_t border_width,
                                    uint16_t _class,
                                    xcb_visualid_t visual,
                                    uint32_t value_mask,
                                    const uint32_t (&data)[N]) {
        return xcb_create_window(connection,
                                 depth,
                                 wid,
                                 parent,
                                 rect.x,
                                 rect.y,
                                 rect.width,
                                 rect.height,
                                 border_width,
                                 _class,
                                 visual,
                                 value_mask,
                                 data);
    }

    xcb_void_cookie_t create_window(uint8_t depth,
                                    xcb_window_t wid,
                                    xcb_window_t parent,
                                    Rect rect,
                                    uint16_t border_width,
                                    uint16_t _class,
                                    xcb_visualid_t visual,
                                    uint32_t value_mask) {
        return xcb_create_window(connection,
                                 depth,
                                 wid,
                                 parent,
                                 rect.x,
                                 rect.y,
                                 rect.width,
                                 rect.height,
                                 border_width,
                                 _class,
                                 visual,
                                 value_mask,
                                 nullptr);
    }
    xcb_void_cookie_t map_window(xcb_window_t window) { return xcb_map_window(connection, window); }
    xcb_void_cookie_t unmap_window(xcb_window_t window) {
        return xcb_unmap_window(connection, window);
    }
    xcb_void_cookie_t destroy_window(xcb_window_t window) {
        return xcb_destroy_window(connection, window);
    }
    xcb_void_cookie_t
    send_event(uint8_t propagate, xcb_window_t dest, uint32_t event_mask, const char* event) {
        return xcb_send_event(connection, propagate, dest, event_mask, event);
    }
    xcb_void_cookie_t allow_events(uint8_t mode, xcb_timestamp_t time) {

        return xcb_allow_events(connection, mode, time);
    }

    xcb_void_cookie_t ungrab_button(uint8_t button, xcb_window_t grab_window, uint16_t modifiers) {
        return xcb_ungrab_button(connection, button, grab_window, modifiers);
    }

    xcb_void_cookie_t grab_button(uint8_t owner_events,
                                  xcb_window_t grab_window,
                                  uint16_t event_mask,
                                  uint8_t pointer_mode,
                                  uint8_t keyboard_mode,
                                  xcb_window_t confine_to,
                                  xcb_cursor_t cursor,
                                  uint8_t button,
                                  uint16_t modifiers) {
        return xcb_grab_button(connection,
                               owner_events,
                               grab_window,
                               event_mask,
                               pointer_mode,
                               keyboard_mode,
                               confine_to,
                               cursor,
                               button,
                               modifiers);
    }

    xcb_void_cookie_t grab_key(bool owner_events,
                               xcb_window_t grab_window,
                               uint16_t modifiers,
                               xcb_keycode_t key,
                               uint8_t pointer_mode,
                               uint8_t keyboard_mode) {
        return xcb_grab_key(
          connection, owner_events, grab_window, modifiers, key, pointer_mode, keyboard_mode);
    }

    xcb_void_cookie_t ungrab_key(xcb_keycode_t key, xcb_window_t grab_window, uint16_t modifiers) {
        return xcb_ungrab_key(connection, key, grab_window, modifiers);
    }
    xcb_grab_pointer_cookie_t grab_pointer_unchecked(uint8_t owner_events,
                                                     xcb_window_t grab_window,
                                                     uint16_t event_mask,
                                                     uint8_t pointer_mode,
                                                     uint8_t keyboard_mode,
                                                     xcb_window_t confine_to,
                                                     xcb_cursor_t cursor,
                                                     xcb_timestamp_t time) {
        return xcb_grab_pointer_unchecked(connection,
                                          owner_events,
                                          grab_window,
                                          event_mask,
                                          pointer_mode,
                                          keyboard_mode,
                                          confine_to,
                                          cursor,
                                          time);
    }
    reply<xcb_grab_pointer_reply_t> grab_pointer_reply(xcb_grab_pointer_cookie_t cookie,
                                                       xcb_generic_error_t** e = nullptr) {
        return reply<xcb_grab_pointer_reply_t>{xcb_grab_pointer_reply(connection, cookie, e)};
    }

    xcb_void_cookie_t ungrab_pointer(xcb_timestamp_t time = XCB_CURRENT_TIME) {
        return xcb_ungrab_pointer(connection, time);
    }

    xcb_grab_keyboard_cookie_t grab_keyboard(uint8_t owner_events,
                                             xcb_window_t grab_window,
                                             xcb_timestamp_t time,
                                             uint8_t pointer_mode,
                                             uint8_t keyboard_mode) {
        return xcb_grab_keyboard(
          connection, owner_events, grab_window, time, pointer_mode, keyboard_mode);
    }
    reply<xcb_grab_keyboard_reply_t> grab_keyboard_reply(xcb_grab_keyboard_cookie_t cookie,
                                                         xcb_generic_error_t** e = nullptr) {
        return reply<xcb_grab_keyboard_reply_t>{
          xcb_grab_keyboard_reply(connection, cookie /**< */, e)};
    }

    xcb_void_cookie_t ungrab_keyboard(xcb_timestamp_t time = XCB_CURRENT_TIME) {
        return xcb_ungrab_keyboard(connection, time);
    }

    xcb_void_cookie_t icccm_set_wm_name(xcb_window_t window,
                                        xcb_atom_t encoding,
                                        uint8_t format,
                                        uint32_t name_len,
                                        const char* name) {
        return xcb_icccm_set_wm_name(connection, window, encoding, format, name_len, name);
    }
    xcb_get_property_cookie_t icccm_get_wm_class_unchecked(xcb_window_t window) {
        return xcb_icccm_get_wm_class_unchecked(connection, window);
    }
    uint8_t icccm_get_wm_class_reply(xcb_get_property_cookie_t cookie,
                                     xcb_icccm_get_wm_class_reply_t* prop,
                                     xcb_generic_error_t** e = nullptr) {
        return xcb_icccm_get_wm_class_reply(connection, cookie, prop, e);
    }
    xcb_void_cookie_t
    icccm_set_wm_class(xcb_window_t window, uint32_t class_len, const char* class_name) {
        return xcb_icccm_set_wm_class(connection, window, class_len, class_name);
    }

    xcb_get_property_cookie_t icccm_get_text_property(xcb_window_t window, xcb_atom_t property) {
        return xcb_icccm_get_text_property(connection, window, property);
    }

    uint8_t icccm_get_wm_transient_for_reply(xcb_get_property_cookie_t cookie,
                                             xcb_window_t* prop,
                                             xcb_generic_error_t** e = nullptr) {
        return xcb_icccm_get_wm_transient_for_reply(connection, cookie, prop, e);
    }
    xcb_get_property_cookie_t icccm_get_wm_transient_for_unchecked(xcb_window_t window) {
        return xcb_icccm_get_wm_transient_for_unchecked(connection, window);
    }
    xcb_get_property_cookie_t icccm_get_wm_normal_hints_unchecked(xcb_window_t window) {
        return xcb_icccm_get_wm_normal_hints_unchecked(connection, window);
    }
    uint8_t icccm_get_wm_normal_hints_reply(xcb_get_property_cookie_t cookie,
                                            xcb_size_hints_t* hints,
                                            xcb_generic_error_t** e = nullptr) {

        return xcb_icccm_get_wm_normal_hints_reply(connection, cookie, hints, e);
    }
    xcb_get_property_cookie_t icccm_get_wm_hints_unchecked(xcb_window_t window) {
        return xcb_icccm_get_wm_hints_unchecked(connection, window);
    }
    uint8_t icccm_get_wm_hints_reply(xcb_get_property_cookie_t cookie,
                                     xcb_icccm_wm_hints_t* hints,
                                     xcb_generic_error_t** e = nullptr) {

        return xcb_icccm_get_wm_hints_reply(connection, cookie, hints, e);
    }
    xcb_get_property_cookie_t icccm_get_wm_protocols_unchecked(xcb_window_t window,
                                                               xcb_atom_t wm_protocol_atom) {
        return xcb_icccm_get_wm_protocols_unchecked(connection, window, wm_protocol_atom);
    }
    uint8_t icccm_get_wm_protocols_reply(xcb_get_property_cookie_t cookie,
                                         xcb_icccm_get_wm_protocols_reply_t* protocols,
                                         xcb_generic_error_t** e = nullptr) {

        return xcb_icccm_get_wm_protocols_reply(connection, cookie, protocols, e);
    }
    int flush() { return xcb_flush(connection); }
    xcb_void_cookie_t
    create_pixmap(uint8_t depth, xcb_pixmap_t pid, xcb_drawable_t drawable, Size sz) {
        return xcb_create_pixmap(connection, depth, pid, drawable, sz.width, sz.height);
    }
    xcb_void_cookie_t free_pixmap(xcb_pixmap_t pixmap) {
        return xcb_free_pixmap(connection, pixmap);
    }

    xcb_query_pointer_cookie_t query_pointer_unchecked(xcb_window_t win) {
        return xcb_query_pointer_unchecked(connection, win);
    }

    reply<xcb_query_pointer_reply_t> query_pointer_reply(xcb_query_pointer_cookie_t cookie,
                                                         xcb_generic_error_t** e = nullptr) {
        return reply<xcb_query_pointer_reply_t>{xcb_query_pointer_reply(connection, cookie, e)};
    }

    xcb_void_cookie_t warp_pointer(xcb_window_t dst_window,
                                   Pos dst,
                                   xcb_window_t src_window = XCB_NONE,
                                   Rect src = {}) {
        return xcb_warp_pointer(
          connection, src_window, dst_window, src.x, src.y, src.width, src.height, dst.x, dst.y);
    }

    xcb_void_cookie_t set_input_focus(uint8_t revert_to, xcb_window_t focus, xcb_timestamp_t time) {
        return xcb_set_input_focus(connection, revert_to, focus, time);
    }
    xcb_get_modifier_mapping_cookie_t get_modifier_mapping() {
        return xcb_get_modifier_mapping(connection);
    }
    reply<xcb_get_modifier_mapping_reply_t>
    get_modifier_mapping_reply(xcb_get_modifier_mapping_cookie_t cookie,
                               xcb_generic_error_t** e = nullptr) {
        return reply<xcb_get_modifier_mapping_reply_t>{
          xcb_get_modifier_mapping_reply(connection, cookie, e)};
    }
    void aux_sync() { xcb_aux_sync(connection); }
    xcb_screen_t* aux_get_screen(int screen) { return xcb_aux_get_screen(connection, screen); }
    void disconnect() {
        xcb_disconnect(connection);
        connection = nullptr;
    }

    class XKB {
        xcb_connection_t* connection;

      public:
        XKB(xcb_connection_t* conn)
            : connection(conn) {}

        xcb_void_cookie_t latch_lock_state(xcb_xkb_device_spec_t deviceSpec,
                                           uint8_t affectModLocks,
                                           uint8_t modLocks,
                                           uint8_t lockGroup,
                                           uint8_t groupLock,
                                           uint8_t affectModLatches,
                                           uint8_t latchGroup,
                                           uint16_t groupLatch) {
            return xcb_xkb_latch_lock_state(connection,
                                            deviceSpec,
                                            affectModLocks,
                                            modLocks,
                                            lockGroup,
                                            groupLock,
                                            affectModLatches,
                                            latchGroup,
                                            groupLatch);
        }
        xcb_xkb_get_state_cookie_t get_state_unchecked(xcb_xkb_device_spec_t deviceSpec) {
            return xcb_xkb_get_state_unchecked(connection, deviceSpec);
        }
        reply<xcb_xkb_get_state_reply_t> get_state_reply(xcb_xkb_get_state_cookie_t cookie,
                                                         xcb_generic_error_t** e = nullptr) {
            return reply<xcb_xkb_get_state_reply_t>{xcb_xkb_get_state_reply(connection, cookie, e)};
        }
        xcb_xkb_get_names_cookie_t get_names_unchecked(xcb_xkb_device_spec_t deviceSpec,
                                                       uint32_t which) {
            return xcb_xkb_get_names_unchecked(connection, deviceSpec, which);
        }
        reply<xcb_xkb_get_names_reply_t> get_names_reply(xcb_xkb_get_names_cookie_t cookie,
                                                         xcb_generic_error_t** e = nullptr) {
            return reply<xcb_xkb_get_names_reply_t>{xcb_xkb_get_names_reply(connection, cookie, e)};
        }
        xcb_xkb_per_client_flags_cookie_t per_client_flags(xcb_xkb_device_spec_t deviceSpec,
                                                           uint32_t change,
                                                           uint32_t value,
                                                           uint32_t ctrlsToChange,
                                                           uint32_t autoCtrls,
                                                           uint32_t autoCtrlsValues) {
            return xcb_xkb_per_client_flags(
              connection, deviceSpec, change, value, ctrlsToChange, autoCtrls, autoCtrlsValues);
        }
        xcb_void_cookie_t select_events(xcb_xkb_device_spec_t deviceSpec,
                                        uint16_t affectWhich,
                                        uint16_t clear,
                                        uint16_t selectAll,
                                        uint16_t affectMap,
                                        uint16_t map,
                                        const void* details) {
            return xcb_xkb_select_events(
              connection, deviceSpec, affectWhich, clear, selectAll, affectMap, map, details);
        }
    };
    XKB xkb() { return XKB{connection}; }

    KeySyms key_symbols_alloc() { return KeySyms(xcb_key_symbols_alloc(connection)); }

    xcb_void_cookie_t convert_selection(xcb_window_t requestor,
                                        xcb_atom_t selection,
                                        xcb_atom_t target,
                                        xcb_atom_t property,
                                        xcb_timestamp_t time) {
        return xcb_convert_selection(connection, requestor, selection, target, property, time);
    }

    xcb_get_selection_owner_cookie_t get_selection_owner(xcb_atom_t selection) {
        return xcb_get_selection_owner(connection, selection);
    }
    xcb_get_selection_owner_cookie_t get_selection_owner_unchecked(xcb_atom_t selection) {
        return xcb_get_selection_owner_unchecked(connection, selection);
    }
    reply<xcb_get_selection_owner_reply_t>
    get_selection_owner_reply(xcb_get_selection_owner_cookie_t cookie,
                              xcb_generic_error_t** e = nullptr) {
        return reply<xcb_get_selection_owner_reply_t>{
          xcb_get_selection_owner_reply(connection, cookie, e)};
    }
    xcb_void_cookie_t
    set_selection_owner(xcb_window_t owner, xcb_atom_t selection, xcb_timestamp_t time) {
        return xcb_set_selection_owner(connection, owner, selection, time);
    }

    event<xcb_generic_event_t> wait_for_event() {
        return event<xcb_generic_event_t>{xcb_wait_for_event(connection)};
    }
    event<xcb_generic_event_t> poll_for_event() {
        return event<xcb_generic_event_t>{xcb_poll_for_event(connection)};
    }

    xcb_void_cookie_t grab_server() { return xcb_grab_server(connection); }
    xcb_void_cookie_t ungrab_server() { return xcb_ungrab_server(connection); }

    xcb_void_cookie_t kill_client(uint32_t resource) {
        return xcb_kill_client(connection, resource);
    }
    xcb_alloc_color_cookie_t alloc_color_unchecked(xcb_colormap_t cmap, Color color) {
        return xcb_alloc_color_unchecked(connection, cmap, color.red, color.green, color.blue);
    }
    reply<xcb_alloc_color_reply_t> alloc_color_reply(xcb_alloc_color_cookie_t cookie,
                                                     xcb_generic_error_t** e = nullptr) {
        return reply<xcb_alloc_color_reply_t>{xcb_alloc_color_reply(connection, cookie, e)};
    }
    xcb_void_cookie_t
    create_colormap(uint8_t alloc, xcb_colormap_t mid, xcb_window_t window, xcb_visualid_t visual) {
        return xcb_create_colormap(connection, alloc, mid, window, visual);
    }
    xcb_void_cookie_t clear_area(uint8_t exposures, xcb_window_t window, Rect rect = {}) {
        return xcb_clear_area(
          connection, exposures, window, rect.x, rect.y, rect.width, rect.height);
    }

    void discard_reply(int sequence) { return xcb_discard_reply(connection, sequence); }

    class Xfixes {
        xcb_connection_t* connection;

      public:
        Xfixes(xcb_connection_t* conn)
            : connection(conn) {}

        xcb_void_cookie_t
        select_selection_input(xcb_window_t window, xcb_atom_t selection, uint32_t event_mask) {
            return xcb_xfixes_select_selection_input(connection, window, selection, event_mask);
        }
    };

    Xfixes xfixes() { return Xfixes{connection}; }

    class Shape {
        xcb_connection_t* connection;

      public:
        Shape(xcb_connection_t* conn)
            : connection(conn) {}

        xcb_shape_get_rectangles_cookie_t get_rectangles(xcb_window_t window,
                                                         xcb_shape_kind_t source_kind) {
            return xcb_shape_get_rectangles(connection, window, source_kind);
        }
        xcb_shape_query_extents_cookie_t query_extents(xcb_window_t destination_window) {
            return xcb_shape_query_extents(connection, destination_window);
        }
        reply<xcb_shape_query_extents_reply_t>
        query_extents_reply(xcb_shape_query_extents_cookie_t cookie,
                            xcb_generic_error_t** e = nullptr) {
            return reply<xcb_shape_query_extents_reply_t>{
              xcb_shape_query_extents_reply(connection, cookie, e)};
        }
        reply<xcb_shape_get_rectangles_reply_t>
        get_rectangles_reply(xcb_shape_get_rectangles_cookie_t cookie,
                             xcb_generic_error_t** e = nullptr) {
            return reply<xcb_shape_get_rectangles_reply_t>{
              xcb_shape_get_rectangles_reply(connection, cookie, e)};
        }
        xcb_void_cookie_t mask(xcb_shape_op_t operation,
                               xcb_shape_kind_t destination_kind,
                               xcb_window_t destination_window,
                               int16_t x_offset,
                               int16_t y_offset,
                               xcb_pixmap_t source_bitmap) {
            return xcb_shape_mask(connection,
                                  operation,
                                  destination_kind,
                                  destination_window,
                                  x_offset,
                                  y_offset,
                                  source_bitmap);
        }
        xcb_void_cookie_t select_input(xcb_window_t destination_window, uint8_t enable) {
            return xcb_shape_select_input(connection, destination_window, enable);
        }
    };

    Shape shape() { return Shape{connection}; }

    xcb_translate_coordinates_cookie_t
    translate_coordinates_unchecked(xcb_window_t src_window, xcb_window_t dst_window, Pos src) {
        return xcb_translate_coordinates_unchecked(
          connection, src_window, dst_window, src.x, src.y);
    }

    reply<xcb_translate_coordinates_reply_t>
    translate_coordinates_reply(xcb_translate_coordinates_cookie_t cookie,
                                xcb_generic_error_t** e = nullptr) {
        return reply<xcb_translate_coordinates_reply_t>{
          xcb_translate_coordinates_reply(connection, cookie, e)};
    }

    class Randr {
        xcb_connection_t* connection;

      public:
        Randr(xcb_connection_t* conn)
            : connection(conn) {}

        xcb_randr_get_output_info_cookie_t
        get_output_info_unchecked(xcb_randr_output_t output, xcb_timestamp_t config_timestamp) {
            return xcb_randr_get_output_info_unchecked(connection, output, config_timestamp);
        }
        xcb_randr_get_output_info_cookie_t get_output_info(xcb_randr_output_t output,
                                                           xcb_timestamp_t config_timestamp) {
            return xcb_randr_get_output_info(connection, output, config_timestamp);
        }
        reply<xcb_randr_get_output_info_reply_t>
        get_output_info_reply(xcb_randr_get_output_info_cookie_t cookie,
                              xcb_generic_error_t** e = nullptr) {
            return reply<xcb_randr_get_output_info_reply_t>{
              xcb_randr_get_output_info_reply(connection, cookie, e)};
        }
        xcb_randr_get_monitors_cookie_t get_monitors(xcb_window_t window, uint8_t get_active) {
            return xcb_randr_get_monitors(connection, window, get_active);
        }
        reply<xcb_randr_get_monitors_reply_t>
        get_monitors_reply(xcb_randr_get_monitors_cookie_t cookie,
                           xcb_generic_error_t** e = nullptr) {
            return reply<xcb_randr_get_monitors_reply_t>{
              xcb_randr_get_monitors_reply(connection, cookie, e)};
        }
        xcb_randr_query_version_cookie_t query_version(uint32_t major_version,
                                                       uint32_t minor_version) {
            return xcb_randr_query_version(connection, major_version, minor_version);
        }
        reply<xcb_randr_query_version_reply_t>
        query_version_reply(xcb_randr_query_version_cookie_t cookie,
                            xcb_generic_error_t** e = nullptr) {
            return reply<xcb_randr_query_version_reply_t>{
              xcb_randr_query_version_reply(connection, cookie, e)};
        }
        xcb_void_cookie_t select_input(xcb_window_t window, uint16_t enable) {
            return xcb_randr_select_input(connection, window, enable);
        }
    };
    const struct xcb_query_extension_reply_t* get_extension_data(xcb_extension_t* ext) {
        return xcb_get_extension_data(connection, ext);
    }

    Randr randr() { return Randr{connection}; }

    xcb_void_cookie_t copy_area(xcb_drawable_t src_drawable,
                                xcb_drawable_t dst_drawable,
                                xcb_gcontext_t gc,
                                Rect srcRect,
                                Pos dstPos) {
        return xcb_copy_area(connection,
                             src_drawable,
                             dst_drawable,
                             gc,
                             srcRect.x,
                             srcRect.y,
                             dstPos.x,
                             dstPos.y,
                             srcRect.width,
                             srcRect.height);
    }

    int errors_context_new(xcb_errors_context_t** ctx) {
        return xcb_errors_context_new(connection, ctx);
    }

    void prefetch_extension_data(xcb_extension_t* ext) {
        return xcb_prefetch_extension_data(connection, ext);
    }

    xcb_void_cookie_t create_gc(xcb_gcontext_t cid,
                                xcb_drawable_t drawable,
                                uint32_t value_mask,
                                const void* value_list) {

        return xcb_create_gc(connection, cid, drawable, value_mask, value_list);
    }
    xcb_void_cookie_t test_fake_input(uint8_t type,
                                      uint8_t detail,
                                      uint32_t time,
                                      xcb_window_t root,
                                      Pos rootPos,
                                      uint8_t deviceid) {
        return xcb_test_fake_input(
          connection, type, detail, time, root, rootPos.x, rootPos.y, deviceid);
    }

    xcb_generic_error_t* request_check(xcb_void_cookie_t cookie) {
        return xcb_request_check(connection, cookie);
    }

    uint32_t get_maximum_request_length() { return xcb_get_maximum_request_length(connection); }

    const xcb_setup_t* get_setup() { return xcb_get_setup(connection); }

    int connection_has_error() { return xcb_connection_has_error(connection); }

    // private:
    xcb_connection_t* connection = nullptr;

    xcb_connection_t* getConnection() { return connection; }
    static Connection connect(const char* displayname, int* screenp) {
        return Connection(xcb_connect(displayname, screenp));
    }
};
} // namespace XCB
