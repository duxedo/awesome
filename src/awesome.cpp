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

#include "common/backtrace.h"
#include "common/version.h"
#include "common/xutil.h"
#include "dbus.h"
#include "event.h"
#include "ewmh.h"
#include "globalconf.h"
#include "objects/screen.h"
#include "options.h"
#include "spawn.h"
#include "systray.h"
#include "xcbcpp/xcb.h"
#include "xkb.h"
#include "xwindow.h"

#include <algorithm>
#include <fmt/core.h>
#include <glib-unix.h>
#include <ranges>
#include <sys/time.h>
#include <uv.h>
#include <xcb/xcb.h>

static Manager* gGlobals = nullptr;
Manager& Manager::get() {
    assert(gGlobals != nullptr);
    return *gGlobals;
}

XCB::Connection& getConnection() { return Manager::get().x.connection; }

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
        log_warn("Random number generator initialization failed: {}", lua_tostring(L, -1));
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
    signal_object_emit(L, &Lua::global_signals, "exit", 1);

    /* Move clients where we want them to be and keep the stacking order intact */
    for (auto* c : Manager::get().getStack()) {
        area_t geometry = client_get_undecorated_geometry(c);
        getConnection().reparent_window(
          c->window, Manager::get().screen->root, geometry.left(), geometry.top());
    }

    /* Save the client order.  This is useful also for "hard" restarts. */
    auto wins = span_alloca(xcb_window_t, Manager::get().clients.size());
    std::ranges::transform(
      Manager::get().clients, wins.begin(), [](auto& cli) { return cli->window; });

    getConnection().replace_property(
      Manager::get().screen->root, AWESOME_CLIENT_ORDER, XCB_ATOM_WINDOW, wins);

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
    getConnection().set_input_focus(
      XCB_INPUT_FOCUS_POINTER_ROOT, XCB_NONE, Manager::get().x.get_timestamp());
    getConnection().aux_sync();

    xkb_free();

    /* Disconnect *after* closing lua */
    xcb_cursor_context_free(Manager::get().x.cursor_ctx);
#ifdef WITH_XCB_ERRORS
    xcb_errors_context_free(Manager::get().x.errors_ctx);
#endif
    getConnection().disconnect();

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
        for (auto*& c : Manager::get().clients) {
            if (c->window == windows[i]) {
                std::swap(c, Manager::get().clients[client_idx]);
                client_idx++;
            }
        }
    }

    client_class.emit_signal(globalconf_get_lua_State(), "list", 0);
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
      true, Manager::get().screen->root, AWESOME_CLIENT_ORDER, XCB_ATOM_WINDOW, 0, UINT_MAX);
    auto wins = conn.query_tree_children(tree_r);
    /* Get the tree of the children windows of the current root window */
    if (!wins) {
        log_fatal("cannot get tree children");
    }

    namespace views = std::ranges::views;

    std::vector winparams = wins.value() | views::transform([&conn](const auto& v) {
                                return std::tuple{v,
                                                  conn.get_window_attributes_unchecked(v),
                                                  xwindow_get_state_unchecked(v),
                                                  conn.get_geometry_unchecked(v)};
                            }) | std::ranges::to<std::vector>();

    auto clients_to_manage = winparams | views::transform([&conn](const auto& v) {
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

    /* Get the WM_Sn atom */
    Manager::get().x.selection_owner_window = getConnection().generate_id();
    getConnection().create_window(Manager::get().screen->root_depth,
                                  Manager::get().x.selection_owner_window,
                                  Manager::get().screen->root,
                                  {-1, -1, 1, 1},
                                  0,
                                  XCB_COPY_FROM_PARENT,
                                  Manager::get().screen->root_visual,
                                  0);
    xwindow_set_class_instance(Manager::get().x.selection_owner_window);
    xwindow_set_name_static(Manager::get().x.selection_owner_window,
                            "Awesome WM_Sn selection owner window");

    auto atom_name = xcb_atom_name_by_screen("WM_S", Manager::get().x.default_screen);
    if (!atom_name) {
        log_fatal("error getting WM_Sn atom name");
    }

    auto atom_q = getConnection().intern_atom_unchecked(false, strlen(atom_name), atom_name);

    p_delete(&atom_name);

    auto atom_r = getConnection().intern_atom_reply(atom_q);
    if (!atom_r) {
        log_fatal("error getting WM_Sn atom");
    }

    Manager::get().x.selection_atom = atom_r->atom;

    /* Is the selection already owned? */
    auto get_sel_reply = getConnection().get_selection_owner_reply(
      getConnection().get_selection_owner(Manager::get().x.selection_atom));
    if (!get_sel_reply) {
        log_fatal("GetSelectionOwner for WM_Sn failed");
    }
    if (!replace && get_sel_reply->owner != XCB_NONE) {
        log_fatal("another window manager is already running (selection owned; use --replace)");
    }

    /* Acquire the selection */
    getConnection().set_selection_owner(Manager::get().x.selection_owner_window,
                                        Manager::get().x.selection_atom,
                                        Manager::get().x.get_timestamp());
    if (get_sel_reply->owner != XCB_NONE) {
        /* Wait for the old owner to go away */
        XCB::reply<xcb_get_geometry_reply_t> geom_reply;
        do {
            geom_reply = getConnection().get_geometry_reply(
              getConnection().get_geometry(get_sel_reply->owner));
        } while (geom_reply);
    }

    /* Announce that we are the new owner */
    xcb_client_message_event_t ev{.response_type = XCB_CLIENT_MESSAGE,
                                  .format = 32,
                                  .sequence = 0,
                                  .window = Manager::get().screen->root,
                                  .type = MANAGER,
                                  .data{.data32 = {Manager::get().x.get_timestamp(),
                                                   Manager::get().x.selection_atom,
                                                   Manager::get().x.selection_owner_window,
                                                   0,
                                                   0}}};

    getConnection().send_event(false, Manager::get().screen->root, 0xFFFFFF, (char*)&ev);
}

static void acquire_timestamp(void) {
    /* Getting a current timestamp is hard. ICCCM recommends a zero-length
     * append to a property, so let's do that.
     */
    xcb_window_t win = Manager::get().screen->root;
    xcb_atom_t atom = XCB_ATOM_RESOURCE_MANAGER; /* Just something random */
    xcb_atom_t type = XCB_ATOM_STRING;           /* Equally random */

    getConnection().grab_server();
    getConnection().change_attributes(
      win, XCB_CW_EVENT_MASK, std::array{XCB_EVENT_MASK_PROPERTY_CHANGE});
    getConnection().append_property(win, atom, type, std::span{"", 0});
    getConnection().clear_attributes(win, XCB_CW_EVENT_MASK);
    xutil_ungrab_server();

    /* Now wait for the event */
    while (auto event = getConnection().wait_for_event()) {
        /* Is it the event we are waiting for? */
        if (XCB_EVENT_RESPONSE_TYPE(event) == XCB_PROPERTY_NOTIFY) {
            xcb_property_notify_event_t* ev = (xcb_property_notify_event_t*)event.get();
            Manager::get().x.update_timestamp(ev);
            break;
        }

        /* Hm, not the right event. */
        if (Manager::get().pending_event) {
            event_handle(Manager::get().pending_event.get());
        }
        Manager::get().pending_event = std::move(event);
    }
}

static XCB::event<xcb_generic_event_t> poll_for_event(void) {
    if (Manager::get().pending_event) {
        return std::move(Manager::get().pending_event);
    }

    return getConnection().poll_for_event();
}

static void a_xcb_check(void) {
    XCB::event<xcb_generic_event_t> mouse;

    while (auto event = poll_for_event()) {
        /* We will treat mouse events later.
         * We cannot afford to treat all mouse motion events,
         * because that would be too much CPU intensive, so we just
         * take the last we get after a bunch of events. */
        if (XCB_EVENT_RESPONSE_TYPE(event) == XCB_MOTION_NOTIFY) {
            mouse = std::move(event);
        } else {
            uint8_t type = XCB_EVENT_RESPONSE_TYPE(event);
            if (mouse && (type == XCB_ENTER_NOTIFY || type == XCB_LEAVE_NOTIFY ||
                          type == XCB_BUTTON_PRESS || type == XCB_BUTTON_RELEASE)) {
                /* Make sure enter/motion/leave/press/release events are handled
                 * in the correct order */
                event_handle(mouse.get());
                mouse.reset();
            }
            event_handle(event.get());
        }
    }

    if (mouse) {
        event_handle(mouse.get());
    }
}

static gboolean a_xcb_io_cb(GIOChannel* source, GIOCondition cond, gpointer data) {
    /* a_xcb_check() already handled all events */

    if (auto err = getConnection().connection_has_error()) {
        log_fatal("X server connection broke (error {})", err);
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
        log_warn("Something was left on the Lua stack, this is a bug!");
        Lua::dumpstack(L);
        lua_settop(L, 0);
    }

    /* Don't sleep if there is a pending event */
    assert(!Manager::get().pending_event);
    Manager::get().pending_event = getConnection().poll_for_event();
    if (Manager::get().pending_event) {
        timeout = 0;
    }

    /* Check how long this main loop iteration took */
    gettimeofday(&now, NULL);
    timersub(&now, &last_wakeup, &length_time);
    length = length_time.tv_sec + length_time.tv_usec * 1.0f / 1e6;
    if (length > main_loop_iteration_limit) {
        log_warn(
          "Last main loop iteration took {:.6f} seconds! Increasing limit for "
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
    log_fatal("signal {}, dumping backtrace\n{}", signum, bt);
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
        log_fatal("Error reading from signal pipe: {}", strerror(errno));
    }

    while ((child = waitpid(-1, &status, WNOHANG)) > 0) {
        spawn_child_exited(child, status);
    }
    if (child < 0 && errno != ECHILD) {
        log_warn("waitpid(-1) failed: {}", strerror(errno));
    }
    return TRUE;
}

/** Function to exit on some signals.
 * \param data currently unused
 */
static gboolean exit_on_signal(gpointer data) {
    g_main_loop_quit(Manager::get().loop);
    return TRUE;
}
void awesome_restart(void) {
    awesome_atexit(true);
    execvp(awesome_argv[0], awesome_argv);
    log_fatal("execv() failed: {}", strerror(errno));
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
    /* Make stdout/stderr line buffered. */
    setvbuf(stdout, NULL, _IOLBF, 0);
    setvbuf(stderr, NULL, _IOLBF, 0);

    /* Text won't be printed correctly otherwise */
    setlocale(LC_ALL, "");
    /* The default values for the init flags */
    int default_init_flags =
      Options::INIT_FLAG_NONE | Options::INIT_FLAG_ARGB | Options::INIT_FLAG_AUTO_SCREEN;

    /* save argv */
    awesome_argv = argv;

    /* if no shebang is detected, check the args. Shebang (#!) args are parsed later */
    auto opts = Options::options_check_args(argc, argv, &default_init_flags);

    /* Get XDG basedir data */
    xdgHandle xdg;
    if (!xdgInitHandle(&xdg)) {
        log_fatal("Function xdgInitHandle() failed, is $HOME unset?");
    }

    /* add XDG_CONFIG_DIR as include path */
    const char* const* xdgconfigdirs = xdgSearchableConfigDirectories(&xdg);
    for (; *xdgconfigdirs; xdgconfigdirs++) {
        opts.searchPaths.push_back(std::filesystem::path(*xdgconfigdirs) / "awesome");
    }

    /* clear the globalconf structure */
    gGlobals = new Manager;
    Manager::get().api_level =
      opts.api_level ? opts.api_level.value() : awesome_default_api_level();
    Manager::get().startup.have_searchpaths = opts.have_searchpaths;
    Manager::get().had_overriden_depth = opts.had_overriden_depth;

    if (opts.no_auto_screen.has_value()) {
        Manager::get().startup.no_auto_screen = opts.no_auto_screen.value();
    }

    /* Check the configfile syntax and exit */
    if (default_init_flags & Options::INIT_FLAG_RUN_TEST) {
        bool success = true;
        /* Get the first config that will be tried */
        auto config = Lua::find_config(
          &xdg, opts.configPath, [](const std::filesystem::path&) { return true; });
        if (!config) {
            fmt::print(stderr, "Config not found");
            return EXIT_FAILURE;
        }
        fmt::print(stdout, "Checking config '{}'... ", config->c_str());

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
            log_fatal("Failed to create pipe");
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
    sa.sa_handler = signal_fatal;
    sa.sa_flags = (decltype(sa.sa_flags))SA_RESETHAND;
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
    Manager::get().focus.need_update = true;

    /* set the default preferred icon size */
    Manager::get().preferred_icon_size = 0;

    /* X stuff */
    Manager::get().x.connection = XCB::Connection::connect(NULL, &Manager::get().x.default_screen);

    if (auto err = getConnection().connection_has_error()) {
        log_fatal("cannot open display (error {})", err);
    }

    Manager::get().screen = getConnection().aux_get_screen(Manager::get().x.default_screen);
    Manager::get().default_visual = draw_default_visual(Manager::get().screen);
    if (default_init_flags & Options::INIT_FLAG_ARGB) {
        Manager::get().visual = draw_argb_visual(Manager::get().screen);
    }
    if (!Manager::get().visual) {
        Manager::get().visual = Manager::get().default_visual;
    }
    Manager::get().default_depth =
      draw_visual_depth(Manager::get().screen, Manager::get().visual->visual_id);
    Manager::get().default_cmap = Manager::get().screen->default_colormap;
    if (Manager::get().default_depth != Manager::get().screen->root_depth) {
        // We need our own color map if we aren't using the default depth
        Manager::get().default_cmap = getConnection().generate_id();
        getConnection().create_colormap(XCB_COLORMAP_ALLOC_NONE,
                                        Manager::get().default_cmap,
                                        Manager::get().screen->root,
                                        Manager::get().visual->visual_id);
    }

#ifdef WITH_XCB_ERRORS
    if (getConnection().errors_context_new(&Manager::get().x.errors_ctx) < 0) {
        log_fatal("Failed to initialize xcb-errors");
    }
#endif

    /* Get a recent timestamp */
    acquire_timestamp();

    /* Prefetch all the extensions we might need */
    getConnection().prefetch_extension_data(&xcb_big_requests_id);
    getConnection().prefetch_extension_data(&xcb_test_id);
    getConnection().prefetch_extension_data(&xcb_randr_id);
    getConnection().prefetch_extension_data(&xcb_xinerama_id);
    getConnection().prefetch_extension_data(&xcb_shape_id);
    getConnection().prefetch_extension_data(&xcb_xfixes_id);

    if (xcb_cursor_context_new(getConnection().getConnection(),
                               Manager::get().screen,
                               &Manager::get().x.cursor_ctx) < 0) {
        log_fatal("Failed to initialize xcb-cursor");
    }
    Manager::get().x.xrmdb = xcb_xrm_database_from_default(getConnection().getConnection());
    if (Manager::get().x.xrmdb == NULL) {
        Manager::get().x.xrmdb = xcb_xrm_database_from_string("");
    }
    if (Manager::get().x.xrmdb == NULL) {
        log_fatal("Failed to initialize xcb-xrm");
    }

    /* Did we get some usable data from the above X11 setup? */
    draw_test_cairo_xcb();

    /* Acquire the WM_Sn selection */
    acquire_WM_Sn(default_init_flags & Options::INIT_FLAG_REPLACE_WM);

    /* initialize dbus */
    a_dbus_init();

    /* Get the file descriptor corresponding to the X connection */
    int xfd = xcb_get_file_descriptor(getConnection().getConnection());
    GIOChannel* channel = g_io_channel_unix_new(xfd);
    g_io_add_watch(channel, G_IO_IN, a_xcb_io_cb, NULL);
    g_io_channel_unref(channel);

    /* Grab server */
    getConnection().grab_server();

    {
        const uint32_t select_input_val = XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT;
        xcb_void_cookie_t cookie;

        /* This causes an error if some other window manager is running */
        cookie = xcb_change_window_attributes_checked(getConnection().getConnection(),
                                                      Manager::get().screen->root,
                                                      XCB_CW_EVENT_MASK,
                                                      &select_input_val);
        if (xcb_request_check(getConnection().getConnection(), cookie)) {
            log_fatal(
              "another window manager is already running (can't select SubstructureRedirect)");
        }
    }

    /* Prefetch the maximum request length */
    xcb_prefetch_maximum_request_length(getConnection().getConnection());

    /* check for xtest extension */
    const xcb_query_extension_reply_t* query;
    query = xcb_get_extension_data(getConnection().getConnection(), &xcb_test_id);
    Manager::get().x.caps.have_xtest = query && query->present;

    /* check for shape extension */
    query = xcb_get_extension_data(getConnection().getConnection(), &xcb_shape_id);
    Manager::get().x.caps.have_shape = query && query->present;
    if (Manager::get().x.caps.have_shape) {
        xcb_shape_query_version_reply_t* reply = xcb_shape_query_version_reply(
          getConnection().getConnection(),
          xcb_shape_query_version_unchecked(getConnection().getConnection()),
          NULL);
        Manager::get().x.caps.have_input_shape =
          reply &&
          (reply->major_version > 1 || (reply->major_version == 1 && reply->minor_version >= 1));
        p_delete(&reply);
    }

    /* check for xfixes extension */
    query = xcb_get_extension_data(getConnection().getConnection(), &xcb_xfixes_id);
    Manager::get().x.caps.have_xfixes = query && query->present;
    if (Manager::get().x.caps.have_xfixes) {
        getConnection().discard_reply(
          xcb_xfixes_query_version(getConnection().getConnection(), 1, 0).sequence);
    }

    event_init();

    /* Allocate the key symbols */
    Manager::get().input.keysyms = getConnection().key_symbols_alloc();

    /* init atom cache */
    atoms_init(getConnection().getConnection());

    ewmh_init();
    systray_init();

    /* init spawn (sn) */
    spawn_init();

    /* init xkb */
    xkb_init();

    /* The default GC is just a newly created associated with a window with
     * depth globalconf.default_depth.
     * The window_no_focus is used for "nothing has the input focus". */
    Manager::get().focus.window_no_focus = getConnection().generate_id();
    Manager::get().gc = getConnection().generate_id();

    getConnection().create_window(Manager::get().default_depth,
                                  Manager::get().focus.window_no_focus,
                                  Manager::get().screen->root,
                                  {-1, -1, 1, 1},
                                  0,
                                  XCB_COPY_FROM_PARENT,
                                  Manager::get().visual->visual_id,

                                  XCB_CW_BACK_PIXEL | XCB_CW_BORDER_PIXEL |
                                    XCB_CW_OVERRIDE_REDIRECT | XCB_CW_COLORMAP,

                                  std::to_array<uint32_t>({Manager::get().screen->black_pixel,
                                                           Manager::get().screen->black_pixel,
                                                           1u,
                                                           Manager::get().default_cmap}));

    xwindow_set_class_instance(Manager::get().focus.window_no_focus);
    xwindow_set_name_static(Manager::get().focus.window_no_focus, "Awesome no input window");

    getConnection().map_window(Manager::get().focus.window_no_focus);
    getConnection().create_gc(Manager::get().gc,
                              Manager::get().focus.window_no_focus,
                              XCB_GC_FOREGROUND | XCB_GC_BACKGROUND,
                              std::to_array<uint32_t>({Manager::get().screen->black_pixel,
                                                       Manager::get().screen->white_pixel}));

    /* Get the window tree associated to this screen */
    xcb_query_tree_cookie_t tree_c =
      getConnection().query_tree_unchecked(Manager::get().screen->root);

    getConnection().change_attributes(
      Manager::get().screen->root, XCB_CW_EVENT_MASK, ROOT_WINDOW_EVENT_MASK);

    /* we will receive events, stop grabbing server */
    xutil_ungrab_server();

    /* get the current wallpaper, from now on we are informed when it changes */
    root_update_wallpaper();

    /* init lua */
    Lua::init(&xdg, opts.searchPaths);

    init_rng();

    ewmh_init_lua();

    /* Parse and run configuration file before adding the screens */
    if (Manager::get().startup.no_auto_screen) {
        /* Disable automatic screen creation, awful.screen has a fallback */
        Manager::get().startup.ignore_screens = true;

        if (!opts.configPath || !Lua::parserc(&xdg, opts.configPath->c_str())) {
            log_fatal("couldn't find any rc file");
        }
    }

    /* init screens information */
    screen_scan();

    /* Parse and run configuration file after adding the screens */
    if (((!Manager::get().startup.no_auto_screen) && !Lua::parserc(&xdg, opts.configPath))) {
        log_fatal("couldn't find any rc file");
    }

    xdgWipeHandle(&xdg);

    /* Both screen scanning mode have this signal, it cannot be in screen_scan
       since the automatic screen generation don't have executed rc.lua yet. */
    screen_emit_scanned();

    /* Exit if the user doesn't read the instructions properly */
    if (Manager::get().startup.no_auto_screen && !Manager::get().screens.size()) {
        log_fatal(
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
    if (Manager::get().loop == NULL) {
        Manager::get().loop = g_main_loop_new(NULL, FALSE);
        g_main_loop_run(Manager::get().loop);
    }
    g_main_loop_unref(Manager::get().loop);
    Manager::get().loop = NULL;

    awesome_atexit(false);

    return Manager::get().exit_code;
}

// vim: filetype=c:expandtab:shiftwidth=4:tabstop=8:softtabstop=4:textwidth=80
