project('pellicle', 'c', 'cpp', default_options: ['cpp_std=c++2a', 'c_std=gnu18'])
xorg_deps = [
    dependency('xcb', version : '>=1.6'),
    dependency('x11'),
    dependency('xcb-cursor'),
    dependency('xcb-randr'),
    dependency('xcb-xtest'),
    dependency('xcb-xinerama'),
    dependency('xcb-shape'),
    dependency('xcb-util', version : '>=0.3.8'),
    dependency('xcb-keysyms', version : '>=0.3.4'),
    dependency('xcb-icccm', version : '>=0.3.8'),
    dependency('xcb-xfixes'),
    dependency('xcb-xkb'),
    dependency('xcb-errors'),
    dependency('xkbcommon-x11'),
    dependency('cairo-xcb'),
    dependency('xproto', version : '>=7.0.15'),
    dependency('xcb-xrm'),
    dependency('libuv'),
    dependency('fmt'),
]
deps = [
    xorg_deps,
    dependency('glib-2.0', version : '>=2.40'),
    dependency('gdk-pixbuf-2.0'),
    dependency('cairo'),
    dependency('xkbcommon'),
    dependency('libstartup-notification-1.0', version : '>=0.10'),
    dependency('libxdg-basedir', version : '>=1.0.0'),
    dependency('dbus-1'),
    dependency('luajit')
]
srcs = [
    'src/awesome.cpp',
    'src/banning.cpp',
    'src/color.cpp',
    'src/dbus.cpp',
    'src/draw.cpp',
    'src/event.cpp',
    'src/ewmh.cpp',
    'src/keygrabber.cpp',
    'src/luaa.cpp',
    'src/mouse.cpp',
    'src/mousegrabber.cpp',
    'src/property.cpp',
    'src/root.cpp',
    'src/selection.cpp',
    'src/spawn.cpp',
    'src/stack.cpp',
    'src/strut.cpp',
    'src/systray.cpp',
    'src/xwindow.cpp',
    'src/options.cpp',
    'src/xkb.cpp',
    'src/xrdb.cpp',
    'src/common/atoms.cpp',
    'src/common/backtrace.cpp',
    'src/common/luaclass.cpp',
    'src/common/lualib.cpp',
    'src/common/luaobject.cpp',
    'src/common/util.cpp',
    'src/common/version.cpp',
    'src/common/xcursor.cpp',
    'src/common/xembed.cpp',
    'src/common/xutil.cpp',
    'src/objects/button.cpp',
    'src/objects/client.cpp',
    'src/objects/drawable.cpp',
    'src/objects/drawin.cpp',
    'src/objects/key.cpp',
    'src/objects/screen.cpp',
    'src/objects/selection_acquire.cpp',
    'src/objects/selection_transfer.cpp',
    'src/objects/selection_watcher.cpp',
    'src/objects/tag.cpp',
    'src/objects/selection_getter.cpp',
    'src/objects/window.cpp'
]

include_dir = [
    'src',
    'build'
]
cc = meson.get_compiler('c')
m_dep = cc.find_library('m', required : false)

atoms_ext = custom_target(
    output : 'atoms-extern.h',
    input : ['build-utils/atoms-ext.sh', 'src/common/atoms.list'],
    capture : true,
    command : ['/bin/sh', '@INPUT0@', '@INPUT1@'],
)

atoms_int = custom_target(
    output : 'atoms-intern.h',
    input : ['build-utils/atoms-int.sh', 'src/common/atoms.list'],
    capture : true,
    command : ['/bin/sh', '@INPUT0@', '@INPUT1@'],
)
#install_config ${INSTALL_PREFIX}/etc
#install xdg -> XDG_CONFIG_DIR | ${INSTALL_PREFIX}/xdg
#install data -> ${INSTALL_PREFIX}/share/pellicle
#install xsessions -> ${INSTALL_PREFIX}/share/xsessions
#install man -> ${INSTALL_PREFIX}/share/man

confdata = configuration_data()
data_path = '/usr/share/awesome'
confdata.set('AWESOME_LUA_LIB_PATH', data_path / 'lib')
confdata.set('XDG_CONFIG_DIR', '/etc/xdg')

confdata.set('AWESOME_THEMES_PATH', data_path / 'themes')
confdata.set('AWESOME_ICON_PATH', data_path / 'icons')
confdata.set('AWESOME_DEFAULT_CONF', '/etc/xdg/awesome/rc.lua')
confdata.set('WITH_DBUS', true)
confdata.set('WITH_XCB_ERRORS', true)
confdata.set('HAS_EXECINFO', 1)
confdata.set('HAS_LUAJIT', 1)

config_h = configure_file(input: 'templates/config.h.in', output: 'config.h', configuration:confdata, install:false)

versdata = configuration_data()
versdata.set('AWESOME_VERSION', '0.1')
versdata.set('AWESOME_RELEASE', 'bouncy')
versdata.set('AWESOME_API_LEVEL', 4)
version_h = configure_file(input: 'templates/awesome-version-internal.h.in', output: 'awesome-version-internal.h', configuration:versdata, install: false)

executable(
    'redundant',
    [srcs, atoms_int, atoms_ext, config_h, version_h],
    dependencies : [deps, m_dep],
    include_directories : include_dir,
    c_args: ['-Wno-unused-function'],
    cpp_args: ['-Wno-unused-function'],
)
