/*
 * tag.h - tag management header
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

#pragma once

#include "client.h"
#include "common/luaclass.h"

#include <algorithm>
#include <memory>

int tags_get_current_or_first_selected_index(void);
void tag_client(lua_State*, client*);
void untag_client(client*, tag_t*);
bool is_client_tagged(client*, tag_t*);
void tag_unref_simplified(tag_t*);

/** Tag type */
struct tag_t: lua_object_t {
    /** Tag name */
    std::string name;
    /** true if activated */
    bool activated;
    /** true if selected */
    bool selected;
    /** clients in this tag */
    std::vector<client*> clients;
};

extern lua_class_t tag_class;

void tag_class_setup(lua_State*);

