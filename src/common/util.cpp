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

/** \brief safe limited strcpy.
 *
 * Copies at most min(<tt>n-1</tt>, \c l) characters from \c src into \c dst,
 * always adding a final \c \\0 in \c dst.
 *
 * \param[in]  dst      destination buffer.
 * \param[in]  n        size of the buffer. Negative sizes are allowed.
 * \param[in]  src      source string.
 * \param[in]  l        maximum number of chars to copy.
 *
 * \return minimum of  \c src \e length and \c l.
 */
ssize_t a_strncpy(char* dst, ssize_t n, const char* src, ssize_t l) {
    ssize_t len = MIN(a_strlen(src), l);

    if (n > 0) {
        ssize_t dlen = MIN(n - 1, len);
        memcpy(dst, src, dlen);
        dst[dlen] = '\0';
    }

    return len;
}

/** \brief safe strcpy.
 *
 * Copies at most <tt>n-1</tt> characters from \c src into \c dst, always
 * adding a final \c \\0 in \c dst.
 *
 * \param[in]  dst      destination buffer.
 * \param[in]  n        size of the buffer. Negative sizes are allowed.
 * \param[in]  src      source string.
 * \return \c src \e length. If this value is \>= \c n then the copy was
 *         truncated.
 */
ssize_t a_strcpy(char* dst, ssize_t n, const char* src) {
    ssize_t len = a_strlen(src);

    if (n > 0) {
        ssize_t dlen = MIN(n - 1, len);
        memcpy(dst, src, dlen);
        dst[dlen] = '\0';
    }

    return len;
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
}
// vim: filetype=c:expandtab:shiftwidth=4:tabstop=8:softtabstop=4:textwidth=80
