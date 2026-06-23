# SPDX-License-Identifier: MIT

nv_rm_c_args = []
nv_rm_deps = []

if dep_libdrm_nvidia.found()
  nv_rm_c_args += '-DHAVE_LIBDRM_NVIDIA=1'
  nv_rm_deps += dep_libdrm_nvidia
endif

libnvidia_rm = static_library(
  'nvidia_rm',
  files('nv_rm.c'),
  include_directories : [
    inc_include, inc_src, inc_mapi, inc_mesa, inc_gallium, inc_gallium_aux,
    include_directories('../common'),
  ],
  c_args : [no_override_init_args, nv_rm_c_args],
  dependencies : nv_rm_deps,
  gnu_symbol_visibility : 'hidden',
  build_by_default : false,
)

idep_nvidia_rm = declare_dependency(
  link_with : libnvidia_rm,
  include_directories : include_directories('.', '../common'),
  dependencies : nv_rm_deps,
)
