/*
 * spawn.c - Lua configuration management
 *
 * Copyright © 2009 Julien Danjou <julien@danjou.info>
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

/** awesome core API
 *
 * @author Julien Danjou &lt;julien@danjou.info&gt;
 * @copyright 2008-2009 Julien Danjou
 * @module awesome
 */

/** For some reason the application aborted startup
 * @param arg Table which only got the "id" key set
 * @signal spawn::canceled
 */

/** When one of the fields from the @{spawn::initiated} table changes
 * @param arg Table which describes the spawn event
 * @signal spawn::change
 */

/** An application finished starting
 * @param arg Table which only got the "id" key set
 * @signal spawn::completed
 */

/** When a new client is beginning to start
 * @param arg Table which describes the spawn event
 * @signal spawn::initiated
 */

/** An application started a spawn event but didn't start in time.
 * @param arg Table which only got the "id" key set
 * @signal spawn::timeout
 */

#include "spawn.h"

#include "common/util.h"
#include "glibconfig.h"
#include "libsn/sn-monitor.h"
#include "luaa.h"

#include <algorithm>
#include <glib.h>
#include <memory>
#include <set>
#include <string_view>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

/** 20 seconds timeout */
#define AWESOME_SPAWN_TIMEOUT 20.0

/** Wrapper for unrefing startup sequence.
 */
struct SnStartupDeleter {
    void operator()(SnStartupSequence* sss) { return sn_startup_sequence_unref(sss); }
};
using StartupSequenceHandle = std::unique_ptr<SnStartupSequence, SnStartupDeleter>;
/** The array of startup sequence running */
std::vector<StartupSequenceHandle> sn_waits;

struct running_child_t {
    GPid pid;
    int exit_callback;
    auto operator<=>(const running_child_t& c) { return pid <=> c.pid; }
    auto operator<=>(GPid pid) { return this->pid <=> pid; }
};

struct ChildPidComparator {
    using is_transparent = void;
    bool operator()(const running_child_t& lhs, const running_child_t& rhs) const {
        return lhs.pid < rhs.pid;
    }
    bool operator()(const running_child_t& lhs, GPid rhs) const { return lhs.pid < rhs; }
    bool operator()(GPid lhs, const running_child_t& rhs) const { return lhs < rhs.pid; }
};

static std::set<running_child_t, ChildPidComparator> running_children;

/** Remove a SnStartupSequence pointer from an array and forget about it.
 * \param s The startup sequence to find, remove and unref.
 * \return True if found and removed.
 */
static inline bool spawn_sequence_remove(SnStartupSequence* s) {
    auto it = std::find_if(
      sn_waits.begin(), sn_waits.end(), [s](const auto& var) { return var.get() == s; });
    if (it == sn_waits.end()) {
        return false;
    }
    sn_waits.erase(it);
    return false;
}

static gboolean spawn_monitor_timeout(gpointer sequence) {
    if (spawn_sequence_remove((SnStartupSequence*)sequence)) {
        auto sigIt = Lua::global_signals.find("spawn::timeout");
        if (sigIt != Lua::global_signals.end()) {
            /* send a timeout signal */
            lua_State* L = globalconf_get_lua_State();
            lua_createtable(L, 0, 2);
            lua_pushstring(L, sn_startup_sequence_get_id((SnStartupSequence*)sequence));
            lua_setfield(L, -2, "id");
            for (auto func : sigIt->second.functions) {
                lua_pushvalue(L, -1);
                luaA_object_push(L, (void*)func);
                Lua::dofunction(L, 1, 0);
            }
            lua_pop(L, 1);
        }
    }
    sn_startup_sequence_unref((SnStartupSequence*)sequence);
    return FALSE;
}

static void spawn_monitor_event(SnMonitorEvent* event, void* data) {
    lua_State* L = globalconf_get_lua_State();
    SnStartupSequence* sequence = sn_monitor_event_get_startup_sequence(event);
    SnMonitorEventType event_type = sn_monitor_event_get_type(event);

    lua_createtable(L, 0, 2);
    lua_pushstring(L, sn_startup_sequence_get_id(sequence));
    lua_setfield(L, -2, "id");

    const char* event_type_str = NULL;

    switch (event_type) {
    case SN_MONITOR_EVENT_INITIATED:
        /* ref the sequence for the array */
        sn_startup_sequence_ref(sequence);
        sn_waits.push_back(StartupSequenceHandle{sequence});
        event_type_str = "spawn::initiated";

        /* Add a timeout function so we do not wait for this event to complete
         * for ever */
        g_timeout_add_seconds(AWESOME_SPAWN_TIMEOUT, spawn_monitor_timeout, sequence);
        /* ref the sequence for the callback event */
        sn_startup_sequence_ref(sequence);
        break;
    case SN_MONITOR_EVENT_CHANGED: event_type_str = "spawn::change"; break;
    case SN_MONITOR_EVENT_COMPLETED: event_type_str = "spawn::completed"; break;
    case SN_MONITOR_EVENT_CANCELED: event_type_str = "spawn::canceled"; break;
    }

    /* common actions */
    switch (event_type) {
    case SN_MONITOR_EVENT_INITIATED:
    case SN_MONITOR_EVENT_CHANGED: {
        const char* s = sn_startup_sequence_get_name(sequence);
        if (s) {
            lua_pushstring(L, s);
            lua_setfield(L, -2, "name");
        }

        if ((s = sn_startup_sequence_get_description(sequence))) {
            lua_pushstring(L, s);
            lua_setfield(L, -2, "description");
        }

        lua_pushinteger(L, sn_startup_sequence_get_workspace(sequence));
        lua_setfield(L, -2, "workspace");

        if ((s = sn_startup_sequence_get_binary_name(sequence))) {
            lua_pushstring(L, s);
            lua_setfield(L, -2, "binary_name");
        }

        if ((s = sn_startup_sequence_get_icon_name(sequence))) {
            lua_pushstring(L, s);
            lua_setfield(L, -2, "icon_name");
        }

        if ((s = sn_startup_sequence_get_wmclass(sequence))) {
            lua_pushstring(L, s);
            lua_setfield(L, -2, "wmclass");
        }
    } break;
    case SN_MONITOR_EVENT_COMPLETED:
    case SN_MONITOR_EVENT_CANCELED: spawn_sequence_remove(sequence); break;
    }

    /* send the signal */
    auto sigIt = Lua::global_signals.find(event_type_str);

    if (sigIt != Lua::global_signals.end()) {
        for (auto func : sigIt->second.functions) {
            lua_pushvalue(L, -1);
            luaA_object_push(L, (void*)func);
            Lua::dofunction(L, 1, 0);
        }
        lua_pop(L, 1);
    }
}

/** Tell the spawn module that an app has been started.
 * \param c The client that just started.
 * \param startup_id The startup id of the started application.
 */
void spawn_start_notify(client* c, const char* startup_id) {
    for (auto& _seq : sn_waits) {
        SnStartupSequence* seq = _seq.get();
        bool found = false;
        const char* seqid = sn_startup_sequence_get_id(seq);

        if (A_STRNEQ(seqid, startup_id)) {
            found = true;
        } else {
            const char* seqclass = sn_startup_sequence_get_wmclass(seq);
            if (c->getCls() == seqclass || c->getInstance() == seqclass) {
                found = true;
            } else {
                auto seqbin = std::string_view(sn_startup_sequence_get_binary_name(seq));
                if (std::ranges::equal(seqbin, c->getCls(), ichar_equals) || std::ranges::equal(seqbin, c->getInstance(), ichar_equals)) {
                    found = true;
                }
            }
        }

        if (found) {
            sn_startup_sequence_complete(seq);
            break;
        }
    }
}

/** Initialize program spawner.
 */
void spawn_init(void) {
    getGlobals().sndisplay = sn_xcb_display_new(getGlobals().connection, NULL, NULL);

    getGlobals().snmonitor = sn_monitor_context_new(
      getGlobals().sndisplay, getGlobals().default_screen, spawn_monitor_event, NULL, NULL);
}

static gboolean spawn_launchee_timeout(gpointer context) {
    sn_launcher_context_complete((SnLauncherContext*)context);
    sn_launcher_context_unref((SnLauncherContext*)context);
    return FALSE;
}

static void spawn_callback(gpointer user_data) {
    SnLauncherContext* context = (SnLauncherContext*)user_data;
    setsid();

    if (context) {
        sn_launcher_context_setup_child_process(context);
    } else {
        /* Unset in case awesome was already started with this variable set */
        unsetenv("DESKTOP_STARTUP_ID");
    }
}

/** Convert a Lua table of strings to a char** array.
 * \param L The Lua VM state.
 * \param idx The index of the table that we should parse.
 * \return The argv array.
 */
static gchar** parse_table_array(lua_State* L, int idx, GError** error) {
    gchar** argv = NULL;
    size_t i, len;

    luaL_checktype(L, idx, LUA_TTABLE);
    idx = Lua::absindex(L, idx);
    len = Lua::rawlen(L, idx);

    /* First verify that the table is sane: All integer keys must contain
     * strings. Do this by pushing them all onto the stack.
     */
    for (i = 0; i < len; i++) {
        lua_rawgeti(L, idx, i + 1);
        if (lua_type(L, -1) != LUA_TSTRING) {
            g_set_error(error, G_SPAWN_ERROR, 0, "Non-string argument at table index %zd", i + 1);
            return NULL;
        }
    }

    /* From this point on nothing can go wrong and so we can safely allocate
     * memory.
     */
    argv = g_new0(gchar*, len + 1);
    for (i = 0; i < len; i++) {
        argv[len - i - 1] = g_strdup(lua_tostring(L, -1));
        lua_pop(L, 1);
    }

    return argv;
}

/** Parse a command line.
 * \param L The Lua VM state.
 * \param idx The index of the argument that we should parse.
 * \return The argv array for the new process.
 */
static gchar** parse_command(lua_State* L, int idx, GError** error) {
    gchar** argv = NULL;

    if (lua_isstring(L, idx)) {
        const char* cmd = luaL_checkstring(L, idx);
        if (!g_shell_parse_argv(cmd, NULL, &argv, error)) {
            return NULL;
        }
    } else if (lua_istable(L, idx)) {
        argv = parse_table_array(L, idx, error);
    } else {
        g_set_error_literal(
          error, G_SPAWN_ERROR, 0, "Invalid argument to spawn(), expected string or table");
        return NULL;
    }

    return argv;
}

/** Callback for when a spawned process exits. */
void spawn_child_exited(pid_t pid, int status) {
    int exit_callback;
    lua_State* L = globalconf_get_lua_State();

    auto it = running_children.find(GPid(pid));
    if (it == running_children.end()) {
        log_warn("Unknown child {} exited with {} {}",
             (int)pid,
             WIFEXITED(status) ? "status" : "signal",
             status);
        return;
    }
    exit_callback = it->exit_callback;
    running_children.erase(it);

    /* 'Decode' the exit status */
    if (WIFEXITED(status)) {
        lua_pushliteral(L, "exit");
        lua_pushinteger(L, WEXITSTATUS(status));
    } else {
        awsm_check(WIFSIGNALED(status));
        lua_pushliteral(L, "signal");
        lua_pushinteger(L, WTERMSIG(status));
    }

    lua_rawgeti(L, LUA_REGISTRYINDEX, exit_callback);
    Lua::dofunction(L, 2, 0);
    Lua::unregister(L, &exit_callback);
}

/** Spawn a program.
 * The program will be started on the default screen.
 *
 * @tparam string|table cmd The command to launch.
 * @tparam[opt=true] boolean use_sn Use startup-notification?
 * @tparam[opt="DEV_NULL"] boolean|string stdin Pass `true` to return a fd for
 *   stdin. Use `"DEV_NULL"` to redirect to /dev/null, or `"INHERIT"` to inherit
 *   the parent's stdin. Implementation note: Pre-2.74 glib doesn't support
 *   *explicit* `DEV_NULL`. When `DEV_NULL` is passed on glib <2.74, Awesome will
 *   use glib's default behaviour.
 * @tparam[opt="INHERIT"] boolean|string stdout Pass `true` to return a fd for
 *   stdout. Use `"DEV_NULL"` to redirect to /dev/null, or `"INHERIT"` to
 *   inherit the parent's stdout. Implementation note: Pre-2.74 glib doesn't
 *   support *explicit* `INHERIT`. When `INHERIT` is passed on glib <2.74,
 *   Awesome will use glib's default behaviour.
 * @tparam[opt="INHERIT"] boolean|string stderr Pass `true` to return a fd for
 *   stderr. Use `"DEV_NULL"` to redirect to /dev/null, or `"INHERIT"` to
 *   inherit the parent's stderr. Implementation note: Pre-2.74 glib doesn't
 *   support *explicit* `INHERIT`. When `INHERIT` is passed on glib <2.74,
 *   Awesome will use glib's default behaviour.
 * @tparam[opt=nil] function exit_callback Function to call on process exit. The
 *   function arguments will be type of exit ("exit" or "signal") and the exit
 *   code / the signal number causing process termination.
 * @tparam[opt=nil] table cmd The environment to use for the spawned program.
 *   Without this the spawned process inherits awesome's environment.
 * @treturn[1] integer Process ID if everything is OK.
 * @treturn[1] string Startup-notification ID, if `use_sn` is true.
 * @treturn[1] integer stdin, if `stdin` is true.
 * @treturn[1] integer stdout, if `stdout` is true.
 * @treturn[1] integer stderr, if `stderr` is true.
 * @treturn[2] string An error string if an error occurred.
 * @staticfct spawn
 */
int luaA_spawn(lua_State* L) {
    gchar **argv = NULL, **envp = NULL;
    bool use_sn = true, return_stdin = false, return_stdout = false, return_stderr = false;
    int stdin_fd = -1, stdout_fd = -1, stderr_fd = -1;
    int *stdin_ptr = NULL, *stdout_ptr = NULL, *stderr_ptr = NULL;
    int flags = G_SPAWN_DEFAULT;
    gboolean retval;
    GPid pid;

    if (lua_gettop(L) >= 2) {
        use_sn = Lua::checkboolean(L, 2);
    /* Valid values for return_std* are:
    * true -> return a fd
    * false -> keep glib's default behaviour
    * "DEV_NULL" -> use direct output to /dev/null
    * "INHERIT" -> use the same fd as the parent
    */
    if(lua_gettop(L) >= 3) {
        if (lua_isstring(L, 3)) {
            const char *str = lua_tostring(L, 3);
            if (a_strcmp(str, "DEV_NULL") == 0){
                // This is the default behaviour. Compiles to a no-op before 2.74.
                #if GLIB_CHECK_VERSION(2, 74, 0)
                flags |= G_SPAWN_STDIN_FROM_DEV_NULL;
                # endif
            } else if (a_strcmp(str, "INHERIT") == 0)
                flags |= G_SPAWN_CHILD_INHERITS_STDIN;
            else
                Lua::typerror(L, 3, "DEV_NULL or INHERIT");
        } else if(lua_isboolean(L, 3)) {
            return_stdin = lua_toboolean(L, 3);
        } else {
            Lua::typerror(L, 3, "boolean or string");
        }
    }
    if(lua_gettop(L) >= 4) {
        if (lua_isstring(L, 4)) {
            const char *str = lua_tostring(L, 4);
            if (a_strcmp(str, "DEV_NULL") == 0)
                flags |= G_SPAWN_STDOUT_TO_DEV_NULL;
            else if (a_strcmp(str, "INHERIT") == 0) {
                // This is the default behaviour. Compiles to a no-op before 2.74.
                #if GLIB_CHECK_VERSION(2, 74, 0)
                flags |= G_SPAWN_CHILD_INHERITS_STDOUT;
                # endif
            } else
                Lua::typerror(L, 4, "DEV_NULL or INHERIT");
        } else if(lua_isboolean(L, 4)) {
            return_stdout = lua_toboolean(L, 4);
        } else {
            Lua::typerror(L, 4, "boolean or string");
        }
    }
    if(lua_gettop(L) >= 5) {
        if (lua_isstring(L, 5)) {
            const char *str = lua_tostring(L, 5);
            if (a_strcmp(str, "DEV_NULL") == 0)
                flags |= G_SPAWN_STDERR_TO_DEV_NULL;
            else if (a_strcmp(str, "INHERIT") == 0) {
                // This is the default behaviour. Compiles to a no-op before 2.74.
                #if GLIB_CHECK_VERSION(2, 74, 0)
                flags |= G_SPAWN_CHILD_INHERITS_STDERR;
                # endif
            } else
                Lua::typerror(L, 5, "DEV_NULL or INHERIT");
        } else if(lua_isboolean(L, 5)) {
            return_stderr = lua_toboolean(L, 5);
        } else {
            Lua::typerror(L, 5, "boolean or string");
        }
    }
    if (!lua_isnoneornil(L, 6))
    {
        Lua::checkfunction(L, 6);
        flags |= G_SPAWN_DO_NOT_REAP_CHILD;
    }
    if (return_stdin) {
        stdin_ptr = &stdin_fd;
    }
    if (return_stdout) {
        stdout_ptr = &stdout_fd;
    }
    if (return_stderr) {
        stderr_ptr = &stderr_fd;
    }

    GError* error = NULL;
    argv = parse_command(L, 1, &error);
    if (!argv || !argv[0]) {
        g_strfreev(argv);
        if (error) {
            lua_pushfstring(L, "spawn: parse error: %s", error->message);
            g_error_free(error);
        } else {
            lua_pushliteral(L, "spawn: There is nothing to execute");
        }
        return 1;
    }

    if (!lua_isnoneornil(L, 7)) {
        envp = parse_table_array(L, 7, &error);
        if (error) {
            g_strfreev(argv);
            g_strfreev(envp);
            lua_pushfstring(L, "spawn: environment parse error: %s", error->message);
            g_error_free(error);
            return 1;
        }
    }

    SnLauncherContext* context = NULL;
    if (use_sn) {
        context = sn_launcher_context_new(getGlobals().sndisplay, getGlobals().default_screen);
        sn_launcher_context_set_name(context, "awesome");
        sn_launcher_context_set_description(context, "awesome spawn");
        sn_launcher_context_set_binary_name(context, argv[0]);
        sn_launcher_context_initiate(context, "awesome", argv[0], getGlobals().get_timestamp());

        /* app will have AWESOME_SPAWN_TIMEOUT seconds to complete,
         * or the timeout function will terminate the launch sequence anyway */
        g_timeout_add_seconds(AWESOME_SPAWN_TIMEOUT, spawn_launchee_timeout, context);
    }

    flags |= G_SPAWN_SEARCH_PATH | G_SPAWN_CLOEXEC_PIPES;
    retval = g_spawn_async_with_pipes(NULL,
                                      argv,
                                      envp,
                                      (GSpawnFlags)flags,
                                      spawn_callback,
                                      context,
                                      &pid,
                                      stdin_ptr,
                                      stdout_ptr,
                                      stderr_ptr,
                                      &error);
    g_strfreev(argv);
    g_strfreev(envp);
    if (!retval) {
        lua_pushstring(L, error->message);
        g_error_free(error);
        if (context) {
            sn_launcher_context_complete(context);
        }
        return 1;
    }

    if (flags & G_SPAWN_DO_NOT_REAP_CHILD) {
        /* Only do this down here to avoid leaks in case of errors */
        running_child_t child = {.pid = pid, .exit_callback = LUA_REFNIL};
        Lua::registerfct(L, 6, &child.exit_callback);
        running_children.insert(child);
    }

    /* push pid on stack */
    lua_pushinteger(L, pid);

    /* push sn on stack */
    if (context) {
        lua_pushstring(L, sn_launcher_context_get_startup_id(context));
    } else {
        lua_pushnil(L);
    }

    if (return_stdin) {
        lua_pushinteger(L, stdin_fd);
    } else {
        lua_pushnil(L);
    }
    if (return_stdout) {
        lua_pushinteger(L, stdout_fd);
    } else {
        lua_pushnil(L);
    }
    if (return_stderr) {
        lua_pushinteger(L, stderr_fd);
    } else {
        lua_pushnil(L);
    }

    return 5;
}

// vim: filetype=c:expandtab:shiftwidth=4:tabstop=8:softtabstop=4:textwidth=80
