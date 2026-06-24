# SPDX-License-Identifier: MIT

files_libnvgpu = files(
  'nvgpu_screen.c',
  'nvgpu_context.c',
  'nvgpu_resource.c',
)

libnvgpu = static_library(
  'nvgpu',
  files_libnvgpu,
  include_directories : [
    inc_include, inc_src, inc_mapi, inc_mesa, inc_gallium, inc_gallium_aux,
    inc_gallium_drivers,
    include_directories('../../../../include'),
    include_directories('../../../nvidia/common'),
    include_directories('../../../nvidia/rm'),
  ],
  c_args : [no_override_init_args],
  gnu_symbol_visibility : 'hidden',
  dependencies : [
    idep_mesautil,
    idep_nir_headers,
    idep_nvidia_common,
    idep_nvidia_rm,
    dep_libdrm,
  ],
  build_by_default : false,
)

driver_nvgpu = declare_dependency(
  compile_args : '-DGALLIUM_NVGPU',
  link_with : [libnvgpu],
  dependencies : [idep_nvidia_common, idep_nvidia_rm],
)
