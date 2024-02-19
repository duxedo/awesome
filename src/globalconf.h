/*
 * globalconf.h - basic globalconf.header
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

#include "common/lualib.h"

#include <cstdlib>
#include <xcb/xproto.h>
#define SN_API_NOT_YET_FROZEN
#include "config.h"
#include "property.h"
#include "xcbcpp/xcb.h"

#include <X11/Xresource.h>
#include <glib.h>
#include <libsn/sn.h>
#include <set>
#ifdef WITH_XCB_ERRORS
#include <xcb/xcb_errors.h>
#endif

#include "common/xembed.h"
#include "objects/key.h"

#define ROOT_WINDOW_EVENT_MASK                                                      \
    (const uint32_t[]) {                                                            \
        XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT | XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY | \
          XCB_EVENT_MASK_ENTER_WINDOW | XCB_EVENT_MASK_LEAVE_WINDOW |               \
          XCB_EVENT_MASK_STRUCTURE_NOTIFY | XCB_EVENT_MASK_BUTTON_PRESS |           \
          XCB_EVENT_MASK_BUTTON_RELEASE | XCB_EVENT_MASK_FOCUS_CHANGE |             \
          XCB_EVENT_MASK_PROPERTY_CHANGE                                            \
    }

struct drawable_t;
struct drawin_t;
class button_t;
struct client;
struct screen_t;
struct tag_t;
struct sequence_pair_t {
    xcb_void_cookie_t begin;
    xcb_void_cookie_t end;
};

void tag_unref_simplified(tag_t*);

struct TagDeleter {
    void operator()(tag_t* tag) { tag_unref_simplified(tag); }
};

using tag_ptr = std::unique_ptr<tag_t, TagDeleter>;

struct XOrg {
    /** Default screen number */
    int default_screen = 0;
    /** Connection ref */
    XCB::Connection connection;
    /** X Resources DB */
    xcb_xrm_database_t* xrmdb = nullptr;
    /** xcb-cursor context */
    xcb_cursor_context_t* cursor_ctx = nullptr;
#ifdef WITH_XCB_ERRORS
    /** xcb-errors context */
    xcb_errors_context_t* errors_ctx;
#endif
    /** Atom for WM_Sn */
    xcb_atom_t selection_atom = 0;
    /** Window owning the WM_Sn selection */
    xcb_window_t selection_owner_window = 0;
    /** Do we have a RandR screen update pending? */
    bool screen_refresh_pending = false;

    struct capabilities {
        /** Check for XTest extension */
        bool have_xtest = false;
        /** Check for SHAPE extension */
        bool have_shape = false;
        /** Check for SHAPE extension with input shape support */
        bool have_input_shape = false;
        /** Check for XFixes extension */
        bool have_xfixes = false;
    } caps;

    uint8_t event_base_shape = 0;
    uint8_t event_base_xkb = 0;
    uint8_t event_base_randr = 0;
    uint8_t event_base_xfixes = 0;

  private:
    xcb_timestamp_t timestamp = 0;

  public:
    xcb_timestamp_t get_timestamp() const { return timestamp; }

    template <typename EventT>
    requires(std::is_same_v<decltype(timestamp), decltype(std::declval<EventT>().time)>)
    void update_timestamp(const EventT* ev) {
        timestamp = ev->time;
    }
};

struct Input {
    /** Keys symbol table */
    XCB::KeySyms keysyms;
};

struct StartupConfig {
    /** Should screens be created before rc.lua is loaded? */
    bool no_auto_screen = false;
    /** Should the screen be created automatically? */
    bool ignore_screens = false;
    /** Custom searchpaths are present, the runtime is tainted */
    bool have_searchpaths = false;
};

/** Main configuration structure */
class Manager {
  public:
    XOrg x;
    Input input;
    StartupConfig startup;
    /** Logical screens */
    std::vector<screen_t*> screens;
    /** The primary screen, access through screen_get_primary() */
    screen_t* primary_screen = nullptr;
    /** Root window key bindings */
    std::vector<keyb_t*> keys;
    /** Root window mouse bindings */
    std::vector<button_t*> buttons;
    /** When --no-argb is used in the modeline or command line */
    bool had_overriden_depth = false;
    /** Clients list */
    std::vector<client*> clients;
    /** Embedded windows */
  private:
    std::vector<client*> stack = {};

  public:
    const std::vector<client*>& getStack() { return stack; }
    std::vector<client*>& refStack() { return stack; }

    std::vector<XEmbed::window> embedded;
    /** Stack client history */
    /** Lua VM state (opaque to avoid mis-use, see globalconf_get_lua_State()) */
    struct {
        lua_State* real_L_dont_use_directly = nullptr;
    } L;
    /** All errors messages from loading config files */
    std::string startup_errors;
    /** main loop that awesome is running on */
    GMainLoop* loop = nullptr;
    /** The key grabber function */
    Lua::FunctionRegistryIdx keygrabber;
    /** The mouse pointer grabber function */
    Lua::FunctionRegistryIdx mousegrabber;
    /** The drawable that currently contains the pointer */
    drawable_t* drawable_under_mouse = nullptr;
    /** Input focus information */
    struct {
        /** Focused client */
        struct client* client = nullptr;
        /** Is there a focus change pending? */
        bool need_update = false;
        /** When nothing has the input focus, this window actually is focused */
        xcb_window_t window_no_focus = 0;
    } focus;
    /** Drawins */
    std::vector<drawin_t*> drawins;
    /** The startup notification display struct */
    SnDisplay* sndisplay = nullptr;
    /** Latest timestamp we got from the X server */

    /** Window that contains the systray */
    struct {
        xcb_window_t window = 0;
        /** Atom for _NET_SYSTEM_TRAY_%d */
        xcb_atom_t atom = 0;
        /** Do we own the systray selection? */
        bool registered = false;
        /** Systray window parent */
        drawin_t* parent = nullptr;
        /** Background color */
        uint32_t background_pixel = 0;
    } systray;
    /** The monitor of startup notifications */
    SnMonitorContext* snmonitor = nullptr;
    /** The visual, used to draw */
    xcb_visualtype_t* visual = nullptr;
    /** The screen's default visual */
    xcb_visualtype_t* default_visual = nullptr;
    /** The screen's information */
    xcb_screen_t* screen = nullptr;
    /** A graphic context. */
    xcb_gcontext_t gc = 0;
    /** Our default depth */
    uint8_t default_depth = 0;
    /** Our default color map */
    xcb_colormap_t default_cmap = 0;
    /** Do we have to reban clients? */
    bool need_lazy_banning = false;
    /** Tag list */
    std::vector<tag_ptr> tags;
    /** List of registered xproperties */
    std::set<xproperty> xproperties;
    /* xkb context */
    struct xkb_context* xkb_ctx = nullptr;
    /* xkb state of dead keys on keyboard */
    struct xkb_state* xkb_state = nullptr;
    /* Do we have a pending xkb update call? */
    bool xkb_update_pending = false;
    /* Do we have a pending reload? */
    bool xkb_reload_keymap = false;
    /* Do we have a pending map change? */
    bool xkb_map_changed = false;
    /* Do we have a pending group change? */
    bool xkb_group_changed = false;
    /** The preferred size of client icons for this screen */
    uint32_t preferred_icon_size = 0;
    /** Cached wallpaper information */
    cairo_surface_t* wallpaper = nullptr;
    /** List of enter/leave events to ignore */
    std::vector<sequence_pair_t> ignore_enter_leave_events;
    xcb_void_cookie_t pending_enter_leave_begin = {0};
    /** List of windows to be destroyed later */
    std::vector<xcb_window_t> destroy_later_windows;
    /** Pending event that still needs to be handled */
    XCB::event<xcb_generic_event_t> pending_event;
    /** The exit code that main() will return with */
    int exit_code = EXIT_SUCCESS;
    /** The Global API level */
    int api_level = 0;

    static Manager& get();
};

XCB::Connection& getConnection();

/** You should always use this as lua_State *L = globalconf_get_lua_State().
 * That way it becomes harder to introduce coroutine-related problems.
 */
static inline lua_State* globalconf_get_lua_State(void) {
    return Manager::get().L.real_L_dont_use_directly;
}

/* Defined in root.c */
void root_update_wallpaper(void);
