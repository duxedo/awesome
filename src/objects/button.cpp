/*
 * button.c - button managing
 *
 * Copyright © 2007-2009 Julien Danjou <julien@danjou.info>
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

/** awesome button API
 *
 * Furthermore to the classes described here, one can also use signals as
 * described in @{signals}.
 *
 * Some signal names are starting with a dot. These dots are artefacts from
 * the documentation generation, you get the real signal name by
 * removing the starting dot.
 *
 * @author Julien Danjou &lt;julien@danjou.info&gt;
 * @copyright 2008-2009 Julien Danjou
 * @coreclassmod button
 */

#include "button.h"

#include "common/luaclass.h"
#include "common/luaobject.h"
#include "lauxlib.h"

#include <vector>

lua_class_t button_class;

/** Button object.
 *
 * @tfield int button The mouse button number, or 0 for any button.
 * @tfield table modifiers The modifier key table that should be pressed while the
 *   button is pressed.
 * @table button
 */

/** Get the number of instances.
 * @treturn int The number of button objects alive.
 * @staticfct instances
 */

/** Set a __index metamethod for all button instances.
 * @tparam function cb The meta-method
 * @staticfct set_index_miss_handler
 */

/** Set a __newindex metamethod for all button instances.
 * @tparam function cb The meta-method
 * @staticfct set_newindex_miss_handler
 */

/** When bound mouse button + modifiers are pressed.
 * @param ... One or more arguments are possible
 * @signal press
 */

/** When property changes.
 * @signal property::button
 */

/** When property changes.
 * @signal property::modifiers
 */

/** When bound mouse button + modifiers are pressed.
 * @param ... One or more arguments are possible
 * @signal release
 */

/** Create a new mouse button bindings.
 * \param L The Lua VM state.
 * \return The number of elements pushed on stack.
 */
static int luaA_button_new(lua_State* L) { return luaA_class_new(L, &button_class); }

/** Set a button array with a Lua table.
 * \param L The Lua VM state.
 * \param oidx The index of the object to store items into.
 * \param idx The index of the Lua table.
 * \param buttons The array button to fill.
 */
void luaA_button_array_set(lua_State* L, int oidx, int idx, std::vector<button_t*>* buttons) {
    Lua::checktable(L, idx);

    for (auto button : *buttons) {
        luaA_object_unref_item(L, oidx, button);
    }

    buttons->clear();

    lua_pushnil(L);
    while (lua_next(L, idx)) {
        if (luaA_toudata(L, -1, &button_class)) {
            buttons->push_back((button_t*)luaA_object_ref_item(L, oidx, -1));
        } else {
            lua_pop(L, 1);
        }
    }
}

/** Push an array of button as an Lua table onto the stack.
 * \param L The Lua VM state.
 * \param oidx The index of the object to get items from.
 * \param buttons The button array to push.
 * \return The number of elements pushed on stack.
 */
int luaA_button_array_get(lua_State* L, int oidx, const std::vector<button_t*>& buttons) {
    lua_createtable(L, buttons.size(), 0);
    for (size_t i = 0; i < buttons.size(); i++) {
        luaA_object_push_item(L, oidx, buttons[i]);
        lua_rawseti(L, -2, i + 1);
    }
    return 1;
}

LUA_OBJECT_EXPORT_PROPERTY(button, button_t, button, lua_pushinteger);
LUA_OBJECT_EXPORT_PROPERTY(button, button_t, modifiers, luaA_pushmodifiers);

static int button_set_modifiers(lua_State* L, button_t* b) {
    b->modifiers = luaA_tomodifiers(L, -1);
    luaA_object_emit_signal(L, -3, "property::modifiers", 0);
    return 0;
}

static int button_set_button(lua_State* L, button_t* b) {
    b->button = luaL_checkinteger(L, -1);
    luaA_object_emit_signal(L, -3, "property::button", 0);
    return 0;
}

void button_class_setup(lua_State* L) {
    static constexpr auto button_methods = DefineClassMethods<&button_class>({
      {"__call", luaA_button_new}
    });

    static constexpr auto button_meta = DefineObjectMethods();

    luaA_class_setup(L,
                     &button_class,
                     "button",
                     NULL,
                     {[](auto* state) { return static_cast<lua_object_t*>(button_new(state)); },
                      destroyObject<button_t>,
                      nullptr,
                      Lua::class_index_miss_property,
                      Lua::class_newindex_miss_property},
                     button_methods.data(),
                     button_meta.data());

    button_class.add_property(
      {"button", button_set_button, luaA_button_get_button, button_set_button});
    button_class.add_property(
      {"modifiers", button_set_modifiers, luaA_button_get_modifiers, button_set_modifiers});
}

/* @DOC_cobject_COMMON@ */

// vim: filetype=c:expandtab:shiftwidth=4:tabstop=8:softtabstop=4:textwidth=80
