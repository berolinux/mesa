# SPDX-License-Identifier: MIT

libnvidia_common = static_library(
  'nvidia_common',
  files('nv_device_info.c', 'nv_smoke_selftest.c'),
  include_directories : [
    inc_include, inc_src, inc_mesa, inc_gallium, inc_gallium_aux,
  ],
  c_args : [no_override_init_args],
  gnu_symbol_visibility : 'hidden',
  build_by_default : false,
)

idep_nvidia_common = declare_dependency(
  link_with : libnvidia_common,
  include_directories : include_directories('.'),
)

# Host-only smoke tool (G1/G2 vertical slice encode selftest, no GPU).
# Built when meson can find a C compiler; not installed by default.
if meson.can_run_host_binaries() or true
  nvidia_smoke_host = executable(
    'nvidia_smoke_host',
    files('../tools/nvidia_smoke_host.c'),
    include_directories : [include_directories('.')],
    install : false,
    build_by_default : false,
  )
  test('nvidia_smoke_host', nvidia_smoke_host,
       args : ['--check-g1-golden'],
       suite : ['nvidia'])
endif
