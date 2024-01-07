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
#pragma once

#include "common/luahdr.h"
#include "common/lualib.h"
#include "common/signal.h"
#include "lauxlib.h"
#include "lua.h"
#include "luaa.h"

#include <array>
#include <cstddef>
#include <fmt/core.h>
#include <map>
#include <string_view>
#include <type_traits>
#include <typeinfo>
#include <unordered_set>

template <class C>
struct TypeIdentifier {
    constexpr static int _id{};
    constexpr static auto id() { return &_id; }
};
namespace array {
template <typename T, std::size_t LL, std::size_t RL>
constexpr std::array<T, LL + RL> join(std::array<T, LL> rhs, std::array<T, RL> lhs) {
    std::array<T, LL + RL> ar;

    auto current = std::copy(rhs.begin(), rhs.end(), ar.begin());
    std::copy(lhs.begin(), lhs.end(), current);

    return ar;
}
template <typename T, std::size_t LL, std::size_t RL>
constexpr std::array<T, LL + RL> join(std::array<T, LL> rhs, T (&&lhs)[RL]) {
    std::array<T, std::size(rhs) + RL> ar;

    auto current = std::copy(rhs.begin(), rhs.end(), ar.begin());
    std::copy(std::begin(lhs), std::end(lhs), current);

    return ar;
}
}

/** Generic type for all objects.
 * All Lua objects can be casted to this type.
 */
struct lua_object_t {
    Signals signals;
};

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
    std::string name;
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
    static lua_class_t* get(lua_State* state, int idx);
    void connect_signal(lua_State* state, const std::string_view& name, lua_CFunction sigfun);
    void connect_signal(lua_State* state, const std::string_view& name, int stackIdx);
    void disconnect_signal(lua_State* state, const std::string_view& name, int stackIdx);
    void emit_signal(lua_State*, const std::string_view& name, int nargs);
};

const char* luaA_typename(lua_State*, int);
lua_class_t* luaA_class_get(lua_State*, int);

void luaA_openlib(lua_State*, const char*, const struct luaL_Reg[], const struct luaL_Reg[]);
namespace internal {
void luaA_class_setup(lua_State* L,
                      lua_class_t* cls,
                      const char* name,
                      lua_class_t* parent,
                      lua_class_allocator_t allocator,
                      lua_class_collector_t collector,
                      lua_class_checker_t checker,
                      lua_class_propfunc_t index_miss_property,
                      lua_class_propfunc_t newindex_miss_property,
                      const struct luaL_Reg methods[],
                      const struct luaL_Reg meta[]);
}

template <typename ObjectT, typename ObjectAdapter>
void luaA_class_setup(lua_State* state,
                      lua_class_t* cls,
                      const char* name,
                      lua_class_t* parent,
                      lua_class_propfunc_t index_miss_property,
                      lua_class_propfunc_t newindex_miss_property,
                      const struct luaL_Reg methods[],
                      const struct luaL_Reg meta[]) {
    lua_class_allocator_t allocator = [](lua_State* state) {
        return static_cast<lua_object_t*>(ObjectAdapter{}.allocator(state));
    };
    lua_class_collector_t collector = [](lua_object_t* obj) {
        ObjectAdapter{}.collector(static_cast<ObjectT*>(obj));
    };
    lua_class_checker_t checker = nullptr;
    constexpr bool hasChecker =
      requires(const ObjectAdapter& m) { ObjectAdapter{}.checker(std::declval<ObjectT*>()); };
    if constexpr (hasChecker) {
        checker = [](lua_object_t* obj) {
            return ObjectAdapter{}.checker(static_cast<ObjectT*>(obj));
        };
    }
    internal::luaA_class_setup(state,
                               cls,
                               name,
                               parent,
                               allocator,
                               collector,
                               checker,
                               index_miss_property,
                               newindex_miss_property,
                               methods,
                               meta);
}

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
    if (lua_isnil(L, udx)) {
        return NULL;
    }
    return luaA_checkudata(L, udx, cls);
}
namespace internal {
template <lua_class_t* cls>
inline constexpr auto LuaClassMethods =
    std::array<luaL_Reg, 6>{
      luaL_Reg{           "connect_signal",
               [](lua_State* L) {
               cls->connect_signal(L, *Lua::checkstring(L, 1), 2);
               return 0;
               }                                                                               },
      {        "disconnect_signal",
               [](lua_State* L) {
               cls->disconnect_signal(L, *Lua::checkstring(L, 1), 2);
               return 0;
               }                                                                               },
      {              "emit_signal",
               [](lua_State* L) {
               cls->emit_signal(L, *Lua::checkstring(L, 1), lua_gettop(L) - 1);
               return 0;
               }                                                                               },
      {                "instances",
               [](lua_State* L) {
               lua_pushinteger(L, cls->instances);
               return 0;
               }                                                                               },
      {   "set_index_miss_handler",
               [](lua_State* L) { return Lua::registerfct(L, 1, &cls->index_miss_handler); }   },
      {"set_newindex_miss_handler",
               [](lua_State* L) { return Lua::registerfct(L, 1, &cls->newindex_miss_handler); }}
    };

inline constexpr auto LuaClassMeta =
    std::array<luaL_Reg, 2>{
      luaL_Reg{   "__index",    luaA_class_index},
      {"__newindex", luaA_class_newindex}
    };
}

template <lua_class_t* cls, size_t N>
consteval auto DefineClassMethods(luaL_Reg (&&arr)[N]) {
    return array::join(array::join(internal::LuaClassMethods<cls>, std::move(arr)),
                       std::array{
                         luaL_Reg{nullptr, nullptr}
    });
}
template <lua_class_t* cls>
consteval auto DefineClassMethods() {
    return array::join(internal::LuaClassMethods<cls>,
                       std::array{
                         luaL_Reg{nullptr, nullptr}
    });
}
