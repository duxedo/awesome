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
#pragma once

#include <algorithm>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

struct signal_t {
    std::vector<const void*> functions;
};
struct SignalHash {
    using is_transparent = void;
    size_t operator()(const std::string& s) const { return std::hash<std::string_view>{}(s); }
    size_t operator()(const std::string_view& s) const { return std::hash<std::string_view>{}(s); }
    size_t operator()(const char* s) const { return std::hash<std::string_view>{}(s); }
};

struct SignalEq {
    using is_transparent = void;
    size_t operator()(const std::string& s, const std::string& r) const { return s == r; }
    size_t operator()(const std::string_view& s, const std::string& r) const { return s == r; }
    size_t operator()(const char* s, const std::string& r) const { return r == s; }
};

struct Signals: public std::unordered_map<std::string, signal_t, SignalHash, SignalEq> {
    /** Connect a signal inside a signal array.
     * You are in charge of reference counting.
     * \param arr The signal array.
     * \param name The signal name.
     * \param ref The reference to add.
     */
    void connect(const std::string_view& name, const void* ref) {
        auto it = this->find(name);
        if (it == this->end()) {
            std::string nm(name.begin(), name.end());
            auto [it, done] = this->try_emplace(nm, signal_t{});
            it->second.functions.push_back(ref);
            return;
        }
        it->second.functions.push_back(ref);
    }
    /** Disconnect a signal inside a signal array.
     * You are in charge of reference counting.
     * \param arr The signal array.
     * \param name The signal name.
     * \param ref The reference to remove.
     */
    bool disconnect(const std::string_view& name, const void* ref) {
        auto it = this->find(name);
        if (it == this->end()) {
            return false;
        }
        auto funIt = std::remove(it->second.functions.begin(), it->second.functions.end(), ref);
        if (funIt == it->second.functions.end()) {
            return false;
        }
        it->second.functions.erase(funIt);
        return true;
    }
};
