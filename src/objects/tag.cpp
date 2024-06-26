/*
 * tag.c - tag management
 *
 * Copyright © 2007-2008 Julien Danjou <julien@danjou.info>
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

/** Labelled container where `client` objects can be stored.
 *
 * What is a tag?
 * ==============
 *
 * In AwesomeWM, a `tag` is a group of clients. It can either be used as labels
 * or as more classical workspaces depending on how they are configured.
 *
 * ![Client geometry](../images/tag_props.svg)
 *
 *  * A **tag** can be attached to **multiple clients**
 *  * A **client** can be attached to **multiple tags**
 *  * A **tag** can only be in 1 screen *any given time*, but can be moved
 *  * All **clients** attached to a tag **must be in the same screen as the tag**
 *
 * Creating tags
 * =============
 *
 * The default config initializes tags like this:
 *
 *    awful.tag(
 *      { "1", "2", "3", "4", "5", "6", "7", "8", "9" },
 *      s,
 *      awful.layout.layouts[1]
 *    )
 *
 * If you wish to have tags with different properties, then `awful.tag.add` is
 * a better choice:
 *
 *    awful.tag.add("First tag", {
 *        icon               = "/path/to/icon1.png",
 *        layout             = awful.layout.suit.tile,
 *        master_fill_policy = "master_width_factor",
 *        gap_single_client  = true,
 *        gap                = 15,
 *        screen             = s,
 *        selected           = true,
 *    })
 *
 *    awful.tag.add("Second tag", {
 *        icon = "/path/to/icon2.png",
 *        layout = awful.layout.suit.max,
 *        screen = s,
 *    })
 *
 * Note: the example above sets "First tag" to be selected explicitly,
 * because otherwise you will find yourself without any selected tag.
 *
 * Accessing tags
 * ==============
 *
 * To access the "current tags", use
 *
 *    local tags = awful.screen.focused().selected_tags
 *
 * See: `awful.screen.focused`
 *
 * See: `screen.selected_tags`
 *
 * To ignore the corner case where multiple tags are selected:
 *
 *    local t = awful.screen.focused().selected_tag
 *
 * See: `screen.selected_tag`
 *
 * To get all tags for the focused screen:
 *
 *    local tags = awful.screen.focused().tags
 *
 * See: `screen.tags`
 *
 * To get all tags:
 *
 *    local tags = root.tags()
 *
 * To get the current tag of the focused client:
 *
 *    local t = client.focus and client.focus.first_tag or nil
 *
 * See: `client.focus`
 * See: `client.first_tag`
 *
 * To get a tag from its name:
 *
 *    local t = awful.tag.find_by_name(awful.screen.focused(), "name")
 *
 * Common keybindings code
 * =======================
 *
 * Here is a few useful shortcuts not part of the default `rc.lua`. Add these
 * functions above `-- {{{ Key bindings`:
 *
 * Delete the current tag
 *
 *    local function delete_tag()
 *        local t = awful.screen.focused().selected_tag
 *        if not t then return end
 *        t:delete()
 *    end
 *
 * Create a new tag at the end of the list
 *
 *    local function add_tag()
 *        awful.tag.add("NewTag", {
 *            screen = awful.screen.focused(),
 *            layout = awful.layout.suit.floating }):view_only()
 *    end
 *
 * Rename the current tag
 *
 *    local function rename_tag()
 *        awful.prompt.run {
 *            prompt       = "New tag name: ",
 *            textbox      = awful.screen.focused().mypromptbox.widget,
 *            exe_callback = function(new_name)
 *                if not new_name or #new_name == 0 then return end
 *
 *                local t = awful.screen.focused().selected_tag
 *                if t then
 *                    t.name = new_name
 *                end
 *            end
 *        }
 *    end
 *
 * Move the focused client to a new tag
 *
 *    local function move_to_new_tag()
 *        local c = client.focus
 *        if not c then return end
 *
 *        local t = awful.tag.add(c.class,{screen= c.screen })
 *        c:tags({t})
 *        t:view_only()
 *    end
 *
 * Copy the current tag at the end of the list
 *
 *    local function copy_tag()
 *        local t = awful.screen.focused().selected_tag
 *        if not t then return end
 *
 *        local clients = t:clients()
 *        local t2 = awful.tag.add(t.name, awful.tag.getdata(t))
 *        t2:clients(clients)
 *        t2:view_only()
 *    end
 *
 * And, in the `globalkeys` table:
 *
 *    awful.key({ modkey,           }, "a", add_tag,
 *              {description = "add a tag", group = "tag"}),
 *    awful.key({ modkey, "Shift"   }, "a", delete_tag,
 *              {description = "delete the current tag", group = "tag"}),
 *    awful.key({ modkey, "Control"   }, "a", move_to_new_tag,
 *              {description = "add a tag with the focused client", group = "tag"}),
 *    awful.key({ modkey, "Mod1"   }, "a", copy_tag,
 *              {description = "create a copy of the current tag", group = "tag"}),
 *    awful.key({ modkey, "Shift"   }, "r", rename_tag,
 *              {description = "rename the current tag", group = "tag"}),
 *
 * See the
 * <a href="../documentation/05-awesomerc.md.html#global_keybindings">
 *   global keybindings
 * </a> for more information about the keybindings.
 *
 * Some signal names are starting with a dot. These dots are artefacts from
 * the documentation generation, you get the real signal name by
 * removing the starting dot.
 *
 * @DOC_uml_nav_tables_tag_EXAMPLE@
 *
 * @author Julien Danjou &lt;julien@danjou.info&gt;
 * @copyright 2008-2009 Julien Danjou
 * @coreclassmod tag
 */

#include "tag.h"

#include "banning.h"
#include "client.h"
#include "common/luaclass.h"
#include "common/luaobject.h"
#include "ewmh.h"
#include "globalconf.h"
#include "luaa.h"
#include "screen.h"

#include <algorithm>

lua_class_t tag_class{
  "tag",
  NULL,
  {[](auto* state) -> lua_object_t* { return newobj<tag_t, tag_class>(state); },
    destroyObject<tag_t>,
    nullptr, Lua::class_index_miss_property,
    Lua::class_newindex_miss_property},
};

/** Emitted when a tag requests to be selected.
 * @signal request::select
 * @tparam string context The reason why it was called.
 * @request tag select ewmh granted When the client request to be moved to a
 *  specific virtual desktop. AwesomeWM interprets virtual desktop as indexed
 *  tags.
 */

/**
 * This signal is emitted to request the list of default layouts.
 *
 * It is emitted on the global `tag` class rather than individual tag objects.
 * This default handler is part of `rc.lua`:
 *
 * @DOC_awful_tag_request_default_layouts_EXAMPLE@
 *
 * External modules can also use this signal to dynamically add additional
 * default layouts.
 *
 * @DOC_awful_tag_module_default_layouts_EXAMPLE@
 *
 * @signal request::default_layouts
 * @tparam string context The context (currently always "startup").
 * @classsignal
 * @request tag default_layouts startup granted When AwesomeWM starts, it queries
 *  for default layout using this request.
 * @see awful.layout.layouts
 * @see awful.layout.append_default_layout
 * @see awful.layout.remove_default_layout
 */

/** This signals is emitted when a tag needs layouts for the first time.
 *
 * If no handler implement it, it will fallback to the content added by
 * `request::default_layouts`
 *
 * @signal request::layouts
 * @tparam string context The context (currently always "awful").
 * @tparam table hints A, currently empty, table with hints.
 */

/** Emitted when a client gets tagged with this tag.
 * @signal tagged
 * @tparam client c The tagged client.
 */

/** Emitted when a client gets untagged with this tag.
 * @signal untagged
 * @tparam client c The untagged client.
 */

/**
 * Tag name.
 *
 * @DOC_sequences_tag_name_EXAMPLE@
 *
 * @property name
 * @tparam[opt=""] string name
 * @propemits false false
 */

/**
 * True if the tag is selected to be viewed.
 *
 * @DOC_sequences_tag_selected_EXAMPLE@
 *
 * @property selected
 * @tparam[opt=false] boolean selected
 * @propemits false false
 */

/**
 * True if the tag is active and can be used.
 *
 * @property activated
 * @tparam[opt=true] boolean activated
 * @propemits false false
 */

/** Get the number of instances.
 *
 * @treturn table The number of tag objects alive.
 * @staticfct instances
 */

/* Set a __index metamethod for all tag instances.
 * @tparam function cb The meta-method
 * @staticfct set_index_miss_handler
 */

/* Set a __newindex metamethod for all tag instances.
 * @tparam function cb The meta-method
 * @staticfct set_newindex_miss_handler
 */

void tag_unref_simplified(tag_t* tag) {
    lua_State* L = globalconf_get_lua_State();
    luaA_object_unref(L, tag);
}

/** View or unview a tag.
 * \param L The Lua VM state.
 * \param udx The index of the tag on the stack.
 * \param view Set selected or not.
 */
static void tag_view(lua_State* L, int udx, bool view) {
    auto tag = tag_class.checkudata<tag_t>(L, udx);
    if (tag->selected != view) {
        tag->selected = view;
        banning_need_update();
        for (auto* screen : Manager::get().screens) {
            screen_update_workarea(screen);
        }

        luaA_object_emit_signal(L, udx, "property::selected", 0);
    }
}

static void tag_client_emit_signal(tag_t* t, client* c, const char* signame) {
    lua_State* L = globalconf_get_lua_State();
    luaA_object_push(L, c);
    luaA_object_push(L, t);
    /* emit signal on client, with new tag as argument */
    luaA_object_emit_signal(L, -2, signame, 1);
    /* re push tag */
    luaA_object_push(L, t);
    /* move tag before client */
    lua_insert(L, -2);
    luaA_object_emit_signal(L, -2, signame, 1);
    /* Remove tag */
    lua_pop(L, 1);
}

/** Tag a client with the tag on top of the stack.
 * \param L The Lua VM state.
 * \param c the client to tag
 */
void tag_client(lua_State* L, client* c) {
    tag_t* t = (tag_t*)luaA_object_ref_class(L, -1, &tag_class);

    /* don't tag twice */
    if (is_client_tagged(c, t)) {
        luaA_object_unref(L, t);
        return;
    }

    t->clients.push_back(c);
    ewmh_client_update_desktop(c);
    banning_need_update();
    screen_update_workarea(c->screen);

    tag_client_emit_signal(t, c, "tagged");
}

/** Untag a client with specified tag.
 * \param c the client to tag
 * \param t the tag to tag the client with
 */
void untag_client(client* c, tag_t* t) {
    for (size_t i = 0; i < t->clients.size(); i++) {
        if (t->clients[i] == c) {
            lua_State* L = globalconf_get_lua_State();
            t->clients.erase(t->clients.begin() + i);
            banning_need_update();
            ewmh_client_update_desktop(c);
            screen_update_workarea(c->screen);
            tag_client_emit_signal(t, c, "untagged");
            luaA_object_unref(L, t);
            return;
        }
    }
}

/** Check if a client is tagged with the specified tag.
 * \param c the client
 * \param t the tag
 * \return true if the client is tagged with the tag, false otherwise.
 */
bool is_client_tagged(client* c, tag_t* t) {
    for (size_t i = 0; i < t->clients.size(); i++) {
        if (t->clients[i] == c) {
            return true;
        }
    }

    return false;
}

/** Get the index of the tag with focused client or first selected
 * \return Its index
 */
int tags_get_current_or_first_selected_index(void) {
    /* Consider "current desktop" a tag, that has focused window,
     * basically a tag user actively interacts with.
     * If no focused windows are present, fallback to first selected.
     */
    if (Manager::get().focus.client) {
        for (int i = 0; i < (int)Manager::get().tags.size(); i++) {
            auto& tag = Manager::get().tags[i];
            if (tag->selected && is_client_tagged(Manager::get().focus.client, tag.get())) {
                return i;
            }
        }
    }
    for (int i = 0; i < (int)Manager::get().tags.size(); i++) {
        auto& tag = Manager::get().tags[i];
        if (tag->selected) {
            return i;
        }
    }
    return 0;
}

/** Create a new tag.
 * \param L The Lua VM state.
 * \luastack
 * \lparam A name.
 * \lreturn A new tag object.
 */
static int luaA_tag_new(lua_State* L) { return tag_class.new_object(L); }

/** Get or set the clients attached to this tag.
 *
 * @tparam[opt=nil] table clients_table None or a table of clients to set as being tagged with
 *  this tag.
 * @treturn table A table with the clients attached to this tags.
 * @method clients
 */
static int luaA_tag_clients(lua_State* L) {
    auto tag = tag_class.checkudata<tag_t>(L, 1);
    auto& clients = tag->clients;
    size_t i;

    if (lua_gettop(L) == 2) {
        Lua::checktable(L, 2);
        for (size_t j = 0; j < clients.size(); j++) {
            client* c = clients[j];

            /* Only untag if we aren't going to add this tag again */
            bool found = false;
            lua_pushnil(L);
            while (lua_next(L, 2)) {
                auto tc = client_class.checkudata<client>(L, -1);
                /* Pop the value from lua_next */
                lua_pop(L, 1);
                if (tc != c) {
                    continue;
                }

                /* Pop the key from lua_next */
                lua_pop(L, 1);
                found = true;
                break;
            }
            if (!found) {
                untag_client(c, tag);
                j--;
            }
        }
        lua_pushnil(L);
        while (lua_next(L, 2)) {
            auto c = client_class.checkudata<client>(L, -1);
            /* push tag on top of the stack */
            lua_pushvalue(L, 1);
            tag_client(L, c);
            lua_pop(L, 1);
        }
    }

    lua_createtable(L, clients.size(), 0);
    for (i = 0; i < clients.size(); i++) {
        luaA_object_push(L, clients[i]);
        lua_rawseti(L, -2, i + 1);
    }

    return 1;
}

/** Set the tag name.
 * \param L The Lua VM state.
 * \param tag The tag to name.
 * \return The number of elements pushed on stack.
 */
static int luaA_tag_set_name(lua_State* L, lua_object_t* tag) {
    const char* buf = luaL_checkstring(L, -1);
    static_cast<tag_t*>(tag)->name = buf ? buf : "";
    luaA_object_emit_signal(L, -3, "property::name", 0);
    ewmh_update_net_desktop_names();
    return 0;
}

/** Set the tag selection status.
 * \param L The Lua VM state.
 * \param tag The tag to set the selection status for.
 * \return The number of elements pushed on stack.
 */
static int luaA_tag_set_selected(lua_State* L, lua_object_t*) {
    tag_view(L, -3, Lua::checkboolean(L, -1));
    return 0;
}

/** Set the tag activated status.
 * \param L The Lua VM state.
 * \param tag The tag to set the activated status for.
 * \return The number of elements pushed on stack.
 */
static int luaA_tag_set_activated(lua_State* L, lua_object_t* o) {
    auto tag = static_cast<tag_t*>(o);
    bool activated = Lua::checkboolean(L, -1);
    if (activated == tag->activated) {
        return 0;
    }

    tag->activated = activated;
    if (activated) {
        lua_pushvalue(L, -3);
        Manager::get().tags.emplace_back((tag_t*)luaA_object_ref_class(L, -1, &tag_class));
    } else {
        auto it = std::ranges::find_if(Manager::get().tags,
                                       [tag](const auto& tagptr) { return tagptr.get() == tag; });
        if (it != Manager::get().tags.end()) {
            auto tmp = std::move(*it);
            (void)tmp.release();
            Manager::get().tags.erase(it);
        }

        if (tag->selected) {
            tag->selected = false;
            luaA_object_emit_signal(L, -3, "property::selected", 0);
            banning_need_update();
        }
        luaA_object_unref(L, tag);
    }
    ewmh_update_net_numbers_of_desktop();
    ewmh_update_net_desktop_names();

    luaA_object_emit_signal(L, -3, "property::activated", 0);

    return 0;
}

void tag_class_setup(lua_State* L) {

    static constexpr auto methods = DefineClassMethods<&tag_class>({
      {"__call", luaA_tag_new}
    });

    static constexpr auto meta = DefineObjectMethods({
      {"clients", luaA_tag_clients}
    });

    tag_class.setup(L, methods.data(), meta.data());
    tag_class.add_property(
      "name", luaA_tag_set_name, exportProp<&tag_t::name>(), luaA_tag_set_name);
    tag_class.add_property(
      "selected", luaA_tag_set_selected, exportProp<&tag_t::selected>(), luaA_tag_set_selected);
    tag_class.add_property(
      "activated", luaA_tag_set_activated, exportProp<&tag_t::activated>(), luaA_tag_set_activated);
}

/* @DOC_cobject_COMMON@ */

// vim: filetype=c:expandtab:shiftwidth=4:tabstop=8:softtabstop=4:textwidth=80
