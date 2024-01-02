/*
 * util.c - useful functions
 *
 * Copyright © 2007-2008 Julien Danjou <julien@danjou.info>
 * Copyright © 2006 Pierre Habouzit <madcoder@debian.org>
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

#include "common/util.h"

#include <cstdio>
#include <errno.h>
#include <fcntl.h>
#include <fmt/core.h>
#include <limits.h>
#include <source_location>
#include <stdarg.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <string>
#include "common/luahdr.h"
#include "lua.h"

const char* a_current_time_str(void) {
    static char buffer[25];
    time_t now;
    struct tm tm;

    time(&now);
    localtime_r(&now, &tm);
    if (!strftime(buffer, sizeof(buffer), "%Y-%m-%d %T ", &tm)) {
        buffer[0] = '\0';
    }

    return buffer;
}

void log_messagev(char tag, FILE* file, const std::source_location loc, std::string_view format, fmt::format_args args) {
    fmt::print("{}{}: awesome: {}:{}: {}\n", tag, a_current_time_str(), loc.function_name(), loc.line(), fmt::vformat(format, args));
    if(tag == 'E') {
        exit(EXIT_FAILURE);
    }
}

/** Execute a command and replace the current process.
 * \param cmd The command to execute.
 */
void a_exec(const char* cmd) {
    static const char* shell = NULL;

    if (!shell && !(shell = getenv("SHELL"))) {
        shell = "/bin/sh";
    }

    execlp(shell, shell, "-c", cmd, NULL);
    log_fatal("execlp() failed: {}", strerror(errno));
}

namespace Lua {
void pushstring(lua_State* L, const std::string & str) {
    lua_pushlstring(L, str.c_str(), str.size());
}
void pushstring(lua_State* L, const char* str) {
    lua_pushstring(L, str);
}
void pushstring(lua_State* L, const std::string_view str) {
    lua_pushlstring(L, str.data(), str.size());
}
}
// vim: filetype=c:expandtab:shiftwidth=4:tabstop=8:softtabstop=4:textwidth=80
