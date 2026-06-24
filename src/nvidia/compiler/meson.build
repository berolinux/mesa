# SPDX-License-Identifier: MIT

libnvidia_compiler = static_library(
  'nvidia_compiler',
  files('nv_nir.c', 'nv_nir_shader.c'),
  include_directories : [
    inc_include, inc_src, inc_mapi, inc_mesa, inc_gallium, inc_gallium_aux,
    inc_compiler,
    include_directories('../common'),
    include_directories('../rm'),
  ],
  c_args : [
    no_override_init_args,
    '-DHAVE_NIR=1',
  ],
  gnu_symbol_visibility : 'hidden',
  dependencies : [idep_nir_headers, idep_mesautil],
  build_by_default : false,
)

idep_nvidia_compiler = declare_dependency(
  link_with : libnvidia_compiler,
  include_directories : include_directories('.', '../common'),
)
