/*
 * stack.h - client stack management header
 *
 * Copyright © 2020 Emmanuel Lepage-Vallee <elv1313@gmail.com>
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

#include <basedir.h>
#include <filesystem>
#include <optional>
#include <vector>

namespace Options {
/**
 * Initialization values extracted from the command line or modeline.
 */
typedef enum {
    INIT_FLAG_NONE = 0x0,
    INIT_FLAG_RUN_TEST = 0x1,
    INIT_FLAG_ARGB = 0x1 << 1,
    INIT_FLAG_REPLACE_WM = 0x1 << 2,
    INIT_FLAG_AUTO_SCREEN = 0x1 << 3,
    INIT_FLAG_ALLOW_FALLBACK = 0x1 << 4,
    INIT_FLAG_FORCE_CMD_ARGS = 0x1 << 5,
} awesome_init_config_t;

using Paths = std::vector<std::filesystem::path>;

struct ConfigResult {
    std::optional<std::filesystem::path> configPath;
    std::optional<int> api_level;
    bool have_searchpaths;
    bool had_overriden_depth;
    std::optional<bool> no_auto_screen;

    Paths searchPaths;
};

bool options_init_config(
  xdgHandle* xdg, char* execpath, const char* configpath, int* init_flags, Paths& paths);
ConfigResult options_check_args(int argc, char** argv, int* init_flags);
}
