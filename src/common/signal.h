/*
 * common/signal.h - Signal handling functions
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

#ifndef AWESOME_COMMON_SIGNAL
#define AWESOME_COMMON_SIGNAL

#include <vector>
#include <compare>
#include <unordered_map>
#include <string>
#include <algorithm>

struct signal_t
{
    std::vector<const void*> functions;
};

using signal_array_t = std::unordered_map<std::string, signal_t> ;
/** Connect a signal inside a signal array.
 * You are in charge of reference counting.
 * \param arr The signal array.
 * \param name The signal name.
 * \param ref The reference to add.
 */
static inline void
signal_connect(signal_array_t *arr, const char *name, const void *ref)
{
    (*arr)[name].functions.push_back(ref);
}

/** Disconnect a signal inside a signal array.
 * You are in charge of reference counting.
 * \param arr The signal array.
 * \param name The signal name.
 * \param ref The reference to remove.
 */
static inline bool
signal_disconnect(signal_array_t *arr, const char *name, const void *ref)
{
    if(auto it = arr->find(name); it != arr->end()) {
        auto funIt = std::remove(it->second.functions.begin(), it->second.functions.end(), ref);
        if(funIt == it->second.functions.end()) {
            return false;
        }
        it->second.functions.erase(funIt);
        return true;
    }
    return false;
}

#endif

// vim: filetype=c:expandtab:shiftwidth=4:tabstop=8:softtabstop=4:textwidth=80
