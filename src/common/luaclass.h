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
#include <functional>
#include <map>
#include <string_view>
#include <strings.h>
#include <type_traits>
#include <typeinfo>
#include <unordered_set>

template <class C>
struct TypeIdentifier {
    constexpr static int _id{};
    constexpr static auto id() { return &_id; }
};

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
    using propfcn = std::function<int(lua_State*, lua_object_t*)>;
    /** Name of the property */
    const std::string_view name;
    /** Callback function called when the property is found in object creation. */
    propfcn newobj;
    /** Callback function called when the property is found in object __index. */
    propfcn index;
    /** Callback function called when the property is found in object __newindex. */
    propfcn newindex;

    template <typename ObjectT>
    lua_class_property_t(std::string_view name,
                         int (*lua_class_newobj)(lua_State*, ObjectT*),
                         int (*lua_class_index)(lua_State*, ObjectT*),
                         int (*lua_class_newindex)(lua_State*, ObjectT*))
        : name(name) {
        if (lua_class_newobj) {
            newobj = [lua_class_newobj](lua_State* state, lua_object_t* obj) {
                return lua_class_newobj(state, static_cast<ObjectT*>(obj));
            };
        }
        if (lua_class_index) {
            index = [lua_class_index](lua_State* state, lua_object_t* obj) {
                return lua_class_index(state, static_cast<ObjectT*>(obj));
            };
        }
        if (lua_class_newindex) {
            newindex = [lua_class_newindex](lua_State* state, lua_object_t* obj) {
                return lua_class_newindex(state, static_cast<ObjectT*>(obj));
            };
        }
    }

    template <typename ObjectT>
    lua_class_property_t(std::string_view name,
                         std::function<int(lua_State*, ObjectT*)> lua_class_newobj,
                         std::function<int(lua_State*, ObjectT*)> lua_class_index,
                         std::function<int(lua_State*, ObjectT*)> lua_class_newindex)
        : name(name) {
        if (lua_class_newobj) {
            newobj = [lua_class_newobj = std::move(lua_class_newobj)](lua_State* state,
                                                                      lua_object_t* obj) {
                return lua_class_newobj(state, static_cast<ObjectT*>(obj));
            };
        }
        if (lua_class_index) {
            index = [lua_class_index = std::move(lua_class_index)](lua_State* state,
                                                                   lua_object_t* obj) {
                return lua_class_index(state, static_cast<ObjectT*>(obj));
            };
        }
        if (lua_class_newindex) {
            newindex = [lua_class_newindex = std::move(lua_class_newindex)](lua_State* state,
                                                                            lua_object_t* obj) {
                return lua_class_newindex(state, static_cast<ObjectT*>(obj));
            };
        }
    }
    template <typename ObjectT>
    static lua_class_property_t make(std::string_view name,
                                     int (*lua_class_newobj)(lua_State*, ObjectT*),
                                     int (*lua_class_index)(lua_State*, ObjectT*),
                                     int (*lua_class_newindex)(lua_State*, ObjectT*)) {
        return lua_class_property_t{name, lua_class_newobj, lua_class_index, lua_class_newindex};
    }
    lua_class_property_t(std::string_view name,
                         lua_class_propfunc_t newobj,
                         lua_class_propfunc_t index,
                         lua_class_propfunc_t newindex)
        : name(name), newobj(newobj), index(index), newindex(newindex) {}
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

/*
 * \param allocator The allocator function used when creating a new object.
 * \param Collector The collector function used when garbage collecting an
 * object.
 * \param checker The check function to call when using luaA_checkudata().
 * \param index_miss_property Function to call when an object of this class
 * receive a __index request on an unknown property.
 * \param newindex_miss_property Function to call when an object of this class
 * receive a __newindex request on an unknown property.
 */
struct ClassInterface {
    lua_class_allocator_t allocator;
    lua_class_collector_t collector;
    lua_class_checker_t checker;
    lua_class_propfunc_t index_miss_property;
    lua_class_propfunc_t newindex_miss_property;
};

class lua_class_t {
    /** Class name */
    std::string _name;
    /** Class signals */
    Signals _signals;
    /** Parent class */
    lua_class_t* _parent = nullptr;
    /** Allocator for creating new objects of that class */
    lua_class_allocator_t _allocator = nullptr;
    /** Garbage collection function */
    lua_class_collector_t _collector = nullptr;
    /** Class properties */
    Properties _properties;
    /** Function to call when a indexing an unknown property */
    lua_class_propfunc_t _index_miss_property = nullptr;
    /** Function to call when a indexing an unknown property */
    lua_class_propfunc_t _newindex_miss_property = nullptr;
    /** Function to call to check if an object is valid */
    lua_class_checker_t _checker = nullptr;
    /** Number of instances of this class in lua */
    unsigned int _instances = 0;
    /** Class tostring method */
    lua_class_propfunc_t _tostring = nullptr;
    /** Function to call on index misses */
    Lua::FunctionRegistryIdx _index_miss_handler;
    /** Function to call on newindex misses */
    Lua::FunctionRegistryIdx _newindex_miss_handler;

  public:
    lua_class_t(std::string name, lua_class_t* parent, ClassInterface iface)
        : _name(std::move(name))
        , _parent(parent)
        , _allocator(iface.allocator)
        , _collector(iface.collector)
        , _index_miss_property(iface.index_miss_property)
        , _newindex_miss_property(iface.newindex_miss_property) {}

    void setup(lua_State* L, const struct luaL_Reg methods[], const struct luaL_Reg meta[]);

    template <std::size_t N>
    void add_property(const char (&name)[N],
                      lua_class_propfunc_t cb_new,
                      lua_class_propfunc_t cb_index,
                      lua_class_propfunc_t cb_newindex) {
        _properties.insert({name, cb_new, cb_index, cb_newindex});
    }
    void add_property(lua_class_property_t prop) { _properties.insert(prop); }
    static lua_class_t* get(lua_State* state, int idx);
    void connect_signal(lua_State* state, const std::string_view& name, lua_CFunction sigfun);
    void connect_signal(lua_State* state, const std::string_view& name, int stackIdx);
    void disconnect_signal(lua_State* state, const std::string_view& name, int stackIdx);
    void emit_signal(lua_State*, const std::string_view& name, int nargs);

    int numRefs() const { return _instances; }
    void ref() { ++_instances; }
    void deref() { --_instances; }

    auto& index_miss_handler() { return _index_miss_handler; }
    auto& newindex_miss_handler() { return _newindex_miss_handler; }

    bool check(lua_object_t* obj) {
        if (_checker) {
            return _checker(obj);
        }
        return true;
    }

    void set_tostring(lua_class_propfunc_t cbk) { _tostring = cbk; }

    bool has_tostring() const { return _tostring; }

    int tostring(lua_State* s, lua_object_t* o) const { return _tostring(s, o); }

    lua_class_t* parent() const { return _parent; }
    const std::string& name() const { return _name; }

    const lua_class_property_t* find_property(std::string_view name) const;

    auto index_miss_property() const { return _index_miss_property; }
    auto newindex_miss_property() const { return _newindex_miss_property; }

    int new_object(lua_State* L);

    lua_object_t* alloc_object(lua_State* L) { return _allocator(L); }

    lua_object_t* checkudata(lua_State*, int ud);
    lua_object_t* toudata(lua_State* L, int ud);

    template <typename T>
    T* toudata(lua_State* L, int ud) {
        return static_cast<T*>(toudata(L, ud));
    }
    template <typename T>
    T* checkudata(lua_State* L, int ud) {
        return static_cast<T*>(checkudata(L, ud));
    }

  private:
    static int lua_gc(lua_State* L);
};

const char* luaA_typename(lua_State*, int);
lua_class_t* luaA_class_get(lua_State*, int);

void luaA_openlib(lua_State*, const char*, const struct luaL_Reg[], const struct luaL_Reg[]);

template <typename T>
void destroyObject(lua_object_t* t) {
    static_cast<T*>(t)->~T();
}

int luaA_usemetatable(lua_State*, int, int);
int luaA_class_index(lua_State*);
int luaA_class_newindex(lua_State*);

namespace internal {
// clang-format off
template <lua_class_t* cls>
inline constexpr auto LuaClassMethods = std::array<luaL_Reg, 6>
{{
{
    "connect_signal",
    [](auto L) {
        cls->connect_signal(L, *Lua::checkstring(L, 1), 2);
        return 0;
    }
},
{
    "disconnect_signal",
    [](lua_State* L) {
        cls->disconnect_signal(L, *Lua::checkstring(L, 1), 2);
        return 0;
    }
},
{
    "emit_signal",
    [](lua_State* L) {
        cls->emit_signal(L, *Lua::checkstring(L, 1), lua_gettop(L) - 1);
        return 0;
    }
},
{
    "instances",
    [](lua_State* L) {
        lua_pushinteger(L, cls->numRefs());
        return 0;
    }
},
{
    "set_index_miss_handler",
   [](lua_State* L) { return Lua::registerfct(L, 1, &cls->index_miss_handler()); }
},
{
    "set_newindex_miss_handler",
   [](lua_State* L) { return Lua::registerfct(L, 1, &cls->newindex_miss_handler()); }
}
}};
// clang-format on

inline constexpr std::array<luaL_Reg, 2> LuaClassMeta = {
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
