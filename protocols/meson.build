wayland_scanner = find_program(
  wayland_scanner_dep.get_variable(pkgconfig: 'wayland_scanner'),
  native: true,
)

wayland_protocols_dir = wayland_protocols_dep.get_variable(pkgconfig: 'pkgdatadir')

wayland_protocols = {
  'xdg-output-unstable-v1': wayland_protocols_dir / 'unstable/xdg-output/xdg-output-unstable-v1.xml',
  'pointer-constraints-unstable-v1': wayland_protocols_dir / 'unstable/pointer-constraints/pointer-constraints-unstable-v1.xml',
  'relative-pointer-unstable-v1': wayland_protocols_dir / 'unstable/relative-pointer/relative-pointer-unstable-v1.xml',
  # not shipped by wayland-protocols, vendored from wlr-protocols
  'wlr-screencopy-unstable-v1': files('wlr-screencopy-unstable-v1.xml'),
}

wayland_protocol_sources = []
foreach name, xml : wayland_protocols
  wayland_protocol_sources += custom_target(
    name + '-client-protocol.h',
    input: xml,
    output: name + '-client-protocol.h',
    command: [wayland_scanner, 'client-header', '@INPUT@', '@OUTPUT@'],
  )
  wayland_protocol_sources += custom_target(
    name + '-protocol.c',
    input: xml,
    output: name + '-protocol.c',
    command: [wayland_scanner, 'private-code', '@INPUT@', '@OUTPUT@'],
  )
endforeach
