project(
  'wlrsetroot',
  'c',
  version: '0.1.0',
  license: 'GPLv3',
  default_options: [
    'c_std=c11',
    'warning_level=2',
  ],
)

cc = meson.get_compiler('c')

# Dependencies
wayland_client = dependency('wayland-client')
wayland_protocols = dependency('wayland-protocols', version: '>=1.14')
wayland_scanner = dependency('wayland-scanner', native: true)

# Math library for floor/ceil if needed
math = cc.find_library('m', required: false)
rt = cc.find_library('rt', required: true)

# Wayland scanner program
wayland_scanner_prog = find_program(
  wayland_scanner.get_variable('wayland_scanner'),
  native: true,
)

# Protocol paths
wayland_protocols_dir = wayland_protocols.get_variable('pkgdatadir')

# Generate wlr-layer-shell protocol
wlr_layer_shell_xml = files('protocol/wlr-layer-shell-unstable-v1.xml')

wlr_layer_shell_c = custom_target(
  'wlr-layer-shell-unstable-v1-client-protocol.c',
  input: wlr_layer_shell_xml,
  output: 'wlr-layer-shell-unstable-v1-client-protocol.c',
  command: [wayland_scanner_prog, 'private-code', '@INPUT@', '@OUTPUT@'],
)

wlr_layer_shell_h = custom_target(
  'wlr-layer-shell-unstable-v1-client-protocol.h',
  input: wlr_layer_shell_xml,
  output: 'wlr-layer-shell-unstable-v1-client-protocol.h',
  command: [wayland_scanner_prog, 'client-header', '@INPUT@', '@OUTPUT@'],
)

# Generate xdg-shell protocol (needed for xdg_popup_interface reference)
xdg_shell_xml = wayland_protocols_dir / 'stable/xdg-shell/xdg-shell.xml'

xdg_shell_c = custom_target(
  'xdg-shell-client-protocol.c',
  input: xdg_shell_xml,
  output: 'xdg-shell-client-protocol.c',
  command: [wayland_scanner_prog, 'private-code', '@INPUT@', '@OUTPUT@'],
)

xdg_shell_h = custom_target(
  'xdg-shell-client-protocol.h',
  input: xdg_shell_xml,
  output: 'xdg-shell-client-protocol.h',
  command: [wayland_scanner_prog, 'client-header', '@INPUT@', '@OUTPUT@'],
)

# Source files
src_files = files(
  'src/main.c',
  'src/xbm.c',
  'src/pool-buffer.c',
)

executable(
  'wlrsetroot',
  src_files,
  wlr_layer_shell_c,
  wlr_layer_shell_h,
  xdg_shell_c,
  xdg_shell_h,
  include_directories: include_directories('include'),
  dependencies: [
    wayland_client,
    math,
    rt,
  ],
  install: true,
)
