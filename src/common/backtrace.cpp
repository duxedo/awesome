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

#ifdef HAS_EXECINFO
#include <execinfo.h>
#endif

#define MAX_STACK_SIZE 32

/** Get a backtrace.
 * \param buf The buffer to fill with backtrace.
 */
std::string backtrace_get() {
    std::string ret;
    void* stack[MAX_STACK_SIZE];
    char** bt;
    int stack_size;

    stack_size = backtrace(stack, std::size(stack));
    bt = backtrace_symbols(stack, stack_size);

    if (bt) {
        for (int i = 0; i < stack_size; i++) {
            if (i > 0) {
                ret += "\n";
            }
            ret += bt[i];
        }
        free(bt);
        return ret;
    }
    return "Cannot get backtrace symbols.";
}

// vim: filetype=c:expandtab:shiftwidth=4:tabstop=8:softtabstop=4:textwidth=80
