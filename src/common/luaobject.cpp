/*
 * luaobject.c - useful functions for handling Lua objects
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

/** Handling of signals.
 *
 * This can not be used as a standalone class, but is instead referenced
 * explicitely in the classes, where it can be used. In the respective classes,
 * it then can be used via `classname:connect_signal(...)` etc.
 * @classmod signals
 */

#include "common/luaobject.h"

#include "common/backtrace.h"
#include "common/luaclass.h"
#include "common/lualib.h"
#include "common/signal.h"
#include "lua.h"

#include <format>

/** Setup the object system at startup.
 * \param L The Lua VM state.
 */
void luaA_object_setup(lua_State* L) {
    /* Push identification string */
    lua_pushliteral(L, LUAA_OBJECT_REGISTRY_KEY);
    /* Create an empty table */
    lua_newtable(L);
    /* Create an empty metatable */
    lua_newtable(L);
    /* Set this empty table as the registry metatable.
     * It's used to store the number of reference on stored objects. */
    lua_setmetatable(L, -2);
    /* Register table inside registry */
    lua_rawset(L, LUA_REGISTRYINDEX);
}

/** Increment a object reference in its store table.
 * \param L The Lua VM state.
 * \param tud The table index on the stack.
 * \param oud The object index on the stack.
 * \return A pointer to the object.
 */
void* luaA_object_incref(lua_State* L, int tud, int oud) {
    /* Get pointer value of the item */
    void* pointer = (void*)lua_topointer(L, oud);

    /* Not reference able. */
    if (!pointer) {
        lua_remove(L, oud);
        return NULL;
    }

    /* Push the pointer (key) */
    lua_pushlightuserdata(L, pointer);
    /* Push the data (value) */
    lua_pushvalue(L, oud < 0 ? oud - 1 : oud);
    /* table.lightudata = data */
    lua_rawset(L, tud < 0 ? tud - 2 : tud);

    /* refcount++ */

    /* Get the metatable */
    lua_getmetatable(L, tud);
    /* Push the pointer (key) */
    lua_pushlightuserdata(L, pointer);
    /* Get the number of references */
    lua_rawget(L, -2);
    /* Get the number of references and increment it */
    int count = lua_tointeger(L, -1) + 1;
    lua_pop(L, 1);
    /* Push the pointer (key) */
    lua_pushlightuserdata(L, pointer);
    /* Push count (value) */
    lua_pushinteger(L, count);
    /* Set metatable[pointer] = count */
    lua_rawset(L, -3);
    /* Pop metatable */
    lua_pop(L, 1);

    /* Remove referenced item */
    lua_remove(L, oud);

    return pointer;
}

/** Decrement a object reference in its store table.
 * \param L The Lua VM state.
 * \param tud The table index on the stack.
 * \param oud The object index on the stack.
 * \return A pointer to the object.
 */
void luaA_object_decref(lua_State* L, int tud, const void* pointer) {
    if (!pointer) {
        return;
    }

    /* First, refcount-- */
    /* Get the metatable */
    lua_getmetatable(L, tud);
    /* Push the pointer (key) */
    lua_pushlightuserdata(L, (void*)pointer);
    /* Get the number of references */
    lua_rawget(L, -2);
    /* Get the number of references and decrement it */
    int count = lua_tointeger(L, -1) - 1;
    /* Did we find the item in our table? (tointeger(nil)-1) is -1 */
    if (count < 0) {
        auto bt = backtrace_get();
        log_warn("BUG: Reference not found: {} {}\n{}", tud, pointer, bt);

        /* Pop reference count and metatable */
        lua_pop(L, 2);
        return;
    }
    lua_pop(L, 1);
    /* Push the pointer (key) */
    lua_pushlightuserdata(L, (void*)pointer);
    /* Hasn't the ref reached 0? */
    if (count) {
        lua_pushinteger(L, count);
    } else {
        /* Yup, delete it, set nil as value */
        lua_pushnil(L);
    }
    /* Set meta[pointer] = count/nil */
    lua_rawset(L, -3);
    /* Pop metatable */
    lua_pop(L, 1);

    /* Wait, no more ref? */
    if (!count) {
        /* Yes? So remove it from table */
        lua_pushlightuserdata(L, (void*)pointer);
        /* Push nil as value */
        lua_pushnil(L);
        /* table[pointer] = nil */
        lua_rawset(L, tud < 0 ? tud - 2 : tud);
    }
}

int luaA_settype(lua_State* L, lua_class_t* lua_class) {
    lua_pushlightuserdata(L, lua_class);
    lua_rawget(L, LUA_REGISTRYINDEX);
    lua_setmetatable(L, -2);
    return 1;
}

/** Add a signal.
 * @tparam string name A signal name.
 * @tparam func func A function to call when the signal is emitted.
 * @function connect_signal
 */
void luaA_object_connect_signal(lua_State* L, int oud, const char* name, lua_CFunction fn) {
    lua_pushcfunction(L, fn);
    luaA_object_connect_signal_from_stack(L, oud, name, -1);
}

/** Remove a signal.
 * @tparam string name A signal name.
 * @tparam func func A function to remove.
 * @function disconnect_signal
 */
void luaA_object_disconnect_signal(lua_State* L, int oud, const char* name, lua_CFunction fn) {
    lua_pushcfunction(L, fn);
    luaA_object_disconnect_signal_from_stack(L, oud, name, -1);
}

/** Add a signal to an object.
 * \param L The Lua VM state.
 * \param oud The object index on the stack.
 * \param name The name of the signal.
 * \param ud The index of function to call when signal is emitted.
 */
void luaA_object_connect_signal_from_stack(lua_State* L, int oud, const char* name, int ud) {
    Lua::checkfunction(L, ud);
    lua_object_t* obj = reinterpret_cast<lua_object_t*>(lua_touserdata(L, oud));
    obj->signals.connect(name, LuaFunction{luaA_object_ref_item(L, oud, ud)});
}

/** Remove a signal to an object.
 * \param L The Lua VM state.
 * \param oud The object index on the stack.
 * \param name The name of the signal.
 * \param ud The index of function to call when signal is emitted.
 */
void luaA_object_disconnect_signal_from_stack(lua_State* L, int oud, const char* name, int ud) {
    Lua::checkfunction(L, ud);
    lua_object_t* obj = reinterpret_cast<lua_object_t*>(lua_touserdata(L, oud));
    void* ref = (void*)lua_topointer(L, ud);
    if (obj->signals.disconnect(name, LuaFunction{ref})) {
        luaA_object_unref_item(L, oud, ref);
    }
    lua_remove(L, ud);
}

void signal_object_emit(lua_State* L, Signals* arr, const std::string_view& name, int nargs) {
    auto signalIt = arr->find(name);
    if (signalIt != arr->end()) {
        int nbfunc = signalIt->second.functions.size();
        luaL_checkstack(
          L,
          nbfunc + nargs + 1,
          fmt::format("Not enough stack space to call signal '{}' (trying to push {} entries)",
                      name,
                      nbfunc + nargs + 1)
            .c_str());
        /* Push all functions and then execute, because this list can change
         * while executing funcs. */
        for (auto func : signalIt->second.functions) {
            luaA_object_push(L, func);
        }

        for (int i = 0; i < nbfunc; i++) {
            /* push all args */
            for (int j = 0; j < nargs; j++) {
                lua_pushvalue(L, -nargs - nbfunc + i);
            }
            /* push first function */
            lua_pushvalue(L, -nargs - nbfunc + i);
            /* remove this first function */
            lua_remove(L, -nargs - nbfunc - 1 + i);
            Lua::dofunction(L, nargs, 0);
        }
    }

    /* remove args */
    lua_pop(L, nargs);
}

/** Emit a signal.
 * @tparam string name A signal name.
 * @param[opt] ... Various arguments.
 * @function emit_signal
 */
void luaA_object_emit_signal(lua_State* L, int oud, const char* name, int nargs) {
    int oud_abs = Lua::absindex(L, oud);
    lua_class_t* lua_class = luaA_class_get(L, oud);
    auto obj = lua_class->toudata<lua_object_t>(L, oud);
    if (!obj) {
        Lua::warn(L, "Trying to emit signal '%s' on non-object", name);
        return;
    } else if (!lua_class->check(obj)) {
        Lua::warn(L, "Trying to emit signal '%s' on invalid object", name);
        return;
    }
    auto signalIt = obj->signals.find(name);
    if (signalIt != obj->signals.end()) {
        int nbfunc = signalIt->second.functions.size();
        luaL_checkstack(L, nbfunc + nargs + 2, "too much signal");
        /* Push all functions and then execute, because this list can change
         * while executing funcs. */
        for (auto func : signalIt->second.functions) {
            luaA_object_push_item(L, oud_abs, func);
        }

        for (int i = 0; i < nbfunc; i++) {
            /* push object */
            lua_pushvalue(L, oud_abs);
            /* push all args */
            for (int j = 0; j < nargs; j++) {
                lua_pushvalue(L, -nargs - nbfunc - 1 + i);
            }
            /* push first function */
            lua_pushvalue(L, -nargs - nbfunc - 1 + i);
            /* remove this first function */
            lua_remove(L, -nargs - nbfunc - 2 + i);
            Lua::dofunction(L, nargs + 1, 0);
        }
    }

    /* Then emit signal on the class */
    lua_pushvalue(L, oud);
    lua_insert(L, -nargs - 1);
    luaA_class_get(L, -nargs - 1)->emit_signal(L, name, nargs + 1);
}

int luaA_object_tostring(lua_State* state) {
    Lua::State L{state};

    lua_class_t* lua_class = luaA_class_get(L.L, 1);
    auto object = lua_class->checkudata<lua_object_t>(L.L, 1);
    int offset = 0;

    for (; lua_class; lua_class = lua_class->parent()) {
        if (offset) {
            L.push("/");
            L.insert(-++offset);
        }
        L.push(lua_class->name());
        L.insert(-++offset);

        if (lua_class->has_tostring()) {
            int k, n;

            L.push("(");
            n = 2 + lua_class->tostring(L.L, object);
            L.push(")");

            for (k = 0; k < n; k++) {
                L.insert(-offset);
            }
            offset += n;
        }
    }

    L.push(std::format(": {}", static_cast<const void*>(object)));

    lua_pushfstring(L.L, ": %p", object);

    L.concat(offset + 1);

    return 1;
}

// vim: filetype=c:expandtab:shiftwidth=4:tabstop=8:softtabstop=4:textwidth=80
