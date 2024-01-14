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
#include "lua.h"
#include "objects/key.h"

#include <vector>

lua_class_t button_class{
  "button",
  NULL,
  {[](auto* state) { return static_cast<lua_object_t*>(button_new(state)); },
    destroyObject<button_t>,
    nullptr, Lua::class_index_miss_property,
    Lua::class_newindex_miss_property}
};

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
        if (button_class.toudata(L, -1)) {
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


void button_t::grab(xcb_window_t win) {
    xcb_grab_button(getGlobals().connection,
                    false,
                    win,
                    (XCB_EVENT_MASK_BUTTON_PRESS | XCB_EVENT_MASK_BUTTON_RELEASE),
                    XCB_GRAB_MODE_SYNC,
                    XCB_GRAB_MODE_ASYNC,
                    XCB_NONE,
                    XCB_NONE,
                    _button,
                    _modifiers);
}

void button_class_setup(lua_State* L) {
    static constexpr auto button_methods = DefineClassMethods<&button_class>({
      {"__call", [](auto* L) { return button_class.new_object(L); }}
    });

    static constexpr auto button_meta = DefineObjectMethods();

    button_class.setup(L, button_methods.data(), button_meta.data());
    auto setBtn = [](lua_State* L, button_t* b) -> int {
        b->set_button(luaL_checkinteger(L, -1));
        luaA_object_emit_signal(L, -3, "property::button", 0);
        return 0;
    };
    auto setMod = [](lua_State* L, button_t* b) {
        b->set_modifiers(luaA_tomodifiers(L, -1));
        luaA_object_emit_signal(L, -3, "property::modifiers", 0);
        return 0;
    };
    button_class.add_property(
      lua_class_property_t::make<button_t>("button", setBtn, [](lua_State * L, button_t * btn) { lua_pushinteger(L, btn->button()); return 1;}, setBtn));
    button_class.add_property(
      lua_class_property_t::make<button_t>("modifiers", setMod, [](lua_State * L, button_t * btn) { return luaA_pushmodifiers(L, btn->modifiers()); }, setMod));
}

/* @DOC_cobject_COMMON@ */

// vim: filetype=c:expandtab:shiftwidth=4:tabstop=8:softtabstop=4:textwidth=80
