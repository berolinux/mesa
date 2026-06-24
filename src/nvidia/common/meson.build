# SPDX-License-Identifier: MIT

libnvidia_common = static_library(
  'nvidia_common',
  files('nv_device_info.c'),
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
