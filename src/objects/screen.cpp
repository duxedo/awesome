/*
 * screen.c - screen management
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

/** A physical or virtual screen object.
 *
 * Screen objects can be added and removed over time. To get a callback for all
 * current and future screens, use `awful.screen.connect_for_each_screen`:
 *
 *    awful.screen.connect_for_each_screen(function(s)
 *        -- do something
 *    end)
 *
 * It is also possible loop over all current screens using:
 *
 *    for s in screen do
 *        -- do something
 *    end
 *
 * Most basic Awesome objects also have a screen property, see `mouse.screen`
 * `client.screen`, `wibox.screen` and `tag.screen`.
 *
 * @DOC_uml_nav_tables_screen_EXAMPLE@
 *
 * @author Julien Danjou &lt;julien@danjou.info&gt;
 * @copyright 2008-2009 Julien Danjou
 * @coreclassmod screen
 */

#include "objects/screen.h"

#include "banning.h"
#include "common/luaclass.h"
#include "common/lualib.h"
#include "common/luaobject.h"
#include "common/util.h"
#include "event.h"
#include "globalconf.h"
#include "lua.h"
#include "luaa.h"
#include "luaconf.h"
#include "objects/client.h"
#include "objects/drawin.h"

#include <algorithm>
#include <stdio.h>
#include <string_view>
#include <xcb/randr.h>
#include <xcb/xcb.h>
#include <xcb/xinerama.h>

/* The XID that is used on fake screens. X11 guarantees that the top three bits
 * of a valid XID are zero, so this will not clash with anything.
 */
#define FAKE_SCREEN_XID ((uint32_t)0xffffffff)

/** AwesomeWM is about to scan for existing screens.
 *
 * Connect to this signal when code needs to be executed after the Lua context
 * is initialized and modules are loaded, but before screens are added.
 *
 * To manage screens manually, set `screen.automatic_factory = false` and
 * connect to the `property::viewports` signal. It is then possible to use
 * `screen.fake_add` to create virtual screens. Be careful when using this,
 * when done incorrectly, no screens will be created. Using Awesome with zero
 * screens is **not** supported.
 *
 * @signal scanning
 * @see property::viewports
 * @see screen.fake_add
 */

/** AwesomeWM is done scanning for screens.
 *
 * Connect to this signal to execute code after the screens have been created,
 * but before the clients are added. This signal can also be used to split
 * physical screens into multiple virtual screens before the clients (and their
 * rules) are executed.
 *
 * Note that if no screens exist at this point, the fallback code will be
 * triggered and the default (detected) screens will be added.
 *
 * @signal scanned
 * @see screen.fake_resize
 * @see screen.fake_add
 */

/** Screen is a table where indexes are screen numbers. You can use `screen[1]`
 * to get access to the first screen, etc. Alternatively, if RANDR information
 * is available, you can use output names for finding screen objects.
 * The primary screen can be accessed as `screen.primary`.
 * Each screen has a set of properties.
 *
 */

/**
 * @signal primary_changed
 */

/**
 * This signal is emitted when a new screen is added to the current setup.
 * @signal added
 */

/**
 * This signal is emitted when a screen is removed from the setup.
 * @signal removed
 * @request tag screen removed granted When a screen is removed, `request::screen`
 *  is called on all screen tags to try to relocate them.
 */

/** This signal is emitted when the list of available screens changes.
 * @signal list
 */

/** When 2 screens are swapped
 * @tparam screen screen The other screen
 * @tparam boolean is_source If self is the source or the destination of the swap
 * @signal swapped
 */

/** This signal is emitted when the list of physical screen viewport changes.
 *
 * Each viewport in the list corresponds to a **physical** screen rectangle, which
 * is **not** the `viewports` property of the `screen` objects.
 *
 * Each entry in the `viewports` entry has the following keys:
 *
 * * `geometry` *(table)*: A table with an `x`, `y`, `width` and `height` keys.
 * * `outputs` *(table)*: All outputs sharing this viewport.
 * * `maximum_dpi` *(number)*: The DPI of the most dense output.
 * * `minimum_dpi` *(number)*: The DPI of the least dense output.
 * * `preferred_dpi` *(number)*: The optimal DPI.
 *
 * @signal property::viewports
 * @tparam table viewports A table containing all physical viewports.
 * @see automatic_factory
 */

/**
 * The primary screen.
 *
 * @tfield screen primary
 */

/**
 * If `screen` objects are created automatically when new viewports are detected.
 *
 * Be very, very careful when setting this to false. You might end up with
 * no screens. This is **not** supported. Always connect to the `scanned`
 * signal to make sure to create a fallback screen if none were created.
 *
 * @tfield[opt=true] boolean screen.automatic_factory
 * @see property::viewports
 */

/**
 * The screen coordinates.
 *
 * The returned table contains the `x`, `y`, `width` and `height` keys.
 *
 * @DOC_screen_geometry_EXAMPLE@
 *
 * @property geometry
 * @tparam table geometry
 * @tparam integer geometry.x The horizontal position.
 * @tparam integer geometry.y The vertical position.
 * @tparam integer geometry.width The width.
 * @tparam integer geometry.height The height.
 * @propertydefault Either from `xrandr` or from `fake_resize`.
 * @propertyunit pixel
 * @propemits false false
 * @readonly
 */

/**
 * The internal screen number.
 *
 * * The indeces are a continuous sequence from 1 to `screen.count()`.
 * * It is **NOT** related to the actual screen position relative to each
 *   other.
 * * 1 is **NOT** necessarily the primary screen.
 * * When screens are added and removed indices **CAN** change.
 *
 * If you really want to keep an array of screens you should use something
 * along:
 *
 *     local myscreens = setmetatable({}, {__mode="k"})
 *     myscreens[ screen[1] ] = "mydata"
 *
 * But it might be a better option to simply store the data directly in the
 * screen object as:
 *
 *     screen[1].mydata = "mydata"
 *
 * Remember that screens are also objects, so if you only want to store a simple
 * property, you can do it directly:
 *
 *     screen[1].answer = 42
 *
 * @property index
 * @tparam integer index
 * @propertydefault The index is not derived from the geometry. It may or may
 *  not be from `xrandr`. It isn't a good idea to rely on indices.
 * @negativeallowed false
 * @see screen
 * @readonly
 */

/**
 * The screen workarea.
 *
 * The workarea is a subsection of the screen where clients can be placed. It
 * usually excludes the toolbars (see `awful.wibar`) and dockable clients
 * (see `client.dockable`) like WindowMaker DockAPP.
 *
 * It can be modified be altering the `wibox` or `client` struts.
 *
 * @DOC_screen_workarea_EXAMPLE@
 *
 * @property workarea
 * @tparam table workarea
 * @tparam integer workarea.x The horizontal position
 * @tparam integer workarea.y The vertical position
 * @tparam integer workarea.width The width
 * @tparam integer workarea.height The height
 * @propertyunit pixel
 * @propertydefault Based on `geometry` with the `awful.wibar` and docks area
 *  substracted.
 * @propemits false false
 * @see client.struts
 * @readonly
 */

/** Get the number of instances.
 *
 * @treturn table The number of screen objects alive.
 * @staticfct instances
 */

/* Set a __index metamethod for all screen instances.
 * @tparam function cb The meta-method
 * @staticfct set_index_miss_handler
 */

/* Set a __newindex metamethod for all screen instances.
 * @tparam function cb The meta-method
 * @staticfct set_newindex_miss_handler
 */

struct screen_output_t {
    /** The XRandR names of the output */
    std::string name;
    /** The size in millimeters */
    uint32_t mm_width, mm_height;
    /** The XID */
    std::vector<xcb_randr_output_t> outputs;
};

/** Check if a screen is valid */
static bool screen_checker(screen_t* s) { return s->valid; }

static lua_class_t screen_class{
  "screen",
  NULL,
  {[](auto* state) -> lua_object_t* { return newobj<screen_t, screen_class>(state); },
    destroyObject<screen_t>,
    [](auto* obj) { return screen_checker(static_cast<screen_t*>(obj)); },
    Lua::class_index_miss_property,
    Lua::class_newindex_miss_property},
};

/** Get a screen argument from the lua stack */
screen_t* luaA_checkscreen(lua_State* L, int sidx) {
    if (lua_isnumber(L, sidx)) {
        int screen = lua_tointeger(L, sidx);
        if (screen < 1 || (size_t)screen > Manager::get().screens.size()) {
            Lua::warn(L,
                      "invalid screen number: %d (of %d existing)",
                      screen,
                      (int)Manager::get().screens.size());
            lua_pushnil(L);
            return NULL;
        }
        return Manager::get().screens[screen - 1];
    } else {
        return screen_class.checkudata<screen_t>(L, sidx);
    }
}

static void screen_deduplicate(lua_State* L, std::vector<screen_t*>* screens) {
    /* Remove duplicate screens */
    for (size_t first = 0; first < screens->size(); first++) {
        screen_t* first_screen = screens->at(first);
        for (size_t second = 0; second < screens->size(); second++) {
            screen_t* second_screen = screens->at(second);
            if (first == second) {
                continue;
            }

            if (first_screen->geometry.width < second_screen->geometry.width &&
                first_screen->geometry.height < second_screen->geometry.height) {
                /* Don't drop a smaller screen */
                continue;
            }

            if (first_screen->geometry.top_left == second_screen->geometry.top_left) {
                /* Found a duplicate */
                first_screen->geometry.width =
                  MAX(first_screen->geometry.width, second_screen->geometry.width);
                first_screen->geometry.height =
                  MAX(first_screen->geometry.height, second_screen->geometry.height);

                screens->erase(screens->begin() + second);
                luaA_object_unref(L, second_screen);

                /* Restart the search */
                screen_deduplicate(L, screens);
                return;
            }
        }
    }
}

/** Keep track of the screen viewport(s) independently from the screen objects.
 *
 * A viewport is a collection of `outputs` objects and their associated
 * metadata. This structure is copied into Lua and then further extended from
 * there. The `id` field allows to differentiate between viewports that share
 * the same position and dimensions without having to rely on userdata pointer
 * comparison.
 *
 * Screen objects are widely used by the public API and imply a very "visible"
 * concept. A viewport is a subset of what the concerns the "screen" class
 * previously handled. It is meant to be used by some low level Lua logic to
 * create screens from Lua rather than from C. This is required to increase the
 * flexibility of multi-screen setup or when screens are connected and
 * disconnected often.
 *
 * Design rationals:
 *
 * * The structure is not directly shared with Lua to avoid having to use the
 *   slow "miss_handler" and unsafe "valid" systems used by other CAPI objects.
 * * The `viewport_t` implements a linked-list because its main purpose is to
 *   offers a deduplication algorithm. Random access is never required.
 * * Everything that can be done in Lua is done in Lua.
 * * Since the legacy and "new" way to initialize screens share a lot of steps,
 *   the C code is bent to share as much code as possible. This will reduce the
 *   "dead code" and improve code coverage by the tests.
 *
 */
typedef struct viewport_t {
    bool marked;
    area_t area;
    int id;
    struct viewport_t* next;
    screen_t* screen;
    std::vector<screen_output_t> outputs;
} viewport_t;

static viewport_t* first_screen_viewport = NULL;
static viewport_t* last_screen_viewport = NULL;
static int screen_area_gid = 1;

static void luaA_viewport_get_outputs(lua_State* L, viewport_t* a) {
    lua_createtable(L, 0, a ? a->outputs.size() : 0);
    if (!a) {
        return;
    }

    int count = 1;

    for (const auto& output : a->outputs) {
        lua_createtable(L, 3, 0);

        lua_pushstring(L, "mm_width");
        lua_pushinteger(L, output.mm_width);
        lua_settable(L, -3);

        lua_pushstring(L, "mm_height");
        lua_pushinteger(L, output.mm_height);
        lua_settable(L, -3);

        lua_pushstring(L, "name");
        lua_pushstring(L, output.name.c_str());
        lua_settable(L, -3);

        lua_pushstring(L, "viewport_id");
        lua_pushinteger(L, a->id);
        lua_settable(L, -3);

        /* Add to the outputs */
        lua_rawseti(L, -2, count++);
    }
}

static int luaA_viewports(lua_State* L) {
    /* All viewports */
    lua_newtable(L);

    viewport_t* a = first_screen_viewport;

    if (!a) {
        return 1;
    }

    int count = 1;

    do {
        lua_newtable(L);

        /* The geometry */
        lua_pushstring(L, "geometry");
        Lua::pusharea(L, a->area);
        /* Add the geometry table to the arguments */
        lua_settable(L, -3);

        /* Add the outputs table to the arguments */
        lua_pushstring(L, "outputs");
        luaA_viewport_get_outputs(L, a);
        lua_settable(L, -3);

        /* Add an identifier to better detect when screens are removed */
        lua_pushstring(L, "id");
        lua_pushinteger(L, a->id);
        lua_settable(L, -3);

        lua_rawseti(L, -2, count++);
    } while ((a = a->next));

    return 1;
}

/* Give Lua a chance to handle or blacklist a viewport before creating the
 * screen object.
 */
static void viewports_notify(lua_State* L) {
    if (!first_screen_viewport) {
        return;
    }

    luaA_viewports(L);

    screen_class.emit_signal(L, "property::_viewports", 1);
}

static viewport_t* viewport_add(lua_State* L, area_t area) {
    /* Search existing to avoid having to deduplicate later */
    viewport_t* a = first_screen_viewport;

    do {
        if (a && a->area == area) {
            a->marked = true;
            return a;
        }
    } while (a && (a = a->next));

    auto node = new viewport_t;
    node->area = area;
    node->id = screen_area_gid++;
    node->next = NULL;
    node->screen = NULL;
    node->marked = true;

    if (!first_screen_viewport) {
        first_screen_viewport = node;
        last_screen_viewport = node;
    } else {
        last_screen_viewport->next = node;
        last_screen_viewport = node;
    }

    assert(first_screen_viewport && last_screen_viewport);

    return node;
}

static void monitor_unmark(void) {
    viewport_t* a = first_screen_viewport;

    if (!a) {
        return;
    }

    do {
        a->marked = false;
    } while ((a = a->next));
}

static void viewport_purge(void) {
    viewport_t* cur = first_screen_viewport;

    /* Move the head of the list */
    while (first_screen_viewport && !first_screen_viewport->marked) {
        cur = first_screen_viewport;
        first_screen_viewport = cur->next;

        auto it = std::ranges::find_if(Manager::get().screens,
                                       [cur](auto* screen) { return cur == screen->viewport; });
        if (it != Manager::get().screens.end()) {
            (*it)->viewport = nullptr;
        }

        cur->outputs.clear();
        delete cur;
    }

    if (!first_screen_viewport) {
        last_screen_viewport = NULL;
        return;
    }

    cur = first_screen_viewport;

    /* Drop unmarked entries */
    do {
        if (cur->next && !cur->next->marked) {
            viewport_t* tmp = cur->next;
            cur->next = cur->next->next;

            if (tmp == last_screen_viewport) {
                last_screen_viewport = cur;
            }

            auto it = std::ranges::find_if(Manager::get().screens,
                                           [tmp](auto* screen) { return tmp == screen->viewport; });
            if (it != Manager::get().screens.end()) {
                (*it)->viewport = nullptr;
            }

            tmp->outputs.clear();

            delete tmp;
        } else {
            cur = cur->next;
        }

    } while (cur);
}

static screen_t* screen_add(lua_State* L, std::vector<screen_t*>* screens) {
    screen_t* new_screen = newobj<screen_t, screen_class>(L);
    luaA_object_ref(L, -1);
    screens->push_back(new_screen);
    new_screen->xid = XCB_NONE;
    new_screen->lifecycle = SCREEN_LIFECYCLE_USER;
    return new_screen;
}

/* Monitors were introduced in RandR 1.5 */
#ifdef XCB_RANDR_GET_MONITORS

static screen_output_t screen_get_randr_output(lua_State* L,
                                               xcb_randr_monitor_info_iterator_t* it) {
    screen_output_t output;
    xcb_randr_output_t* randr_outputs;

    output.mm_width = it->data->width_in_millimeters;
    output.mm_height = it->data->height_in_millimeters;

    auto name_c = getConnection().get_atom_name_unchecked(it->data->name);
    auto name_r = getConnection().get_atom_name_reply(name_c);

    if (name_r) {
        const char* name = xcb_get_atom_name_name(name_r.get());
        size_t len = xcb_get_atom_name_name_length(name_r.get());
        output.name.assign(name, len);
    } else {
        output.name = "unknown";
    }

    randr_outputs = xcb_randr_monitor_info_outputs(it->data);

    for (int i = 0; i < xcb_randr_monitor_info_outputs_length(it->data); i++) {
        output.outputs.push_back(randr_outputs[i]);
    }

    return output;
}

static void screen_scan_randr_monitors(lua_State* L, std::vector<screen_t*>* screens) {
    auto monitors_c = getConnection().randr().get_monitors(Manager::get().screen->root, 1);
    auto monitors_r = getConnection().randr().get_monitors_reply(monitors_c);
    xcb_randr_monitor_info_iterator_t monitor_iter;

    if (monitors_r == NULL) {
        log_warn("RANDR GetMonitors failed; this should not be possible");
        return;
    }

    for (monitor_iter = xcb_randr_get_monitors_monitors_iterator(monitors_r.get());
         monitor_iter.rem;
         xcb_randr_monitor_info_next(&monitor_iter)) {
        screen_t* new_screen;

        screen_output_t output = screen_get_randr_output(L, &monitor_iter);

        viewport_t* viewport = viewport_add(L,
                                            area_t{
                                              {monitor_iter.data->x, monitor_iter.data->y},
                                              monitor_iter.data->width,
                                              monitor_iter.data->height
        });

        viewport->outputs.push_back(output);

        if (Manager::get().startup.ignore_screens) {
            continue;
        }

        new_screen = screen_add(L, screens);
        new_screen->lifecycle = (screen_lifecycle_t)(new_screen->lifecycle | SCREEN_LIFECYCLE_C);
        viewport->screen = new_screen;
        new_screen->viewport = viewport;
        new_screen->geometry.top_left = {monitor_iter.data->x, monitor_iter.data->y};
        new_screen->geometry.width = monitor_iter.data->width;
        new_screen->geometry.height = monitor_iter.data->height;
        new_screen->xid = monitor_iter.data->name;
    }
}
#else
static void screen_scan_randr_monitors(lua_State* L, screen_array_t* screens) {}
#endif

static void screen_get_randr_crtcs_outputs(lua_State* L,
                                           xcb_randr_get_crtc_info_reply_t* crtc_info_r,
                                           std::vector<screen_output_t>* outputs) {
    xcb_randr_output_t* randr_outputs = xcb_randr_get_crtc_info_outputs(crtc_info_r);

    for (int j = 0; j < xcb_randr_get_crtc_info_outputs_length(crtc_info_r); j++) {
        auto output_info_c =
          getConnection().randr().get_output_info(randr_outputs[j], XCB_CURRENT_TIME);
        auto output_info_r = getConnection().randr().get_output_info_reply(output_info_c, NULL);
        screen_output_t output;

        if (!output_info_r) {
            log_warn("RANDR GetOutputInfo failed; this should not be possible");
            continue;
        }

        outputs->emplace_back(screen_output_t{
          .name{(char*)xcb_randr_get_output_info_name(output_info_r.get()),
                (size_t)xcb_randr_get_output_info_name_length(output_info_r.get())},
          .mm_width = output_info_r->mm_width,
          .mm_height = output_info_r->mm_height,
          .outputs{randr_outputs[j]}
        });
    }
}

static void screen_scan_randr(lua_State* L, std::vector<screen_t*>* screens) {
    const xcb_query_extension_reply_t* extension_reply;
    uint32_t major_version;
    uint32_t minor_version;

    /* Check for extension before checking for XRandR */
    extension_reply = getConnection().get_extension_data(&xcb_randr_id);
    if (!extension_reply || !extension_reply->present) {
        return;
    }

    auto version_reply =
      getConnection().randr().query_version_reply(getConnection().randr().query_version(1, 5), 0);
    if (!version_reply) {
        return;
    }

    major_version = version_reply->major_version;
    minor_version = version_reply->minor_version;

    /* Do we agree on a supported version? */
    if (major_version != 1 || minor_version < 2) {
        return;
    }

    /* We want to know when something changes */
    getConnection().randr().select_input(Manager::get().screen->root,
                                         XCB_RANDR_NOTIFY_MASK_OUTPUT_CHANGE);

    screen_scan_randr_monitors(L, screens);

    if (screens->size() == 0 && !Manager::get().startup.ignore_screens) {
        /* Scanning failed, disable randr again */
        getConnection().randr().select_input(Manager::get().screen->root, 0);
        log_fatal("screen scan failed (found 0 screens)");
    }
}

static void screen_scan_xinerama(lua_State* L, std::vector<screen_t*>* screens) {
    bool xinerama_is_active;
    xcb_xinerama_is_active_reply_t* xia;
    xcb_xinerama_query_screens_reply_t* xsq;
    xcb_xinerama_screen_info_t* xsi;
    int xinerama_screen_number;

    /* Check for extension before checking for Xinerama */
    auto extension_reply = getConnection().get_extension_data(&xcb_xinerama_id);
    if (!extension_reply || !extension_reply->present) {
        return;
    }

    xia = xcb_xinerama_is_active_reply(getConnection().getConnection(),
                                       xcb_xinerama_is_active(getConnection().getConnection()),
                                       NULL);
    xinerama_is_active = xia && xia->state;
    p_delete(&xia);
    if (!xinerama_is_active) {
        return;
    }

    xsq = xcb_xinerama_query_screens_reply(
      getConnection().getConnection(),
      xcb_xinerama_query_screens_unchecked(getConnection().getConnection()),
      NULL);

    if (!xsq) {
        log_warn("Xinerama QueryScreens failed; this should not be possible");
        return;
    }

    xsi = xcb_xinerama_query_screens_screen_info(xsq);
    xinerama_screen_number = xcb_xinerama_query_screens_screen_info_length(xsq);

    for (int screen = 0; screen < xinerama_screen_number; screen++) {
        viewport_t* viewport = viewport_add(
          L,
          area_t{
            {xsi[screen].x_org, xsi[screen].y_org},
            xsi[screen].width, xsi[screen].height
        });

        if (Manager::get().startup.ignore_screens) {
            continue;
        }

        screen_t* s = screen_add(L, screens);
        viewport->screen = s;
        s->viewport = viewport;
        s->lifecycle = (screen_lifecycle_t)(s->lifecycle | SCREEN_LIFECYCLE_C);
        s->geometry.top_left = {xsi[screen].x_org, xsi[screen].y_org};
        s->geometry.width = xsi[screen].width;
        s->geometry.height = xsi[screen].height;
    }

    p_delete(&xsq);
}

static void screen_scan_x11(lua_State* L, std::vector<screen_t*>* screens) {
    xcb_screen_t* xcb_screen = Manager::get().screen;

    viewport_t* viewport =
      viewport_add(L,
                   area_t{
                     {0, 0},
                     xcb_screen->width_in_pixels, xcb_screen->height_in_pixels
    });

    if (Manager::get().startup.ignore_screens) {
        return;
    }

    screen_t* s = screen_add(L, screens);
    viewport->screen = s;
    s->lifecycle = (screen_lifecycle_t)(s->lifecycle | SCREEN_LIFECYCLE_C);
    s->viewport = viewport;
    s->geometry.top_left = {0, 0};
    s->geometry.width = xcb_screen->width_in_pixels;
    s->geometry.height = xcb_screen->height_in_pixels;
}

static void screen_added(lua_State* L, screen_t* screen) {
    screen->workarea = screen->geometry;
    screen->valid = true;
    luaA_object_push(L, screen);
    luaA_object_emit_signal(L, -1, "_added", 0);
    lua_pop(L, 1);
}

void screen_emit_scanned(void) {
    lua_State* L = globalconf_get_lua_State();
    screen_class.emit_signal(L, "scanned", 0);
}

void screen_emit_scanning(void) {
    lua_State* L = globalconf_get_lua_State();
    screen_class.emit_signal(L, "scanning", 0);
}

static void screen_scan_common(bool quiet) {
    lua_State* L;

    L = globalconf_get_lua_State();

    monitor_unmark();

    screen_scan_randr(L, &Manager::get().screens);
    if (Manager::get().screens.size() == 0) {
        screen_scan_xinerama(L, &Manager::get().screens);
    }
    if (Manager::get().screens.size() == 0) {
        screen_scan_x11(L, &Manager::get().screens);
    }

    awsm_check(Manager::get().screens.size() > 0 || Manager::get().startup.ignore_screens);

    screen_deduplicate(L, &Manager::get().screens);

    for (auto* screen : Manager::get().screens) {
        screen_added(L, screen);
    }

    viewport_purge();

    if (!quiet) {
        viewports_notify(L);
    }

    screen_update_primary();
}

/** Get screens informations and fill global configuration.
 */
void screen_scan(void) {
    screen_emit_scanning();
    screen_scan_common(false);
}

static int luaA_scan_quiet(lua_State* L) {
    screen_scan_common(true);
    return 0;
}

/* Called when a screen is removed, removes references to the old screen */
static void screen_removed(lua_State* L, int sidx) {
    auto screen = screen_class.checkudata<screen_t>(L, sidx);

    luaA_object_emit_signal(L, sidx, "removed", 0);

    if (Manager::get().primary_screen == screen) {
        Manager::get().primary_screen = NULL;
    }

    for (auto* c : Manager::get().clients) {
        if (c->screen == screen) {
            screen_client_moveto(c, screen_getbycoord(c->geometry.top_left), false);
        }
    }
}

void screen_cleanup(void) {
    Manager::get().screens.clear();

    monitor_unmark();
    viewport_purge();
}

static void screen_modified(screen_t* existing_screen, screen_t* other_screen) {
    lua_State* L = globalconf_get_lua_State();

    if (existing_screen->geometry != other_screen->geometry) {
        area_t old_geometry = existing_screen->geometry;
        existing_screen->geometry = other_screen->geometry;
        luaA_object_push(L, existing_screen);
        Lua::pusharea(L, old_geometry);
        luaA_object_emit_signal(L, -2, "property::geometry", 1);
        lua_pop(L, 1);
        screen_update_workarea(existing_screen);
    }

    const int other_len = other_screen->viewport ? other_screen->viewport->outputs.size() : 0;

    const int existing_len =
      existing_screen->viewport ? existing_screen->viewport->outputs.size() : 0;

    bool outputs_changed =
      (!(existing_screen->viewport && other_screen->viewport)) || existing_len != other_len;

    if (existing_screen->viewport && other_screen->viewport && !outputs_changed) {
        for (int i = 0; i < (int)existing_screen->viewport->outputs.size(); i++) {
            screen_output_t* existing_output = &existing_screen->viewport->outputs[i];
            screen_output_t* other_output = &other_screen->viewport->outputs[i];
            outputs_changed |= existing_output->mm_width != other_output->mm_width;
            outputs_changed |= existing_output->mm_height != other_output->mm_height;
            outputs_changed |= (existing_output->name != other_output->name);
        }
    }

    /* Brute-force update the outputs by swapping */
    if (existing_screen->viewport || other_screen->viewport) {
        viewport_t* tmp = other_screen->viewport;
        other_screen->viewport = existing_screen->viewport;

        existing_screen->viewport = tmp;

        if (outputs_changed) {
            luaA_object_push(L, existing_screen);
            luaA_object_emit_signal(L, -1, "property::_outputs", 0);
            lua_pop(L, 1);
        }
    }
}

static gboolean screen_refresh(gpointer unused) {
    Manager::get().x.screen_refresh_pending = false;

    monitor_unmark();

    std::vector<screen_t*> new_screens;
    std::vector<screen_t*> removed_screens;
    lua_State* L = globalconf_get_lua_State();
    bool list_changed = false;

    screen_scan_randr_monitors(L, &new_screens);

    viewport_purge();

    viewports_notify(L);

    screen_deduplicate(L, &new_screens);

    /* Running without any screens at all is no fun. */
    if (new_screens.empty()) {
        screen_scan_x11(L, &new_screens);
    }

    /* Add new screens */
    for (auto* new_screen : new_screens) {
        auto it = std::ranges::find_if(Manager::get().screens, [new_screen](auto* screen) {
            return screen->xid == new_screen->xid;
        });
        if (it == Manager::get().screens.end()) {
            Manager::get().screens.push_back(new_screen);
            screen_added(L, new_screen);
            /* Get an extra reference since both new_screens and
             * globalconf.screens reference this screen now */
            luaA_object_push(L, new_screen);
            luaA_object_ref(L, -1);

            list_changed = true;
        }
    }

    /* Remove screens which are gone */
    for (size_t i = 0; i < Manager::get().screens.size(); i++) {
        screen_t* old_screen = Manager::get().screens[i];
        bool found = old_screen->xid == FAKE_SCREEN_XID;

        auto it = std::ranges::find_if(
          new_screens, [old_screen](auto* screen) { return screen->xid == old_screen->xid; });
        found |= it != new_screens.end();

        if (old_screen->lifecycle & SCREEN_LIFECYCLE_C && !found) {
            Manager::get().screens.erase(Manager::get().screens.begin() + i);
            i--;
            removed_screens.push_back(old_screen);

            list_changed = true;
        }
    }
    for (auto* old_screen : removed_screens) {
        luaA_object_push(L, old_screen);
        screen_removed(L, -1);
        lua_pop(L, 1);
        old_screen->valid = false;
        luaA_object_unref(L, old_screen);
    }
    removed_screens.clear();

    /* Update changed screens */
    for (auto* existing_screen : Manager::get().screens) {
        for (auto* new_screen : new_screens) {
            if (existing_screen->xid == new_screen->xid) {
                screen_modified(existing_screen, new_screen);
            }
        }
    }

    for (auto* screen : new_screens) {
        luaA_object_unref(L, screen);
    }
    new_screens.clear();

    screen_update_primary();

    if (list_changed) {
        screen_class.emit_signal(L, "list", 0);
    }

    return G_SOURCE_REMOVE;
}

void screen_schedule_refresh(void) {
    if (Manager::get().x.screen_refresh_pending) {
        return;
    }

    Manager::get().x.screen_refresh_pending = true;
    g_idle_add_full(G_PRIORITY_LOW, screen_refresh, NULL, NULL);
}

/** Return the squared distance of the given screen to the coordinates.
 * \param screen The screen
 * \param x X coordinate
 * \param y Y coordinate
 * \return Squared distance of the point to the screen.
 */
static unsigned int screen_get_distance_squared(screen_t* s, int x, int y) {
    auto [sx, sy] = s->geometry.top_left;
    int sheight = s->geometry.height, swidth = s->geometry.width;
    unsigned int dist_x, dist_y;

    /* Calculate distance in X coordinate */
    if (x < sx) {
        dist_x = sx - x;
    } else if (x < sx + swidth) {
        dist_x = 0;
    } else {
        dist_x = x - sx - swidth;
    }

    /* Calculate distance in Y coordinate */
    if (y < sy) {
        dist_y = sy - y;
    } else if (y < sy + sheight) {
        dist_y = 0;
    } else {
        dist_y = y - sy - sheight;
    }

    return dist_x * dist_x + dist_y * dist_y;
}

/** Return the first screen number where the coordinates belong to.
 * \param x X coordinate
 * \param y Y coordinate
 * \return Screen pointer or screen param if no match or no multi-head.
 */
screen_t* screen_getbycoord(point p) {
    for (auto* s : Manager::get().screens) {
        if (s->geometry.inside(p)) {
            return s;
        }
    }

    /* No screen found, find nearest screen. */
    screen_t* nearest_screen = NULL;
    unsigned int nearest_dist = UINT_MAX;
    for (auto* s : Manager::get().screens) {
        unsigned int dist_sq = screen_get_distance_squared(s, p.x, p.y);
        if (dist_sq < nearest_dist) {
            nearest_dist = dist_sq;
            nearest_screen = s;
        }
    }
    return nearest_screen;
}

/** Is there any overlap between the given geometry and a given screen?
 * \param screen The logical screen number.
 * \param geom The geometry
 * \return True if there is any overlap between the geometry and a given screen.
 */
bool screen_area_in_screen(screen_t* s, area_t geom) {
    return (geom.top_left.x < s->geometry.top_left.x + s->geometry.width) &&
           (geom.top_left.x + geom.width > s->geometry.top_left.x) &&
           (geom.top_left.y < s->geometry.top_left.y + s->geometry.height) &&
           (geom.top_left.y + geom.height > s->geometry.top_left.y);
}

void screen_update_workarea(screen_t* screen) {
    area_t area = screen->geometry;
    uint16_t top = 0, bottom = 0, left = 0, right = 0;

#define COMPUTE_STRUT(o)                                                                          \
    {                                                                                             \
        if ((o)->strut.top_start_x || (o)->strut.top_end_x || (o)->strut.top) {                   \
            if ((o)->strut.top)                                                                   \
                top = MAX(top, (o)->strut.top);                                                   \
            else                                                                                  \
                top =                                                                             \
                  MAX(top, ((o)->geometry.top_left.y - area.top_left.y) + (o)->geometry.height);  \
        }                                                                                         \
        if ((o)->strut.bottom_start_x || (o)->strut.bottom_end_x || (o)->strut.bottom) {          \
            if ((o)->strut.bottom)                                                                \
                bottom = MAX(bottom, (o)->strut.bottom);                                          \
            else                                                                                  \
                bottom = MAX(bottom, (area.top_left.y + area.height) - (o)->geometry.top_left.y); \
        }                                                                                         \
        if ((o)->strut.left_start_y || (o)->strut.left_end_y || (o)->strut.left) {                \
            if ((o)->strut.left)                                                                  \
                left = MAX(left, (o)->strut.left);                                                \
            else                                                                                  \
                left =                                                                            \
                  MAX(left, ((o)->geometry.top_left.x - area.top_left.x) + (o)->geometry.width);  \
        }                                                                                         \
        if ((o)->strut.right_start_y || (o)->strut.right_end_y || (o)->strut.right) {             \
            if ((o)->strut.right)                                                                 \
                right = MAX(right, (o)->strut.right);                                             \
            else                                                                                  \
                right = MAX(right, (area.top_left.x + area.width) - (o)->geometry.top_left.x);    \
        }                                                                                         \
    }

    for (auto* c : Manager::get().clients) {
        if (c->screen == screen && client_isvisible(c)) {
            COMPUTE_STRUT(c)
        }
    }

    for (auto* drawin : Manager::get().drawins) {
        if (drawin->visible) {
            screen_t* d_screen = screen_getbycoord(drawin->geometry.top_left);
            if (d_screen == screen) {
                COMPUTE_STRUT(drawin)
            }
        }
    }

#undef COMPUTE_STRUT

    area.top_left += {left, top};
    area.width -= MIN(area.width, left + right);
    area.height -= MIN(area.height, top + bottom);

    if (area == screen->workarea) {
        return;
    }

    area_t old_workarea = screen->workarea;
    screen->workarea = area;
    lua_State* L = globalconf_get_lua_State();
    luaA_object_push(L, screen);
    Lua::pusharea(L, old_workarea);
    luaA_object_emit_signal(L, -2, "property::workarea", 1);
    lua_pop(L, 1);
}

/** Move a client to a virtual screen.
 * \param c The client to move.
 * \param new_screen The destination screen.
 * \param doresize Set to true if we also move the client to the new x and
 *        y of the new screen.
 */
void screen_client_moveto(client* c, screen_t* new_screen, bool doresize) {
    lua_State* L = globalconf_get_lua_State();
    screen_t* old_screen = c->screen;
    area_t from, to;
    bool had_focus = false;

    if (new_screen == c->screen) {
        return;
    }

    if (Manager::get().focus.client == c) {
        had_focus = true;
    }

    c->screen = new_screen;

    if (!doresize) {
        luaA_object_push(L, c);
        if (old_screen != NULL) {
            luaA_object_push(L, old_screen);
        } else {
            lua_pushnil(L);
        }
        luaA_object_emit_signal(L, -2, "property::screen", 1);
        lua_pop(L, 1);
        if (had_focus) {
            client_focus(c);
        }
        return;
    }

    from = old_screen->geometry;
    to = c->screen->geometry;

    area_t new_geometry = c->geometry;

    new_geometry.top_left = to.top_left + new_geometry.top_left - from.top_left;

    /* resize the client if it doesn't fit the new screen */
    if (new_geometry.width > to.width) {
        new_geometry.width = to.width;
    }
    if (new_geometry.height > to.height) {
        new_geometry.height = to.height;
    }

    /* make sure the client is still on the screen */
    if (new_geometry.right() > to.right()) {
        new_geometry.top_left.x = to.right() - new_geometry.width;
    }
    if (new_geometry.bottom() > to.bottom()) {
        new_geometry.top_left.y = to.bottom() - new_geometry.height;
    }
    if (!screen_area_in_screen(new_screen, new_geometry)) {
        /* If all else fails, force the client to end up on screen. */
        new_geometry.top_left = to.top_left;
    }

    /* move / resize the client */
    client_resize(c, new_geometry, false);

    /* emit signal */
    luaA_object_push(L, c);
    if (old_screen != NULL) {
        luaA_object_push(L, old_screen);
    } else {
        lua_pushnil(L);
    }
    luaA_object_emit_signal(L, -2, "property::screen", 1);
    lua_pop(L, 1);

    if (had_focus) {
        client_focus(c);
    }
}

/** Get a screen's index. */
int screen_get_index(lua_object_t* s) {
    auto it = std::ranges::find(Manager::get().screens, static_cast<screen_t*>(s));
    return it != Manager::get().screens.end() ? (it - Manager::get().screens.begin()) + 1 : 0;
}

void screen_update_primary(void) {
    screen_t* primary_screen = NULL;
    xcb_randr_get_output_primary_reply_t* primary = xcb_randr_get_output_primary_reply(
      getConnection().getConnection(),
      xcb_randr_get_output_primary(getConnection().getConnection(), Manager::get().screen->root),
      NULL);

    if (!primary) {
        return;
    }

    for (auto* screen : Manager::get().screens) {
        if (screen->viewport) {
            for (auto& output : screen->viewport->outputs) {
                for (auto randr_output : output.outputs) {
                    if (randr_output == primary->output) {
                        primary_screen = screen;
                    }
                }
            }
        }
    }
    p_delete(&primary);

    if (!primary_screen || primary_screen == Manager::get().primary_screen) {
        return;
    }

    lua_State* L = globalconf_get_lua_State();
    screen_t* old = Manager::get().primary_screen;
    Manager::get().primary_screen = primary_screen;

    if (old) {
        luaA_object_push(L, old);
        luaA_object_emit_signal(L, -1, "primary_changed", 0);
        lua_pop(L, 1);
    }
    luaA_object_push(L, primary_screen);
    luaA_object_emit_signal(L, -1, "primary_changed", 0);
    lua_pop(L, 1);
}

screen_t* screen_get_primary(void) {
    if (!Manager::get().primary_screen && Manager::get().screens.size() > 0) {
        Manager::get().primary_screen = Manager::get().screens[0];

        lua_State* L = globalconf_get_lua_State();
        luaA_object_push(L, Manager::get().primary_screen);
        luaA_object_emit_signal(L, -1, "primary_changed", 0);
        lua_pop(L, 1);
    }
    return Manager::get().primary_screen;
}

/** Screen module.
 * \param L The Lua VM state.
 * \return The number of elements pushed on stack.
 * \luastack
 * \lfield number The screen number, to get a screen.
 */
static int luaA_screen_module_index(lua_State* L) {
    auto type = lua_type(L, 2);
    if (type != LUA_TSTRING) {
        return luaA_object_push(L, luaA_checkscreen(L, 2));
    }
    auto name = Lua::tostring(L, 2);

    if (name == "primary") {
        return luaA_object_push(L, screen_get_primary());
    } else if (name == "automatic_factory") {
        lua_pushboolean(L, !Manager::get().startup.ignore_screens);
        return 1;
    }

    for (auto* screen : Manager::get().screens) {
        if (name == screen->name) {
            return luaA_object_push(L, screen);
        } else if (screen->viewport) {
            for (const auto& output : screen->viewport->outputs) {
                if (output.name == name) {
                    return luaA_object_push(L, screen);
                }
            }
        }
    }

    Lua::warn(L, "Unknown screen output name: %s", name->data());
    lua_pushnil(L);
    return 1;
}

static int luaA_screen_module_newindex(lua_State* L) {
    auto buf = Lua::checkstring(L, 2);

    if (buf == "automatic_factory") {
        Manager::get().startup.ignore_screens = !Lua::checkboolean(L, 3);

        /* It *can* be useful if screens are added/removed later, but generally,
         * setting this should be done before screens are added
         */
        if (Manager::get().startup.ignore_screens && !Manager::get().startup.no_auto_screen) {
            Lua::warn(L,
                      "Setting automatic_factory only makes sense when AwesomeWM is"
                      " started with `--screen off`");
        }
    }

    return Lua::default_newindex(L);
}

/** Iterate over screens.
 * @usage
 * for s in screen do
 *     print("Oh, wow, we have screen " .. tostring(s))
 * end
 * @treturn function A lua iterator function.
 * @staticfct screen
 */
static int luaA_screen_module_call(lua_State* L) {
    int idx;

    if (lua_isnoneornil(L, 3)) {
        idx = 0;
    } else {
        idx = screen_get_index(luaA_checkscreen(L, 3));
    }
    if (idx >= 0 && idx < (int)Manager::get().screens.size()) {
        /* No +1 needed, index starts at 1, C array at 0 */
        luaA_object_push(L, Manager::get().screens[idx]);
    } else {
        lua_pushnil(L);
    }
    return 1;
}

static int luaA_screen_get_index(lua_State* L, lua_object_t* s) {
    lua_pushinteger(L, screen_get_index(s));
    return 1;
}

static int luaA_screen_get_outputs(lua_State* L, lua_object_t* s) {
    luaA_viewport_get_outputs(L, static_cast<screen_t*>(s)->viewport);

    /* The table of tables we created. */
    return 1;
}

static int luaA_screen_get_managed(lua_State* L, lua_object_t* o) {
    auto s = static_cast<screen_t*>(o);
    if (s->lifecycle & SCREEN_LIFECYCLE_LUA) {
        lua_pushstring(L, "Lua");
    } else if (s->lifecycle & SCREEN_LIFECYCLE_C) {
        lua_pushstring(L, "C");
    } else {
        lua_pushstring(L, "none");
    }

    return 1;
}

static int set_name(lua_State* L, lua_object_t* s) {
    static_cast<screen_t*>(s)->name = *Lua::checkstring(L, -1);
    return 0;
}

static int get_name(lua_State* L, lua_object_t* o) {
    auto s = static_cast<screen_t*>(o);
    Lua::State{L}.push(s->name.empty() ? s->name : fmt::format("screen{}", screen_get_index(s)));
    return 1;
}

/** Get the number of screens.
 *
 * @treturn number The screen count.
 * @staticfct count
 */
static int luaA_screen_count(lua_State* L) {
    lua_pushinteger(L, Manager::get().screens.size());
    return 1;
}

/** Add a fake screen.
 *
 * To vertically split the first screen in 2 equal parts, use:
 *
 *    local geo = screen[1].geometry
 *    local new_width = math.ceil(geo.width/2)
 *    local new_width2 = geo.width - new_width
 *    screen[1]:fake_resize(geo.x, geo.y, new_width, geo.height)
 *    screen.fake_add(geo.x + new_width, geo.y, new_width2, geo.height)
 *
 * Both virtual screens will have their own taglist and wibars.
 *
 * @tparam integer x X-coordinate for screen.
 * @tparam integer y Y-coordinate for screen.
 * @tparam integer width Width for screen.
 * @tparam integer height Height for screen.
 * @treturn screen The new screen.
 * @constructorfct fake_add
 */
static int luaA_screen_fake_add(lua_State* L) {
    int x = luaL_checkinteger(L, 1);
    int y = luaL_checkinteger(L, 2);
    int width = luaL_checkinteger(L, 3);
    int height = luaL_checkinteger(L, 4);

    /* If the screen is managed by internal Lua code */
    bool managed = false;

    /* Allow undocumented arguments for internal use only */
    if (lua_istable(L, 5)) {
        lua_getfield(L, 5, "_managed");
        managed = lua_isboolean(L, 6) && Lua::checkboolean(L, 6);

        lua_pop(L, 1);
    }

    screen_t* s;

    s = screen_add(L, &Manager::get().screens);
    s->lifecycle =
      (screen_lifecycle_t)(s->lifecycle | (managed ? SCREEN_LIFECYCLE_LUA : SCREEN_LIFECYCLE_USER));
    s->geometry.top_left = {x, y};
    s->geometry.width = width;
    s->geometry.height = height;
    s->xid = FAKE_SCREEN_XID;

    screen_added(L, s);
    screen_class.emit_signal(L, "list", 0);
    luaA_object_push(L, s);

    for (auto* c : Manager::get().clients) {
        screen_client_moveto(c, screen_getbycoord((c)->geometry.top_left), false);
    }

    return 1;
}

/** Remove a screen.
 *
 * @DOC_sequences_screen_fake_remove_EXAMPLE@
 *
 * @method fake_remove
 * @noreturn
 */
static int luaA_screen_fake_remove(lua_State* L) {
    auto s = screen_class.checkudata<screen_t>(L, 1);
    int idx = screen_get_index(s) - 1;
    if (idx < 0) {
        /* WTF? */
        return 0;
    }

    if (Manager::get().screens.size() == 1) {
        Lua::warn(L,
                  "Removing last screen through fake_remove(). "
                  "This is a very, very, very bad idea!");
    }

    Manager::get().screens.erase(Manager::get().screens.begin() + idx);
    luaA_object_push(L, s);
    screen_removed(L, -1);
    lua_pop(L, 1);
    screen_class.emit_signal(L, "list", 0);
    luaA_object_unref(L, s);
    s->valid = false;

    return 0;
}

/** Resize a screen.
 *
 * Calling this will resize the screen even if it no longer matches the viewport
 * size.
 *
 * @DOC_sequences_screen_fake_resize_EXAMPLE@
 *
 * @tparam integer x The new X-coordinate for screen.
 * @tparam integer y The new Y-coordinate for screen.
 * @tparam integer width The new width for screen.
 * @tparam integer height The new height for screen.
 * @method fake_resize
 * @noreturn
 * @see split
 * @see geometry
 */
static int luaA_screen_fake_resize(lua_State* L) {
    auto screen = screen_class.checkudata<screen_t>(L, 1);
    int x = luaL_checkinteger(L, 2);
    int y = luaL_checkinteger(L, 3);
    int width = luaL_checkinteger(L, 4);
    int height = luaL_checkinteger(L, 5);
    area_t old_geometry = screen->geometry;

    screen->geometry.top_left = {x, y};
    screen->geometry.width = width;
    screen->geometry.height = height;

    screen_update_workarea(screen);

    Lua::pusharea(L, old_geometry);
    luaA_object_emit_signal(L, 1, "property::geometry", 1);

    /* Note: calling `screen_client_moveto` from here will create more issues
     * than it would fix. Keep in mind that it means `c.screen` will be wrong
     * until Lua it `fake_add` fixes it.
     */

    return 0;
}

/** Swap a screen with another one in global screen list.
 *
 * @DOC_sequences_screen_swap_EXAMPLE@
 *
 * @tparam client s A screen to swap with.
 * @method swap
 * @noreturn
 */
static int luaA_screen_swap(lua_State* L) {
    auto s = screen_class.checkudata<screen_t>(L, 1);
    auto swap = screen_class.checkudata<screen_t>(L, 2);

    if (s != swap) {
        screen_t **ref_s = NULL, **ref_swap = NULL;
        for (auto*& item : Manager::get().screens) {
            if (item == s) {
                ref_s = &item;
            } else if (item == swap) {
                ref_swap = &item;
            }
            if (ref_s && ref_swap) {
                break;
            }
        }
        if (!ref_s || !ref_swap) {
            return luaL_error(L, "Invalid call to screen:swap()");
        }

        /* swap ! */
        *ref_s = swap;
        *ref_swap = s;

        screen_class.emit_signal(L, "list", 0);

        luaA_object_push(L, swap);
        lua_pushboolean(L, true);
        luaA_object_emit_signal(L, -4, "swapped", 2);

        luaA_object_push(L, swap);
        luaA_object_push(L, s);
        lua_pushboolean(L, false);
        luaA_object_emit_signal(L, -3, "swapped", 2);
    }

    return 0;
}

void screen_class_setup(lua_State* L) {
    static constexpr auto methods = DefineClassMethods<&screen_class>({
      {      "count",           luaA_screen_count},
      { "_viewports",              luaA_viewports},
      {"_scan_quiet",             luaA_scan_quiet},
      {    "__index",    luaA_screen_module_index},
      { "__newindex", luaA_screen_module_newindex},
      {     "__call",     luaA_screen_module_call},
      {   "fake_add",        luaA_screen_fake_add}
    });

    static constexpr auto meta = DefineObjectMethods({
      {"fake_remove", luaA_screen_fake_remove},
      {"fake_resize", luaA_screen_fake_resize},
      {       "swap",        luaA_screen_swap},
    });

    screen_class.setup(L, methods.data(), meta.data());

    screen_class.add_property("geometry", NULL, exportProp<&screen_t::geometry>(), NULL);
    screen_class.add_property("index", NULL, luaA_screen_get_index, NULL);
    screen_class.add_property("_outputs", NULL, luaA_screen_get_outputs, NULL);
    screen_class.add_property("_managed", NULL, luaA_screen_get_managed, NULL);
    screen_class.add_property("workarea", NULL, exportProp<&screen_t::workarea>(), NULL);
    screen_class.add_property("name", set_name, get_name, set_name);
}

/* @DOC_cobject_COMMON@ */

// vim: filetype=c:expandtab:shiftwidth=4:tabstop=8:softtabstop=4:textwidth=80
