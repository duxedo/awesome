/*
 * luaclass.h - useful functions for handling Lua classes
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

#ifndef AWESOME_COMMON_LUACLASS
#define AWESOME_COMMON_LUACLASS

#include "common/luahdr.h"
#include "common/signal.h"

#include <cstddef>
#include <string_view>
#include <unordered_set>

#define LUA_OBJECT_HEADER Signals signals;

/** Generic type for all objects.
 * All Lua objects can be casted to this type.
 */
typedef struct {
    LUA_OBJECT_HEADER
} lua_object_t;

typedef lua_object_t* (*lua_class_allocator_t)(lua_State*);
typedef void (*lua_class_collector_t)(lua_object_t*);

typedef int (*lua_class_propfunc_t)(lua_State*, lua_object_t*);

typedef bool (*lua_class_checker_t)(lua_object_t*);

struct lua_class_property_t {
    /** Name of the property */
    const std::string_view name;
    /** Callback function called when the property is found in object creation. */
    lua_class_propfunc_t newobj;
    /** Callback function called when the property is found in object __index. */
    lua_class_propfunc_t index;
    /** Callback function called when the property is found in object __newindex. */
    lua_class_propfunc_t newindex;
};

namespace Detail {
struct PropertyHash {
    using hash_type = std::hash<std::string_view>;
    using is_transparent = void;
    std::size_t operator()(const char* str) const { return hash_type{}(str); }
    std::size_t operator()(std::string_view str) const { return hash_type{}(str); }
    std::size_t operator()(const std::string& str) const { return hash_type{}(str); }
    std::size_t operator()(const lua_class_property_t& prop) const {
        return hash_type{}(prop.name);
    }
};

struct PropertyEqual {
    using is_transparent = void;
    bool operator()(const char* lhs, const lua_class_property_t& rhs) const {
        return rhs.name == lhs;
    }
    bool operator()(const std::string_view& lhs, const lua_class_property_t& rhs) const {
        return rhs.name == lhs;
    }
    bool operator()(const std::string& lhs, const lua_class_property_t& rhs) const {
        return rhs.name == lhs;
    }
    bool operator()(const lua_class_property_t& lhs, const lua_class_property_t& rhs) const {
        return lhs.name == rhs.name;
    }
};
} // namespace Detail

using Properties =
  std::unordered_set<lua_class_property_t, Detail::PropertyHash, Detail::PropertyEqual>;

struct lua_class_t {
    /** Class name */
    const char* name;
    /** Class signals */
    Signals signals;
    /** Parent class */
    lua_class_t* parent;
    /** Allocator for creating new objects of that class */
    lua_class_allocator_t allocator;
    /** Garbage collection function */
    lua_class_collector_t collector;
    /** Class properties */
    Properties properties;
    /** Function to call when a indexing an unknown property */
    lua_class_propfunc_t index_miss_property;
    /** Function to call when a indexing an unknown property */
    lua_class_propfunc_t newindex_miss_property;
    /** Function to call to check if an object is valid */
    lua_class_checker_t checker;
    /** Number of instances of this class in lua */
    unsigned int instances;
    /** Class tostring method */
    lua_class_propfunc_t tostring;
    /** Function to call on index misses */
    int index_miss_handler;
    /** Function to call on newindex misses */
    int newindex_miss_handler;

    template <std::size_t N>
    void add_property(const char (&name)[N],
                      lua_class_propfunc_t cb_new,
                      lua_class_propfunc_t cb_index,
                      lua_class_propfunc_t cb_newindex) {
        properties.insert(
          {.name = name, .newobj = cb_new, .index = cb_index, .newindex = cb_newindex});
    }
};

const char* luaA_typename(lua_State*, int);
lua_class_t* luaA_class_get(lua_State*, int);

void luaA_class_connect_signal(lua_State*, lua_class_t*, const std::string_view&, lua_CFunction);
void luaA_class_connect_signal_from_stack(lua_State*, lua_class_t*, const std::string_view&, int);
void luaA_class_disconnect_signal_from_stack(lua_State*,
                                             lua_class_t*,
                                             const std::string_view&,
                                             int);
void luaA_class_emit_signal(lua_State*, lua_class_t*, const std::string_view&, int);

void luaA_openlib(lua_State*, const char*, const struct luaL_Reg[], const struct luaL_Reg[]);
void luaA_class_setup(lua_State*,
                      lua_class_t*,
                      const char*,
                      lua_class_t*,
                      lua_class_allocator_t,
                      lua_class_collector_t,
                      lua_class_checker_t,
                      lua_class_propfunc_t,
                      lua_class_propfunc_t,
                      const struct luaL_Reg[],
                      const struct luaL_Reg[]);

int luaA_usemetatable(lua_State*, int, int);
int luaA_class_index(lua_State*);
int luaA_class_newindex(lua_State*);
int luaA_class_new(lua_State*, lua_class_t*);

void* luaA_checkudata(lua_State*, int, lua_class_t*);
void* luaA_toudata(lua_State* L, int ud, lua_class_t*);

static inline void luaA_class_set_tostring(lua_class_t* cls, lua_class_propfunc_t callback) {
    cls->tostring = callback;
}

static inline void* luaA_checkudataornil(lua_State* L, int udx, lua_class_t* cls) {
    if (lua_isnil(L, udx))
        return NULL;
    return luaA_checkudata(L, udx, cls);
}

#define LUA_CLASS_FUNCS(prefix, lua_class)                                                   \
    static inline int luaA_##prefix##_class_connect_signal(lua_State* L) {                   \
        luaA_class_connect_signal_from_stack(L, &(lua_class), Lua::checkstring(L, 1), 2);    \
        return 0;                                                                            \
    }                                                                                        \
                                                                                             \
    static inline int luaA_##prefix##_class_disconnect_signal(lua_State* L) {                \
        luaA_class_disconnect_signal_from_stack(L, &(lua_class), Lua::checkstring(L, 1), 2); \
        return 0;                                                                            \
    }                                                                                        \
                                                                                             \
    static inline int luaA_##prefix##_class_emit_signal(lua_State* L) {                      \
        luaA_class_emit_signal(L, &(lua_class), Lua::checkstring(L, 1), lua_gettop(L) - 1);  \
        return 0;                                                                            \
    }                                                                                        \
                                                                                             \
    static inline int luaA_##prefix##_class_instances(lua_State* L) {                        \
        lua_pushinteger(L, (lua_class).instances);                                           \
        return 1;                                                                            \
    }                                                                                        \
                                                                                             \
    static inline int luaA_##prefix##_set_index_miss_handler(lua_State* L) {                 \
        return luaA_registerfct(L, 1, &(lua_class).index_miss_handler);                      \
    }                                                                                        \
                                                                                             \
    static inline int luaA_##prefix##_set_newindex_miss_handler(lua_State* L) {              \
        return luaA_registerfct(L, 1, &(lua_class).newindex_miss_handler);                   \
    }

#define LUA_CLASS_METHODS(class)                                         \
    {"connect_signal", luaA_##class##_class_connect_signal},             \
      {"disconnect_signal", luaA_##class##_class_disconnect_signal},     \
      {"emit_signal", luaA_##class##_class_emit_signal},                 \
      {"instances", luaA_##class##_class_instances},                     \
      {"set_index_miss_handler", luaA_##class##_set_index_miss_handler}, \
      {"set_newindex_miss_handler", luaA_##class##_set_newindex_miss_handler},

#define LUA_CLASS_META {"__index", luaA_class_index}, {"__newindex", luaA_class_newindex},

#endif

// vim: filetype=c:expandtab:shiftwidth=4:tabstop=8:softtabstop=4:textwidth=80
