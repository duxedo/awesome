/*
 * client.c - client management
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

/** A process window managed by AwesomeWM.
 *
 * Clients are the name used by Awesome (and X11) to refer to a window.
 *
 * A program can have multiple clients (e.g. for dialogs) or none at all (e.g.
 * command line applications).
 * Clients are usually grouped by classes.
 * A class is the name used by X11 to help the window manager distinguish
 * between windows and write rules for them.  A client's behavior is also
 * defined by its `type` and `size_hints` properties.
 * See the `xprop` command line application to query properties for a client.
 *
 * ![Client geometry](../images/client_geo.svg)
 *
 * The client's `:geometry()` function returns a table with *x*, *y*, *width*
 * and *height*.  The area returned **excludes the border width**.
 * All clients also have a `shape_bounding` and `shape_clip` used to "crop" the
 * client's content.
 * Finally, each clients can have titlebars (see `awful.titlebar`).
 *
 * Some signal names are starting with a dot. These dots are artefacts from
 * the documentation generation, you get the real signal name by
 * removing the starting dot.
 *
 * Accessing client objects can be done in multiple ways depending on the
 * context.
 * To get the currently focused client:
 *
 *    local c = client.focus
 *    if c then
 *        -- do something
 *    end
 *
 * To get a list of all clients, use `client:get`:
 *
 *    for _, c in ipairs(client.get()) do
 *        -- do something
 *    end
 *
 * To execute a callback when a new client is added, use the `manage` signal:
 *
 *    client.connect_signal("request::manage", function(c)
 *        -- do something
 *    end)
 *
 * To be notified when a property of a client changed:
 *
 *    client.connect_signal("property::name", function(c)
 *        -- do something
 *    end)
 *
 * To be notified when a property of a specific client `c` changed:
 *
 *    c:connect_signal("property::name", function()
 *        -- do something
 *    end)
 *
 * To get all the clients for a screen use either `screen.clients` or
 * `screen.tiled_clients`.
 *
 * @DOC_uml_nav_tables_client_EXAMPLE@
 *
 * @author Julien Danjou &lt;julien@danjou.info&gt;
 * @copyright 2008-2009 Julien Danjou
 * @coreclassmod client
 */

#include "objects/client.h"

#include "common/atoms.h"
#include "common/luaclass.h"
#include "common/luaobject.h"
#include "common/xutil.h"
#include "draw.h"
#include "event.h"
#include "ewmh.h"
#include "globalconf.h"
#include "lua.h"
#include "math.h"
#include "objects/drawable.h"
#include "objects/screen.h"
#include "objects/tag.h"
#include "property.h"
#include "spawn.h"
#include "systray.h"
#include "xwindow.h"

#include <algorithm>
#include <array>
#include <cairo-xcb.h>
#include <cstdint>
#include <ranges>
#include <string_view>
#include <xcb/shape.h>
#include <xcb/xcb_atom.h>
#include <xcb/xproto.h>

extern lua_class_t window_class;

static bool client_checker(client* c) { return c->window != XCB_NONE; }

lua_class_t client_class{
  "client",
  &window_class,
  {[](auto* state) -> lua_object_t* { return newobj<client, client_class>(state); },
                destroyObject<client>,
                [](auto* obj) { return client_checker(static_cast<client*>(obj)); },
                Lua::class_index_miss_property,
                Lua::class_newindex_miss_property},
};

/** Client class.
 *
 * This table allow to add more dynamic properties to the clients. For example,
 * doing:
 *
 *     function awful.client.object.set_my_cool_property(c, value)
 *         -- Some logic code
 *         c._my_secret_my_cool_property = value
 *         c:emit_signal("property::my_cool_property)
 *     end
 *
 *     function awful.client.object.get_my_cool_property()
 *         return c._my_secret_my_cool_property
 *     end
 *
 * Will add a new "my_cool_property" dyanmic property to all client. These
 * methods will be called when an user does `c.my_cool_property = "something"`
 * or set them in `awdul.rules`.
 *
 * Note that doing this isn't required to set random properties to the client,
 * it is only useful when setting or getting these properties require code to
 * executed.
 *
 * @table awful.client.object
 */

/** Emitted when AwesomeWM is about to scan for existing clients.
 *
 * Connect to this signal when code needs to be executed after screens are
 * initialized, but before clients are added.
 *
 * @signal scanning
 * @classsignal
 */

/** Emitted when AwesomeWM is done scanning for clients.
 *
 * This is emitted before the `startup` signal and after the `scanning` signal.
 *
 * @signal scanned
 * @classsignal
 */

/** Emitted when a client gains focus.
 * @signal focus
 * @classsignal
 */

/** Emitted before `request::manage`, after `request::unmanage`,
 * and when clients swap.
 * @signal list
 * @classsignal
 */

/** Emitted when 2 clients are swapped
 * @tparam client client The other client
 * @tparam boolean is_source If self is the source or the destination of the swap
 * @signal swapped
 */

/** Emitted when a new client appears and gets managed by Awesome.
 *
 * This request should be implemented by code which track the client. It isn't
 * recommended to use this to initialize the client content. This use case is
 * a better fit for `ruled.client`, which has built-in dependency management.
 * Using this request to mutate the client state will likely conflict with
 * `ruled.client`.
 *
 * @signal request::manage
 * @tparam client c The client.
 * @tparam string context What created the client. It is currently either "new"
 *  or "startup".
 * @tparam table hints More metadata (currently empty, it exists for compliance
 *  with the other `request::` signals).
 * @request client border added granted When a new client needs a its initial
 *  border settings.
 * @classsignal
 */

/** Emitted when a client is going away.
 *
 * Each places which store `client` objects in non-weak table or whose state
 * depend on the current client should answer this request.
 *
 * The contexts are:
 *
 * * **user**: `c:unmanage()` was called.
 * * **reparented**: The window was reparented to another window. It is no
 *   longer a stand alone client.
 * * **destroyed**: The window was closed.
 *
 * @signal request::unmanage
 * @tparam client c The client.
 * @tparam string context Why was the client unmanaged.
 * @tparam table hints More metadata (currently empty, it exists for compliance
 *  with the other `request::` signals).
 * @classsignal
 */

/** Use `request::manage`.
 * @deprecatedsignal manage
 */

/** Use `request::unmanage`.
 * @deprecatedsignal unmanage
 */

/** Emitted when a mouse button is pressed in a client.
 * @signal button::press
 */

/** Emitted when a mouse button is released in a client.
 *
 * @signal button::release
 */

/** Emitted when the mouse enters a client.
 *
 * @signal mouse::enter
 */

/** Emitted when the mouse leaves a client.
 *
 * @signal mouse::leave
 */

/**
 * Emitted when the mouse moves within a client.
 *
 * @signal mouse::move
 */

/** Emitted when a client should get activated (focused and/or raised).
 *
 * **Contexts are:**
 *
 * * *ewmh*: When a client asks for focus (from `X11` events).
 * * *autofocus.check_focus*: When autofocus is enabled (from
 *   `awful.autofocus`).
 * * *autofocus.check_focus_tag*: When autofocus is enabled
 *   (from `awful.autofocus`).
 * * *client.jumpto*: When a custom lua extension asks a client to be focused
 *   (from `client.jump_to`).
 * * *client.swap.global_bydirection*: When client swapping requires a focus
 *   change (from `awful.client.swap.bydirection`).
 * * *client.movetotag*: When a client is moved to a new tag
 *   (from `client.move_to_tag`).
 * * *client.movetoscreen*: When the client is moved to a new screen
 *   (from `client.move_to_screen`).
 * * *client.focus.byidx*: When selecting a client using its index
 *   (from `awful.client.focus.byidx`).
 * * *client.focus.history.previous*: When cycling through history
 *   (from `awful.client.focus.history.previous`).
 * * *menu.clients*: When using the builtin client menu
 *   (from `awful.menu.clients`).
 * * *rules*: When a new client is focused from a rule (from `ruled.client`).
 * * *screen.focus*: When a screen is focused (from `awful.screen.focus`).
 *
 * Default implementation: `awful.ewmh.activate`.
 *
 * To implement focus stealing filters see `awful.ewmh.add_activate_filter`.
 *
 * @signal request::activate
 * @tparam client c The client.
 * @tparam string context The context where this signal was used.
 * @tparam[opt] table hints A table with additional hints:
 * @tparam[opt=false] boolean hints.raise Should the client be raised?
 * @request client activate ewmh granted When the client asks to be activated.
 * @classsignal
 */

/** Emitted when an event could lead to the client being activated.
 *
 * This is an layer "on top" of `request::activate` for event which are not
 * actual request for activation/focus, but where "it would be nice" if the
 * client got the focus. This includes the focus-follow-mouse model and focusing
 * previous clients when the selected tag changes.
 *
 * This idea is that `request::autoactivate` will emit `request::activate`.
 * However it is much easier to replace the handler for `request::autoactivate`
 * than it is to replace the handler for `request::activate`. Thus it provides
 * a nice abstraction to simplify handling the focus when switching tags or
 * moving the mouse.
 *
 * @signal request::autoactivate
 * @tparam client c The client.
 * @tparam string context The context where this signal was used.
 * @tparam[opt] table hints A table with additional hints:
 * @tparam[opt=false] boolean hints.raise Should the client be raised?
 * @classsignal
 *
 */

/** Emitted when something request a client's geometry to be modified.
 *
 * @signal request::geometry
 * @tparam client c The client
 * @tparam string context Why and what to resize. This is used for the
 *   handlers to know if they are capable of applying the new geometry.
 * @tparam[opt={}] table hints Additional arguments. Each context handler may
 *   interpret this differently.
 * @request client geometry client_maximize_horizontal granted When a client
 *  (programmatically) asks for the maximization to be changed.
 * @request client geometry client_maximize_vertical granted When a client
 *  (programmatically) asks for the maximization to be changed.
 * @classsignal
 */

/** Emitted when a client requests to be moved to a tag or needs a new tag.
 *
 * @signal request::tag
 * @tparam client c The client requesting a new tag.
 * @tparam[opt] tag tag A preferred tag.
 * @tparam[opt] table hints
 * @tparam[opt] string hints.reason
 * @tparam[opt] screen hints.screen
 * @classsignal
 */

/** Emitted when any client's `urgent` property changes.
 *
 * Emitted both when `urgent = true` and `urgent = false`, so you will likely
 * want to check `c.urgent` within the signal callback.
 *
 *    client.connect_signal("property::urgent", function(c)
 *        if c.urgent then
 *            naughty.notify {
 *                title = "Urgent client",
 *                message = c.name,
 *            }
 *        end
 *    end)
 *
 * @signal request::urgent
 * @tparam client c The client whose property changed.
 * @classsignal
 */

/** Emitted once to request default client mousebindings during the initial
 * startup sequence.
 *
 * This signal gives all modules a chance to register their default client
 * mousebindings.
 * They will then be added to all new clients, unless rules overwrite them via
 * the `buttons` property.
 *
 * @signal request::default_mousebindings
 * @tparam string context The reason why the signal was sent (currently always
 *  `startup`).
 * @classsignal
 */

/** Emitted once to request default client keybindings during the initial
 * startup sequence.
 *
 * This signal gives all modules a chance to register their default client
 * keybindings.
 * They will then be added to all new clients, unless rules overwrite them via
 * the `keys` property.
 *
 * @signal request::default_keybindings
 * @tparam string context The reason why the signal was sent (currently always
 * @classsignal
 * @request client default_keybindings startup granted Sent when AwesomeWM starts.
 */

/** Emitted when a client gets tagged.
 * @signal tagged
 * @tparam tag t The tag object.
 * @see tags
 * @see untagged
 */

/** Emitted when a client gets unfocused.
 * @signal unfocus
 */

/** Emitted when a client gets untagged.
 * @signal untagged
 * @tparam tag t The tag object.
 * @see tags
 * @see tagged
 */

/**
 * Emitted when the client is raised within its layer.
 *
 * @signal raised
 * @see below
 * @see above
 * @see ontop
 * @see raise
 * @see lower
 * @see lowered
 */

/** Emitted when the client is lowered within its layer.
 *
 * @signal lowered
 * @see below
 * @see above
 * @see ontop
 * @see raise
 * @see lower
 * @see raised
 */

/**
 * The focused `client` or nil (in case there is none).
 *
 * It is not recommended to set the focused client using
 * this property. Please use @{client.activate} instead of
 * `client.focus = c`. Setting the focus directly bypasses
 * all the filters and emits fewer signals, which tend to
 * cause unwanted side effects and make it harder to alter
 * the code behavior in the future. It usually takes *more*
 * code to use this rather than @{client.activate} because all
 * the boilerplate code (such as `c:raise()`) needs to be
 * added everywhere.
 *
 * The main use case for this field is to check *when* there
 * is an active client.
 *
 *     if client.focus ~= nil then
 *         -- do something
 *     end
 *
 * If you want to check if a client is active, use:
 *
 *     if c.active then
 *         -- do something
 *     end
 *
 * @tfield client focus
 * @see active
 * @see activate
 * @see request::activate
 */

/**
 * The X window id.
 *
 * This is rarely useful, but some DBus protocols will
 * have this ID in their API, so it can be useful when
 * writing AwesomeWM bindings for them.
 *
 * @property window
 * @tparam integer window
 * @propertydefault This is generated by X11.
 * @negativeallowed false
 * @propemits false false
 * @readonly
 */

/**
 * The client title.
 *
 * This is the text which will be shown in `awful.widget.tasklist`
 * and `awful.titlebar.widget.titlewidget`.
 *
 * @property name
 * @tparam string name
 * @propertydefault This is provided by the application.
 * @propemits false false
 * @see awful.titlebar
 * @see awful.widget.tasklist
 */

/**
 * True if the client does not want to be in taskbar.
 *
 * Some clients, like docked bars or some `sticky` clients
 * such as wallpaper sensors like Conky have no value in
 * the `awful.widget.tasklist` and should not be shown there.
 *
 * The default value of this property reflects the value of the
 * `_NET_WM_STATE_SKIP_TASKBAR` X11 protocol xproperty. Clients can modify this
 * state through this property.
 *
 * @DOC_awful_client_skip_tasklist1_EXAMPLE@
 *
 * @property skip_taskbar
 * @tparam[opt=false] boolean skip_taskbar
 * @propemits false false
 * @see sticky
 * @see hidden
 * @see unmanage
 */

/**
 * The window type.
 *
 * This is useful in, among other places, the `ruled.client` rules to apply
 * different properties depending on the client types. It is also used
 * throughout the API to alter the client (and `wibox`) behavior depending on
 * the `type`. For example, clients with the `dock` type are placed on the side
 * of the screen while other like `combo` are totally ignored and never
 * considered `client`s in the first place.
 *
 * Valid types are:
 *
 * <table class='widget_list' border=1>
 * <tr style='font-weight: bold;'>
 *  <th align='center'>Name</th>
 *  <th align='center'>Description</th>
 * </tr>
 * <tr><td><b>desktop</b></td><td>The root client, it cannot be moved or resized.</td></tr>
 * <tr><td><b>dock</b></td><td>A client attached to the side of the screen.</td></tr>
 * <tr><td><b>splash</b></td><td>A client, usually without titlebar shown when an application
 * starts.</td></tr> <tr><td><b>dialog</b></td><td>A dialog, see `transient_for`.</td></tr>
 * <tr><td><b>menu</b></td><td>A context menu.</td></tr>
 * <tr><td><b>toolbar</b></td><td>A floating toolbar.</td></tr>
 * <tr><td><b>utility</b></td><td></td></tr>
 * <tr><td><b>dropdown_menu</b></td><td>A context menu attached to a parent position.</td></tr>
 * <tr><td><b>popup_menu</b></td><td>A context menu.</td></tr>
 * <tr><td><b>notification</b></td><td>A notification popup.</td></tr>
 * <tr><td><b>combo</b></td><td>A combobox list menu.</td></tr>
 * <tr><td><b>dnd</b></td><td>A drag and drop indicator.</td></tr>
 * <tr><td><b>normal</b></td><td>A normal application main window.</td></tr>
 * </table>
 *
 * More information can be found
 * [here](https://specifications.freedesktop.org/wm-spec/wm-spec-latest.html#idm140200472629520)
 *
 * @property type
 * @tparam string type
 * @propemits false false
 * @propertydefault This is provided by the application.
 * @readonly
 * @see ruled.client
 */

/**
 * The client class.
 *
 * A class usually maps to the application name. It is useful in, among other
 * places, the rules to apply different properties to different clients. It
 * is also useful, along with `instance`, to implement "windows counter"
 * used in many popular docks and Alt-Tab like popups.
 *
 * To get a client class from the command line, use the command:
 *
 *    xprop WM_CLASS
 *
 * The class will be the second string.
 *
 * This *should* never change after the client is created, but some
 * buggy application like the Spotify desktop client are known to
 * violate the specification and do it anyway. There *is* a signal for
 * this property, but it should hopefully never be useful. If your
 * applications change their classes, please report a bug to them
 * and point to ICCCM §4.1.2.5.
 * It tends to break `ruled.client` and other AwesomeWM APIs.
 *
 * @property class
 * @tparam string class
 * @propemits false false
 * @propertydefault This is provided by the application.
 * @readonly
 * @see instance
 * @see ruled.client
 */

/**
 * The client instance.
 *
 * The `instance` is a subtype of the `class`. Each `class` can have
 * multiple instances. This is useful in the `ruled.client` rules to
 * filter clients and apply different properties to them.
 *
 * To get a client instance from the command line, use the command:
 *
 *     xprop WM_CLASS
 *
 * The instance will be the first string.
 *
 * This *should* never change after the client is created. There
 * *is* a signal for * this property, but it should hopefully never
 * be useful. If your applications change their classes, please
 * report a bug to them and point to ICCCM §4.1.2.5.
 * It tends to break `ruled.client` and other AwesomeWM APIs.
 *
 * @property instance
 * @tparam string instance
 * @propertydefault This is provided by the application.
 * @propemits false false
 * @readonly
 * @see class
 * @see ruled.client
 */

/**
 * The client PID, if available.
 *
 * This will never change.
 *
 * @property pid
 * @tparam integer pid
 * @negativeallowed false
 * @propertydefault This is randomly assigned by the kernel.
 * @propemits false false
 * @readonly
 */

/**
 * The window role, if available.
 *
 * @property role
 * @tparam string role
 * @propertydefault This is provided by the application.
 * @propemits false false
 * @readonly
 * @see instance
 * @see class
 */

/**
 * The machine the client is running on.
 *
 * X11 windows can "live" in one computer but be shown
 * in another one. This is called "network transparency"
 * and is either used directly by allowing remote windows
 * using the `xhosts` command or using proxies such as
 * `ssh -X` or `ssh -Y`.
 *
 * According to EWMH, this property contains the value
 * returned by `gethostname()` on the computer that the
 * client is running on.
 *
 * @property machine
 * @tparam string machine
 * @propertydefault This is the hostname unless the client is from an
 *  SSH session or using the rarely used direct X11 network socket.
 * @propemits false false
 * @readonly
 */

/**
 * The client name when iconified.
 *
 * @property icon_name
 * @tparam string icon_name
 * @propertydefault This is provided by the application.
 * @propemits false false
 * @readonly
 */

/**
 * The client icon as a surface.
 *
 * This property holds the client icon closest to the size configured via
 * @{awesome.set_preferred_icon_size}.
 *
 * It is not a path or a "real" file. Rather, it is already a bitmap surface.
 *
 * Typically you would want to use @{awful.widget.clienticon} to get this as a
 * widget.
 *
 * Working with icons is tricky because their surfaces do not use reference
 * counting correctly. If `gears.surface(c.icon)` is called multiple time on
 * the same icon, it will cause a double-free error and Awesome will crash. To
 * get a copy of the icon, you can use:
 *
 *    local s = gears.surface(c.icon)
 *    local img = cairo.ImageSurface.create(cairo.Format.ARGB32, s:get_width(), s:get_height())
 *    local cr  = cairo.Context(img)
 *    cr:set_source_surface(s, 0, 0)
 *    cr:paint()
 *
 * (Note that `awesome.set_preferred_icon_size` defaults to `0` if it wasn't
 * set. It means that, by default, the preferred icon provided will be the
 * smallest available)
 *
 * @property icon
 * @tparam image icon
 * @propertydefault This is provided by the application.
 * @propemits false false
 * @usage local ib = wibox.widget.imagebox(c.icon)
 * @see awful.widget.clienticon
 */

/**
 * The available sizes of client icons. This is a table where each entry
 * contains the width and height of an icon.
 *
 * Example:
 *
 *    {
 *      { 24, 24 },
 *      { 32, 32 },
 *      { 64, 64 },
 *    }
 *
 * @property icon_sizes
 * @tparam table icon_sizes
 * @tablerowtype A list of tables. Each table has the following rows:
 * @tablerowkey integer 1 The width value.
 * @tablerowkey integer 2 The height value.
 * @propertydefault This is provided by the application.
 * @propemits false false
 * @readonly
 * @see awful.widget.clienticon
 * @see get_icon
 */

/**
 * Client screen.
 *
 * The `screen` corresponds to the top-left corner of the window.
 *
 * Please note that clients can only be on one screen at once. X11
 * does not natively allow clients to be in multiple locations at
 * once. Changing the screen directly will affect the tags and may
 * cause several other changes to the state in order to ensure that
 * a client's position and its screen are consistent.
 *
 * @DOC_sequences_client_screen_EXAMPLE@
 *
 * @property screen
 * @tparam screen screen
 * @propertydefault This usually correspond to where the top-left (or other
 *  gravities) is placed. Then it is mapped to the screen `geometry`.
 * @propemits false false
 * @see move_to_screen
 */

/**
 * Define if the client must be hidden (Never mapped, invisible in taskbar).
 *
 * @property hidden
 * @tparam[opt=false] boolean hidden
 * @propemits false false
 * @see minimized
 * @see skip_taskbar
 * @see unmanage
 */

/**
 * Define if the client must be iconified (Only visible in taskbar).
 *
 * Minimized clients are still part of tags and screens, but
 * they are not displayed. You can unminimize using `c.minimized = false`,
 * but if you also want to set the focus, it is better to use:
 *
 *    c:activate { context = "unminimized", raise = true }
 *
 * @DOC_sequences_client_minimize1_EXAMPLE@
 *
 * @property minimized
 * @tparam[opt=false] boolean minimized
 * @propemits false false
 * @see hidden
 * @see isvisible
 * @see activate
 */

/**
 * Honor size hints, e.g. respect size ratio.
 *
 * For example, a terminal such as `xterm` require the client size to be a
 * multiple of the character size. Honoring size hints will cause the terminal
 * window to have a small gap at the bottom.
 *
 * This is enabled by default. To disable it by default, see `ruled.client`.
 *
 * @property size_hints_honor
 * @tparam[opt=true] boolean size_hints_honor
 * @propemits false false
 * @see size_hints
 */

/**
 * The client border width.
 *
 * When manually set (for example, in `ruled.client` rules), this value
 * will be static. Otherwise, it is controlled by many `beautiful` variables.
 *
 * Be careful, the borders are **around** the geometry, not part of it. If
 * you want more fancy border, use the `awful.titlebar` API to create
 * titlebars on each side of the client.
 *
 * @DOC_awful_client_border_width_EXAMPLE@
 *
 * @property border_width
 * @tparam[opt=nil] integer|nil border_width
 * @propertytype nil Let AwesomeWM manage it based on the client state.
 * @negativeallowed false
 * @propertyunit pixel
 * @propemits false false
 * @usebeautiful beautiful.border_width_active
 * @usebeautiful beautiful.border_width_normal
 * @usebeautiful beautiful.border_width_new
 * @usebeautiful beautiful.border_width_urgent
 * @usebeautiful beautiful.border_width_floating
 * @usebeautiful beautiful.border_width_floating_active
 * @usebeautiful beautiful.border_width_floating_normal
 * @usebeautiful beautiful.border_width_floating_new
 * @usebeautiful beautiful.border_width_floating_urgent
 * @usebeautiful beautiful.border_width_maximized
 * @usebeautiful beautiful.border_width_maximized_active
 * @usebeautiful beautiful.border_width_maximized_normal
 * @usebeautiful beautiful.border_width_maximized_new
 * @usebeautiful beautiful.border_width_maximized_urgent
 * @usebeautiful beautiful.border_width_fullscreen
 * @usebeautiful beautiful.border_width_fullscreen_active
 * @usebeautiful beautiful.border_width_fullscreen_normal
 * @usebeautiful beautiful.border_width_fullscreen_new
 * @usebeautiful beautiful.border_width_fullscreen_urgent
 * @usebeautiful beautiful.fullscreen_hide_border Hide the border on fullscreen clients.
 * @usebeautiful beautiful.maximized_hide_border Hide the border on maximized clients.
 * @see request::border
 * @see awful.permissions.update_border
 * @see border_color
 */

/**
 * The client border color.
 *
 * @DOC_awful_client_border_color_EXAMPLE@
 *
 * Note that setting this directly will override and disable all related theme
 * variables.
 *
 * Setting a transparent color (e.g. to implement dynamic borders without size
 * changes) is supported, but requires the color to be set to `#00000000`
 * specifically. Other RGB colors with an alpha of `0` won't work.
 *
 * @property border_color
 * @tparam[opt=nil] color|nil border_color
 * @propertytype nil Let AwesomeWM manage it based on the client state.
 * @propertydefault
 * @propemits false false
 * @usebeautiful beautiful.border_color_marked The fallback color when the
 *  client is marked.
 * @usebeautiful beautiful.border_color_active The fallback color when the
 *  client is active (focused).
 * @usebeautiful beautiful.border_color_normal The fallback color when the
 *  client isn't active/floating/new/urgent/maximized/floating/fullscreen.
 * @usebeautiful beautiful.border_color_new The fallback color when the
 *  client is new.
 * @usebeautiful beautiful.border_color_urgent The fallback color when the
 *  client is urgent.
 * @usebeautiful beautiful.border_color_floating The fallback color when the
 *  client is floating and the other colors are not set.
 * @usebeautiful beautiful.border_color_floating_active The color when the
 *  client is floating and is active (focused).
 * @usebeautiful beautiful.border_color_floating_normal The color when the
 *  client is floating and not new/urgent/active.
 * @usebeautiful beautiful.border_color_floating_new
 * @usebeautiful beautiful.border_color_floating_urgent The color when the
 *  client is floating and urgent.
 * @usebeautiful beautiful.border_color_maximized
 * @usebeautiful beautiful.border_color_maximized_active
 * @usebeautiful beautiful.border_color_maximized_normal
 * @usebeautiful beautiful.border_color_maximized_new
 * @usebeautiful beautiful.border_color_maximized_urgent The color when the
 *  client is urbent and maximized.
 * @usebeautiful beautiful.border_color_fullscreen
 * @usebeautiful beautiful.border_color_fullscreen_active
 * @usebeautiful beautiful.border_color_fullscreen_normal
 * @usebeautiful beautiful.border_color_fullscreen_new
 * @usebeautiful beautiful.border_color_fullscreen_urgent The color when the
 *  client is fullscreen and urgent.
 * @see request::border
 * @see awful.permissions.update_border
 * @see gears.color
 * @see border_width
 */

/**
 * Set to `true` when the client ask for attention.
 *
 * The urgent state is the visual equivalent of the "bell" noise from
 * old computer. It is set by the client when their state changed and
 * they need attention. For example, a chat client will set it when
 * a new message arrive. Some terminals, like `rxvt-unicode`, will also
 * set it when calling the `bell` command.
 *
 * There is many ways an urgent client can become for visible:
 *
 *  * Highlight in the `awful.widget.taglist` and `awful.widget.tasklist`
 *  * Highlight in the `awful.titlebar`
 *  * Highlight of the client border color (or width).
 *  * Accessible using `Mod4+u` in the default config.
 *  * Emit the `property::urgent` signal.
 *
 * @DOC_awful_client_urgent1_EXAMPLE@
 *
 * @property urgent
 * @tparam[opt=false] boolean urgent
 * @propemits false false
 * @request client border active granted When a client becomes active and is no
 *  longer urgent.
 * @request client border inactive granted When a client stop being active and
 *  is no longer urgent.
 * @request client border urgent granted When a client stop becomes urgent.
 * @see request::border
 * @see awful.client.urgent.jumpto
 * @usebeautiful beautiful.border_color_urgent The fallback color when the
 *  client is urgent.
 * @usebeautiful beautiful.border_color_floating_urgent The color when the
 *  client is floating and urgent.
 * @usebeautiful beautiful.border_color_maximized_urgent The color when the
 *  client is urbent and maximized.
 * @usebeautiful beautiful.border_color_fullscreen_urgent The color when the
 *  client is fullscreen and urgent.
 * @usebeautiful beautiful.border_width_urgent The fallback border width when
 *  the client is urgent.
 * @usebeautiful beautiful.border_width_floating_urgent The border width when
 *  the client is floating and urgent.
 * @usebeautiful beautiful.border_width_maximized_urgent The border width when
 *  the client is maximized and urgent.
 * @usebeautiful beautiful.border_width_fullscreen_urgent The border width when
 *  the client is fullscreen and urgent.
 * @usebeautiful beautiful.titlebar_fg_urgent
 * @usebeautiful beautiful.titlebar_bg_urgent
 * @usebeautiful beautiful.titlebar_bgimage_urgent
 * @usebeautiful beautiful.fg_urgent
 * @usebeautiful beautiful.bg_urgent
 */

/**
 * A cairo surface for the client window content.
 *
 * To get the screenshot, use:
 *
 *    gears.surface(c.content)
 *
 * To save it, use:
 *
 *    gears.surface(c.content):write_to_png(path)
 *
 * Please note that this only creates a new cairo surface
 * referring to the client's content. This means that
 * changes to the client's content may or may not become
 * visible in the returned surface. If you want to take a
 * screenshot, a copy of the surface's content needs to
 * be taken. Note that the content of parts of a window
 * that are currently not visible are undefined.
 *
 * The only way to get an animated client screenshot widget is to poll this
 * property multiple time per seconds. This is obviously a bad idea.
 *
 * This property has no signals when the content changes.
 *
 * @property content
 * @tparam raw_curface content
 * @propertydefault This is a live surface. Always use `gears.surface` to take
 *  a snapshot.
 * @readonly
 * @see gears.surface
 */

/**
 * The client opacity.
 *
 * The opacity only works when a compositing manager, such as
 * [picom](https://github.com/yshui/picom/), is used. Otherwise,
 * the clients will remain opaque.
 *
 * @DOC_awful_client_opacity1_EXAMPLE@
 *
 * @property opacity
 * @tparam[opt=1.0] number opacity
 * @rangestart 0.0 Transparent.
 * @rangestop 1.0 Opaque.
 * @propemits false false
 * @see request::border
 * @see awesome.composite_manager_running
 */

/**
 * The client is on top of every other windows.
 *
 * @property ontop
 * @tparam[opt=false] boolean ontop
 * @propemits false false
 * @see below
 * @see above
 */

/**
 * The client is above normal windows.
 *
 * @property above
 * @tparam[opt=false] boolean above
 * @propemits false false
 * @see below
 * @see ontop
 */

/**
 * The client is below normal windows.
 *
 * @property below
 * @tparam[opt=false] boolean below
 * @propemits false false
 * @see above
 * @see ontop
 */

/**
 * The client is fullscreen or not.
 *
 * @DOC_sequences_client_fullscreen_EXAMPLE@
 *
 * @property fullscreen
 * @tparam[opt=false] boolean fullscreen
 * @propemits false false
 * @request client geometry fullscreen granted When the client must be resized
 *  because it became (or stop being) fullscreen.
 * @see maximized_horizontal
 * @see maximized_vertical
 * @see immobilized_horizontal
 * @see immobilized_vertical
 * @see maximized

 */

/**
 * The client is maximized (horizontally and vertically) or not.
 *
 * @DOC_sequences_client_maximized_EXAMPLE@
 *
 * @property maximized
 * @tparam[opt=false] boolean maximized
 * @propemits false false
 * @request client geometry maximized granted When the client must be resized
 *  because it became (or stop being) maximized.
 * @see request::border
 * @see maximized_horizontal
 * @see maximized_vertical
 * @see fullscreen
 * @see immobilized_horizontal
 * @see immobilized_vertical
 */

/**
 * The client is maximized horizontally or not.
 *
 * @DOC_sequences_client_maximized_horizontal_EXAMPLE@
 *
 * @property maximized_horizontal
 * @tparam[opt=false] boolean maximized_horizontal
 * @propemits false false
 * @request client geometry maximized_horizontal granted When the client must be resized
 *  because it became (or stop being) maximized horizontally.
 * @see maximized_vertical
 * @see fullscreen
 * @see immobilized_horizontal
 * @see immobilized_vertical
 * @see maximized
 */

/**
 * The client is maximized vertically or not.
 *
 * @DOC_sequences_client_maximized_vertical_EXAMPLE@
 *
 * @property maximized_vertical
 * @tparam[opt=false] boolean maximized_vertical
 * @propemits false false
 * @request client geometry maximized_vertical granted When the client must be resized
 *  because it became (or stop being) maximized vertically.
 * @see maximized_horizontal
 * @see fullscreen
 * @see immobilized_horizontal
 * @see immobilized_vertical
 * @see maximized
 */

/**
 * The client the window is transient for.
 *
 * A transient window is a client that "belongs" to another
 * client. If the client is also `modal`, then  the parent client
 * cannot be focused while the child client exists.
 * This is common for "Save as" dialogs or other dialogs where it
 * is not possible to modify the content of the "parent" client
 * while the dialog is open.
 *
 * However, `modal` is not a requirement for using the `transient_for`
 * concept. "Tools" such as popup palette in canvas-and-palettes
 * applications can belong to each other without being modal.
 *
 * @property transient_for
 * @tparam[opt=nil] client|nil transient_for
 * @propemits false false
 * @readonly
 * @see modal
 * @see type
 * @see is_transient_for
 * @see get_transient_for_matching
 */

/**
 * Window identification unique to a group of windows.
 *
 * This is the ID of the group window, not a client object.
 * The group window is most likely not a visible client, but
 * only an invisible and internal window.
 *
 * @property group_window
 * @tparam integer group_window
 * @propertydefault This is auto-generated by X11.
 * @negativeallowed false
 * @propemits false false
 * @readonly
 * @see leader_window
 */

/**
 * Identification unique to windows spawned by the same command.
 *
 * This is the ID of the group window, not a client object.
 *
 * @property leader_window
 * @tparam integer leader_window
 * @propertydefault This is auto-generated by X11.
 * @negativeallowed false
 * @propemits false false
 * @readonly
 * @see transient_for
 * @see modal
 * @see group_window
 */

/**
 * A table with size hints of the client.
 *
 * For details on the meaning of the fields, refer to ICCCM § 4.1.2.3
 * `WM_NORMAL_HINTS`.
 *
 * Please note that most fields are optional and may or may not be set.
 *
 * When the client is tiled, the `size_hints` usually get in the way and
 * cause the layouts to behave incorrectly. To mitigate this, it is often
 * advised to set `size_hints_honor` to `false` in the `ruled.client` rules.
 *
 * @property size_hints
 * @tparam[opt=nil] table|nil size_hints
 * @tparam[opt] table|nil size_hints.user_position A table with `x` and `y` keys. It
 *  contains the preferred position of the client. This is set when the
 *  position has been modified by the user. See `program_position`.
 * @tparam[opt] table|nil size_hints.program_position A table with `x` and `y` keys. It
 *  contains the preferred position of the client. This is set when the
 *  application itself requests a specific position. See `user_position`.
 * @tparam[opt] table|nil size_hints.user_size A table with `width` and `height`. This
 *  contains the client preferred size when it has previously been set by
 *  the user. See `program_size` for the equivalent when the applications
 *  itself wants to specify its preferred size.
 * @tparam[opt] table|nil size_hints.program_size A table with `width` and `height`. This
 *  contains the client preferred size as specified by the application.
 * @tparam[opt] integer|nil size_hints.max_width The maximum width (in pixels).
 * @tparam[opt] integer|nil size_hints.max_height The maximum height (in pixels).
 * @tparam[opt] integer|nil size_hints.min_width The minimum width (in pixels).
 * @tparam[opt] integer|nil size_hints.min_height The minimum height (in pixels).
 * @tparam[opt] integer|nil size_hints.width_inc The number of pixels by which the
 *  client width may be increased or decreased. For example, for terminals,
 *  the size has to be proportional with the monospace font size.
 * @tparam[opt] integer|nil size_hints.height_inc The number of pixels by which the
 *  client height may be increased or decreased. For example, for terminals,
 *  the size has to be proportional with the monospace font size.
 * @tparam[opt] string|nil size_hints.win_gravity The client `gravity` defines the corder
 *   from which the size is computed. For most clients, it is `north_west`, which
 *   corresponds to the top-left of the window. This will affect how the client
 *   is resized and other size related operations.
 * @tparam[opt] integer|nil size_hints.min_aspect_num
 * @tparam[opt] integer|nil size_hints.min_aspect_den
 * @tparam[opt] integer|nil size_hints.max_aspect_num
 * @tparam[opt] integer|nil size_hints.max_aspect_den
 * @tparam[opt] integer|nil size_hints.base_width
 * @tparam[opt] integer|nil size_hints.base_height
 * @propemits false false
 * @readonly
 * @see size_hints_honor
 * @see geometry
 */

/**
 * The motif WM hints of the client.
 *
 * This is nil if the client has no motif hints. Otherwise, this is a table that
 * contains the present properties. Note that awesome provides these properties
 * as-is and does not interpret them for you. For example, if the function table
 * only has "resize" set to true, this means that the window requests to be only
 * resizable, but asks for the other functions not to be able. If however both
 * "resize" and "all" are set, this means that all but the resize function
 * should be enabled.
 *
 * @property motif_wm_hints
 * @tparam[opt={}] table motif_wm_hints
 * @tparam[opt] boolean motif_wm_hints.functions.all
 * @tparam[opt] boolean motif_wm_hints.functions.resize
 * @tparam[opt] boolean motif_wm_hints.functions.move
 * @tparam[opt] boolean motif_wm_hints.functions.minimize
 * @tparam[opt] boolean motif_wm_hints.functions.maximize
 * @tparam[opt] boolean motif_wm_hints.functions.close
 * @tparam[opt] boolean motif_wm_hints.decorations.all
 * @tparam[opt] boolean motif_wm_hints.decorations.border
 * @tparam[opt] boolean motif_wm_hints.decorations.resizeh
 * @tparam[opt] boolean motif_wm_hints.decorations.title
 * @tparam[opt] boolean motif_wm_hints.decorations.menu
 * @tparam[opt] boolean motif_wm_hints.decorations.minimize
 * @tparam[opt] boolean motif_wm_hints.decorations.maximize
 * @tparam[opt] string motif_wm_hints.input_mode This is either `modeless`,
 *  `primary_application_modal`, `system_modal`,
 *  `full_application_modal` or `unknown`.
 * @tparam[opt] boolean motif_wm_hints.status.tearoff_window
 * @propemits false false
 * @readonly
 */

/**
 * Set the client sticky (Available on all tags).
 *
 * Please note that AwesomeWM implements `sticky` clients
 * per screens rather than globally like some other
 * implementations.
 *
 * @DOC_sequences_client_sticky_EXAMPLE@
 *
 * @property sticky
 * @tparam[opt=false] boolean sticky
 * @propemits false false
 * @see skip_taskbar
 */

/**
 * Indicate if the client is modal.
 *
 * A transient window is a client that "belongs" to another
 * client. If the client is also `modal`, then it always has
 * to be on top of the other window *and* the parent client
 * cannot be focused while the child client exists.
 * This is common for "Save as" dialogs or other dialogs where
 * is not possible to modify the content of the "parent" client
 * while the dialog is open.
 *
 * However, `modal` is not a requirement for using the `transient_for`
 * concept. "Tools" such as popup palette in canvas-and-palettes
 * applications can belong to each other without being modal.
 *
 * @property modal
 * @tparam boolean modal
 * @propertydefault This is provided by the application.
 * @propemits false false
 * @see transient_for
 */

/**
 * True if the client can receive the input focus.
 *
 * The client will not get focused even when the user
 * click on it.
 *
 * @property focusable
 * @tparam[opt=true] boolean focusable
 * @propemits false false
 * @see shape_input
 * @see client.focus
 * @see active
 * @see activate
 */

/**
 * The client's bounding shape as set by awesome as a (native) cairo surface.
 *
 * The bounding shape is the outer shape of the client. It is outside of the
 * border.
 *
 * Do not use this directly unless you want total control over the shape (such
 * as shape with holes). Even then, it is usually recommended to use transparency
 * in the titlebars and a compositing manager. For the vast majority of use
 * cases, use the `shape` property.
 *
 * @property shape_bounding
 * @tparam image shape_bounding
 * @propertydefault An A1 surface where all pixels are white.
 * @propemits false false
 * @see shape
 * @see gears.surface.apply_shape_bounding
 * @see gears.shape
 * @see shape_clip
 * @see shape_input
 * @see client_shape_bounding
 * @see client_shape_clip
 * @see gears.surface
 */

/**
 * The client's clip shape as set by awesome as a (native) cairo surface.
 *
 * The shape_clip is the shape of the client *content*. It is *inside* the
 * border.
 *
 * @property shape_clip
 * @tparam image shape_clip
 * @propertydefault An A1 surface where all pixels are white.
 * @propemits false false
 * @see shape_bounding
 * @see shape_input
 * @see shape
 * @see gears.surface.apply_shape_bounding
 * @see gears.shape
 * @see client_shape_bounding
 * @see client_shape_clip
 * @see gears.surface
 */

/**
 * The client's input shape as set by awesome as a (native) cairo surface.
 *
 * The input shape is the shape where mouse input will be passed to the
 * client rather than propagated below it.
 *
 * @property shape_input
 * @tparam image shape_input
 * @propertydefault An A1 surface where all pixels are white.
 * @propemits false false
 * @see shape_bounding
 * @see shape_clip
 * @see shape
 * @see gears.surface.apply_shape_bounding
 * @see gears.shape
 * @see client_shape_bounding
 * @see client_shape_clip
 * @see gears.surface
 */

/**
 * The client's bounding shape as set by the program as a (native) cairo surface.
 *
 * @property client_shape_bounding
 * @tparam image client_shape_bounding
 * @propertydefault An A1 surface where all pixels are white.
 * @propemits false false
 * @readonly
 * @see shape_bounding
 * @see shape_clip
 * @see shape_input
 * @see shape
 * @see gears.surface.apply_shape_bounding
 * @see gears.shape
 * @see client_shape_clip
 * @see gears.surface
 */

/**
 * The client's clip shape as set by the program as a (native) cairo surface.
 *
 * @property client_shape_clip
 * @tparam image client_shape_clip
 * @propertydefault An A1 surface where all pixels are white.
 * @propemits false false
 * @readonly
 * @see shape_bounding
 * @see shape_clip
 * @see shape_input
 * @see shape
 * @see gears.surface.apply_shape_bounding
 * @see gears.shape
 * @see client_shape_bounding
 * @see gears.surface
 */

/**
 * The FreeDesktop StartId.
 *
 * When a client is spawned (like using a terminal or `awful.spawn`), a startup
 * notification identifier is created. When the client is created, this
 * identifier remain the same. This allow to match a spawn event to an actual
 * client.
 *
 * This is used to display a different mouse cursor when the application is
 * loading and also to attach some properties to the newly created client (like
 * a `tag` or `floating` state).
 *
 * Some applications, like `xterm`, don't support startup notification. While
 * not perfect, the addition the following code to `rc.lua` will mitigate the
 * issue. Please note that this code is Linux specific.
 *
 *    local blacklisted_snid = setmetatable({}, {__mode = "v" })
 *
 *    --- Make startup notification work for some clients like XTerm. This is ugly
 *    -- but works often enough to be useful.
 *    local function fix_startup_id(c)
 *        -- Prevent "broken" sub processes created by `c` to inherit its SNID
 *        if c.startup_id then
 *            blacklisted_snid[c.startup_id] = blacklisted_snid[c.startup_id] or c
 *            return
 *        end
 *
 *        if not c.pid then return end
 *
 *        -- Read the process environment variables
 *        local f = io.open("/proc/"..c.pid.."/environ", "rb")
 *
 *        -- It will only work on Linux, that's already 99% of the userbase.
 *        if not f then return end
 *
 *        local value = _VERSION <= "Lua 5.1" and "([^\z]*)\0" or "([^\0]*)\0"
 *        local snid = f:read("*all"):match("STARTUP_ID=" .. value)
 *        f:close()
 *
 *        -- If there is already a client using this SNID, it means it's either a
 *        -- subprocess or another window for the same process. While it makes sense
 *        -- in some case to apply the same rules, it is not always the case, so
 *        -- better doing nothing rather than something stupid.
 *        if blacklisted_snid[snid] then return end
 *
 *        c.startup_id = snid
 *
 *        blacklisted_snid[snid] = c
 *    end
 *
 *    ruled.client.add_rule_source(
 *        "snid", fix_startup_id, {}, {"awful.spawn", "ruled.client"}
 *    )
 *
 * @property startup_id
 * @tparam string startup_id
 * @propertydefault This is optionally provided by the application.
 * @propemits false false
 * @see awful.spawn
 */

/**
 * If the client that this object refers to is still managed by awesome.
 *
 * To avoid errors, use:
 *
 *    local is_valid = pcall(function() return c.valid end) and c.valid
 *
 * @property valid
 * @tparam[opt=true] boolean valid
 * @propemits false false
 * @readonly
 * @see kill
 */

/**
 * The first tag of the client.
 *
 * Optimized form of `c:tags()[1]`. Not every workflow uses the
 * ability to set multiple tags to a client. It is often enough
 * to only get the first tag and ignore everything else.
 *
 * @property first_tag
 * @tparam[opt=nil] tag|nil first_tag
 * @propemits false false
 * @readonly
 * @see tags
 */

/** Return client struts (reserved space at the edge of the screen).
 *
 * The struts area is a table with a `left`, `right`, `top` and `bottom`
 * keys to define how much space of the screen `workarea` this client
 * should reserve for itself.
 *
 * This corresponds to EWMH's `_NET_WM_STRUT` and `_NET_WM_STRUT_PARTIAL`.
 *
 * In the example below, 2 object affect the workarea (using their struts):
 *
 * * The top wibar add a `top=24`
 * * The bottom-left client add `bottom=100, left=100`
 *
 * @DOC_screen_struts_EXAMPLE@
 *
 * @tparam table|nil struts A table with new strut values, or none.
 * @tparam[opt=0] integer struts.left
 * @tparam[opt=0] integer struts.right
 * @tparam[opt=0] integer struts.top
 * @tparam[opt=0] integer struts.bottom
 * @treturn table A table with strut values.
 * @method struts
 * @see geometry
 * @see screen.workarea
 * @see dockable
 */

/** Get or set mouse buttons bindings for a client.
 *
 * @property buttons
 * @tparam[opt={}] table buttons
 * @tablerowtype A list of `awful.button`s objects.
 * @propemits false false
 * @see awful.button
 * @see append_mousebinding
 * @see remove_mousebinding
 * @see request::default_mousebindings
 */

/** Get the number of instances.
 *
 * @treturn integer The number of client objects alive.
 * @staticfct instances
 */

/* Set a __index metamethod for all client instances.
 * @tparam function cb The meta-method
 * @staticfct set_index_miss_handler
 */

/* Set a __newindex metamethod for all client instances.
 * @tparam function cb The meta-method
 * @staticfct set_newindex_miss_handler
 */

typedef enum {
    CLIENT_MAXIMIZED_NONE = 0 << 0,
    CLIENT_MAXIMIZED_V = 1 << 0,
    CLIENT_MAXIMIZED_H = 1 << 1,
    CLIENT_MAXIMIZED_BOTH = 1 << 2, /* V|H == BOTH, but ~(V|H) != ~(BOTH)... */
} client_maximized_t;

static area_t titlebar_get_area(client* c, client_titlebar_t bar);
static drawable_t*
titlebar_get_drawable(lua_State* L, client* c, int cl_idx, client_titlebar_t bar);
static void client_resize_do(client* c, area_t geometry);
static void
client_set_maximized_common(lua_State* L, int cidx, bool s, const char* type, const int val);

/** Collect a client.
 * \param L The Lua VM state.
 * \return The number of element pushed on stack.
 */

client::~client() { xcb_icccm_get_wm_protocols_reply_wipe(&protocols); }

/** Change the clients urgency flag.
 * \param L The Lua VM state.
 * \param cidx The client index on the stack.
 * \param urgent The new flag state.
 */
void client_set_urgent(lua_State* L, int cidx, bool urgent) {
    auto c = client_class.checkudata<client>(L, cidx);

    if (c->urgent != urgent) {
        c->urgent = urgent;

        luaA_object_emit_signal(L, cidx, "property::urgent", 0);
    }
}

#define DO_CLIENT_SET_PROPERTY(prop)                                                              \
    void client_set_##prop(lua_State* L, int cidx, decltype(std::declval<client>().prop) value) { \
        auto c = client_class.checkudata<client>(L, cidx);                                        \
        if (c->prop != value) {                                                                   \
            c->prop = value;                                                                      \
            luaA_object_emit_signal(L, cidx, "property::" #prop, 0);                              \
        }                                                                                         \
    }
DO_CLIENT_SET_PROPERTY(group_window)
DO_CLIENT_SET_PROPERTY(type)
DO_CLIENT_SET_PROPERTY(transient_for)
DO_CLIENT_SET_PROPERTY(pid)
DO_CLIENT_SET_PROPERTY(skip_taskbar)
#undef DO_CLIENT_SET_PROPERTY

#define DO_CLIENT_SET_STRING_PROPERTY2(prop, signal)               \
    void client_set_##prop(lua_State* L, int cidx, char* value) {  \
        auto c = client_class.checkudata<client>(L, cidx);         \
        if (A_STREQ(c->prop, value)) {                             \
            p_delete(&value);                                      \
            return;                                                \
        }                                                          \
        p_delete(&c->prop);                                        \
        c->prop = value;                                           \
        luaA_object_emit_signal(L, cidx, "property::" #signal, 0); \
    }
#define DO_CLIENT_SET_STRING_PROPERTY3(prop, getter, setter, signal)           \
    void client_set_##prop(lua_State* L, int cidx, const std::string& value) { \
        auto c = client_class.checkudata<client>(L, cidx);                     \
        if (c->getter() == value) {                                            \
            return;                                                            \
        }                                                                      \
        c->setter(value);                                                      \
        luaA_object_emit_signal(L, cidx, "property::" #signal, 0);             \
    }
#define DO_CLIENT_SET_STRING_PROPERTY4(name, signal) \
    DO_CLIENT_SET_STRING_PROPERTY3(name, get##name, set##name, signal)
#define DO_CLIENT_SET_STRING_PROPERTY(prop) DO_CLIENT_SET_STRING_PROPERTY2(prop, prop)
DO_CLIENT_SET_STRING_PROPERTY4(Name, name)
DO_CLIENT_SET_STRING_PROPERTY4(AltName, name)
DO_CLIENT_SET_STRING_PROPERTY4(IconName, icon_name)
DO_CLIENT_SET_STRING_PROPERTY4(AltIconName, icon_name)
DO_CLIENT_SET_STRING_PROPERTY4(StartupId, startup_id)
DO_CLIENT_SET_STRING_PROPERTY4(Role, role)
DO_CLIENT_SET_STRING_PROPERTY4(Machine, machine)
#undef DO_CLIENT_SET_STRING_PROPERTY

void client_emit_scanned(void) {
    lua_State* L = globalconf_get_lua_State();
    client_class.emit_signal(L, "scanned", 0);
}

void client_emit_scanning(void) {
    lua_State* L = globalconf_get_lua_State();
    client_class.emit_signal(L, "scanning", 0);
}

void client_set_motif_wm_hints(lua_State* L, int cidx, motif_wm_hints_t hints) {
    auto c = client_class.checkudata<client>(L, cidx);
    if (memcmp(&c->motif_wm_hints, &hints, sizeof(c->motif_wm_hints)) == 0) {
        return;
    }

    memcpy(&c->motif_wm_hints, &hints, sizeof(c->motif_wm_hints));
    luaA_object_emit_signal(L, cidx, "property::motif_wm_hints", 0);
}

void client_find_transient_for(client* c) {
    size_t counter;
    client *tc, *tmp;
    lua_State* L = globalconf_get_lua_State();

    /* This might return NULL, in which case we unset transient_for */
    tmp = tc = client_getbywin(c->transient_for_window);

    /* Verify that there are no loops in the transient_for relation after we are done */
    for (counter = 0; tmp != NULL && counter <= Manager::get().getStack().size(); counter++) {
        if (tmp == c) {
            /* We arrived back at the client we started from, so there is a loop */
            counter = Manager::get().getStack().size() + 1;
        }
        tmp = tmp->transient_for;
    }

    if (counter > Manager::get().getStack().size()) {
        /* There was a loop, so unset .transient_for */
        tc = NULL;
    }

    luaA_object_push(L, c);
    client_set_transient_for(L, -1, tc);
    lua_pop(L, 1);
}

void client_set_ClassInstance(lua_State* L,
                              int cidx,
                              const std::string& cls,
                              const std::string& instance) {
    auto c = client_class.checkudata<client>(L, cidx);
    c->setCls(cls);
    luaA_object_emit_signal(L, cidx, "property::class", 0);
    c->setInstance(instance);
    luaA_object_emit_signal(L, cidx, "property::instance", 0);
}

/** Returns true if a client is tagged with one of the active tags.
 * \param c The client to check.
 * \return true if the client is visible, false otherwise.
 */
bool client_on_selected_tags(client* c) {
    if (c->sticky) {
        return true;
    }

    for (auto& tag : Manager::get().tags) {
        if (tag.get()->selected && is_client_tagged(c, tag.get())) {
            return true;
        }
    }

    return false;
}
template <typename Predicate>
static client* find_client(const std::vector<client*>& clients, Predicate&& pred) {
    auto it = std::ranges::find_if(clients, pred);
    return it != clients.end() ? *it : nullptr;
}
/** Get a client by its window.
 * \param w The client window to find.
 * \return A client pointer if found, NULL otherwise.
 */
client* client_getbywin(xcb_window_t w) {
    return find_client(Manager::get().clients, [w](auto c) { return c->window == w; });
}

client* client_getbynofocuswin(xcb_window_t w) {
    return find_client(Manager::get().clients, [w](auto c) { return c->nofocus_window == w; });
}

/** Get a client by its frame window.
 * \param w The client window to find.
 * \return A client pointer if found, NULL otherwise.
 */
client* client_getbyframewin(xcb_window_t w) {
    return find_client(Manager::get().clients, [w](auto c) { return c->frame_window == w; });
}

/** Unfocus a client (internal).
 * \param c The client.
 */
static void client_unfocus_internal(client* c) {
    lua_State* L = globalconf_get_lua_State();
    Manager::get().focus.client = NULL;

    luaA_object_push(L, c);

    lua_pushboolean(L, false);
    luaA_object_emit_signal(L, -2, "property::active", 1);
    luaA_object_emit_signal(L, -1, "unfocus", 0);
    lua_pop(L, 1);
}

/** Unfocus a client.
 * \param c The client.
 */
static void client_unfocus(client* c) {
    client_unfocus_internal(c);
    Manager::get().focus.need_update = true;
}

/** Check if client supports atom a protocol in WM_PROTOCOL.
 * \param c The client.
 * \param atom The protocol atom to check for.
 * \return True if client has the atom in protocol, false otherwise.
 */
bool client_hasproto(client* c, xcb_atom_t atom) {
    for (uint32_t i = 0; i < c->protocols.atoms_len; i++) {
        if (c->protocols.atoms[i] == atom) {
            return true;
        }
    }
    return false;
}

/** Prepare banning a client by running all needed lua events.
 * \param c The client.
 */
void client_ban_unfocus(client* c) {
    /* Wait until the last moment to take away the focus from the window. */
    if (Manager::get().focus.client == c) {
        client_unfocus(c);
    }
}

/** Ban client and move it out of the viewport.
 * \param c The client.
 */
void client_ban(client* c) {
    if (!c->isbanned) {
        client_ignore_enterleave_events();
        getConnection().unmap_window(c->frame_window);
        client_restore_enterleave_events();

        c->isbanned = true;

        client_ban_unfocus(c);
    }
}

/** This is part of The Bob Marley Algorithm: we ignore enter and leave window
 * in certain cases, like map/unmap or move, so we don't get spurious events.
 * The implementation works by noting the range of sequence numbers for which we
 * should ignore events. We grab the server to make sure that only we could
 * generate events in this range.
 */
void client_ignore_enterleave_events(void) {
    awsm_check(Manager::get().pending_enter_leave_begin.sequence == 0);
    Manager::get().pending_enter_leave_begin = getConnection().grab_server();
    /* If the connection is broken, we get a request with sequence number 0
     * which would then trigger an assertion in
     * client_restore_enterleave_events(). Handle this nicely.
     */
    if (auto err = getConnection().connection_has_error()) {
        log_fatal("X server connection broke (error {})", err);
    }
    awsm_check(Manager::get().pending_enter_leave_begin.sequence != 0);
}

void client_restore_enterleave_events(void) {
    sequence_pair_t pair;

    awsm_check(Manager::get().pending_enter_leave_begin.sequence != 0);
    pair.begin = Manager::get().pending_enter_leave_begin;
    pair.end = xcb_no_operation(getConnection().getConnection());
    xutil_ungrab_server();
    Manager::get().pending_enter_leave_begin.sequence = 0;
    Manager::get().ignore_enter_leave_events.push_back(pair);
}

/** Record that a client got focus.
 * \param c The client.
 * \return true if the client focus changed, false otherwise.
 */
bool client_focus_update(client* c) {
    lua_State* L = globalconf_get_lua_State();

    if (Manager::get().focus.client && Manager::get().focus.client != c) {
        /* When we are called due to a FocusIn event (=old focused client
         * already unfocused), we don't want to cause a SetInputFocus,
         * because the client which has focus now could be using globally
         * active input model (or 'no input').
         */
        client_unfocus_internal(Manager::get().focus.client);
    }

    bool focused_new = Manager::get().focus.client != c;
    Manager::get().focus.client = c;

    /* According to EWMH, we have to remove the urgent state from a client.
     * This should be done also for the current/focused client (FS#1310). */
    luaA_object_push(L, c);
    client_set_urgent(L, -1, false);

    if (focused_new) {
        lua_pushboolean(L, true);
        luaA_object_emit_signal(L, -2, "property::active", 1);
        luaA_object_emit_signal(L, -1, "focus", 0);
    }

    lua_pop(L, 1);

    return focused_new;
}

/** Give focus to client, or to first client if client is NULL.
 * \param c The client.
 */
void client_focus(client* c) {
    /* We have to set focus on first client */
    if (!c && Manager::get().clients.size() && !(c = Manager::get().clients[0])) {
        return;
    }

    if (client_focus_update(c)) {
        Manager::get().focus.need_update = true;
    }
}

static xcb_window_t client_get_nofocus_window(client* c) {
    if (c->nofocus_window == XCB_NONE) {
        c->nofocus_window = getConnection().generate_id();
        getConnection().create_window(Manager::get().default_depth,
                                      c->nofocus_window,
                                      c->frame_window,
                                      {-2, -2, 1, 1},
                                      0,
                                      XCB_COPY_FROM_PARENT,
                                      Manager::get().visual->visual_id,
                                      0);
        getConnection().map_window(c->nofocus_window);
        xwindow_grabkeys(c->nofocus_window, c->keys);
    }
    return c->nofocus_window;
}

void client_focus_refresh(void) {
    client* c = Manager::get().focus.client;
    xcb_window_t win = Manager::get().focus.window_no_focus;

    if (!Manager::get().focus.need_update) {
        return;
    }

    if (c && client_on_selected_tags(c)) {
        /* Make sure this window is unbanned and e.g. not minimized */
        client_unban(c);
        /* Sets focus on window - using xcb_set_input_focus or WM_TAKE_FOCUS */
        if (!c->nofocus) {
            win = c->window;
        } else {
            win = client_get_nofocus_window(c);
        }

        if (client_hasproto(c, WM_TAKE_FOCUS)) {
            xwindow_takefocus(c->window);
        }
    }

    /* If nothing has the focus or the currently focused client does not want
     * us to focus it, this sets the focus to the root window. This makes sure
     * the previously focused client actually gets unfocused. Alternatively, the
     * new client gets the input focus.
     */
    getConnection().set_input_focus(XCB_INPUT_FOCUS_PARENT, win, Manager::get().x.get_timestamp());

    /* Do this last, because client_unban() might set it to true */
    Manager::get().focus.need_update = false;
}

static void client_border_refresh(void) {
    for (auto* c : Manager::get().clients) {
        window_border_refresh((window_t*)c);
    }
}

static void client_geometry_refresh(void) {
    bool ignored_enterleave = false;
    for (auto* c : Manager::get().clients) {
        /* Compute the client window's and frame window's geometry */
        area_t geometry = c->geometry;
        area_t real_geometry = c->geometry;
        if (!c->fullscreen) {
            if ((real_geometry.width < c->titlebar[CLIENT_TITLEBAR_LEFT].size +
                                         c->titlebar[CLIENT_TITLEBAR_RIGHT].size) ||
                (real_geometry.height < c->titlebar[CLIENT_TITLEBAR_TOP].size +
                                          c->titlebar[CLIENT_TITLEBAR_BOTTOM].size)) {
                log_warn(
                  "Resizing a window to a negative size!? Have width {}-{}-{}={}"
                  " and height {}-{}-{}={}",
                  real_geometry.width,
                  c->titlebar[CLIENT_TITLEBAR_LEFT].size,
                  c->titlebar[CLIENT_TITLEBAR_RIGHT].size,
                  real_geometry.width - c->titlebar[CLIENT_TITLEBAR_LEFT].size -
                    c->titlebar[CLIENT_TITLEBAR_RIGHT].size,
                  real_geometry.height,
                  c->titlebar[CLIENT_TITLEBAR_TOP].size,
                  c->titlebar[CLIENT_TITLEBAR_BOTTOM].size,
                  real_geometry.height - c->titlebar[CLIENT_TITLEBAR_TOP].size -
                    c->titlebar[CLIENT_TITLEBAR_BOTTOM].size);
            }

            real_geometry.top_left = {c->titlebar[CLIENT_TITLEBAR_LEFT].size,
                                      c->titlebar[CLIENT_TITLEBAR_TOP].size};
            real_geometry.width -= c->titlebar[CLIENT_TITLEBAR_LEFT].size;
            real_geometry.width -= c->titlebar[CLIENT_TITLEBAR_RIGHT].size;
            real_geometry.height -= c->titlebar[CLIENT_TITLEBAR_TOP].size;
            real_geometry.height -= c->titlebar[CLIENT_TITLEBAR_BOTTOM].size;

            if (real_geometry.width == 0 || real_geometry.height == 0) {
                log_warn("Resizing a window to size zero!?");
            }
        } else {
            real_geometry.top_left = {0, 0};
        }

        /* Is there anything to do? */
        if (geometry == c->x11_frame_geometry && real_geometry == c->x11_client_geometry) {
            if (c->got_configure_request) {
                /* ICCCM 4.1.5 / 4.2.3, if nothing was changed, send an event saying so */
                client_send_configure(c);
                c->got_configure_request = false;
            }
            continue;
        }

        if (!ignored_enterleave) {
            client_ignore_enterleave_events();
            ignored_enterleave = true;
        }

        getConnection().configure_window(c->frame_window,
                                         XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y |
                                           XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT,
                                         std::array<uint32_t, 4>{(uint32_t)geometry.top_left.x,
                                                                 (uint32_t)geometry.top_left.y,
                                                                 geometry.width,
                                                                 geometry.height});
        std::array<uint32_t, 4> geo = {(uint32_t)real_geometry.top_left.x,
                                       (uint32_t)real_geometry.top_left.y,
                                       real_geometry.width,
                                       real_geometry.height};
        getConnection().configure_window(c->window,
                                         XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y |
                                           XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT,
                                         geo);

        c->x11_frame_geometry = geometry;
        c->x11_client_geometry = real_geometry;

        /* ICCCM 4.2.3 says something else, but Java always needs this... */
        client_send_configure(c);
        c->got_configure_request = false;
    }
    if (ignored_enterleave) {
        client_restore_enterleave_events();
    }
}

void client_refresh(void) {
    client_geometry_refresh();
    client_border_refresh();
    client_focus_refresh();
}

void client_destroy_later(void) {
    bool ignored_enterleave = false;
    for (auto window : Manager::get().destroy_later_windows) {
        if (!ignored_enterleave) {
            client_ignore_enterleave_events();
            ignored_enterleave = true;
        }
        getConnection().destroy_window(window);
    }
    if (ignored_enterleave) {
        client_restore_enterleave_events();
    }

    /* Everything's done, clear the list */
    Manager::get().destroy_later_windows.clear();
}

static void border_width_callback(client* c, uint16_t old_width, uint16_t new_width) {
    if (c->size_hints.flags & XCB_ICCCM_SIZE_HINT_P_WIN_GRAVITY) {
        area_t geometry = c->geometry;
        int16_t diff = new_width - old_width;
        xwindow_translate_for_gravity((xcb_gravity_t)c->size_hints.win_gravity,
                                      diff,
                                      diff,
                                      diff,
                                      diff,
                                      &geometry.top_left.x,
                                      &geometry.top_left.y);
        /* inform client about changes */
        client_resize_do(c, geometry);
    }
}

static void client_update_properties(lua_State* L, int cidx, client* c) {
    /* get all hints */
    xcb_get_property_cookie_t wm_normal_hints = property_get_wm_normal_hints(c);
    xcb_get_property_cookie_t wm_hints = property_get_wm_hints(c);
    xcb_get_property_cookie_t wm_transient_for = property_get_wm_transient_for(c);
    xcb_get_property_cookie_t wm_client_leader = property_get_wm_client_leader(c);
    xcb_get_property_cookie_t wm_client_machine = property_get_wm_client_machine(c);
    xcb_get_property_cookie_t wm_window_role = property_get_wm_window_role(c);
    xcb_get_property_cookie_t net_wm_pid = property_get_net_wm_pid(c);
    xcb_get_property_cookie_t net_wm_icon = property_get_net_wm_icon(c);
    xcb_get_property_cookie_t wm_name = property_get_wm_name(c);
    xcb_get_property_cookie_t net_wm_name = property_get_net_wm_name(c);
    xcb_get_property_cookie_t wm_icon_name = property_get_wm_icon_name(c);
    xcb_get_property_cookie_t net_wm_icon_name = property_get_net_wm_icon_name(c);
    xcb_get_property_cookie_t wm_class = property_get_wm_class(c);
    xcb_get_property_cookie_t wm_protocols = property_get_wm_protocols(c);
    xcb_get_property_cookie_t motif_wm_hints = property_get_motif_wm_hints(c);
    xcb_get_property_cookie_t opacity = xwindow_get_opacity_unchecked(c->window);

    /* update strut */
    ewmh_process_client_strut(c);

    /* Now process all replies */
    property_update_wm_normal_hints(c, wm_normal_hints);
    property_update_wm_hints(c, wm_hints);
    property_update_wm_transient_for(c, wm_transient_for);
    property_update_wm_client_leader(c, wm_client_leader);
    property_update_wm_client_machine(c, wm_client_machine);
    property_update_wm_window_role(c, wm_window_role);
    property_update_net_wm_pid(c, net_wm_pid);
    property_update_net_wm_icon(c, net_wm_icon);
    property_update_wm_name(c, wm_name);
    property_update_net_wm_name(c, net_wm_name);
    property_update_wm_icon_name(c, wm_icon_name);
    property_update_net_wm_icon_name(c, net_wm_icon_name);
    property_update_wm_class(c, wm_class);
    property_update_wm_protocols(c, wm_protocols);
    property_update_motif_wm_hints(c, motif_wm_hints);
    window_set_opacity(L, cidx, xwindow_get_opacity_from_cookie(opacity));
}

/** Manage a new client.
 * \param w The window.
 * \param wgeom Window geometry.
 * \param startup True if we are managing at startup time.
 */
void client_manage(xcb_window_t w,
                   xcb_get_geometry_reply_t* wgeom,
                   xcb_get_window_attributes_reply_t* wattr) {
    const uint32_t select_input_val[] = {CLIENT_SELECT_INPUT_EVENT_MASK};

    if (systray_iskdedockapp(w)) {
        systray_request_handle(w);
        return;
    }

    /* If this is a new client that just has been launched, then request its
     * startup id. */
    xcb_get_property_cookie_t startup_id_q = getConnection().get_property(
      false, w, _NET_STARTUP_ID, XCB_GET_PROPERTY_TYPE_ANY, 0, UINT_MAX);

    /* Make sure the window is automatically mapped if awesome exits or dies. */
    getConnection().change_save_set(XCB_SET_MODE_INSERT, w);
    if (Manager::get().x.caps.have_shape) {
        getConnection().shape().select_input(w, 1);
    }

    lua_State* L = globalconf_get_lua_State();
    client* c = newobj<client, client_class>(L);
    xcb_screen_t* s = Manager::get().screen;
    c->border_width_callback = (void (*)(void*, uint16_t, uint16_t))border_width_callback;

    /* consider the window banned */
    c->isbanned = true;
    /* Store window and visual */
    c->window = w;
    c->visualtype = draw_find_visual(Manager::get().screen, wattr->visual);
    c->frame_window = getConnection().generate_id();
    const uint32_t values[] = {Manager::get().screen->black_pixel,
                               XCB_GRAVITY_NORTH_WEST,
                               XCB_GRAVITY_NORTH_WEST,
                               1,
                               FRAME_SELECT_INPUT_EVENT_MASK,
                               Manager::get().default_cmap};
    getConnection().create_window(Manager::get().default_depth,
                                  c->frame_window,
                                  s->root,
                                  {wgeom->x, wgeom->y, wgeom->width, wgeom->height},
                                  wgeom->border_width,
                                  XCB_COPY_FROM_PARENT,
                                  Manager::get().visual->visual_id,
                                  XCB_CW_BORDER_PIXEL | XCB_CW_BIT_GRAVITY | XCB_CW_WIN_GRAVITY |
                                    XCB_CW_OVERRIDE_REDIRECT | XCB_CW_EVENT_MASK | XCB_CW_COLORMAP,
                                  values);

    /* The client may already be mapped, thus we must be sure that we don't send
     * ourselves an UnmapNotify due to the xcb_reparent_window().
     *
     * Grab the server to make sure we don't lose any events.
     */
    getConnection().grab_server();

    getConnection().clear_attributes(Manager::get().screen->root, XCB_CW_EVENT_MASK);
    xcb_void_cookie_t reparent_cookie = getConnection().reparent_window_checked(w, c->frame_window, 0, 0);
    getConnection().map_window(w);
    getConnection().change_attributes(
      Manager::get().screen->root, XCB_CW_EVENT_MASK, ROOT_WINDOW_EVENT_MASK);
    xutil_ungrab_server();

    /* Do this now so that we don't get any events for the above
     * (Else, reparent could cause an UnmapNotify) */
    getConnection().change_attributes(w, XCB_CW_EVENT_MASK, select_input_val);

    /* The frame window gets the border, not the real client window */
    getConnection().configure_window(w, XCB_CONFIG_WINDOW_BORDER_WIDTH, std::array<uint32_t, 1>{0});

    /* Move this window to the bottom of the stack. Without this we would force
     * other windows which will be above this one to redraw themselves because
     * this window occludes them for a tiny moment. The next stack_refresh()
     * will fix this up and move the window to its correct place. */

    getConnection().configure_window(
      c->frame_window, XCB_CONFIG_WINDOW_STACK_MODE, std::array<uint32_t, 1>{XCB_STACK_MODE_BELOW});

    /* Duplicate client and push it in client list */
    lua_pushvalue(L, -1);
    Manager::get().clients.insert(Manager::get().clients.begin(), (client*)luaA_object_ref(L, -1));

    /* Set the right screen */
    screen_client_moveto(c, screen_getbycoord({wgeom->x, wgeom->y}), false);

    /* Store initial geometry and emits signals so we inform that geometry have
     * been set. */

    c->geometry.top_left = {wgeom->x, wgeom->y};
    c->geometry.width = wgeom->width;
    c->geometry.height = wgeom->height;

    luaA_object_emit_signal(L, -1, "property::x", 0);
    luaA_object_emit_signal(L, -1, "property::y", 0);
    luaA_object_emit_signal(L, -1, "property::width", 0);
    luaA_object_emit_signal(L, -1, "property::height", 0);
    luaA_object_emit_signal(L, -1, "property::window", 0);
    luaA_object_emit_signal(L, -1, "property::geometry", 0);

    /* Set border width */
    window_set_border_width(L, -1, wgeom->border_width);

    /* we honor size hints by default */
    c->size_hints_honor = true;
    luaA_object_emit_signal(L, -1, "property::size_hints_honor", 0);

    /* update all properties */
    client_update_properties(L, -1, c);

    /* check if this is a TRANSIENT_FOR of another client */
    for (auto* oc : Manager::get().clients) {
        if (oc->transient_for_window == w) {
            client_find_transient_for(oc);
        }
    }

    /* Put the window in normal state. */
    xwindow_set_state(c->window, XCB_ICCCM_WM_STATE_NORMAL);

    /* Then check clients hints */
    ewmh_client_check_hints(c);

    /* Push client in stack */
    stack_client_push(c);

    /* Request our response */
    auto reply = getConnection().get_property_reply(startup_id_q);
    /* Say spawn that a client has been started, with startup id as argument */
    auto startup_id = xutil_get_text_property_from_reply(reply);

    if (startup_id.empty() && c->leader_window != XCB_NONE) {
        /* GTK hides this property elsewhere. No idea why. */
        startup_id_q = getConnection().get_property(
          false, c->leader_window, _NET_STARTUP_ID, XCB_GET_PROPERTY_TYPE_ANY, 0, UINT_MAX);
        reply = getConnection().get_property_reply(startup_id_q);
        startup_id = xutil_get_text_property_from_reply(reply);
    }
    c->setStartupId(startup_id);

    spawn_start_notify(c, startup_id.c_str());

    client_class.emit_signal(L, "list", 0);

    /* Add the context */
    if (Manager::get().loop == NULL) {
        lua_pushstring(L, "startup");
    } else {
        lua_pushstring(L, "new");
    }

    /* Hints */
    lua_newtable(L);

    /* client is still on top of the stack; emit signal */
    luaA_object_emit_signal(L, -3, "request::manage", 2);

    /*TODO v6: remove this*/
    luaA_object_emit_signal(L, -1, "manage", 0);

    xcb_generic_error_t* error = getConnection().request_check(reparent_cookie);
    if (error != NULL) {
        log_warn(
          "Failed to manage window with name '{}', class '{}', instance '{}', because reparenting "
          "failed.",
          c->getName().c_str(),
          c->getCls().c_str(),
          c->getInstance().c_str());
        event_handle((xcb_generic_event_t*)error);
        p_delete(&error);
        client_unmanage(c, CLIENT_UNMANAGE_FAILED);
    }

    /* pop client */
    lua_pop(L, 1);
}

static void client_remove_titlebar_geometry(client* c, area_t* geometry) {
    geometry->top_left +=
      {c->titlebar[CLIENT_TITLEBAR_LEFT].size, c->titlebar[CLIENT_TITLEBAR_TOP].size};
    geometry->width -= c->titlebar[CLIENT_TITLEBAR_LEFT].size;
    geometry->width -= c->titlebar[CLIENT_TITLEBAR_RIGHT].size;
    geometry->height -= c->titlebar[CLIENT_TITLEBAR_TOP].size;
    geometry->height -= c->titlebar[CLIENT_TITLEBAR_BOTTOM].size;
}

static void client_add_titlebar_geometry(client* c, area_t* geometry) {
    geometry->top_left -=
      {c->titlebar[CLIENT_TITLEBAR_LEFT].size, c->titlebar[CLIENT_TITLEBAR_TOP].size};
    geometry->width += c->titlebar[CLIENT_TITLEBAR_LEFT].size;
    geometry->width += c->titlebar[CLIENT_TITLEBAR_RIGHT].size;
    geometry->height += c->titlebar[CLIENT_TITLEBAR_TOP].size;
    geometry->height += c->titlebar[CLIENT_TITLEBAR_BOTTOM].size;
}

area_t client_get_undecorated_geometry(client* c) {
    area_t geometry = c->geometry;
    if (!c->fullscreen) {
        int diff_left = c->titlebar[CLIENT_TITLEBAR_LEFT].size;
        int diff_right = c->titlebar[CLIENT_TITLEBAR_RIGHT].size;
        int diff_top = c->titlebar[CLIENT_TITLEBAR_TOP].size;
        int diff_bottom = c->titlebar[CLIENT_TITLEBAR_BOTTOM].size;
        geometry.width -= diff_left + diff_right;
        geometry.height -= diff_top + diff_bottom;
        if (c->size_hints.flags & XCB_ICCCM_SIZE_HINT_P_WIN_GRAVITY) {
            xwindow_translate_for_gravity((xcb_gravity_t)c->size_hints.win_gravity,
                                          -diff_left - c->border_width,
                                          -diff_top - c->border_width,
                                          -diff_right - c->border_width,
                                          -diff_bottom - c->border_width,
                                          &geometry.top_left.x,
                                          &geometry.top_left.y);
        }
    }
    return geometry;
}

/** Send a synthetic configure event to a window.
 */
void client_send_configure(client* c) {
    area_t geometry = c->geometry;

    if (!c->fullscreen) {
        client_remove_titlebar_geometry(c, &geometry);
    }
    xwindow_configure(c->window, geometry, c->border_width);
}

/** Apply size hints to the client's new geometry.
 */
static area_t client_apply_size_hints(client* c, area_t geometry) {
    int32_t minw = 0, minh = 0;
    int32_t basew = 0, baseh = 0, real_basew = 0, real_baseh = 0;

    if (c->fullscreen) {
        return geometry;
    }

    /* Size hints are applied to the window without any decoration */
    client_remove_titlebar_geometry(c, &geometry);

    if (c->size_hints.flags & XCB_ICCCM_SIZE_HINT_BASE_SIZE) {
        basew = c->size_hints.base_width;
        baseh = c->size_hints.base_height;
        real_basew = basew;
        real_baseh = baseh;
    } else if (c->size_hints.flags & XCB_ICCCM_SIZE_HINT_P_MIN_SIZE) {
        /* base size is substituted with min size if not specified */
        basew = c->size_hints.min_width;
        baseh = c->size_hints.min_height;
    }

    if (c->size_hints.flags & XCB_ICCCM_SIZE_HINT_P_MIN_SIZE) {
        minw = c->size_hints.min_width;
        minh = c->size_hints.min_height;
    } else if (c->size_hints.flags & XCB_ICCCM_SIZE_HINT_BASE_SIZE) {
        /* min size is substituted with base size if not specified */
        minw = c->size_hints.base_width;
        minh = c->size_hints.base_height;
    }

    /* Handle the size aspect ratio */
    if (c->size_hints.flags & XCB_ICCCM_SIZE_HINT_P_ASPECT && c->size_hints.min_aspect_den > 0 &&
        c->size_hints.max_aspect_den > 0 && geometry.height > real_baseh &&
        geometry.width > real_basew) {
        /* ICCCM mandates:
         * If a base size is provided along with the aspect ratio fields, the base size should be
         * subtracted from the window size prior to checking that the aspect ratio falls in range.
         * If a base size is not provided, nothing should be subtracted from the window size. (The
         * minimum size is not to be used in place of the base size for this purpose.)
         */
        double dx = geometry.width - real_basew;
        double dy = geometry.height - real_baseh;
        double ratio = dx / dy;
        double min = c->size_hints.min_aspect_num / (double)c->size_hints.min_aspect_den;
        double max = c->size_hints.max_aspect_num / (double)c->size_hints.max_aspect_den;

        if (max > 0 && min > 0 && ratio > 0) {
            if (ratio < min) {
                /* dx is lower than allowed, make dy lower to compensate this (+ 0.5 to force proper
                 * rounding). */
                dy = dx / min + 0.5;
                geometry.width = dx + real_basew;
                geometry.height = dy + real_baseh;
            } else if (ratio > max) {
                /* dx is too high, lower it (+0.5 for proper rounding) */
                dx = dy * max + 0.5;
                geometry.width = dx + real_basew;
                geometry.height = dy + real_baseh;
            }
        }
    }

    /* Handle the minimum size */
    geometry.width = MAX(geometry.width, minw);
    geometry.height = MAX(geometry.height, minh);

    /* Handle the maximum size */
    if (c->size_hints.flags & XCB_ICCCM_SIZE_HINT_P_MAX_SIZE) {
        if (c->size_hints.max_width) {
            geometry.width = MIN(geometry.width, c->size_hints.max_width);
        }
        if (c->size_hints.max_height) {
            geometry.height = MIN(geometry.height, c->size_hints.max_height);
        }
    }

    /* Handle the size increment */
    if (c->size_hints.flags & (XCB_ICCCM_SIZE_HINT_P_RESIZE_INC | XCB_ICCCM_SIZE_HINT_BASE_SIZE) &&
        c->size_hints.width_inc && c->size_hints.height_inc) {
        auto t1 = unsigned_subtract(geometry.width, basew);
        auto t2 = unsigned_subtract(geometry.height, baseh);
        geometry.width -= t1 % c->size_hints.width_inc;
        geometry.height -= t2 % c->size_hints.height_inc;
    }

    client_add_titlebar_geometry(c, &geometry);
    return geometry;
}

static void client_resize_do(client* c, area_t geometry) {
    lua_State* L = globalconf_get_lua_State();

    screen_t* new_screen = c->screen;
    if (!screen_area_in_screen(new_screen, geometry)) {
        new_screen = screen_getbycoord(geometry.top_left);
    }

    /* Also store geometry including border */
    area_t old_geometry = c->geometry;
    c->geometry = geometry;

    luaA_object_push(L, c);
    if (old_geometry != geometry) {
        luaA_object_emit_signal(L, -1, "property::geometry", 0);
    }
    if (old_geometry.top_left != geometry.top_left) {
        luaA_object_emit_signal(L, -1, "property::position", 0);
        if (old_geometry.top_left.x != geometry.top_left.x) {
            luaA_object_emit_signal(L, -1, "property::x", 0);
        }
        if (old_geometry.top_left.y != geometry.top_left.y) {
            luaA_object_emit_signal(L, -1, "property::y", 0);
        }
    }
    if (old_geometry.width != geometry.width || old_geometry.height != geometry.height) {
        luaA_object_emit_signal(L, -1, "property::size", 0);
        if (old_geometry.width != geometry.width) {
            luaA_object_emit_signal(L, -1, "property::width", 0);
        }
        if (old_geometry.height != geometry.height) {
            luaA_object_emit_signal(L, -1, "property::height", 0);
        }
    }
    lua_pop(L, 1);

    screen_client_moveto(c, new_screen, false);

    /* Update all titlebars */
    for (int bar = CLIENT_TITLEBAR_TOP; bar < CLIENT_TITLEBAR_COUNT; bar++) {
        if (c->titlebar[bar].drawable == NULL && c->titlebar[bar].size == 0) {
            continue;
        }

        luaA_object_push(L, c);
        auto drawable = (drawable_t*)titlebar_get_drawable(L, c, -1, (client_titlebar_t)bar);
        luaA_object_push_item(L, -1, drawable);

        area_t area = titlebar_get_area(c, (client_titlebar_t)bar);

        /* Convert to global coordinates */
        area.top_left += geometry.top_left;

        if (c->fullscreen) {
            area.width = area.height = 0;
        }
        drawable_set_geometry(L, -1, area);

        /* Pop the client and the drawable */
        lua_pop(L, 2);
    }
}

/** Resize client window.
 * The sizes given as parameters are with borders!
 * \param c Client to resize.
 * \param geometry New window geometry.
 * \param honor_hints Use size hints.
 * \return true if an actual resize occurred.
 */
bool client_resize(client* c, area_t geometry, bool honor_hints) {
    if (honor_hints) {
        /* We could get integer underflows in client_remove_titlebar_geometry()
         * without these checks here.
         */
        if (geometry.width <
            c->titlebar[CLIENT_TITLEBAR_LEFT].size + c->titlebar[CLIENT_TITLEBAR_RIGHT].size) {
            return false;
        }
        if (geometry.height <
            c->titlebar[CLIENT_TITLEBAR_TOP].size + c->titlebar[CLIENT_TITLEBAR_BOTTOM].size) {
            return false;
        }
        geometry = client_apply_size_hints(c, geometry);
    }

    if (geometry.width <
        c->titlebar[CLIENT_TITLEBAR_LEFT].size + c->titlebar[CLIENT_TITLEBAR_RIGHT].size) {
        return false;
    }
    if (geometry.height <
        c->titlebar[CLIENT_TITLEBAR_TOP].size + c->titlebar[CLIENT_TITLEBAR_BOTTOM].size) {
        return false;
    }

    if (geometry.width == 0 || geometry.height == 0) {
        return false;
    }

    if (c->geometry != geometry) {
        client_resize_do(c, geometry);

        return true;
    }

    return false;
}

/** Set a client minimized, or not.
 * \param L The Lua VM state.
 * \param cidx The client index.
 * \param s Set or not the client minimized.
 */
void client_set_minimized(lua_State* L, int cidx, bool s) {
    auto c = client_class.checkudata<client>(L, cidx);

    if (c->minimized == s) {
        return;
    }
    c->minimized = s;
    banning_need_update();
    if (s) {
        /* ICCCM: To transition from ICONIC to NORMAL state, the client
         * should just map the window. Thus, iconic clients need to be
         * unmapped, else the MapWindow request doesn't have any effect.
         */
        xwindow_set_state(c->window, XCB_ICCCM_WM_STATE_ICONIC);

        const uint32_t client_select_input_val[] = {CLIENT_SELECT_INPUT_EVENT_MASK};
        const uint32_t frame_select_input_val[] = {FRAME_SELECT_INPUT_EVENT_MASK};
        getConnection().grab_server();
        getConnection().clear_attributes(Manager::get().screen->root, XCB_CW_EVENT_MASK);
        getConnection().clear_attributes(c->frame_window, XCB_CW_EVENT_MASK);
        getConnection().clear_attributes(c->window, XCB_CW_EVENT_MASK);
        getConnection().unmap_window(c->window);
        getConnection().change_attributes(
          Manager::get().screen->root, XCB_CW_EVENT_MASK, ROOT_WINDOW_EVENT_MASK);
        getConnection().change_attributes(
          c->frame_window, XCB_CW_EVENT_MASK, frame_select_input_val);
        getConnection().change_attributes(c->window, XCB_CW_EVENT_MASK, client_select_input_val);
        xutil_ungrab_server();
    } else {
        xwindow_set_state(c->window, XCB_ICCCM_WM_STATE_NORMAL);
        getConnection().map_window(c->window);
    }
    if (strut_has_value(&c->strut)) {
        screen_update_workarea(c->screen);
    }
    luaA_object_emit_signal(L, cidx, "property::minimized", 0);
}

/** Set a client hidden, or not.
 * \param L The Lua VM state.
 * \param cidx The client index.
 * \param s Set or not the client hidden.
 */
static void client_set_hidden(lua_State* L, int cidx, bool s) {
    auto c = client_class.checkudata<client>(L, cidx);

    if (c->hidden != s) {
        c->hidden = s;
        banning_need_update();
        if (strut_has_value(&c->strut)) {
            screen_update_workarea(c->screen);
        }
        luaA_object_emit_signal(L, cidx, "property::hidden", 0);
    }
}

/** Set a client sticky, or not.
 * \param L The Lua VM state.
 * \param cidx The client index.
 * \param s Set or not the client sticky.
 */
void client_set_sticky(lua_State* L, int cidx, bool s) {
    auto c = client_class.checkudata<client>(L, cidx);

    if (c->sticky != s) {
        c->sticky = s;
        banning_need_update();
        ewmh_client_update_desktop(c);
        if (strut_has_value(&c->strut)) {
            screen_update_workarea(c->screen);
        }
        luaA_object_emit_signal(L, cidx, "property::sticky", 0);
    }
}

/** Set a client focusable, or not.
 * \param L The Lua VM state.
 * \param cidx The client index.
 * \param s Set or not the client's focusable property.
 */
static void client_set_focusable(lua_State* L, int cidx, bool s) {
    auto c = client_class.checkudata<client>(L, cidx);

    if (c->focusable != s) {
        c->focusable = s;
        luaA_object_emit_signal(L, cidx, "property::focusable", 0);
    }
}

/** Unset a client's focusable property and make it use the default again.
 * \param L The Lua VM state.
 * \param cidx The client index.
 */
static void client_unset_focusable(lua_State* L, int cidx) {
    auto c = client_class.checkudata<client>(L, cidx);

    if (c->focusable.has_value()) {
        c->focusable.reset();
        luaA_object_emit_signal(L, cidx, "property::focusable", 0);
    }
}

/** Set a client fullscreen, or not.
 * \param L The Lua VM state.
 * \param cidx The client index.
 * \param s Set or not the client fullscreen.
 */
void client_set_fullscreen(lua_State* L, int cidx, bool s) {
    auto c = client_class.checkudata<client>(L, cidx);

    if (c->fullscreen != s) {
        /* become fullscreen! */
        if (s) {
            /* You can only be part of one of the special layers. */
            client_set_below(L, cidx, false);
            client_set_above(L, cidx, false);
            client_set_ontop(L, cidx, false);
        }
        int abs_cidx = Lua::absindex(L, cidx);
        lua_pushstring(L, "fullscreen");
        c->fullscreen = s;
        luaA_object_emit_signal(L, abs_cidx, "request::geometry", 1);
        luaA_object_emit_signal(L, abs_cidx, "property::fullscreen", 0);
        /* Force a client resize, so that titlebars get shown/hidden */
        client_resize_do(c, c->geometry);
        stack_windows();
    }
}

/** Set a client horizontally|vertically maximized.
 * \param L The Lua VM state.
 * \param cidx The client index.
 * \param s The maximized status.
 */
void client_set_maximized_common(lua_State* L, int cidx, bool s, const char* type, const int val) {
    auto c = client_class.checkudata<client>(L, cidx);

    /* Store the current and next state on 2 bit */
    const client_maximized_t current =
      (client_maximized_t)((c->maximized_vertical ? CLIENT_MAXIMIZED_V : CLIENT_MAXIMIZED_NONE) |
                           (c->maximized_horizontal ? CLIENT_MAXIMIZED_H : CLIENT_MAXIMIZED_NONE) |
                           (c->maximized ? CLIENT_MAXIMIZED_BOTH : CLIENT_MAXIMIZED_NONE));
    client_maximized_t next = (client_maximized_t)(s ? (val | current) : (current & (~val)));

    /* When both are already set during startup, assume `maximized` is true*/
    if (next == (CLIENT_MAXIMIZED_H | CLIENT_MAXIMIZED_V) && !Manager::get().loop) {
        next = CLIENT_MAXIMIZED_BOTH;
    }

    if (current != next) {
        int abs_cidx = Lua::absindex(L, cidx);
        int max_before = c->maximized;
        int h_before = c->maximized_horizontal;
        int v_before = c->maximized_vertical;

        /*Update the client properties */
        c->maximized_horizontal = !!(next & CLIENT_MAXIMIZED_H);
        c->maximized_vertical = !!(next & CLIENT_MAXIMIZED_V);
        c->maximized = !!(next & CLIENT_MAXIMIZED_BOTH);

        /* Request the changes to be applied */
        lua_pushstring(L, type);
        luaA_object_emit_signal(L, abs_cidx, "request::geometry", 1);

        /* Notify changes in the relevant properties */
        if (h_before != c->maximized_horizontal) {
            luaA_object_emit_signal(L, abs_cidx, "property::maximized_horizontal", 0);
        }
        if (v_before != c->maximized_vertical) {
            luaA_object_emit_signal(L, abs_cidx, "property::maximized_vertical", 0);
        }
        if (max_before != c->maximized) {
            luaA_object_emit_signal(L, abs_cidx, "property::maximized", 0);
        }

        stack_windows();
    }
}

void client_set_maximized(lua_State* L, int cidx, bool s) {
    return client_set_maximized_common(L, cidx, s, "maximized", CLIENT_MAXIMIZED_BOTH);
}

void client_set_maximized_horizontal(lua_State* L, int cidx, bool s) {
    return client_set_maximized_common(L, cidx, s, "maximized_horizontal", CLIENT_MAXIMIZED_H);
}

void client_set_maximized_vertical(lua_State* L, int cidx, bool s) {
    return client_set_maximized_common(L, cidx, s, "maximized_vertical", CLIENT_MAXIMIZED_V);
}

/** Set a client above, or not.
 * \param L The Lua VM state.
 * \param cidx The client index.
 * \param s Set or not the client above.
 */
void client_set_above(lua_State* L, int cidx, bool s) {
    auto c = client_class.checkudata<client>(L, cidx);

    if (c->above != s) {
        /* You can only be part of one of the special layers. */
        if (s) {
            client_set_below(L, cidx, false);
            client_set_ontop(L, cidx, false);
            client_set_fullscreen(L, cidx, false);
        }
        c->above = s;
        stack_windows();
        luaA_object_emit_signal(L, cidx, "property::above", 0);
    }
}

/** Set a client below, or not.
 * \param L The Lua VM state.
 * \param cidx The client index.
 * \param s Set or not the client below.
 */
void client_set_below(lua_State* L, int cidx, bool s) {
    auto c = client_class.checkudata<client>(L, cidx);

    if (c->below != s) {
        /* You can only be part of one of the special layers. */
        if (s) {
            client_set_above(L, cidx, false);
            client_set_ontop(L, cidx, false);
            client_set_fullscreen(L, cidx, false);
        }
        c->below = s;
        stack_windows();
        luaA_object_emit_signal(L, cidx, "property::below", 0);
    }
}

/** Set a client modal, or not.
 * \param L The Lua VM state.
 * \param cidx The client index.
 * \param s Set or not the client modal attribute.
 */
void client_set_modal(lua_State* L, int cidx, bool s) {
    auto c = client_class.checkudata<client>(L, cidx);

    if (c->modal != s) {
        c->modal = s;
        stack_windows();
        luaA_object_emit_signal(L, cidx, "property::modal", 0);
    }
}

/** Set a client ontop, or not.
 * \param L The Lua VM state.
 * \param cidx The client index.
 * \param s Set or not the client ontop attribute.
 */
void client_set_ontop(lua_State* L, int cidx, bool s) {
    auto c = client_class.checkudata<client>(L, cidx);

    if (c->ontop != s) {
        /* You can only be part of one of the special layers. */
        if (s) {
            client_set_above(L, cidx, false);
            client_set_below(L, cidx, false);
            client_set_fullscreen(L, cidx, false);
        }
        c->ontop = s;
        stack_windows();
        luaA_object_emit_signal(L, cidx, "property::ontop", 0);
    }
}

/** Unban a client and move it back into the viewport.
 * \param c The client.
 */
void client_unban(client* c) {
    lua_State* L = globalconf_get_lua_State();
    if (c->isbanned) {
        client_ignore_enterleave_events();
        getConnection().map_window(c->frame_window);
        client_restore_enterleave_events();

        c->isbanned = false;

        /* An unbanned client shouldn't be minimized or hidden */
        luaA_object_push(L, c);
        client_set_minimized(L, -1, false);
        client_set_hidden(L, -1, false);
        lua_pop(L, 1);

        if (Manager::get().focus.client == c) {
            Manager::get().focus.need_update = true;
        }
    }
}

/** Unmanage a client.
 * \param c The client.
 * \param reason Why was the unmanage done.
 */
void client_unmanage(client* c, client_unmanage_t reason) {
    lua_State* L = globalconf_get_lua_State();

    /* Reset transient_for attributes of windows that might be referring to us */
    for (auto* tc : Manager::get().clients) {
        if (tc->transient_for == c) {
            tc->transient_for = NULL;
        }
    }

    if (Manager::get().focus.client == c) {
        client_unfocus(c);
    }

    /* remove client from global list and everywhere else */
    if (auto it = std::ranges::find(Manager::get().clients, c);
        it != Manager::get().clients.end()) {
        Manager::get().clients.erase(it);
    }
    stack_client_remove(c);
    for (size_t i = 0; i < Manager::get().tags.size(); i++) {
        untag_client(c, Manager::get().tags[i].get());
    }

    luaA_object_push(L, c);

    /* Give the context to Lua */
    switch (reason) {
        break;
    case CLIENT_UNMANAGE_USER: lua_pushstring(L, "user"); break;
    case CLIENT_UNMANAGE_REPARENT: lua_pushstring(L, "reparented"); break;
    case CLIENT_UNMANAGE_UNMAP:
    case CLIENT_UNMANAGE_FAILED:
    case CLIENT_UNMANAGE_DESTROYED: lua_pushstring(L, "destroyed"); break;
    }

    /* Hints */
    lua_newtable(L);

    luaA_object_emit_signal(L, -3, "request::unmanage", 2);
    luaA_object_emit_signal(L, -1, "unmanage", 0);
    lua_pop(L, 1);

    client_class.emit_signal(L, "list", 0);

    if (strut_has_value(&c->strut)) {
        screen_update_workarea(c->screen);
    }

    /* Get rid of all titlebars */
    for (int bar = CLIENT_TITLEBAR_TOP; bar < CLIENT_TITLEBAR_COUNT; bar++) {
        if (c->titlebar[bar].drawable == NULL) {
            continue;
        }

        if (Manager::get().drawable_under_mouse == c->titlebar[bar].drawable) {
            /* Leave drawable before we invalidate the client */
            lua_pushnil(L);
            event_drawable_under_mouse(L, -1);
            lua_pop(L, 1);
        }

        /* Forget about the drawable */
        luaA_object_push(L, c);
        luaA_object_unref_item(L, -1, c->titlebar[bar].drawable);
        c->titlebar[bar].drawable = NULL;
        lua_pop(L, 1);
    }

    /* Clear our event mask so that we don't receive any events from now on,
     * especially not for the following requests. */
    if (reason != CLIENT_UNMANAGE_DESTROYED) {
        getConnection().clear_attributes(c->window, XCB_CW_EVENT_MASK);
    }
    getConnection().clear_attributes(c->frame_window, XCB_CW_EVENT_MASK);

    if (reason != CLIENT_UNMANAGE_DESTROYED) {
        xwindow_buttons_grab(c->window, {});
        xwindow_grabkeys(c->window, {});
        area_t geometry = client_get_undecorated_geometry(c);
        getConnection().unmap_window(c->window);
        getConnection().reparent_window(
          c->window, Manager::get().screen->root, geometry.top_left.x, geometry.top_left.y);
    }

    if (c->nofocus_window != XCB_NONE) {
        Manager::get().destroy_later_windows.push_back(c->nofocus_window);
    }
    Manager::get().destroy_later_windows.push_back(c->frame_window);

    if (reason != CLIENT_UNMANAGE_DESTROYED) {
        /* Remove this window from the save set since this shouldn't be made visible
         * after a restart anymore. */
        getConnection().change_save_set(XCB_SET_MODE_DELETE, c->window);
        if (Manager::get().x.caps.have_shape) {
            getConnection().shape().select_input(c->window, 0);
        }

        /* Do this last to avoid races with clients. According to ICCCM, clients
         * arent allowed to re-use the window until after this. */
        xwindow_set_state(c->window, XCB_ICCCM_WM_STATE_WITHDRAWN);
    }

    /* set client as invalid */
    c->window = XCB_NONE;

    luaA_object_unref(L, c);
}

/** Kill a client via a WM_DELETE_WINDOW request or KillClient if not
 * supported.
 * \param c The client to kill.
 */
void client_kill(client* c) {
    if (client_hasproto(c, WM_DELETE_WINDOW)) {
        xcb_client_message_event_t ev;

        memset(&ev, 0, sizeof(ev));

        ev.response_type = XCB_CLIENT_MESSAGE;
        ev.window = c->window;
        ev.format = 32;
        ev.data.data32[1] = Manager::get().x.get_timestamp();
        ev.type = WM_PROTOCOLS;
        ev.data.data32[0] = WM_DELETE_WINDOW;

        getConnection().send_event(false, c->window, XCB_EVENT_MASK_NO_EVENT, (char*)&ev);
    } else {
        getConnection().kill_client(c->window);
    }
}

/** Get all clients into a table.
 *
 * @tparam[opt] integer|screen screen A screen number to filter clients on.
 * @tparam[opt] boolean stacked Return clients in stacking order? (ordered from
 *   top to bottom).
 * @treturn table A table with clients.
 * @staticfct get
 * @usage for _, c in ipairs(client.get()) do
 *     -- do something
 * end
 */
static int luaA_client_get(lua_State* L) {
    screen_t* screen = NULL;
    bool stacked = false;

    if (!lua_isnoneornil(L, 1)) {
        screen = luaA_checkscreen(L, 1);
    }

    if (!lua_isnoneornil(L, 2)) {
        stacked = Lua::checkboolean(L, 2);
    }

    lua_newtable(L);

    auto pushclient = [i = int(1), L](auto* c) mutable {
        luaA_object_push(L, c);
        lua_rawseti(L, -2, i++);
    };

    if (stacked) {
        for (auto* c :
             Manager::get().getStack() | std::views::reverse |
               std::views::filter([screen](auto* c) { return !screen || c->screen == screen; })) {
            pushclient(c);
        }
    } else {
        for (auto* c : Manager::get().clients | std::views::filter([screen](auto* c) {
                           return !screen || c->screen == screen;
                       })) {
            pushclient(c);
        }
    }

    return 1;
}

/** Check if a client is visible on its screen.
 *
 * @treturn boolean A boolean value, true if the client is visible, false otherwise.
 * @method isvisible
 */
static int luaA_client_isvisible(lua_State* L) {
    auto c = client_class.checkudata<client>(L, 1);
    lua_pushboolean(L, client_isvisible(c));
    return 1;
}

/** Set client icons.
 * \param L The Lua VM state.
 * \param array Array of icons to set.
 */
void client_set_icons(client* c, std::vector<cairo_surface_handle> array) {
    c->icons.clear();
    c->icons = std::move(array);

    lua_State* L = globalconf_get_lua_State();
    luaA_object_push(L, c);
    luaA_object_emit_signal(L, -1, "property::icon", 0);
    luaA_object_emit_signal(L, -1, "property::icon_sizes", 0);
    lua_pop(L, 1);
}

/** Set a client icon.
 * \param L The Lua VM state.
 * \param cidx The client index on the stack.
 * \param iidx The image index on the stack.
 */
static void client_set_icon(client* c, cairo_surface_t* s) {
    std::vector<cairo_surface_handle> array;
    if (s && cairo_surface_status(s) == CAIRO_STATUS_SUCCESS) {
        array.emplace_back(draw_dup_image_surface(s));
    }
    client_set_icons(c, std::move(array));
}

/** Set a client icon.
 * \param c The client to change.
 * \param icon A bitmap containing the icon.
 * \param mask A mask for the bitmap (optional)
 */
void client_set_icon_from_pixmaps(client* c, xcb_pixmap_t icon, xcb_pixmap_t mask) {
    cairo_surface_t *s_icon, *result;

    auto geom_icon_c = getConnection().get_geometry_unchecked(icon);
    xcb_get_geometry_cookie_t geom_mask_c;
    if (mask) {
        geom_mask_c = getConnection().get_geometry_unchecked(mask);
    }
    auto geom_icon_r = getConnection().get_geometry_reply(geom_icon_c);
    XCB::reply<xcb_get_geometry_reply_t> geom_mask_r;
    if (mask) {
        geom_mask_r = getConnection().get_geometry_reply(geom_mask_c);
    }

    if (!geom_icon_r || (mask && !geom_mask_r)) {
        return;
    }
    if ((geom_icon_r->depth != 1 && geom_icon_r->depth != Manager::get().screen->root_depth) ||
        (geom_mask_r && geom_mask_r->depth != 1)) {
        log_warn(
          "Got pixmaps with depth ({}, {}) while processing icon, but only depth 1 and {} are "
          "allowed",
          geom_icon_r->depth,
          geom_mask_r ? geom_mask_r->depth : 0,
          Manager::get().screen->root_depth);
        return;
    }

    if (geom_icon_r->depth == 1) {
        s_icon = cairo_xcb_surface_create_for_bitmap(getConnection().getConnection(),
                                                     Manager::get().screen,
                                                     icon,
                                                     geom_icon_r->width,
                                                     geom_icon_r->height);
    } else {
        s_icon = cairo_xcb_surface_create(getConnection().getConnection(),
                                          icon,
                                          Manager::get().default_visual,
                                          geom_icon_r->width,
                                          geom_icon_r->height);
    }
    result = s_icon;

    if (mask) {
        cairo_surface_t* s_mask;
        cairo_t* cr;

        result = cairo_surface_create_similar(
          s_icon, CAIRO_CONTENT_COLOR_ALPHA, geom_icon_r->width, geom_icon_r->height);
        s_mask = cairo_xcb_surface_create_for_bitmap(getConnection().getConnection(),
                                                     Manager::get().screen,
                                                     mask,
                                                     geom_icon_r->width,
                                                     geom_icon_r->height);
        cr = cairo_create(result);

        cairo_set_source_surface(cr, s_icon, 0, 0);
        cairo_mask_surface(cr, s_mask, 0, 0);
        cairo_surface_destroy(s_mask);
        cairo_destroy(cr);
    }

    client_set_icon(c, result);

    cairo_surface_destroy(result);
    if (result != s_icon) {
        cairo_surface_destroy(s_icon);
    }
}

/** Kill a client.
 *
 * This method can be used to close (kill) a **client** using the
 * X11 protocol. To use the POSIX way to kill a **process**, use
 * `awesome.kill` (using the client `pid` property).
 *
 * @DOC_sequences_client_kill1_EXAMPLE@
 *
 * @method kill
 * @noreturn
 * @see awesome.kill
 */
static int luaA_client_kill(lua_State* L) {
    auto c = client_class.checkudata<client>(L, 1);
    client_kill(c);
    return 0;
}

/** Swap a client with another one in global client list.
 *
 * @DOC_sequences_client_swap1_EXAMPLE@
 *
 * @tparam client c A client to swap with.
 * @noreturn
 * @method swap
 * @emits swapped
 * @emitstparam swapped client other The other client.
 * @emitstparam swapped boolean is_origin `true` when `:swap()` was called
 *  on *self* rather than the other client. `false` when
 *  `:swap()` was called on the other client.
 * @emits list
 * @see swapped
 * @see awful.client.swap.bydirection
 * @see awful.client.swap.global_bydirection
 * @see awful.client.swap.byidx
 * @see awful.client.cycle
 */
static int luaA_client_swap(lua_State* L) {
    auto c = client_class.checkudata<client>(L, 1);
    auto swap = client_class.checkudata<client>(L, 2);

    if (c != swap) {
        client **ref_c = NULL, **ref_swap = NULL;
        for (auto*& item : Manager::get().clients) {
            if (item == c) {
                ref_c = &item;
            } else if (item == swap) {
                ref_swap = &item;
            }
            if (ref_c && ref_swap) {
                break;
            }
        }
        /* swap ! */
        *ref_c = swap;
        *ref_swap = c;

        client_class.emit_signal(L, "list", 0);

        luaA_object_push(L, swap);
        lua_pushboolean(L, true);
        luaA_object_emit_signal(L, -4, "swapped", 2);

        luaA_object_push(L, swap);
        luaA_object_push(L, c);
        lua_pushboolean(L, false);
        luaA_object_emit_signal(L, -3, "swapped", 2);
    }

    return 0;
}

/** Access or set the client tags.
 *
 * Use the `first_tag` field to access the first tag of a client directly.
 *
 * @DOC_sequences_client_tags1_EXAMPLE@
 *
 * @tparam table tags_table A table with tags to set, or `nil` to get the
 *   current tags.
 * @treturn table A table with all tags.
 * @method tags
 * @emits property::tags
 * @see first_tag
 * @see toggle_tag
 */
static int luaA_client_tags(lua_State* L) {
    auto c = client_class.checkudata<client>(L, 1);
    int j = 0;

    if (lua_gettop(L) == 2) {
        Lua::checktable(L, 2);
        for (size_t i = 0; i < Manager::get().tags.size(); i++) {
            /* Only untag if we aren't going to add this tag again */
            bool found = false;
            lua_pushnil(L);
            while (lua_next(L, 2)) {
                auto t = (tag_t*)lua_touserdata(L, -1);
                /* Pop the value from lua_next */
                lua_pop(L, 1);
                if (t != Manager::get().tags[i].get()) {
                    continue;
                }

                /* Pop the key from lua_next */
                lua_pop(L, 1);
                found = true;
                break;
            }
            if (!found) {
                untag_client(c, Manager::get().tags[i].get());
            }
        }
        lua_pushnil(L);
        while (lua_next(L, 2)) {
            tag_client(L, c);
        }

        lua_pop(L, 1);

        luaA_object_emit_signal(L, -1, "property::tags", 0);
    }

    lua_newtable(L);
    for (const auto& tag : Manager::get().tags) {
        if (is_client_tagged(c, tag.get())) {
            luaA_object_push(L, tag.get());
            lua_rawseti(L, -2, ++j);
        }
    }

    return 1;
}

/** Get the first tag of a client.
 */
static int luaA_client_get_first_tag(lua_State* L, lua_object_t* o) {
    auto c = static_cast<client*>(o);
    for (const auto& tag : Manager::get().tags) {
        if (is_client_tagged(c, tag.get())) {
            luaA_object_push(L, tag.get());
            return 1;
        }
    }

    return 0;
}

/** Raise a client on top of others which are on the same layer.
 *
 * @method raise
 * @noreturn
 * @emits raised
 * @see above
 * @see below
 * @see ontop
 * @see lower
 */
static int luaA_client_raise(lua_State* L) {
    auto c = client_class.checkudata<client>(L, 1);

    /* Avoid sending the signal if nothing was done */
    if (c->transient_for == NULL && Manager::get().getStack().size() &&
        Manager::get().getStack().back() == c) {
        return 0;
    }

    client_raise(c);

    return 0;
}

/** Lower a client on bottom of others which are on the same layer.
 *
 * @method lower
 * @noreturn
 * @emits lowered
 * @see above
 * @see below
 * @see ontop
 * @see raise
 */
static int luaA_client_lower(lua_State* L) {
    auto c = client_class.checkudata<client>(L, 1);

    /* Avoid sending the signal if nothing was done */
    if (Manager::get().getStack().size() && Manager::get().getStack().front() == c) {
        return 0;
    }

    stack_client_push(c);

    /* Traverse all transient layers. */
    for (client* tc = c->transient_for; tc; tc = tc->transient_for) {
        stack_client_push(tc);
    }

    /* Notify the listeners */
    luaA_object_push(L, c);
    luaA_object_emit_signal(L, -1, "lowered", 0);
    lua_pop(L, 1);

    return 0;
}

/** Stop managing a client.
 *
 * @method unmanage
 * @noreturn
 */
static int luaA_client_unmanage(lua_State* L) {
    auto c = client_class.checkudata<client>(L, 1);
    client_unmanage(c, CLIENT_UNMANAGE_USER);
    return 0;
}

static area_t titlebar_get_area(client* c, client_titlebar_t bar) {
    area_t result = c->geometry;
    result.top_left = {0, 0};

    // Let's try some ascii art:
    // ---------------------------
    // |         Top             |
    // |-------------------------|
    // |L|                     |R|
    // |e|                     |i|
    // |f|                     |g|
    // |t|                     |h|
    // | |                     |t|
    // |-------------------------|
    // |        Bottom           |
    // ---------------------------

    switch (bar) {
    case CLIENT_TITLEBAR_BOTTOM:
        result.top_left.y = c->geometry.height - c->titlebar[bar].size;
        /* Fall through */
    case CLIENT_TITLEBAR_TOP: result.height = c->titlebar[bar].size; break;
    case CLIENT_TITLEBAR_RIGHT:
        result.top_left.x = c->geometry.width - c->titlebar[bar].size;
        /* Fall through */
    case CLIENT_TITLEBAR_LEFT:
        result.top_left.y = c->titlebar[CLIENT_TITLEBAR_TOP].size;
        result.width = c->titlebar[bar].size;
        result.height -= c->titlebar[CLIENT_TITLEBAR_TOP].size;
        result.height -= c->titlebar[CLIENT_TITLEBAR_BOTTOM].size;
        break;
    default: log_fatal("Unknown titlebar kind {}\n", (int)bar);
    }

    return result;
}

drawable_t* client_get_drawable_offset(client* c, point* pt) {
    for (int bar = CLIENT_TITLEBAR_TOP; bar < CLIENT_TITLEBAR_COUNT; bar++) {
        area_t area = titlebar_get_area(c, (client_titlebar_t)bar);
        if (!area.inside(*pt)) {
            continue;
        }

        *pt -= area.top_left;

        return c->titlebar[bar].drawable;
    }

    return NULL;
}

drawable_t* client_get_drawable(client* c, point pt) { return client_get_drawable_offset(c, &pt); }

static void client_refresh_titlebar_partial(
  client* c, client_titlebar_t bar, int16_t x, int16_t y, uint16_t width, uint16_t height) {
    if (c->titlebar[bar].drawable == NULL || c->titlebar[bar].drawable->pixmap == XCB_NONE ||
        !c->titlebar[bar].drawable->refreshed) {
        return;
    }

    /* Is the titlebar part of the area that should get redrawn? */
    area_t area = titlebar_get_area(c, bar);
    if (area.left() >= x + width || area.right() <= x) {
        return;
    }
    if (area.top() >= y + height || area.bottom() <= y) {
        return;
    }

    /* Redraw the affected parts */
    cairo_surface_flush(c->titlebar[bar].drawable->surface);
    getConnection().copy_area(
      c->titlebar[bar].drawable->pixmap,
      c->frame_window,
      Manager::get().gc,
      {static_cast<int16_t>(x - area.left()), static_cast<int16_t>(y - area.top()), width, height},
      {x, y});
}

#define HANDLE_TITLEBAR_REFRESH(name, index)                           \
    static void client_refresh_titlebar_##name(client* c) {            \
        area_t area = titlebar_get_area(c, index);                     \
        client_refresh_titlebar_partial(                               \
          c, index, area.left(), area.top(), area.width, area.height); \
    }
HANDLE_TITLEBAR_REFRESH(top, CLIENT_TITLEBAR_TOP)
HANDLE_TITLEBAR_REFRESH(right, CLIENT_TITLEBAR_RIGHT)
HANDLE_TITLEBAR_REFRESH(bottom, CLIENT_TITLEBAR_BOTTOM)
HANDLE_TITLEBAR_REFRESH(left, CLIENT_TITLEBAR_LEFT)

/**
 * Refresh all titlebars that are in the specified rectangle
 */
void client_refresh_partial(client* c, int16_t x, int16_t y, uint16_t width, uint16_t height) {
    for (int bar = CLIENT_TITLEBAR_TOP; bar < CLIENT_TITLEBAR_COUNT; bar++) {
        client_refresh_titlebar_partial(c, (client_titlebar_t)bar, x, y, width, height);
    }
}

static drawable_t*
titlebar_get_drawable(lua_State* L, client* c, int cl_idx, client_titlebar_t bar) {
    if (c->titlebar[bar].drawable == NULL) {
        cl_idx = Lua::absindex(L, cl_idx);
        switch (bar) {
        case CLIENT_TITLEBAR_TOP:
            drawable_allocator(L, (drawable_refresh_callback*)client_refresh_titlebar_top, c);
            break;
        case CLIENT_TITLEBAR_BOTTOM:
            drawable_allocator(L, (drawable_refresh_callback*)client_refresh_titlebar_bottom, c);
            break;
        case CLIENT_TITLEBAR_RIGHT:
            drawable_allocator(L, (drawable_refresh_callback*)client_refresh_titlebar_right, c);
            break;
        case CLIENT_TITLEBAR_LEFT:
            drawable_allocator(L, (drawable_refresh_callback*)client_refresh_titlebar_left, c);
            break;
        default: log_fatal("Unknown titlebar kind {}\n", (int)bar);
        }
        c->titlebar[bar].drawable = (drawable_t*)luaA_object_ref_item(L, cl_idx, -1);
    }

    return c->titlebar[bar].drawable;
}

static void titlebar_resize(lua_State* L, int cidx, client* c, client_titlebar_t bar, int size) {
    const char* property_name;

    if (size < 0) {
        return;
    }

    if (size == c->titlebar[bar].size) {
        return;
    }

    /* Now resize the client (and titlebars!) suitably (the client without
     * titlebars should keep its current size!) */
    area_t geometry = c->geometry;
    int change = size - c->titlebar[bar].size;
    int16_t diff_top = 0, diff_bottom = 0, diff_right = 0, diff_left = 0;
    switch (bar) {
    case CLIENT_TITLEBAR_TOP:
        geometry.height += change;
        diff_top = change;
        property_name = "property::titlebar_top";
        break;
    case CLIENT_TITLEBAR_BOTTOM:
        geometry.height += change;
        diff_bottom = change;
        property_name = "property::titlebar_bottom";
        break;
    case CLIENT_TITLEBAR_RIGHT:
        geometry.width += change;
        diff_right = change;
        property_name = "property::titlebar_right";
        break;
    case CLIENT_TITLEBAR_LEFT:
        geometry.width += change;
        diff_left = change;
        property_name = "property::titlebar_left";
        break;
    default: log_fatal("Unknown titlebar kind {}\n", (int)bar);
    }

    if (c->size_hints.flags & XCB_ICCCM_SIZE_HINT_P_WIN_GRAVITY) {
        xwindow_translate_for_gravity((xcb_gravity_t)c->size_hints.win_gravity,
                                      diff_left,
                                      diff_top,
                                      diff_right,
                                      diff_bottom,
                                      &geometry.top_left.x,
                                      &geometry.top_left.y);
    }

    c->titlebar[bar].size = size;
    client_resize_do(c, geometry);

    luaA_object_emit_signal(L, cidx, property_name, 0);
}

template <client_titlebar_t N>
static int client_titlebar(lua_State* L) {
    auto c = client_class.checkudata<client>(L, 1);

    if (lua_gettop(L) == 2) {
        int size = lua_isnil(L, 2) ? 0 : ceil(Lua::checknumber_range(L, 2, 0, MAX_X11_SIZE));
        titlebar_resize(L, 1, c, N, size);
    }

    luaA_object_push_item(L, 1, titlebar_get_drawable(L, c, 1, N));
    lua_pushinteger(L, c->titlebar[N].size);
    return 2;
}

/** Return or set client geometry.
 *
 * @DOC_sequences_client_geometry1_EXAMPLE@
 *
 * @tparam table|nil geo A table with new coordinates, or nil.
 * @tparam integer geo.x The horizontal position.
 * @tparam integer geo.y The vertical position.
 * @tparam integer geo.width The width.
 * @tparam integer geo.height The height.
 * @treturn table A table with client geometry and coordinates.
 * @method geometry
 * @see struts
 * @see x
 * @see y
 * @see width
 * @see height
 */
static int luaA_client_geometry(lua_State* L) {
    auto c = client_class.checkudata<client>(L, 1);

    if (lua_gettop(L) == 2 && !lua_isnil(L, 2)) {
        area_t geometry;

        Lua::checktable(L, 2);
        geometry.top_left = {
          (int)round(Lua::getopt_number_range(
            L, 2, "x", c->geometry.top_left.x, MIN_X11_COORDINATE, MAX_X11_COORDINATE)),
          (int)round(Lua::getopt_number_range(
            L, 2, "y", c->geometry.top_left.y, MIN_X11_COORDINATE, MAX_X11_COORDINATE))};
        if (client_isfixed(c)) {
            geometry.width = c->geometry.width;
            geometry.height = c->geometry.height;
        } else {
            geometry.width = ceil(Lua::getopt_number_range(
              L, 2, "width", c->geometry.width, MIN_X11_SIZE, MAX_X11_SIZE));
            geometry.height = ceil(Lua::getopt_number_range(
              L, 2, "height", c->geometry.height, MIN_X11_SIZE, MAX_X11_SIZE));
        }

        client_resize(c, geometry, c->size_hints_honor);
    }

    return Lua::pusharea(L, c->geometry);
}

/** Apply size hints to a size.
 *
 * This method applies the client size hints. The client
 * will be resized according to the size hints as long
 * as `size_hints_honor` is true. Regardless of the
 * status of `size_hints_honor`, this method will
 * return the size with the size hints applied.
 *
 * @tparam integer width Desired width of client
 * @tparam integer height Desired height of client
 * @treturn integer Actual width of client
 * @treturn integer Actual height of client
 * @method apply_size_hints
 * @see size_hints
 * @see size_hints_honor
 */
static int luaA_client_apply_size_hints(lua_State* L) {
    auto c = client_class.checkudata<client>(L, 1);
    area_t geometry = c->geometry;
    if (!client_isfixed(c)) {
        geometry.width = ceil(Lua::checknumber_range(L, 2, MIN_X11_SIZE, MAX_X11_SIZE));
        geometry.height = ceil(Lua::checknumber_range(L, 3, MIN_X11_SIZE, MAX_X11_SIZE));
    }

    if (c->size_hints_honor) {
        geometry = client_apply_size_hints(c, geometry);
    }

    lua_pushinteger(L, geometry.width);
    lua_pushinteger(L, geometry.height);
    return 2;
}

static int luaA_client_set_screen(lua_State* L, lua_object_t* c) {
    screen_client_moveto(static_cast<client*>(c), luaA_checkscreen(L, -1), true);
    return 0;
}

static int luaA_client_set_hidden(lua_State* L, lua_object_t* c) {
    client_set_hidden(L, -3, Lua::checkboolean(L, -1));
    return 0;
}

static int luaA_client_set_minimized(lua_State* L, lua_object_t* c) {
    client_set_minimized(L, -3, Lua::checkboolean(L, -1));
    return 0;
}

static int luaA_client_set_fullscreen(lua_State* L, lua_object_t* c) {
    client_set_fullscreen(L, -3, Lua::checkboolean(L, -1));
    return 0;
}

static int luaA_client_set_modal(lua_State* L, lua_object_t* c) {
    client_set_modal(L, -3, Lua::checkboolean(L, -1));
    return 0;
}

static int luaA_client_set_maximized(lua_State* L, lua_object_t* c) {
    client_set_maximized(L, -3, Lua::checkboolean(L, -1));
    return 0;
}

static int luaA_client_set_maximized_horizontal(lua_State* L, lua_object_t* c) {
    client_set_maximized_horizontal(L, -3, Lua::checkboolean(L, -1));
    return 0;
}

static int luaA_client_set_maximized_vertical(lua_State* L, lua_object_t* c) {
    client_set_maximized_vertical(L, -3, Lua::checkboolean(L, -1));
    return 0;
}

static int luaA_client_set_icon(lua_State* L, lua_object_t* c) {
    cairo_surface_t* surf = NULL;
    if (!lua_isnil(L, -1)) {
        surf = (cairo_surface_t*)lua_touserdata(L, -1);
    }
    client_set_icon(static_cast<client*>(c), surf);
    return 0;
}

static int luaA_client_set_focusable(lua_State* L, lua_object_t* c) {
    if (lua_isnil(L, -1)) {
        client_unset_focusable(L, -3);
    } else {
        client_set_focusable(L, -3, Lua::checkboolean(L, -1));
    }
    return 0;
}

static int luaA_client_set_sticky(lua_State* L, lua_object_t* c) {
    client_set_sticky(L, -3, Lua::checkboolean(L, -1));
    return 0;
}

static int luaA_client_set_size_hints_honor(lua_State* L, lua_object_t* c) {
    static_cast<client*>(c)->size_hints_honor = Lua::checkboolean(L, -1);
    luaA_object_emit_signal(L, -3, "property::size_hints_honor", 0);
    return 0;
}

static int luaA_client_set_ontop(lua_State* L, lua_object_t*) {
    client_set_ontop(L, -3, Lua::checkboolean(L, -1));
    return 0;
}

static int luaA_client_set_below(lua_State* L, lua_object_t*) {
    client_set_below(L, -3, Lua::checkboolean(L, -1));
    return 0;
}

static int luaA_client_set_above(lua_State* L, lua_object_t*) {
    client_set_above(L, -3, Lua::checkboolean(L, -1));
    return 0;
}

static int luaA_client_set_urgent(lua_State* L, lua_object_t*) {
    client_set_urgent(L, -3, Lua::checkboolean(L, -1));
    return 0;
}

static int luaA_client_set_skip_taskbar(lua_State* L, lua_object_t*) {
    client_set_skip_taskbar(L, -3, Lua::checkboolean(L, -1));
    return 0;
}

static int luaA_client_get_name(lua_State* L, lua_object_t* o) {
    auto c = static_cast<client*>(o);
    lua_pushstring(L, !c->getName().empty() ? c->getName().c_str() : c->getAltName().c_str());
    return 1;
}

/** Set the client name.
 * \param L The Lua VM state.
 * \param client The client to name.
 * \return The number of elements pushed on stack.
 */
static int luaA_client_set_name(lua_State* L, lua_object_t*) {
    const auto name = Lua::checkstring(L, -1);
    client_set_Name(L, 1, std::string{name->begin(), name->end()});
    return 0;
}

static int luaA_client_get_icon_name(lua_State* L, lua_object_t* o) {
    auto c = static_cast<client*>(o);
    lua_pushstring(
      L, !c->getIconName().empty() ? c->getIconName().c_str() : c->getAltIconName().c_str());
    return 1;
}

static int luaA_client_set_startup_id(lua_State* L, lua_object_t*) {
    const char* startup_id = luaL_checkstring(L, -1);
    client_set_StartupId(L, 1, startup_id);
    return 0;
}

static int luaA_client_get_motif_wm_hints(lua_State* L, lua_object_t* o) {
    auto c = static_cast<client*>(o);
    if (!(c->motif_wm_hints.hints & MWM_HINTS_AWESOME_SET)) {
        return 0;
    }

    lua_newtable(L);

#define HANDLE_BIT(field, flag, name)                      \
    do {                                                   \
        lua_pushboolean(L, c->motif_wm_hints.field& flag); \
        lua_setfield(L, -2, name);                         \
    } while (0)

    if (c->motif_wm_hints.hints & MWM_HINTS_FUNCTIONS) {
        lua_newtable(L);
        HANDLE_BIT(functions, MWM_FUNC_ALL, "all");
        HANDLE_BIT(functions, MWM_FUNC_RESIZE, "resize");
        HANDLE_BIT(functions, MWM_FUNC_MOVE, "move");
        HANDLE_BIT(functions, MWM_FUNC_MINIMIZE, "minimize");
        HANDLE_BIT(functions, MWM_FUNC_MAXIMIZE, "maximize");
        HANDLE_BIT(functions, MWM_FUNC_CLOSE, "close");
        lua_setfield(L, -2, "functions");
    }

    if (c->motif_wm_hints.hints & MWM_HINTS_DECORATIONS) {
        lua_newtable(L);
        HANDLE_BIT(decorations, MWM_DECOR_ALL, "all");
        HANDLE_BIT(decorations, MWM_DECOR_BORDER, "border");
        HANDLE_BIT(decorations, MWM_DECOR_RESIZEH, "resizeh");
        HANDLE_BIT(decorations, MWM_DECOR_TITLE, "title");
        HANDLE_BIT(decorations, MWM_DECOR_MENU, "menu");
        HANDLE_BIT(decorations, MWM_DECOR_MINIMIZE, "minimize");
        HANDLE_BIT(decorations, MWM_DECOR_MAXIMIZE, "maximize");
        lua_setfield(L, -2, "decorations");
    }

    if (c->motif_wm_hints.hints & MWM_HINTS_INPUT_MODE) {
        switch (c->motif_wm_hints.input_mode) {
        case MWM_INPUT_MODELESS: lua_pushliteral(L, "modeless"); break;
        case MWM_INPUT_PRIMARY_APPLICATION_MODAL:
            lua_pushliteral(L, "primary_application_modal");
            break;
        case MWM_INPUT_SYSTEM_MODAL: lua_pushliteral(L, "system_modal"); break;
        case MWM_INPUT_FULL_APPLICATION_MODAL: lua_pushliteral(L, "full_application_modal"); break;
        default: lua_pushfstring(L, "unknown (%d)", (int)c->motif_wm_hints.input_mode); break;
        }
        lua_setfield(L, -2, "input_mode");
    }

    if (c->motif_wm_hints.hints & MWM_HINTS_STATUS) {
        lua_newtable(L);
        HANDLE_BIT(status, MWM_TEAROFF_WINDOW, "tearoff_window");
        lua_setfield(L, -2, "status");
    }

#undef HANDLE_BIT

    return 1;
}

static int luaA_client_get_content(lua_State* L, lua_object_t* o) {
    auto c = static_cast<client*>(o);
    cairo_surface_t* surface;
    int width = c->geometry.width;
    int height = c->geometry.height;

    /* Just the client size without decorations */
    width -= c->titlebar[CLIENT_TITLEBAR_LEFT].size + c->titlebar[CLIENT_TITLEBAR_RIGHT].size;
    height -= c->titlebar[CLIENT_TITLEBAR_TOP].size + c->titlebar[CLIENT_TITLEBAR_BOTTOM].size;

    surface = cairo_xcb_surface_create(
      getConnection().getConnection(), c->window, c->visualtype, width, height);

    /* lua has to make sure to free the ref or we have a leak */
    lua_pushlightuserdata(L, surface);
    return 1;
}

static int luaA_client_get_icon(lua_State* L, lua_object_t* o) {
    auto c = static_cast<client*>(o);
    if (c->icons.empty()) {
        return 0;
    }

    /* Pick the closest available size, only picking a smaller icon if no bigger
     * one is available.
     */
    cairo_surface_t* found = NULL;
    int found_size = 0;
    int preferred_size = Manager::get().preferred_icon_size;

    for (auto& surf : c->icons) {
        int width = cairo_image_surface_get_width(surf.get());
        int height = cairo_image_surface_get_height(surf.get());
        int size = MAX(width, height);

        /* pick the icon if it's a better match than the one we already have */
        bool found_icon_too_small = found_size < preferred_size;
        bool found_icon_too_large = found_size > preferred_size;
        bool icon_empty = width == 0 || height == 0;
        bool better_because_bigger = found_icon_too_small && size > found_size;
        bool better_because_smaller =
          found_icon_too_large && size >= preferred_size && size < found_size;
        if (!icon_empty && (better_because_bigger || better_because_smaller || found_size == 0)) {
            found = surf.get();
            found_size = size;
        }
    }

    /* lua gets its own reference which it will have to destroy */
    lua_pushlightuserdata(L, cairo_surface_reference(found));
    return 1;
}

static int luaA_client_get_focusable(lua_State* L, lua_object_t* o) {
    auto* c = static_cast<client*>(o);

    if (c->focusable.has_value()) {
        lua_pushboolean(L, c->focusable.value());
        return 1;
    }

    lua_pushboolean(L, !c->nofocus || client_hasproto(c, WM_TAKE_FOCUS));
    return 1;
}

static int luaA_client_get_size_hints(lua_State* L, lua_object_t* o) {
    auto c = static_cast<client*>(o);
    const char* u_or_p = NULL;

    lua_createtable(L, 0, 1);

    if (c->size_hints.flags & XCB_ICCCM_SIZE_HINT_US_POSITION) {
        u_or_p = "user_position";
    } else if (c->size_hints.flags & XCB_ICCCM_SIZE_HINT_P_POSITION) {
        u_or_p = "program_position";
    }

    if (u_or_p) {
        lua_createtable(L, 0, 2);
        lua_pushinteger(L, c->size_hints.x);
        lua_setfield(L, -2, "x");
        lua_pushinteger(L, c->size_hints.y);
        lua_setfield(L, -2, "y");
        lua_setfield(L, -2, u_or_p);
        u_or_p = NULL;
    }

    if (c->size_hints.flags & XCB_ICCCM_SIZE_HINT_US_SIZE) {
        u_or_p = "user_size";
    } else if (c->size_hints.flags & XCB_ICCCM_SIZE_HINT_P_SIZE) {
        u_or_p = "program_size";
    }

    if (u_or_p) {
        lua_createtable(L, 0, 2);
        lua_pushinteger(L, c->size_hints.width);
        lua_setfield(L, -2, "width");
        lua_pushinteger(L, c->size_hints.height);
        lua_setfield(L, -2, "height");
        lua_setfield(L, -2, u_or_p);
    }

    if (c->size_hints.flags & XCB_ICCCM_SIZE_HINT_P_MIN_SIZE) {
        lua_pushinteger(L, c->size_hints.min_width);
        lua_setfield(L, -2, "min_width");
        lua_pushinteger(L, c->size_hints.min_height);
        lua_setfield(L, -2, "min_height");
    }

    if (c->size_hints.flags & XCB_ICCCM_SIZE_HINT_P_MAX_SIZE) {
        lua_pushinteger(L, c->size_hints.max_width);
        lua_setfield(L, -2, "max_width");
        lua_pushinteger(L, c->size_hints.max_height);
        lua_setfield(L, -2, "max_height");
    }

    if (c->size_hints.flags & XCB_ICCCM_SIZE_HINT_P_RESIZE_INC) {
        lua_pushinteger(L, c->size_hints.width_inc);
        lua_setfield(L, -2, "width_inc");
        lua_pushinteger(L, c->size_hints.height_inc);
        lua_setfield(L, -2, "height_inc");
    }

    if (c->size_hints.flags & XCB_ICCCM_SIZE_HINT_P_ASPECT) {
        lua_pushinteger(L, c->size_hints.min_aspect_num);
        lua_setfield(L, -2, "min_aspect_num");
        lua_pushinteger(L, c->size_hints.min_aspect_den);
        lua_setfield(L, -2, "min_aspect_den");
        lua_pushinteger(L, c->size_hints.max_aspect_num);
        lua_setfield(L, -2, "max_aspect_num");
        lua_pushinteger(L, c->size_hints.max_aspect_den);
        lua_setfield(L, -2, "max_aspect_den");
    }

    if (c->size_hints.flags & XCB_ICCCM_SIZE_HINT_BASE_SIZE) {
        lua_pushinteger(L, c->size_hints.base_width);
        lua_setfield(L, -2, "base_width");
        lua_pushinteger(L, c->size_hints.base_height);
        lua_setfield(L, -2, "base_height");
    }

    if (c->size_hints.flags & XCB_ICCCM_SIZE_HINT_P_WIN_GRAVITY) {
        switch (c->size_hints.win_gravity) {
        default: lua_pushliteral(L, "north_west"); break;
        case XCB_GRAVITY_NORTH: lua_pushliteral(L, "north"); break;
        case XCB_GRAVITY_NORTH_EAST: lua_pushliteral(L, "north_east"); break;
        case XCB_GRAVITY_WEST: lua_pushliteral(L, "west"); break;
        case XCB_GRAVITY_CENTER: lua_pushliteral(L, "center"); break;
        case XCB_GRAVITY_EAST: lua_pushliteral(L, "east"); break;
        case XCB_GRAVITY_SOUTH_WEST: lua_pushliteral(L, "south_west"); break;
        case XCB_GRAVITY_SOUTH: lua_pushliteral(L, "south"); break;
        case XCB_GRAVITY_SOUTH_EAST: lua_pushliteral(L, "south_east"); break;
        case XCB_GRAVITY_STATIC: lua_pushliteral(L, "static"); break;
        }
        lua_setfield(L, -2, "win_gravity");
    }

    return 1;
}

/** Get the client's child window bounding shape.
 * \param L The Lua VM state.
 * \param client The client object.
 * \return The number of elements pushed on stack.
 */
static int luaA_client_get_client_shape_bounding(lua_State* L, lua_object_t* o) {
    auto c = static_cast<client*>(o);
    cairo_surface_t* surf = xwindow_get_shape(c->window, XCB_SHAPE_SK_BOUNDING);
    if (!surf) {
        return 0;
    }
    /* lua has to make sure to free the ref or we have a leak */
    lua_pushlightuserdata(L, surf);
    return 1;
}

/** Get the client's frame window bounding shape.
 * \param L The Lua VM state.
 * \param client The client object.
 * \return The number of elements pushed on stack.
 */
static int luaA_client_get_shape_bounding(lua_State* L, lua_object_t* o) {
    auto c = static_cast<client*>(o);
    cairo_surface_t* surf = xwindow_get_shape(c->frame_window, XCB_SHAPE_SK_BOUNDING);
    if (!surf) {
        return 0;
    }
    /* lua has to make sure to free the ref or we have a leak */
    lua_pushlightuserdata(L, surf);
    return 1;
}

/** Set the client's frame window bounding shape.
 * \param L The Lua VM state.
 * \param client The client object.
 * \return The number of elements pushed on stack.
 */
static int luaA_client_set_shape_bounding(lua_State* L, lua_object_t* o) {
    auto c = static_cast<client*>(o);
    cairo_surface_t* surf = NULL;
    if (!lua_isnil(L, -1)) {
        surf = (cairo_surface_t*)lua_touserdata(L, -1);
    }
    xwindow_set_shape(c->frame_window,
                      c->geometry.width + (c->border_width * 2),
                      c->geometry.height + (c->border_width * 2),
                      XCB_SHAPE_SK_BOUNDING,
                      surf,
                      -c->border_width);
    luaA_object_emit_signal(L, -3, "property::shape_bounding", 0);
    return 0;
}

/** Get the client's child window clip shape.
 * \param L The Lua VM state.
 * \param client The client object.
 * \return The number of elements pushed on stack.
 */
static int luaA_client_get_client_shape_clip(lua_State* L, lua_object_t* o) {
    auto c = static_cast<client*>(o);
    cairo_surface_t* surf = xwindow_get_shape(c->window, XCB_SHAPE_SK_CLIP);
    if (!surf) {
        return 0;
    }
    /* lua has to make sure to free the ref or we have a leak */
    lua_pushlightuserdata(L, surf);
    return 1;
}

/** Get the client's frame window clip shape.
 * \param L The Lua VM state.
 * \param client The client object.
 * \return The number of elements pushed on stack.
 */
static int luaA_client_get_shape_clip(lua_State* L, lua_object_t* o) {
    auto c = static_cast<client*>(o);
    cairo_surface_t* surf = xwindow_get_shape(c->frame_window, XCB_SHAPE_SK_CLIP);
    if (!surf) {
        return 0;
    }
    /* lua has to make sure to free the ref or we have a leak */
    lua_pushlightuserdata(L, surf);
    return 1;
}

/** Set the client's frame window clip shape.
 * \param L The Lua VM state.
 * \param client The client object.
 * \return The number of elements pushed on stack.
 */
static int luaA_client_set_shape_clip(lua_State* L, lua_object_t* o) {
    auto c = static_cast<client*>(o);
    cairo_surface_t* surf = NULL;
    if (!lua_isnil(L, -1)) {
        surf = (cairo_surface_t*)lua_touserdata(L, -1);
    }
    xwindow_set_shape(
      c->frame_window, c->geometry.width, c->geometry.height, XCB_SHAPE_SK_CLIP, surf, 0);
    luaA_object_emit_signal(L, -3, "property::shape_clip", 0);
    return 0;
}

/** Get the client's frame window input shape.
 * \param L The Lua VM state.
 * \param client The client object.
 * \return The number of elements pushed on stack.
 */
static int luaA_client_get_shape_input(lua_State* L, lua_object_t* o) {
    auto c = static_cast<client*>(o);
    cairo_surface_t* surf = xwindow_get_shape(c->frame_window, XCB_SHAPE_SK_INPUT);
    if (!surf) {
        return 0;
    }
    /* lua has to make sure to free the ref or we have a leak */
    lua_pushlightuserdata(L, surf);
    return 1;
}

/** Set the client's frame window input shape.
 * \param L The Lua VM state.
 * \param client The client object.
 * \return The number of elements pushed on stack.
 */
static int luaA_client_set_shape_input(lua_State* L, lua_object_t* o) {
    auto c = static_cast<client*>(o);
    cairo_surface_t* surf = NULL;
    if (!lua_isnil(L, -1)) {
        surf = (cairo_surface_t*)lua_touserdata(L, -1);
    }
    xwindow_set_shape(c->frame_window,
                      c->geometry.width + (c->border_width * 2),
                      c->geometry.height + (c->border_width * 2),
                      XCB_SHAPE_SK_INPUT,
                      surf,
                      -c->border_width);
    luaA_object_emit_signal(L, -3, "property::shape_input", 0);
    return 0;
}

/** Get or set keys bindings for a client.
 *
 * @property keys
 * @tparam[opt={}] table keys
 * @tablerowtype A list of `awful.key`s objects.
 * @propemits false false
 * @see awful.key
 * @see append_keybinding
 * @see remove_keybinding
 * @see request::default_keybindings
 */
static int luaA_client_keys(lua_State* L) {
    auto c = client_class.checkudata<client>(L, 1);
    auto& keys = c->keys;

    if (lua_gettop(L) == 2) {
        luaA_key_array_set(L, 1, 2, &keys);
        luaA_object_emit_signal(L, 1, "property::keys", 0);
        xwindow_grabkeys(c->window, keys);
        if (c->nofocus_window) {
            xwindow_grabkeys(c->nofocus_window, c->keys);
        }
    }

    return luaA_key_array_get(L, 1, keys);
}

static int luaA_client_get_icon_sizes(lua_State* L, lua_object_t* o) {
    auto c = static_cast<client*>(o);
    int index = 1;

    lua_newtable(L);
    for (auto& s : c->icons) {
        /* Create a table { width, height } and append it to the table */
        lua_createtable(L, 2, 0);

        lua_pushinteger(L, cairo_image_surface_get_width(s.get()));
        lua_rawseti(L, -2, 1);

        lua_pushinteger(L, cairo_image_surface_get_height(s.get()));
        lua_rawseti(L, -2, 2);

        lua_rawseti(L, -2, index++);
    }
    return 1;
}

/** Get the client's n-th icon.
 *
 * The icon index can be deternined by inspecting the `icon_sizes` property first.
 *
 * The user has the responsibility to test the value returned by this function
 * to ensure an icon have been returned.
 *
 * It is recommended to use the `awful.widget.clienticon` widget when the
 * client icon is used in a widget structure.
 *
 * Note that this function tests the provided index and raise an "invalid icon
 * index" error if the provided index doesn't exist in the client's icons list
 * (by raising an error, the function will be stopped and nothing will be
 * returned to the caller).
 *
 * @tparam integer index The index in the list of icons to get.
 * @treturn surface A lightuserdata for a cairo surface. This reference must be
 * destroyed!
 * @method get_icon
 * @see icon_sizes
 * @see awful.widget.clienticon
 */
static int luaA_client_get_some_icon(lua_State* L) {
    auto c = client_class.checkudata<client>(L, 1);
    int index = luaL_checkinteger(L, 2);
    luaL_argcheck(L, (index >= 1 && index <= (int)c->icons.size()), 2, "invalid icon index");
    lua_pushlightuserdata(L, cairo_surface_reference(c->icons[index - 1].get()));
    return 1;
}

static int client_tostring(lua_State* L, lua_object_t* o) {
    auto c = static_cast<client*>(o);
    const auto& name = c->getName().empty() ? c->getAltName() : c->getName();
    ssize_t limit = 20;

    lua_pushlstring(L, name.c_str(), MIN((ssize_t)name.size(), limit));
    if ((ssize_t)name.size() > limit) {
        lua_pushstring(L, "...");
        return 2;
    }
    return 1;
}

/* Client module.
 * \param L The Lua VM state.
 * \return The number of pushed elements.
 */
static int luaA_client_module_index(lua_State* L) {
    auto buf = Lua::checkstring(L, 2);

    if (buf == "focus") {
        return luaA_object_push(L, Manager::get().focus.client);
    }
    return 0;
}

/* Client module new index.
 * \param L The Lua VM state.
 * \return The number of pushed elements.
 */
static int luaA_client_module_newindex(lua_State* L) {
    if (Lua::checkstring(L, 2) == "focus") {
        auto c = !lua_isnil(L, 3) ? client_class.checkudata<client>(L, 3) : nullptr;
        if (c) {
            client_focus(c);
        } else if (Manager::get().focus.client) {
            client_unfocus(Manager::get().focus.client);
        }
    }
    return 0;
}

void client_class_setup(lua_State* L) {
    static constexpr auto methods = DefineClassMethods<&client_class>({
      {       "get",             luaA_client_get},
      {   "__index",    luaA_client_module_index},
      {"__newindex", luaA_client_module_newindex}
    });

    static constexpr auto meta = DefineObjectMethods({
      {           "_keys",                        luaA_client_keys},
      {       "isvisible",                   luaA_client_isvisible},
      {        "geometry",                    luaA_client_geometry},
      {"apply_size_hints",            luaA_client_apply_size_hints},
      {            "tags",                        luaA_client_tags},
      {            "kill",                        luaA_client_kill},
      {            "swap",                        luaA_client_swap},
      {           "raise",                       luaA_client_raise},
      {           "lower",                       luaA_client_lower},
      {        "unmanage",                    luaA_client_unmanage},
      {    "titlebar_top",    client_titlebar<CLIENT_TITLEBAR_TOP>},
      {  "titlebar_right",  client_titlebar<CLIENT_TITLEBAR_RIGHT>},
      { "titlebar_bottom", client_titlebar<CLIENT_TITLEBAR_BOTTOM>},
      {   "titlebar_left",   client_titlebar<CLIENT_TITLEBAR_LEFT>},
      {        "get_icon",               luaA_client_get_some_icon}
    });

    client_class.setup(L, methods.data(), meta.data());

    client_class.set_tostring(client_tostring);
    client_class.add_property(
      "name", luaA_client_set_name, exportProp<&client::getName>(), luaA_client_set_name);
    client_class.add_property("transient_for", NULL, exportProp<&client::transient_for>(), NULL);
    client_class.add_property("skip_taskbar",
                              luaA_client_set_skip_taskbar,
                              exportProp<&client::skip_taskbar>(),
                              luaA_client_set_skip_taskbar);
    client_class.add_property("content", NULL, luaA_client_get_content, NULL);
    client_class.add_property("type", NULL, exportProp<&client::type>(), NULL);
    client_class.add_property("class", NULL, exportProp<&client::getCls>(), NULL);
    client_class.add_property("instance", NULL, exportProp<&client::getInstance>(), NULL);
    client_class.add_property("role", NULL, exportProp<&client::getRole>(), NULL);
    client_class.add_property("pid", NULL, exportProp<&client::pid, 0>(), NULL);
    client_class.add_property("leader_window", NULL, exportProp<&client::leader_window>(), NULL);
    client_class.add_property(
      "machine", NULL, exportProp<&client::getMachine, [] { return ""; }>(), NULL);
    client_class.add_property("icon_name", NULL, luaA_client_get_icon_name, NULL);
    client_class.add_property(
      "screen", NULL, exportProp<&client::screen, nullptr>(), luaA_client_set_screen);
    client_class.add_property(
      "hidden", luaA_client_set_hidden, exportProp<&client::hidden>(), luaA_client_set_hidden);
    client_class.add_property("minimized",
                              luaA_client_set_minimized,
                              exportProp<&client::minimized>(),
                              luaA_client_set_minimized);
    client_class.add_property("fullscreen",
                              luaA_client_set_fullscreen,
                              exportProp<&client::fullscreen>(),
                              luaA_client_set_fullscreen);
    client_class.add_property(
      "modal", luaA_client_set_modal, exportProp<&client::modal>(), luaA_client_set_modal);
    client_class.add_property("motif_wm_hints", NULL, luaA_client_get_motif_wm_hints, NULL);
    client_class.add_property("group_window", NULL, exportProp<&client::group_window>(), NULL);
    client_class.add_property("maximized",
                              luaA_client_set_maximized,
                              exportProp<&client::maximized>(),
                              luaA_client_set_maximized);
    client_class.add_property("maximized_horizontal",
                              luaA_client_set_maximized_horizontal,
                              exportProp<&client::maximized_horizontal>(),
                              luaA_client_set_maximized_horizontal);
    client_class.add_property("maximized_vertical",
                              luaA_client_set_maximized_vertical,
                              exportProp<&client::maximized_vertical>(),
                              luaA_client_set_maximized_vertical);
    client_class.add_property(
      "icon", luaA_client_set_icon, luaA_client_get_icon, luaA_client_set_icon);
    client_class.add_property("icon_sizes", NULL, luaA_client_get_icon_sizes, NULL);
    client_class.add_property(
      "ontop", luaA_client_set_ontop, exportProp<&client::ontop>(), luaA_client_set_ontop);
    client_class.add_property(
      "above", luaA_client_set_above, exportProp<&client::above>(), luaA_client_set_above);
    client_class.add_property(
      "below", luaA_client_set_below, exportProp<&client::below>(), luaA_client_set_below);
    client_class.add_property(
      "sticky", luaA_client_set_sticky, exportProp<&client::sticky>(), luaA_client_set_sticky);
    client_class.add_property("size_hints_honor",
                              luaA_client_set_size_hints_honor,
                              exportProp<&client::size_hints_honor>(),
                              luaA_client_set_size_hints_honor);
    client_class.add_property(
      "urgent", luaA_client_set_urgent, exportProp<&client::urgent>(), luaA_client_set_urgent);
    client_class.add_property("size_hints", NULL, luaA_client_get_size_hints, NULL);
    client_class.add_property(
      "focusable", luaA_client_set_focusable, luaA_client_get_focusable, luaA_client_set_focusable);
    client_class.add_property("shape_bounding",
                              luaA_client_set_shape_bounding,
                              luaA_client_get_shape_bounding,
                              luaA_client_set_shape_bounding);
    client_class.add_property("shape_clip",
                              luaA_client_set_shape_clip,
                              luaA_client_get_shape_clip,
                              luaA_client_set_shape_clip);
    client_class.add_property("shape_input",
                              luaA_client_set_shape_input,
                              luaA_client_get_shape_input,
                              luaA_client_set_shape_input);
    client_class.add_property(
      "client_shape_bounding", NULL, luaA_client_get_client_shape_bounding, NULL);
    client_class.add_property("startup_id",
                              luaA_client_set_startup_id,
                              exportProp<&client::getStartupId>(),
                              luaA_client_set_startup_id);
    client_class.add_property("client_shape_clip", NULL, luaA_client_get_client_shape_clip, NULL);
    client_class.add_property("first_tag", NULL, luaA_client_get_first_tag, NULL);
}

/* @DOC_cobject_COMMON@ */

/* @DOC_client_theme_COMMON@ */
