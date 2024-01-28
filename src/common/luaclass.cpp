/*
 * luaclass.c - useful functions for handling Lua classes
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

#include "common/luaclass.h"

#include "common/lualib.h"
#include "common/luaobject.h"
#include "common/signal.h"

#include <string_view>

#define CONNECTED_SUFFIX "::connected"

/** Convert a object to a udata if possible.
 * \param L The Lua VM state.
 * \param ud The index.
 * \return A pointer to the object, NULL otherwise.
 */
lua_object_t* lua_class_t::toudata(lua_State* L, int ud) {
    if (void* p = lua_touserdata(L, ud);
        p && lua_getmetatable(L, ud)) /* does it have a metatable? */
    {
        /* Get the lua_class_t that matches this metatable */
        lua_rawget(L, LUA_REGISTRYINDEX);
        lua_class_t* metatable_class = reinterpret_cast<lua_class_t*>(lua_touserdata(L, -1));

        /* remove lightuserdata (lua_class pointer) */
        lua_pop(L, 1);

        /* Now, check that the class given in argument is the same as the
         * metatable's object, or one of its parent (inheritance) */
        for (; metatable_class; metatable_class = metatable_class->parent()) {
            if (metatable_class == this) {
                return reinterpret_cast<lua_object_t*>(p);
            }
        }
    }
    return nullptr;
}

/** Check for a udata class.
 * \param L The Lua VM state.
 * \param ud The object index on the stack.
 */
lua_object_t* lua_class_t::checkudata(lua_State* L, int ud) {
    auto p = toudata(L, ud);
    if (!p) {
        Lua::typerror(L, ud, name().c_str());
    } else if (!check(p)) {
        luaL_error(L, "invalid object");
    }
    return p;
}

/** Get an object lua_class.
 * \param L The Lua VM state.
 * \param idx The index of the object on the stack.
 */
lua_class_t* luaA_class_get(lua_State* L, int idx) {
    int type = lua_type(L, idx);

    if (type == LUA_TUSERDATA && lua_getmetatable(L, idx)) {
        /* Use the metatable has key to get the class from registry */
        lua_rawget(L, LUA_REGISTRYINDEX);
        lua_class_t* cls = reinterpret_cast<lua_class_t*>(lua_touserdata(L, -1));
        lua_pop(L, 1);
        return cls;
    }

    return NULL;
}

/** Enhanced version of lua_typename that recognizes setup Lua classes.
 * \param L The Lua VM state.
 * \param idx The index of the object on the stack.
 */
const char* luaA_typename(lua_State* L, int idx) {
    int type = lua_type(L, idx);

    if (type == LUA_TUSERDATA) {
        lua_class_t* lua_class = luaA_class_get(L, idx);
        if (lua_class) {
            return lua_class->name().c_str();
        }
    }

    return lua_typename(L, type);
}

void luaA_openlib(lua_State* L,
                  const char* name,
                  const struct luaL_Reg methods[],
                  const struct luaL_Reg meta[]) {
    luaL_newmetatable(L, name);     /* 1 */
    lua_pushvalue(L, -1);           /* dup metatable                      2 */
    lua_setfield(L, -2, "__index"); /* metatable.__index = metatable      1 */

    Lua::setfuncs(L, meta);             /* 1 */
    Lua::registerlib(L, name, methods); /* 2 */
    lua_pushvalue(L, -1);               /* dup self as metatable              3 */
    lua_setmetatable(L, -2);            /* set self as metatable              2 */
    lua_pop(L, 2);
}

/** Newindex meta function for objects after they were GC'd.
 * \param L The Lua VM state.
 * \return The number of elements pushed on stack.
 */
static int newindex_invalid(lua_State* L) {
    return luaL_error(L, "attempt to index an object that was already garbage collected");
}

/** Index meta function for objects after they were GC'd.
 * \param L The Lua VM state.
 * \return The number of elements pushed on stack.
 */
static int index_invalid(lua_State* L) {
    auto attr = Lua::checkstring(L, 2);
    if (attr == "valid") {
        lua_pushboolean(L, false);
        return 1;
    }
    return newindex_invalid(L);
}

/** Garbage collect a Lua object.
 * \param L The Lua VM state.
 * \return The number of elements pushed on stack.
 */
int lua_class_t::lua_gc(lua_State* L) {
    lua_object_t* item = reinterpret_cast<lua_object_t*>(lua_touserdata(L, 1));
    item->signals.clear();
    /* Get the object class */
    lua_class_t* cls = reinterpret_cast<lua_class_t*>(luaA_class_get(L, 1));
    cls->deref();
    /* Call the collector function of the class (class is responsible for
     * collecting parent classes */
    if (cls->_collector) {
        cls->_collector(item);
    }
    /* Unset its metatable so that e.g. luaA_toudata() will no longer accept
     * this object. This is needed since other __gc methods can still use this.
     * We also make sure that `item.valid == false`.
     */
    lua_newtable(L);
    lua_pushcfunction(L, index_invalid);
    lua_setfield(L, -2, "__index");
    lua_pushcfunction(L, newindex_invalid);
    lua_setfield(L, -2, "__newindex");
    lua_setmetatable(L, 1);
    return 0;
}

/** Setup a new Lua class.
 * \param L The Lua VM state.
 * \param methods The methods to set on the class table.
 * \param meta The meta-methods to set on the class objects.
 */
void lua_class_t::setup(lua_State* L,
                        const struct luaL_Reg methods[],
                        const struct luaL_Reg meta[]) {
    /* Create the object metatable */
    lua_newtable(L);
    /* Register it with class pointer as key in the registry
     * class-pointer -> metatable */
    lua_pushlightuserdata(L, this);
    /* Duplicate the object metatable */
    lua_pushvalue(L, -2);
    lua_rawset(L, LUA_REGISTRYINDEX);
    /* Now register class pointer with metatable as key in the registry
     * metatable -> class-pointer */
    lua_pushvalue(L, -1);
    lua_pushlightuserdata(L, this);
    lua_rawset(L, LUA_REGISTRYINDEX);

    /* Duplicate objects metatable */
    lua_pushvalue(L, -1);
    /* Set garbage collector in the metatable */
    lua_pushcfunction(L, lua_class_t::lua_gc);
    lua_setfield(L, -2, "__gc");

    lua_setfield(L, -2, "__index"); /* metatable.__index = metatable      1 */

    Lua::setfuncs(L, meta);                      /* 1 */
    Lua::registerlib(L, _name.c_str(), methods); /* 2 */
    lua_pushvalue(L, -1);                        /* dup self as metatable              3 */
    lua_setmetatable(L, -2);                     /* set self as metatable              2 */
    lua_pop(L, 2);
}

void lua_class_t::connect_signal(lua_State* L, const std::string_view& name, lua_CFunction fn) {
    lua_pushcfunction(L, fn);
    connect_signal(L, name, -1);
}

void lua_class_t::connect_signal(lua_State* L, const std::string_view& name, int ud) {
    Lua::checkfunction(L, ud);

    /* Duplicate the function in the stack */
    lua_pushvalue(L, ud);

    std::string buf(name);
    buf += CONNECTED_SUFFIX;
    /* Create a new signal to notify there is a global connection. */

    /* Emit a signal to notify Lua of the global connection.
     *
     * This can useful during initialization where the signal needs to be
     * artificially emitted for existing objects as soon as something connects
     * to it
     */
    emit_signal(L, buf, 1);

    /* Register the signal to the CAPI list */
    _signals.connect(name, LuaFunction{luaA_object_ref(L, ud)});
}

void lua_class_t::disconnect_signal(lua_State* L, const std::string_view& name, int ud) {
    Lua::checkfunction(L, ud);
    void* ref = (void*)lua_topointer(L, ud);
    if (_signals.disconnect(name, LuaFunction{ref})) {
        luaA_object_unref(L, (void*)ref);
    }
    lua_remove(L, ud);
}

void lua_class_t::emit_signal(lua_State* L, const std::string_view& name, int nargs) {
    signal_object_emit(L, &_signals, name, nargs);
}

/** Try to use the metatable of an object.
 * \param L The Lua VM state.
 * \param idxobj The index of the object.
 * \param idxfield The index of the field (attribute) to get.
 * \return The number of element pushed on stack.
 */
int luaA_usemetatable(lua_State* L, int idxobj, int idxfield) {
    lua_class_t* cls = luaA_class_get(L, idxobj);

    for (; cls; cls = cls->parent()) {
        /* Push the cls */
        lua_pushlightuserdata(L, cls);
        /* Get its metatable from registry */
        lua_rawget(L, LUA_REGISTRYINDEX);
        /* Push the field */
        lua_pushvalue(L, idxfield);
        /* Get the field in the metatable */
        lua_rawget(L, -2);
        /* Do we have a field like that? */
        if (!lua_isnil(L, -1)) {
            /* Yes, so remove the metatable and return it! */
            lua_remove(L, -2);
            return 1;
        }
        /* No, so remove the metatable and its value */
        lua_pop(L, 2);
    }

    return 0;
}

const lua_class_property_t* lua_class_t::find_property(std::string_view name) const {
    for (auto cls = this; cls; cls = cls->parent()) {
        auto it = cls->_properties.find(name);
        if (it != cls->_properties.end()) {
            return &(*it);
        }
    }
    return nullptr;
}

/** Get a property of a object.
 * \param L The Lua VM state.
 * \param lua_class The Lua class.
 * \param fieldidx The index of the field name.
 * \return The object property if found, NULL otherwise.
 */
static const lua_class_property_t*
luaA_class_property_get(lua_State* L, lua_class_t* lua_class, int fieldidx) {
    /* Lookup the property using token */
    const auto attr = Lua::checkstring(L, fieldidx);
    return attr ? lua_class->find_property(attr.value()) : nullptr;
}

/** Generic index meta function for objects.
 * \param L The Lua VM state.
 * \return The number of elements pushed on stack.
 */
int luaA_class_index(lua_State* L) {
    /* Try to use metatable first. */
    if (luaA_usemetatable(L, 1, 2)) {
        return 1;
    }

    lua_class_t* cls = reinterpret_cast<lua_class_t*>(luaA_class_get(L, 1));

    /* Is this the special 'valid' property? This is the only property
     * accessible for invalid objects and thus needs special handling. */
    auto attr = Lua::checkstring(L, 2);

    if (attr == "valid") {
        auto p = cls->toudata<lua_object_t>(L, 1);
        lua_pushboolean(L, p && (cls->check(p)));
        return 1;
    }

    auto pushdata = [](lua_State* L, lua_class_t* cls) {
        cls->checkudata(L, 1);
        Lua::getuservalue(L, 1);
        lua_getfield(L, -1, "data");
        return 1;
    };

    /* This is the table storing the object private variables.
     */
    if (attr == "_private") {
        return pushdata(L, cls);
    } else if (attr == "data") {
        luaA_deprecate(L, "Use `._private` instead of `.data`");
        return pushdata(L, cls);
    }

    /* Property does exist and has an index callback */
    if (auto prop = cls->find_property(attr.value())) {
        if (prop->index) {
            return prop->index(L, cls->checkudata<lua_object_t>(L, 1));
        }
        return 0;
    }

    if (cls->index_miss_handler() != LUA_REFNIL) {
        return Lua::call_handler(L, cls->index_miss_handler());
    }

    if (cls->index_miss_property()) {
        return cls->index_miss_property()(L, cls->checkudata<lua_object_t>(L, 1));
    }

    return 0;
}

/** Generic newindex meta function for objects.
 * \param L The Lua VM state.
 * \return The number of elements pushed on stack.
 */
int luaA_class_newindex(lua_State* L) {
    /* Try to use metatable first. */
    if (luaA_usemetatable(L, 1, 2)) {
        return 1;
    }

    lua_class_t* cls = reinterpret_cast<lua_class_t*>(luaA_class_get(L, 1));

    /* Property does exist and has a newindex callback */
    if (auto prop = luaA_class_property_get(L, cls, 2)) {
        if (prop->newindex) {
            return prop->newindex(L, cls->checkudata<lua_object_t>(L, 1));
        }
        return 0;
    }

    if (cls->newindex_miss_handler() != LUA_REFNIL) {
        return Lua::call_handler(L, cls->newindex_miss_handler());
    }
    if (cls->newindex_miss_property()) {
        return cls->newindex_miss_property()(L, cls->checkudata<lua_object_t>(L, 1));
    }

    return 0;
}

/** Generic constructor function for objects.
 * \param L The Lua VM state.
 * \return The number of elements pushed on stack.
 */
int lua_class_t::new_object(lua_State* L) {
    /* Check we have a table that should contains some properties */
    Lua::checktable(L, 2);

    /* Create a new object */
    lua_object_t* object = alloc_object(L);

    /* Push the first key before iterating */
    lua_pushnil(L);
    /* Iterate over the property keys */
    while (lua_next(L, 2)) {
        /* Check that the key is a string.
         * We cannot call tostring blindly or Lua will convert a key that is a
         * number TO A STRING, confusing lua_next() */
        if (lua_isstring(L, -2)) {
            auto prop = luaA_class_property_get(L, this, -2);

            if (prop && prop->newobj) {
                prop->newobj(L, object);
            }
        }
        /* Remove value */
        lua_pop(L, 1);
    }

    return 1;
}

#undef CONNECTED_SUFFIX

// vim: filetype=c:expandtab:shiftwidth=4:tabstop=8:softtabstop=4:textwidth=80
