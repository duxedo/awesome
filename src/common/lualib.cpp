/*
 * lualib.h - useful functions and type for Lua
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

#include "common/lualib.h"

#include "common/luaclass.h"
#include "common/luaobject.h"
#include "lua.h"
#include "luaa.h"

namespace Lua {

lua_CFunction lualib_dofunction_on_error;

void checkfunction(lua_State* L, int idx) {
    if (!lua_isfunction(L, idx)) {
        typerror(L, idx, "function");
    }
}

void checktable(lua_State* L, int idx) {
    if (!lua_istable(L, idx)) {
        typerror(L, idx, "table");
    }
}

void dumpstack(lua_State* L) {
    fprintf(stderr, "-------- Lua stack dump ---------\n");
    for (int i = lua_gettop(L); i; i--) {
        int t = lua_type(L, i);
        switch (t) {
        case LUA_TSTRING: fprintf(stderr, "%d: string: `%s'\n", i, lua_tostring(L, i)); break;
        case LUA_TBOOLEAN:
            fprintf(stderr, "%d: bool:   %s\n", i, lua_toboolean(L, i) ? "true" : "false");
            break;
        case LUA_TNUMBER: fprintf(stderr, "%d: number: %g\n", i, lua_tonumber(L, i)); break;
        case LUA_TNIL: fprintf(stderr, "%d: nil\n", i); break;
        default:
            fprintf(stderr,
                    "%d: %s\t#%d\t%p\n",
                    i,
                    lua_typename(L, t),
                    (int)rawlen(L, i),
                    lua_topointer(L, i));
            break;
        }
    }
    fprintf(stderr, "------- Lua stack dump end ------\n");
}

int State::push(const std::string& str) {
    lua_pushlstring(L, str.c_str(), str.size());
    return 1;
}
int State::push(const std::string_view str) {
    lua_pushlstring(L, str.data(), str.size());
    return 1;
}
int State::push(int val) {
    lua_pushinteger(L, val);
    return 1;
};
int State::push(unsigned int val) {
    lua_pushinteger(L, val);
    return 1;
};
int State::push(bool val) {
    lua_pushboolean(L, val);
    return 1;
};
int State::push(lua_object_t* val) {
    luaA_object_push(L, val);
    return 1;
};
int call_handler(lua_State* L, Lua::FunctionRegistryIdx handlerIdx) {
    /* This is based on luaA_dofunction, but allows multiple return values */
    auto handler = handlerIdx.idx.idx;
    assert(handler != LUA_REFNIL);

    int nargs = lua_gettop(L);

    /* Push error handling function and move it before args */
    lua_pushcfunction(L, dofunction_error);
    lua_insert(L, -nargs - 1);
    int error_func_pos = 1;

    /* push function and move it before args */
    lua_rawgeti(L, LUA_REGISTRYINDEX, handler);
    lua_insert(L, -nargs - 1);

    if (lua_pcall(L, nargs, LUA_MULTRET, error_func_pos)) {
        log_warn("{}", lua_tostring(L, -1));
        /* Remove error function and error string */
        lua_pop(L, 2);
        return 0;
    }
    /* Remove error function */
    lua_remove(L, error_func_pos);
    return lua_gettop(L);
}
} // namespace Lua
// vim: filetype=c:expandtab:shiftwidth=4:tabstop=8:softtabstop=4:textwidth=80
