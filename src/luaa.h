/*
 * luaa.h - Lua configuration management header
 *
 * Copyright © 2008-2009 Julien Danjou <julien@danjou.info>
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

#include "common/lualib.h"
#include "common/signal.h"
#include "config.h"
#include "draw.h"
#include "lauxlib.h"

#include <basedir.h>
#include <filesystem>
#include <optional>
#include <vector>

#if !(501 <= LUA_VERSION_NUM && LUA_VERSION_NUM < 505)
#error \
  "Awesome only supports Lua versions 5.1-5.4 and LuaJIT2, please refer to https://awesomewm.org/apidoc/documentation/10-building-and-testing.md.html#Building"
#endif

using Paths = std::vector<std::filesystem::path>;
struct lua_object_t;

#define luaA_deprecate(L, repl)                                                                  \
    do {                                                                                         \
        Lua::warn(                                                                               \
          L, "%s: This function is deprecated and will be removed, see %s", __FUNCTION__, repl); \
        lua_pushlstring(L, __FUNCTION__, sizeof(__FUNCTION__));                                  \
        signal_object_emit(L, &Lua::global_signals, "debug::deprecation", 1);                    \
    } while (0)

namespace Lua {

/** Print a warning about some Lua code.
 * This is less mean than luaL_error() which setjmp via lua_error() and kills
 * everything. This only warn, it's up to you to then do what's should be done.
 * \param L The Lua VM state.
 * \param fmt The warning message.
 */
static inline void __attribute__((format(printf, 2, 3))) warn(lua_State* L, const char* fmt, ...) {
    va_list ap;
    luaL_where(L, 1);
    fprintf(stderr, "%s%sW: ", a_current_time_str(), lua_tostring(L, -1));
    lua_pop(L, 1);
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fprintf(stderr, "\n");

#if HAS_LUAJIT || LUA_VERSION_NUM >= 502
    luaL_traceback(L, L, NULL, 2);
    fprintf(stderr, "%s\n", lua_tostring(L, -1));
    lua_pop(L, 1);
#endif
}

static inline int typerror(lua_State* L, int narg, const char* tname) {
    const char* msg = lua_pushfstring(L, "%s expected, got %s", tname, luaL_typename(L, narg));
#if HAS_LUAJIT || LUA_VERSION_NUM >= 502
    luaL_traceback(L, L, NULL, 2);
    lua_concat(L, 2);
#endif
    return luaL_argerror(L, narg, msg);
}

static inline int rangerror(lua_State* L, int narg, double min, double max) {
    const char* msg = lua_pushfstring(
      L, "value in [%f, %f] expected, got %f", min, max, (double)lua_tonumber(L, narg));
#if HAS_LUAJIT || LUA_VERSION_NUM >= 502
    luaL_traceback(L, L, NULL, 2);
    lua_concat(L, 2);
#endif
    return luaL_argerror(L, narg, msg);
}

static inline void getuservalue(lua_State* L, int idx) {
#if LUA_VERSION_NUM >= 502
    lua_getuservalue(L, idx);
#else
    lua_getfenv(L, idx);
#endif
}

static inline void setuservalue(lua_State* L, int idx) {
#if LUA_VERSION_NUM >= 502
    lua_setuservalue(L, idx);
#else
    lua_setfenv(L, idx);
#endif
}

static inline size_t rawlen(lua_State* L, int idx) {
#if LUA_VERSION_NUM >= 502
    return lua_rawlen(L, idx);
#else
    return lua_objlen(L, idx);
#endif
}

static inline void registerlib(lua_State* L, const char* libname, const luaL_Reg* l) {
    assert(libname);
#if LUA_VERSION_NUM >= 502
    lua_newtable(L);
    luaL_setfuncs(L, l, 0);
    lua_pushvalue(L, -1);
    lua_setglobal(L, libname);
#else
    luaL_register(L, libname, l);
#endif
}

static inline void setfuncs(lua_State* L, const luaL_Reg* l) {
#if HAS_LUAJIT || LUA_VERSION_NUM >= 502
    luaL_setfuncs(L, l, 0);
#else
    luaL_register(L, NULL, l);
#endif
}

static inline bool checkboolean(lua_State* L, int n) {
    if (!lua_isboolean(L, n)) {
        typerror(L, n, "boolean");
    }
    return lua_toboolean(L, n);
}

static inline lua_Number getopt_number(lua_State* L, int idx, const char* name, lua_Number def) {
    lua_getfield(L, idx, name);
    if (lua_isnil(L, -1) || lua_isnumber(L, -1)) {
        def = luaL_optnumber(L, -1, def);
    }
    lua_pop(L, 1);
    return def;
}

static inline lua_Number checknumber_range(lua_State* L, int n, lua_Number min, lua_Number max) {
    lua_Number result = lua_tonumber(L, n);
    if (result < min || result > max) {
        rangerror(L, n, min, max);
    }
    return result;
}

static inline lua_Number
optnumber_range(lua_State* L, int narg, lua_Number def, lua_Number min, lua_Number max) {
    if (lua_isnoneornil(L, narg)) {
        return def;
    }
    return checknumber_range(L, narg, min, max);
}

static inline lua_Number getopt_number_range(
  lua_State* L, int idx, const char* name, lua_Number def, lua_Number min, lua_Number max) {
    lua_getfield(L, idx, name);
    if (lua_isnil(L, -1) || lua_isnumber(L, -1)) {
        def = optnumber_range(L, -1, def, min, max);
    }
    lua_pop(L, 1);
    return def;
}

static inline int checkinteger(lua_State* L, int n) {
    lua_Number d = lua_tonumber(L, n);
    if (d != (int)d) {
        typerror(L, n, "integer");
    }
    return d;
}

static inline lua_Integer optinteger(lua_State* L, int narg, lua_Integer def) {
    return luaL_opt(L, checkinteger, narg, def);
}

static inline int getopt_integer(lua_State* L, int idx, const char* name, lua_Integer def) {
    lua_getfield(L, idx, name);
    if (lua_isnil(L, -1) || lua_isnumber(L, -1)) {
        def = optinteger(L, -1, def);
    }
    lua_pop(L, 1);
    return def;
}

static inline int checkinteger_range(lua_State* L, int n, lua_Number min, lua_Number max) {
    int result = checkinteger(L, n);
    if (result < min || result > max) {
        rangerror(L, n, min, max);
    }
    return result;
}

static inline lua_Integer
optinteger_range(lua_State* L, int narg, lua_Integer def, lua_Number min, lua_Number max) {
    if (lua_isnoneornil(L, narg)) {
        return def;
    }
    return checkinteger_range(L, narg, min, max);
}

static inline int luaA_getopt_integer_range(
  lua_State* L, int idx, const char* name, lua_Integer def, lua_Number min, lua_Number max) {
    lua_getfield(L, idx, name);
    if (lua_isnil(L, -1) || lua_isnumber(L, -1)) {
        def = optinteger_range(L, -1, def, min, max);
    }
    lua_pop(L, 1);
    return def;
}

/** Push a area type to a table on stack.
 * \param L The Lua VM state.
 * \param geometry The area geometry to push.
 * \return The number of elements pushed on stack.
 */
static inline int pusharea(lua_State* L, area_t geometry) {
    lua_createtable(L, 0, 4);
    lua_pushinteger(L, geometry.top_left.x);
    lua_setfield(L, -2, "x");
    lua_pushinteger(L, geometry.top_left.y);
    lua_setfield(L, -2, "y");
    lua_pushinteger(L, geometry.width);
    lua_setfield(L, -2, "width");
    lua_pushinteger(L, geometry.height);
    lua_setfield(L, -2, "height");
    return 1;
}

template <>
struct Pusher<area_t> {
    int push(State& L, area_t geometry) {
        lua_createtable(L.L, 0, 4);
        lua_pushinteger(L.L, geometry.top_left.x);
        lua_setfield(L.L, -2, "x");
        lua_pushinteger(L.L, geometry.top_left.y);
        lua_setfield(L.L, -2, "y");
        lua_pushinteger(L.L, geometry.width);
        lua_setfield(L.L, -2, "width");
        lua_pushinteger(L.L, geometry.height);
        lua_setfield(L.L, -2, "height");
        return 1;
    }
};

/** Register an Lua object.
 * \param L The Lua stack.
 * \param idx Index of the object in the stack.
 * \param ref A int address: it will be filled with the int
 * registered. If the address points to an already registered object, it will
 * be unregistered.
 * \return Always 0.
 */
static inline int lregister(lua_State* L, int idx, RegistryIdx* ref) {
    lua_pushvalue(L, idx);
    if (ref->idx != LUA_REFNIL) {
        luaL_unref(L, LUA_REGISTRYINDEX, ref->idx);
    }
    ref->idx = luaL_ref(L, LUA_REGISTRYINDEX);
    return 0;
}

/** Unregister a Lua object.
 * \param L The Lua stack.
 * \param ref A reference to a Lua object.
 */
static inline void unregister(lua_State* L, RegistryIdx* ref) {
    luaL_unref(L, LUA_REGISTRYINDEX, ref->idx);
    ref->idx = LUA_REFNIL;
}

/** Register a function.
 * \param L The Lua stack.
 * \param idx Index of the function in the stack.
 * \param fct A int address: it will be filled with the int
 * registered. If the address points to an already registered function, it will
 * be unregistered.
 * \return lregister value.
 */
static inline int registerfct(lua_State* L, int idx, FunctionRegistryIdx* fct) {
    Lua::checkfunction(L, idx);
    return lregister(L, idx, &fct->idx);
}

/** Unregister a Function.
 * \param L The Lua stack.
 * \param ref A reference to a Lua object.
 */
static inline void unregister(lua_State* L, FunctionRegistryIdx* ref) { unregister(L, &ref->idx); }
using config_callback = bool(const std::filesystem::path&);

extern Signals global_signals;

void init(xdgHandle*, const Paths& searchPaths);

std::optional<std::filesystem::path>
find_config(xdgHandle*, std::optional<std::filesystem::path>, config_callback*);
bool parserc(xdgHandle*, std::optional<std::filesystem::path>);

/** Global signals */
int class_index_miss_property(lua_State*, lua_object_t*);
int class_newindex_miss_property(lua_State*, lua_object_t*);
int default_index(lua_State*);
int default_newindex(lua_State*);
void emit_startup(void);

void systray_invalidate(void);
} // namespace Lua
