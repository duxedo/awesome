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

#include "options.h"

#include "common/util.h"
#include "common/version.h"
#include "config.h"

#include <basedir_fs.h>
#include <bits/getopt_core.h>
#include <cstring>
#include <filesystem>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

#define KEY_VALUE_BUF_MAX 64
#define READ_BUF_MAX 127

namespace Options {

static std::optional<int> check_api_level(char* value) {
    if (!value) {
        return {};
    }

    char* ptr;
    int ret = strtol(value, &ptr, 10);

    /* There is no valid number at all */
    if (value == ptr) {
        fprintf(stderr, "Invalid API level %s\n", value);
        return {};
    }

    /* There is a number, but also letters, this is invalid */
    if (ptr[0] != '\0' && ptr[0] != '.') {
        fprintf(stderr, "Invalid API level %s\n", value);
        return {};
    }

    /* This API level doesn't exist, fallback to v4 */
    if (ret < 4) {
        ret = 4;
    }

    return ret;
}

static void push_arg(std::vector<char*>& args, char* value, size_t* len) {
    value[*len] = '\0';
    args.push_back(strdup(value));
    (*len) = 0;
}

/*
 * Support both shebang and modeline modes.
 */
bool options_init_config(
  xdgHandle* xdg, char* execpath, const char* configpath, int* init_flags, Paths& paths) {
    /* The different state the parser can have. */
    enum {
        MODELINE_STATE_INIT,        /* Start of line        */
        MODELINE_STATE_NEWLINE,     /* Start of line        */
        MODELINE_STATE_COMMENT,     /* until --             */
        MODELINE_STATE_MODELINE,    /* until awesome_mode:  */
        MODELINE_STATE_SHEBANG,     /* until !              */
        MODELINE_STATE_KEY_DELIM,   /* until the key begins */
        MODELINE_STATE_KEY,         /* until '='            */
        MODELINE_STATE_VALUE_DELIM, /* after ':'            */
        MODELINE_STATE_VALUE,       /* until ',' or '\n'    */
        MODELINE_STATE_COMPLETE,    /* Parsing is done      */
        MODELINE_STATE_INVALID,     /* note a modeline      */
        MODELINE_STATE_ERROR        /* broken modeline      */
    } state = MODELINE_STATE_INIT;

    /* The parsing mode */
    enum {
        MODELINE_MODE_NONE,    /* No modeline */
        MODELINE_MODE_LINE,    /* modeline    */
        MODELINE_MODE_SHEBANG, /* #! shebang  */
    } mode = MODELINE_MODE_NONE;

    static const unsigned char name[] = "awesome_mode:";
    static char key_buf[KEY_VALUE_BUF_MAX + 1] = {'\0'};
    static char file_buf[READ_BUF_MAX + 1] = {'\0'};
    size_t pos = 0;

    std::vector<char*> argv;
    argv.push_back(execpath ? strdup(execpath) : strdup(""));

    FILE* fp = NULL;

    /* It is too early to know which config works. So we assume
     * the first one found is the one to use for the modeline. This
     * may or may not end up being the config that works. However since
     * knowing if the config works requires to load it, and loading must
     * be done only after the modeline is parsed, this is the best we can
     * do
     */
    if (!configpath) {
        const char* xdg_confpath = xdgConfigFind("awesome/rc.lua", xdg);

        /* xdg_confpath is "string1\0string2\0string3\0\0" */
        if (xdg_confpath && *xdg_confpath) {
            fp = fopen(xdg_confpath, "r");
        } else {
            fp = fopen(AWESOME_DEFAULT_CONF, "r");
        }

        p_delete(&xdg_confpath);
    } else {
        fp = fopen(configpath, "r");
    }

    /* Share the error codepath with parsing errors */
    if (!fp) {
        return false;
    }

    /* Try to read the first line */
    if (!fgets(file_buf, READ_BUF_MAX, fp)) {
        fclose(fp);
        return false;
    }

    unsigned char c;

    /* Simple state machine to translate both modeline and shebang into argv */
    for (int i = 0; (c = file_buf[i++]) != '\0';) {
        /* Be very permissive, skip the unknown, UTF is not allowed */
        if ((c > 126 || c < 32) && c != 10 && c != 13 && c != 9) {
            static bool once = true;
            /* Print a warning once */
            if (once) {
                fprintf(stderr, "WARNING: modelines must use ASCII\n");
                once = false;
            }
            continue;
        }

        switch (state) {
        case MODELINE_STATE_INIT:
            switch (c) {
            case '#': state = MODELINE_STATE_SHEBANG; break;
            case ' ':
            case '-': state = MODELINE_STATE_COMMENT; break;
            default: state = MODELINE_STATE_INVALID;
            }
            break;
        case MODELINE_STATE_NEWLINE:
            switch (c) {
            case ' ':
            case '-': state = MODELINE_STATE_COMMENT; break;
            default: state = MODELINE_STATE_INVALID;
            }
            break;
        case MODELINE_STATE_COMMENT:
            switch (c) {
            case '-': state = MODELINE_STATE_MODELINE; break;
            default: state = MODELINE_STATE_INVALID;
            }
            break;
        case MODELINE_STATE_MODELINE:
            if (c == ' ') {
                break;
            } else if (c != name[pos++]) {
                state = MODELINE_STATE_INVALID;
                pos = 0;
            }

            if (pos == 13) {
                pos = 0;
                state = MODELINE_STATE_KEY_DELIM;
                mode = MODELINE_MODE_LINE;
            }

            break;
        case MODELINE_STATE_SHEBANG:
            switch (c) {
            case '!':
                mode = MODELINE_MODE_SHEBANG;
                state = MODELINE_STATE_KEY_DELIM;
                break;
            default: state = MODELINE_STATE_INVALID;
            }
            break;
        case MODELINE_STATE_KEY_DELIM:
            switch (c) {
            case ' ':
            case '\t':
            case ':':
            case '=': break;
            case '\n':
            case '\r': state = MODELINE_STATE_ERROR; break;
            default:
                /* In modeline mode, assume all keys are the long name */
                switch (mode) {
                case MODELINE_MODE_LINE:
                    strcpy(key_buf, "--");
                    pos = 2;
                    break;
                case MODELINE_MODE_SHEBANG:
                case MODELINE_MODE_NONE: break;
                };
                state = MODELINE_STATE_KEY;
                key_buf[pos++] = c;
            }
            break;
        case MODELINE_STATE_KEY:
            switch (c) {
            case '=':
                push_arg(argv, key_buf, &pos);
                state = MODELINE_STATE_VALUE_DELIM;
                break;
            case ' ':
            case '\t':
            case ':': push_arg(argv, key_buf, &pos); state = MODELINE_STATE_KEY_DELIM;
            }
            break;
        case MODELINE_STATE_VALUE_DELIM:
            switch (c) {
            case ' ':
            case '\t': break;
            case '\n':
            case '\r': state = MODELINE_STATE_ERROR; break;
            case ':': state = MODELINE_STATE_KEY_DELIM; break;
            default: state = MODELINE_STATE_VALUE; key_buf[pos++] = c;
            }
            break;
        case MODELINE_STATE_VALUE:
            switch (c) {
            case ',':
            case ' ':
            case ':':
            case '\t':
                push_arg(argv, key_buf, &pos);
                state = MODELINE_STATE_KEY_DELIM;
                break;
            case '\n':
            case '\r':
                push_arg(argv, key_buf, &pos);
                state = MODELINE_STATE_COMPLETE;
                break;
            default: key_buf[pos++] = c;
            }
            break;
        case MODELINE_STATE_INVALID:
            /* This cannot happen, the `if` below should prevent it */
            state = MODELINE_STATE_ERROR;
            break;
        case MODELINE_STATE_COMPLETE:
        case MODELINE_STATE_ERROR: break;
        }

        /* No keys or values are that large */
        if (pos >= KEY_VALUE_BUF_MAX) {
            state = MODELINE_STATE_ERROR;
        }

        /* Stop parsing when completed */
        if (state == MODELINE_STATE_ERROR || state == MODELINE_STATE_COMPLETE) {
            break;
        }

        /* Try the next line */
        if (((i == READ_BUF_MAX || file_buf[i] == '\0') && !feof(fp)) ||
            state == MODELINE_STATE_INVALID) {
            if (state == MODELINE_STATE_KEY || state == MODELINE_STATE_VALUE) {
                push_arg(argv, key_buf, &pos);
            }

            /* Skip empty lines */
            do {
                if (fgets(file_buf, READ_BUF_MAX, fp)) {
                    state = MODELINE_STATE_NEWLINE;
                } else {
                    state = argv.size() ? MODELINE_STATE_COMPLETE : MODELINE_STATE_ERROR;
                    break;
                }

                i = 0; /* Always reset `i` to avoid an unlikely invalid read */
            } while (i == 0 && file_buf[0] == '\n');
        }
    }

    fclose(fp);

    /* Reset the global POSIX args counter */
    optind = 0;

    /* Be future proof, allow let unknown keys live, let the Lua code decide */
    (*init_flags) |= INIT_FLAG_ALLOW_FALLBACK;

    auto opts = options_check_args(argv.size(), argv.data(), init_flags);
    paths.insert(paths.end(), opts.searchPaths.begin(), opts.searchPaths.end());
    /* Cleanup */
    for (auto& each : argv) {
        free(each);
    }

    return state == MODELINE_STATE_COMPLETE;
}

/** Print help and exit(2) with given exit_code.
 * \param exit_code The exit code.
 */
static void __attribute__((noreturn)) exit_help(int exit_code) {
    FILE* outfile = (exit_code == EXIT_SUCCESS) ? stdout : stderr;
    fprintf(outfile,
            "Usage: awesome [OPTION]\n\
  -h, --help             show help\n\
  -v, --version          show version\n\
  -c, --config FILE      configuration file to use\n\
  -f, --force            ignore modelines and apply the command line arguments\n\
  -s, --search DIR       add a directory to the library search path\n\
  -k, --check            check configuration file syntax\n\
  -a, --no-argb          disable client transparency support\n\
  -l  --api-level LEVEL  select a different API support level than the current version \n\
  -m, --screen on|off    enable or disable automatic screen creation (default: on)\n\
  -r, --replace          replace an existing window manager\n");
    exit(exit_code);
}

#define ARG 1
#define NO_ARG 0

ConfigResult options_check_args(int argc, char** argv, int* init_flags) {

    static struct option long_options[] = {
      {     "help", NO_ARG, NULL,  'h'},
      {  "version", NO_ARG, NULL,  'v'},
      {   "config",    ARG, NULL,  'c'},
      {    "force", NO_ARG, NULL,  'f'},
      {    "check", NO_ARG, NULL,  'k'},
      {   "search",    ARG, NULL,  's'},
      {  "no-argb", NO_ARG, NULL,  'a'},
      {  "replace", NO_ARG, NULL,  'r'},
      {   "screen",    ARG, NULL,  'm'},
      {"api-level",    ARG, NULL,  'l'},
      {     "reap",    ARG, NULL, '\1'},
      {       NULL, NO_ARG, NULL,    0}
    };

    ConfigResult ret;
    int opt;
    using namespace std::literals;

    while ((opt = getopt_long(argc, argv, "vhfkc:arms:l:", long_options, NULL)) != -1) {
        switch (opt) {
        case 'v': eprint_version(); break;
        case 'h':
            if (!((*init_flags) & INIT_FLAG_ALLOW_FALLBACK)) {
                exit_help(EXIT_SUCCESS);
            }
            break;
        case 'f': (*init_flags) |= INIT_FLAG_FORCE_CMD_ARGS; break;
        case 'k': (*init_flags) |= INIT_FLAG_RUN_TEST; break;
        case 'c':
            if (ret.configPath) {
                log_fatal("--config may only be specified once");
            }
            ret.configPath = optarg;

            /* Make sure multi-file config works */
            ret.searchPaths.push_back(ret.configPath.value().parent_path());
            break;
        case 'm':
            /* Validation */
            if ((!optarg) || ("off"sv != optarg && "on"sv != optarg)) {
                log_fatal("The possible values of -m/--screen are \"on\" or \"off\"");
            }

            ret.no_auto_screen = ("off"sv == optarg);

            (*init_flags) &= ~INIT_FLAG_AUTO_SCREEN;

            break;
        case 's': ret.searchPaths.push_back(optarg); break;
        case 'a':
            ret.had_overriden_depth = true;
            (*init_flags) &= ~INIT_FLAG_ARGB;
            break;
        case 'r': (*init_flags) |= INIT_FLAG_REPLACE_WM; break;
        case 'l': ret.api_level = check_api_level(optarg); break;
        case '\1':
            /* Silently ignore --reap and its argument */
            break;
        default:
            if (!((*init_flags) & INIT_FLAG_ALLOW_FALLBACK)) {
                exit_help(EXIT_FAILURE);
            }
            break;
        }
    }

    return ret;
}
} // namespace Options
#undef AR
#undef NO_ARG
#undef KEY_VALUE_BUF_MAX
#undef READ_BUF_MAX
