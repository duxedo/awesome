/*
 * common/backtrace.c - Backtrace handling functions
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

#include "common/backtrace.h"

#include "config.h"

#include <cstdlib>
#include <cstring>

#ifdef HAS_EXECINFO
#include <execinfo.h>
#endif
enum { MAX_STACK_SIZE = 32, MAX_BT_SIZE = 2048 };

#include <array>

static char bt_text[MAX_BT_SIZE] = "\0";

/** Get a backtrace.
 * \param buf The buffer to fill with backtrace.
 */
const char* backtrace_get() {
    void* stack[MAX_STACK_SIZE];
    char** bt;
    int stack_size;
    memset(bt_text, 0, sizeof(bt_text));

    stack_size = backtrace(stack, std::size(stack));
    bt = backtrace_symbols(stack, stack_size);
    char* curr = bt_text;
    if (bt) {
        for (int i = 0; i < stack_size && curr < bt_text + MAX_BT_SIZE; i++) {
            size_t dataleft = (MAX_BT_SIZE - (curr - bt_text));
            if (i > 0 && dataleft >= 3) {
                strncat(curr, "\n", (MAX_BT_SIZE - (curr - bt_text)));
                curr += 2;
                dataleft -= 2;
            }
            auto len = std::min(dataleft - 1, strlen(bt[i]));
            if (len <= 4) {
                free(bt);
                return bt_text;
            }
            strncat(curr, bt[i], len);
            curr += len;
        }
        free(bt);
        return bt_text;
    }
    return "Cannot get backtrace symbols.";
}

