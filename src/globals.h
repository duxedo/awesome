#pragma once
#include "common/luahdr.h"
#ifdef WITH_DBUS
extern const struct luaL_Reg awesome_dbus_lib[];
#endif
extern const struct luaL_Reg awesome_keygrabber_lib[];
extern const struct luaL_Reg awesome_mousegrabber_lib[];
extern const struct luaL_Reg awesome_mouse_methods[];
extern const struct luaL_Reg awesome_mouse_meta[];
extern const struct luaL_Reg awesome_root_methods[];
extern const struct luaL_Reg awesome_root_meta[];
