/*
 * awesome.c - awesome main functions
 *
 * Copyright © 2007-2008 Julien Danjou <julien@danjou.info>
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

#include "awesome.h"

#include "banning.h"
#include "common/atoms.h"
#include "common/backtrace.h"
#include "common/version.h"
#include "common/xutil.h"
#include "dbus.h"
#include "event.h"
#include "ewmh.h"
#include "globalconf.h"
#include "objects/client.h"
#include "objects/screen.h"
#include "options.h"
#include "spawn.h"
#include "systray.h"
#include "xcbcpp/xcb.h"
#include "xkb.h"
#include "xwindow.h"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <filesystem>
#include <getopt.h>
#include <glib-unix.h>
#include <iterator>
#include <locale.h>
#include <ranges>
#include <signal.h>
#include <stdio.h>
#include <sys/time.h>
#include <unistd.h>
#include <uv.h>
#include <xcb/bigreq.h>
#include <xcb/randr.h>
#include <xcb/shape.h>
#include <xcb/xcb_atom.h>
#include <xcb/xcb_aux.h>
#include <xcb/xcb_event.h>
#include <xcb/xcb_icccm.h>
#include <xcb/xfixes.h>
#include <xcb/xinerama.h>
#include <xcb/xproto.h>
#include <xcb/xtest.h>

static Globals* gGlobals = nullptr;
Globals& getGlobals() {
    assert(gGlobals != nullptr);
    return *gGlobals;
}

XCB::Connection& getConnection() { return getGlobals()._connection; }

/** argv used to run awesome */
static char** awesome_argv;

/** time of last main loop wakeup */
static struct timeval last_wakeup;

/** current limit for the main loop's runtime */
static float main_loop_iteration_limit = 0.1;

/** A pipe that is used to asynchronously handle SIGCHLD */
static int sigchld_pipe[2];

/* Initialise various random number generators */
static void init_rng(void) {
    /* LuaJIT uses its own, internal RNG, so initialise that */
    lua_State* L = globalconf_get_lua_State();

    /* Get math.randomseed */
    lua_getglobal(L, "math");
    lua_getfield(L, -1, "randomseed");

    /* Push a seed */
    lua_pushnumber(L, g_random_int());

    /* Call math.randomseed */
    if (lua_pcall(L, 1, 0, 0)) {
        warn("Random number generator initialization failed: %s", lua_tostring(L, -1));
        /* Remove error function and error string */
        lua_pop(L, 2);
        return;
    }

    /* Remove "math" global */
    lua_pop(L, 1);

    /* Lua 5.1, Lua 5.2, and (sometimes) Lua 5.3 use rand()/srand() */
    srand(g_random_int());

    /* When Lua 5.3 is built with LUA_USE_POSIX, it uses random()/srandom() */
    srandom(g_random_int());
}

/** Call before exiting.
 */
void awesome_atexit(bool restart) {
    lua_State* L = globalconf_get_lua_State();
    lua_pushboolean(L, restart);
    signal_object_emit(L, &global_signals, "exit", 1);

    /* Move clients where we want them to be and keep the stacking order intact */
    for (auto* c : getGlobals().getStack()) {
        area_t geometry = client_get_undecorated_geometry(c);
        getConnection().reparent_window(
          c->window, getGlobals().screen->root, geometry.x, geometry.y);
    }

    /* Save the client order.  This is useful also for "hard" restarts. */
    xcb_window_t* wins = p_alloca(xcb_window_t, getGlobals().clients.size());
    size_t n = 0;
    for (auto* client : getGlobals().clients) {
        wins[n++] = client->window;
    }

    getConnection().replace_property(
      getGlobals().screen->root, AWESOME_CLIENT_ORDER, XCB_ATOM_WINDOW, std::span{wins, n});

    a_dbus_cleanup();

    systray_cleanup();

    /* Close Lua */
    lua_close(L);

    screen_cleanup();

    /* X11 is a great protocol. There is a save-set so that reparenting WMs
     * don't kill clients when they shut down. However, when a focused windows
     * is saved, the focus will move to its parent with revert-to none.
     * Immediately afterwards, this parent is destroyed and the focus is gone.
     * Work around this by placing the focus where we like it to be.
     */
    xcb_set_input_focus(getGlobals().connection,
                        XCB_INPUT_FOCUS_POINTER_ROOT,
                        XCB_NONE,
                        getGlobals().get_timestamp());
    xcb_aux_sync(getGlobals().connection);

    xkb_free();

    /* Disconnect *after* closing lua */
    xcb_cursor_context_free(getGlobals().cursor_ctx);
#ifdef WITH_XCB_ERRORS
    xcb_errors_context_free(globalconf.errors_ctx);
#endif
    xcb_disconnect(getGlobals().connection);

    close(sigchld_pipe[0]);
    close(sigchld_pipe[1]);
}

/** Restore the client order after a restart */
static void restore_client_order(xcb_get_property_cookie_t prop_cookie) {
    int client_idx = 0;
    xcb_window_t* windows;

    auto reply = getConnection().get_property_reply(prop_cookie);
    if (!reply || reply->format != 32 || reply->value_len == 0) {
        return;
    }

    windows = (xcb_window_t*)xcb_get_property_value(reply.get());
    for (uint32_t i = 0; i < reply->value_len; i++) {
        //   Find windows[i] and swap it to where it belongs
        for (auto*& c : getGlobals().clients) {
            if (c->window == windows[i]) {
                client* tmp = c;
                c = getGlobals().clients[client_idx];
                getGlobals().clients[client_idx] = tmp;
                client_idx++;
            }
        }
    }

    luaA_class_emit_signal(globalconf_get_lua_State(), &client_class, "list", 0);
}
/** Scan X to find windows to manage.
 */
static void scan(xcb_query_tree_cookie_t tree_c) {
    auto& conn = getConnection();
    auto tree_r = conn.query_tree_reply(tree_c);

    if (!tree_r) {
        return;
    }

    /* This gets the property and deletes it */
    auto prop_cookie = conn.get_property_unchecked(
      true, getGlobals().screen->root, AWESOME_CLIENT_ORDER, XCB_ATOM_WINDOW, 0, UINT_MAX);
    auto wins = conn.query_tree_children(tree_r);
    /* Get the tree of the children windows of the current root window */
    if (!wins) {
        fatal("cannot get tree children");
    }

    std::vector winparams = wins.value() | std::ranges::views::transform([&conn](const auto& v) {
                                return std::tuple{v,
                                                  conn.get_window_attributes_unckecked(v),
                                                  xwindow_get_state_unchecked(v),
                                                  conn.get_geometry_unchecked(v)};
                            }) |
                            range::to<std::vector>{};

    auto clients_to_manage = winparams | std::ranges::views::transform([&conn](const auto& v) {
                                 auto& [win, attr_c, state_c, geo_c] = v;
                                 return std::tuple{
                                   win,
                                   conn.get_window_attributes_reply(attr_c),
                                   conn.get_geometry_reply(geo_c),
                                   xwindow_get_state_reply(state_c),
                                 };
                             });

    for (const auto& [win, attr_r, geom_r, state] : clients_to_manage) {
        if (geom_r && attr_r && !attr_r->override_redirect &&
            attr_r->map_state != XCB_MAP_STATE_UNMAPPED && state != XCB_ICCCM_WM_STATE_WITHDRAWN) {
            client_manage(win, geom_r.get(), attr_r.get());
        }
    }

    restore_client_order(prop_cookie);
}

static void acquire_WM_Sn(bool replace) {
    xcb_intern_atom_cookie_t atom_q;
    xcb_intern_atom_reply_t* atom_r;
    char* atom_name;
    xcb_get_selection_owner_reply_t* get_sel_reply;

    /* Get the WM_Sn atom */
    getGlobals().selection_owner_window = getConnection().generate_id();
    xcb_create_window(getGlobals().connection,
                      getGlobals().screen->root_depth,
                      getGlobals().selection_owner_window,
                      getGlobals().screen->root,
                      -1,
                      -1,
                      1,
                      1,
                      0,
                      XCB_COPY_FROM_PARENT,
                      getGlobals().screen->root_visual,
                      0,
                      NULL);
    xwindow_set_class_instance(getGlobals().selection_owner_window);
    xwindow_set_name_static(getGlobals().selection_owner_window,
                            "Awesome WM_Sn selection owner window");

    atom_name = xcb_atom_name_by_screen("WM_S", getGlobals().default_screen);
    if (!atom_name) {
        fatal("error getting WM_Sn atom name");
    }

    atom_q =
      xcb_intern_atom_unchecked(getGlobals().connection, false, a_strlen(atom_name), atom_name);

    p_delete(&atom_name);

    atom_r = xcb_intern_atom_reply(getGlobals().connection, atom_q, NULL);
    if (!atom_r) {
        fatal("error getting WM_Sn atom");
    }

    getGlobals().selection_atom = atom_r->atom;
    p_delete(&atom_r);

    /* Is the selection already owned? */
    get_sel_reply = xcb_get_selection_owner_reply(
      getGlobals().connection,
      xcb_get_selection_owner(getGlobals().connection, getGlobals().selection_atom),
      NULL);
    if (!get_sel_reply) {
        fatal("GetSelectionOwner for WM_Sn failed");
    }
    if (!replace && get_sel_reply->owner != XCB_NONE) {
        fatal("another window manager is already running (selection owned; use --replace)");
    }

    /* Acquire the selection */
    xcb_set_selection_owner(getGlobals().connection,
                            getGlobals().selection_owner_window,
                            getGlobals().selection_atom,
                            getGlobals().get_timestamp());
    if (get_sel_reply->owner != XCB_NONE) {
        /* Wait for the old owner to go away */
        xcb_get_geometry_reply_t* geom_reply = NULL;
        do {
            p_delete(&geom_reply);
            geom_reply = xcb_get_geometry_reply(
              getGlobals().connection,
              xcb_get_geometry(getGlobals().connection, get_sel_reply->owner),
              NULL);
        } while (geom_reply != NULL);
    }
    p_delete(&get_sel_reply);

    /* Announce that we are the new owner */
    xcb_client_message_event_t ev{.response_type = XCB_CLIENT_MESSAGE,
                                  .format = 32,
                                  .sequence = 0,
                                  .window = getGlobals().screen->root,
                                  .type = MANAGER,
                                  .data{.data32 = {getGlobals().get_timestamp(),
                                                   getGlobals().selection_atom,
                                                   getGlobals().selection_owner_window,
                                                   0,
                                                   0}}};

    xcb_send_event(getGlobals().connection, false, getGlobals().screen->root, 0xFFFFFF, (char*)&ev);
}

static void acquire_timestamp(void) {
    /* Getting a current timestamp is hard. ICCCM recommends a zero-length
     * append to a property, so let's do that.
     */
    xcb_generic_event_t* event;
    xcb_window_t win = getGlobals().screen->root;
    xcb_atom_t atom = XCB_ATOM_RESOURCE_MANAGER; /* Just something random */
    xcb_atom_t type = XCB_ATOM_STRING;           /* Equally random */

    xcb_grab_server(getGlobals().connection);
    getConnection().change_attributes(
      win, XCB_CW_EVENT_MASK, std::array{XCB_EVENT_MASK_PROPERTY_CHANGE});
    getConnection().append_property(win, atom, type, std::span{"", 0});
    getConnection().clear_attributes(win, XCB_CW_EVENT_MASK);
    xutil_ungrab_server(getGlobals().connection);

    /* Now wait for the event */
    while ((event = xcb_wait_for_event(getGlobals().connection))) {
        /* Is it the event we are waiting for? */
        if (XCB_EVENT_RESPONSE_TYPE(event) == XCB_PROPERTY_NOTIFY) {
            xcb_property_notify_event_t* ev = (xcb_property_notify_event_t*)event;
            getGlobals().update_timestamp(ev);
            p_delete(&event);
            break;
        }

        /* Hm, not the right event. */
        if (getGlobals().pending_event != NULL) {
            event_handle(getGlobals().pending_event);
            p_delete(&getGlobals().pending_event);
        }
        getGlobals().pending_event = event;
    }
}

static xcb_generic_event_t* poll_for_event(void) {
    if (getGlobals().pending_event) {
        xcb_generic_event_t* event = getGlobals().pending_event;
        getGlobals().pending_event = NULL;
        return event;
    }

    return xcb_poll_for_event(getGlobals().connection);
}

static void a_xcb_check(void) {
    xcb_generic_event_t *mouse = NULL, *event;

    while ((event = poll_for_event())) {
        /* We will treat mouse events later.
         * We cannot afford to treat all mouse motion events,
         * because that would be too much CPU intensive, so we just
         * take the last we get after a bunch of events. */
        if (XCB_EVENT_RESPONSE_TYPE(event) == XCB_MOTION_NOTIFY) {
            p_delete(&mouse);
            mouse = event;
        } else {
            uint8_t type = XCB_EVENT_RESPONSE_TYPE(event);
            if (mouse && (type == XCB_ENTER_NOTIFY || type == XCB_LEAVE_NOTIFY ||
                          type == XCB_BUTTON_PRESS || type == XCB_BUTTON_RELEASE)) {
                /* Make sure enter/motion/leave/press/release events are handled
                 * in the correct order */
                event_handle(mouse);
                p_delete(&mouse);
            }
            event_handle(event);
            p_delete(&event);
        }
    }

    if (mouse) {
        event_handle(mouse);
        p_delete(&mouse);
    }
}

static gboolean a_xcb_io_cb(GIOChannel* source, GIOCondition cond, gpointer data) {
    /* a_xcb_check() already handled all events */

    if (xcb_connection_has_error(getGlobals().connection)) {
        fatal("X server connection broke (error %d)",
              xcb_connection_has_error(getGlobals().connection));
    }

    return TRUE;
}

static gint a_glib_poll(GPollFD* ufds, guint nfsd, gint timeout) {
    guint res;
    struct timeval now, length_time;
    float length;
    int saved_errno;
    lua_State* L = globalconf_get_lua_State();

    /* Do all deferred work now */
    awesome_refresh();

    /* Check if the Lua stack is the way it should be */
    if (lua_gettop(L) != 0) {
        warn("Something was left on the Lua stack, this is a bug!");
        luaA_dumpstack(L);
        lua_settop(L, 0);
    }

    /* Don't sleep if there is a pending event */
    assert(getGlobals().pending_event == NULL);
    getGlobals().pending_event = xcb_poll_for_event(getGlobals().connection);
    if (getGlobals().pending_event != NULL) {
        timeout = 0;
    }

    /* Check how long this main loop iteration took */
    gettimeofday(&now, NULL);
    timersub(&now, &last_wakeup, &length_time);
    length = length_time.tv_sec + length_time.tv_usec * 1.0f / 1e6;
    if (length > main_loop_iteration_limit) {
        warn(
          "Last main loop iteration took %.6f seconds! Increasing limit for "
          "this warning to that value.",
          length);
        main_loop_iteration_limit = length;
    }

    /* Actually do the polling, record time of wakeup and check for new xcb events */
    res = g_poll(ufds, nfsd, timeout);
    saved_errno = errno;
    gettimeofday(&last_wakeup, NULL);
    a_xcb_check();
    errno = saved_errno;

    return res;
}

static void signal_fatal(int signum) {
    auto bt = backtrace_get();
    fatal("signal %d, dumping backtrace\n%s", signum, bt);
}

/* Signal handler for SIGCHLD. Causes reap_children() to be called. */
static void signal_child(int signum) {
    assert(signum == SIGCHLD);
    int res = write(sigchld_pipe[1], " ", 1);
    (void)res;
    assert(res == 1);
}

/* There was a SIGCHLD signal. Read from sigchld_pipe and reap children. */
static gboolean reap_children(GIOChannel* channel, GIOCondition condition, gpointer user_data) {
    pid_t child;
    int status;
    char buffer[1024];
    ssize_t result = read(sigchld_pipe[0], &buffer[0], sizeof(buffer));
    if (result < 0) {
        fatal("Error reading from signal pipe: %s", strerror(errno));
    }

    while ((child = waitpid(-1, &status, WNOHANG)) > 0) {
        spawn_child_exited(child, status);
    }
    if (child < 0 && errno != ECHILD) {
        warn("waitpid(-1) failed: %s", strerror(errno));
    }
    return TRUE;
}

/** Function to exit on some signals.
 * \param data currently unused
 */
static gboolean exit_on_signal(gpointer data) {
    g_main_loop_quit(getGlobals().loop);
    return TRUE;
}
void awesome_restart(void) {
    awesome_atexit(true);
    execvp(awesome_argv[0], awesome_argv);
    fatal("execv() failed: %s", strerror(errno));
}

/** Function to restart awesome on some signals.
 * \param data currently unused
 */
static gboolean restart_on_signal(gpointer data) {
    awesome_restart();
    return TRUE;
}

/** Hello, this is main.
 * \param argc Who knows.
 * \param argv Who knows.
 * \return EXIT_SUCCESS I hope.
 */
int main(int argc, char** argv) {
    std::vector<std::filesystem::path> searchpath;
    int xfd;
    xdgHandle xdg;
    xcb_query_tree_cookie_t tree_c;

    /* Make stdout/stderr line buffered. */
    setvbuf(stdout, NULL, _IOLBF, 0);
    setvbuf(stderr, NULL, _IOLBF, 0);

    /* Text won't be printed correctly otherwise */
    setlocale(LC_ALL, "");
    /* The default values for the init flags */
    int default_init_flags =
      Options::INIT_FLAG_NONE | Options::INIT_FLAG_ARGB | Options::INIT_FLAG_AUTO_SCREEN;

    /* clear the globalconf structure */
    gGlobals = new Globals;
    getGlobals().api_level = awesome_default_api_level();

    /* save argv */
    awesome_argv = argv;

    // char *confpath = options_detect_shebang(argc, argv);

    /* if no shebang is detected, check the args. Shebang (#!) args are parsed later */
    auto opts = Options::options_check_args(argc, argv, &default_init_flags);

    /* Get XDG basedir data */
    if (!xdgInitHandle(&xdg)) {
        fatal("Function xdgInitHandle() failed, is $HOME unset?");
    }

    /* add XDG_CONFIG_DIR as include path */
    const char* const* xdgconfigdirs = xdgSearchableConfigDirectories(&xdg);
    for (; *xdgconfigdirs; xdgconfigdirs++) {
        opts.searchPaths.push_back(std::filesystem::path(*xdgconfigdirs) / "awesome");
    }

    /* Check the configfile syntax and exit */
    if (default_init_flags & Options::INIT_FLAG_RUN_TEST) {
        bool success = true;
        /* Get the first config that will be tried */
        auto config = Lua::find_config(
          &xdg, opts.configPath, [](const std::filesystem::path&) { return true; });
        if (!config) {
            fprintf(stderr, "Config not found");
            return EXIT_FAILURE;
        }
        fprintf(stdout, "Checking config '%s'... ", config->c_str());

        /* Try to parse it */
        lua_State* L = luaL_newstate();
        if (luaL_loadfile(L, config->c_str())) {
            const char* err = lua_tostring(L, -1);
            fprintf(stdout, "\nERROR: %s\n", err);
            success = false;
        }
        lua_close(L);

        if (!success) {
            return EXIT_FAILURE;
        } else {
            fprintf(stdout, "OK\n");
            return EXIT_SUCCESS;
        }
    }

    /* Parse `rc.lua` to see if it has an AwesomeWM modeline */
    if (!(default_init_flags & Options::INIT_FLAG_FORCE_CMD_ARGS)) {
        Options::options_init_config(&xdg,
                                     awesome_argv[0],
                                     opts.configPath ? opts.configPath->c_str() : nullptr,
                                     &default_init_flags,
                                     opts.searchPaths);
    }

    /* Setup pipe for SIGCHLD processing */
    {
        if (!g_unix_open_pipe(sigchld_pipe, FD_CLOEXEC, NULL)) {
            fatal("Failed to create pipe");
        }

        GIOChannel* channel = g_io_channel_unix_new(sigchld_pipe[0]);
        g_io_add_watch(channel, G_IO_IN, reap_children, NULL);
        g_io_channel_unref(channel);
    }

    /* register function for signals */
    g_unix_signal_add(SIGINT, exit_on_signal, NULL);
    g_unix_signal_add(SIGTERM, exit_on_signal, NULL);
    g_unix_signal_add(SIGHUP, restart_on_signal, NULL);
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = signal_fatal, sa.sa_flags = (decltype(sa.sa_flags))SA_RESETHAND;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGABRT, &sa, 0);
    sigaction(SIGBUS, &sa, 0);
    sigaction(SIGFPE, &sa, 0);
    sigaction(SIGILL, &sa, 0);
    sigaction(SIGSEGV, &sa, 0);
    signal(SIGPIPE, SIG_IGN);

    sa.sa_handler = signal_child;
    sa.sa_flags = SA_NOCLDSTOP | SA_RESTART;
    sigaction(SIGCHLD, &sa, 0);

    /* We have no clue where the input focus is right now */
    getGlobals().focus.need_update = true;

    /* set the default preferred icon size */
    getGlobals().preferred_icon_size = 0;

    /* X stuff */
    getGlobals().connection = xcb_connect(NULL, &getGlobals().default_screen);
    if (xcb_connection_has_error(getGlobals().connection)) {
        fatal("cannot open display (error %d)", xcb_connection_has_error(getGlobals().connection));
    }

    getGlobals().screen = xcb_aux_get_screen(getGlobals().connection, getGlobals().default_screen);
    getGlobals().default_visual = draw_default_visual(getGlobals().screen);
    if (default_init_flags & Options::INIT_FLAG_ARGB) {
        getGlobals().visual = draw_argb_visual(getGlobals().screen);
    }
    if (!getGlobals().visual) {
        getGlobals().visual = getGlobals().default_visual;
    }
    getGlobals().default_depth =
      draw_visual_depth(getGlobals().screen, getGlobals().visual->visual_id);
    getGlobals().default_cmap = getGlobals().screen->default_colormap;
    if (getGlobals().default_depth != getGlobals().screen->root_depth) {
        // We need our own color map if we aren't using the default depth
        getGlobals().default_cmap = getConnection().generate_id();
        xcb_create_colormap(getGlobals().connection,
                            XCB_COLORMAP_ALLOC_NONE,
                            getGlobals().default_cmap,
                            getGlobals().screen->root,
                            getGlobals().visual->visual_id);
    }

#ifdef WITH_XCB_ERRORS
    if (xcb_errors_context_new(globalconf.connection, &globalconf.errors_ctx) < 0) {
        fatal("Failed to initialize xcb-errors");
    }
#endif

    /* Get a recent timestamp */
    acquire_timestamp();

    /* Prefetch all the extensions we might need */
    xcb_prefetch_extension_data(getGlobals().connection, &xcb_big_requests_id);
    xcb_prefetch_extension_data(getGlobals().connection, &xcb_test_id);
    xcb_prefetch_extension_data(getGlobals().connection, &xcb_randr_id);
    xcb_prefetch_extension_data(getGlobals().connection, &xcb_xinerama_id);
    xcb_prefetch_extension_data(getGlobals().connection, &xcb_shape_id);
    xcb_prefetch_extension_data(getGlobals().connection, &xcb_xfixes_id);

    if (xcb_cursor_context_new(
          getGlobals().connection, getGlobals().screen, &getGlobals().cursor_ctx) < 0) {
        fatal("Failed to initialize xcb-cursor");
    }
    getGlobals().xrmdb = xcb_xrm_database_from_default(getGlobals().connection);
    if (getGlobals().xrmdb == NULL) {
        getGlobals().xrmdb = xcb_xrm_database_from_string("");
    }
    if (getGlobals().xrmdb == NULL) {
        fatal("Failed to initialize xcb-xrm");
    }

    /* Did we get some usable data from the above X11 setup? */
    draw_test_cairo_xcb();

    /* Acquire the WM_Sn selection */
    acquire_WM_Sn(default_init_flags & Options::INIT_FLAG_REPLACE_WM);

    /* initialize dbus */
    a_dbus_init();

    /* Get the file descriptor corresponding to the X connection */
    xfd = xcb_get_file_descriptor(getGlobals().connection);
    GIOChannel* channel = g_io_channel_unix_new(xfd);
    g_io_add_watch(channel, G_IO_IN, a_xcb_io_cb, NULL);
    g_io_channel_unref(channel);

    /* Grab server */
    xcb_grab_server(getGlobals().connection);

    {
        const uint32_t select_input_val = XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT;
        xcb_void_cookie_t cookie;

        /* This causes an error if some other window manager is running */
        cookie = xcb_change_window_attributes_checked(
          getGlobals().connection, getGlobals().screen->root, XCB_CW_EVENT_MASK, &select_input_val);
        if (xcb_request_check(getGlobals().connection, cookie)) {
            fatal("another window manager is already running (can't select SubstructureRedirect)");
        }
    }

    /* Prefetch the maximum request length */
    xcb_prefetch_maximum_request_length(getGlobals().connection);

    /* check for xtest extension */
    const xcb_query_extension_reply_t* query;
    query = xcb_get_extension_data(getGlobals().connection, &xcb_test_id);
    getGlobals().have_xtest = query && query->present;

    /* check for shape extension */
    query = xcb_get_extension_data(getGlobals().connection, &xcb_shape_id);
    getGlobals().have_shape = query && query->present;
    if (getGlobals().have_shape) {
        xcb_shape_query_version_reply_t* reply =
          xcb_shape_query_version_reply(getGlobals().connection,
                                        xcb_shape_query_version_unchecked(getGlobals().connection),
                                        NULL);
        getGlobals().have_input_shape =
          reply &&
          (reply->major_version > 1 || (reply->major_version == 1 && reply->minor_version >= 1));
        p_delete(&reply);
    }

    /* check for xfixes extension */
    query = xcb_get_extension_data(getGlobals().connection, &xcb_xfixes_id);
    getGlobals().have_xfixes = query && query->present;
    if (getGlobals().have_xfixes) {
        xcb_discard_reply(getGlobals().connection,
                          xcb_xfixes_query_version(getGlobals().connection, 1, 0).sequence);
    }

    event_init();

    /* Allocate the key symbols */
    getGlobals().keysyms = xcb_key_symbols_alloc(getGlobals().connection);

    /* init atom cache */
    atoms_init(getGlobals().connection);

    ewmh_init();
    systray_init();

    /* init spawn (sn) */
    spawn_init();

    /* init xkb */
    xkb_init();

    /* The default GC is just a newly created associated with a window with
     * depth globalconf.default_depth.
     * The window_no_focus is used for "nothing has the input focus". */
    getGlobals().focus.window_no_focus = getConnection().generate_id();
    getGlobals().gc = getConnection().generate_id();
    uint32_t create_window_values[] = {getGlobals().screen->black_pixel,
                                       getGlobals().screen->black_pixel,
                                       1,
                                       getGlobals().default_cmap};
    xcb_create_window(getGlobals().connection,
                      getGlobals().default_depth,
                      getGlobals().focus.window_no_focus,
                      getGlobals().screen->root,
                      -1,
                      -1,
                      1,
                      1,
                      0,
                      XCB_COPY_FROM_PARENT,
                      getGlobals().visual->visual_id,
                      XCB_CW_BACK_PIXEL | XCB_CW_BORDER_PIXEL | XCB_CW_OVERRIDE_REDIRECT |
                        XCB_CW_COLORMAP,
                      create_window_values);
    xwindow_set_class_instance(getGlobals().focus.window_no_focus);
    xwindow_set_name_static(getGlobals().focus.window_no_focus, "Awesome no input window");
    xcb_map_window(getGlobals().connection, getGlobals().focus.window_no_focus);
    uint32_t create_gc_flags[] = {getGlobals().screen->black_pixel,
                                  getGlobals().screen->white_pixel};
    xcb_create_gc(getGlobals().connection,
                  getGlobals().gc,
                  getGlobals().focus.window_no_focus,
                  XCB_GC_FOREGROUND | XCB_GC_BACKGROUND,
                  create_gc_flags);

    /* Get the window tree associated to this screen */
    tree_c = getConnection().query_tree_unckecked(getGlobals().screen->root);

    getConnection().change_attributes(
      getGlobals().screen->root, XCB_CW_EVENT_MASK, ROOT_WINDOW_EVENT_MASK);

    /* we will receive events, stop grabbing server */
    xutil_ungrab_server(getGlobals().connection);

    /* get the current wallpaper, from now on we are informed when it changes */
    root_update_wallpaper();

    /* init lua */
    Lua::init(&xdg, opts.searchPaths);

    init_rng();

    ewmh_init_lua();

    /* Parse and run configuration file before adding the screens */
    if (getGlobals().no_auto_screen) {
        /* Disable automatic screen creation, awful.screen has a fallback */
        getGlobals().ignore_screens = true;

        if (!opts.configPath || !Lua::parserc(&xdg, opts.configPath->c_str())) {
            fatal("couldn't find any rc file");
        }
    }

    /* init screens information */
    screen_scan();

    /* Parse and run configuration file after adding the screens */
    if (((!getGlobals().no_auto_screen) && !Lua::parserc(&xdg, opts.configPath))) {
        fatal("couldn't find any rc file");
    }

    xdgWipeHandle(&xdg);

    /* Both screen scanning mode have this signal, it cannot be in screen_scan
       since the automatic screen generation don't have executed rc.lua yet. */
    screen_emit_scanned();

    /* Exit if the user doesn't read the instructions properly */
    if (getGlobals().no_auto_screen && !getGlobals().screens.size()) {
        fatal(
          "When -m/--screen is set to \"off\", you **must** create a "
          "screen object before or inside the screen \"scanned\" "
          " signal. Using AwesomeWM with no screen is **not supported**.");
    }

    client_emit_scanning();

    /* scan existing windows */
    scan(tree_c);

    client_emit_scanned();

    Lua::emit_startup();

    /* Setup the main context */
    g_main_context_set_poll_func(g_main_context_default(), &a_glib_poll);
    gettimeofday(&last_wakeup, NULL);

    /* main event loop (if not NULL, awesome.quit() was already called) */
    if (getGlobals().loop == NULL) {
        getGlobals().loop = g_main_loop_new(NULL, FALSE);
        g_main_loop_run(getGlobals().loop);
    }
    g_main_loop_unref(getGlobals().loop);
    getGlobals().loop = NULL;

    awesome_atexit(false);

    return getGlobals().exit_code;
}

// vim: filetype=c:expandtab:shiftwidth=4:tabstop=8:softtabstop=4:textwidth=80
