# SPDX-License-Identifier: MIT
# Vulkan driver "nvrm" - open NVIDIA RM-backed implementation
#
# Early bring-up: sources compile but full ICD entrypoint generation
# (vk_entrypoints_gen) is wired incrementally.  Until then we build as a
# static library that mesa links tests/tools against; shared ICD is enabled
# once entrypoint tables are generated.

nvrm_files = files(
  'nvrm_instance.c',
  'nvrm_device.c',
  'nvrm_cmd_buffer.c',
  'nvrm_memory.c',
)

# Hand-maintained entrypoint tables (minimal) so we compile without codegen yet.
nvrm_stub_entrypoints = files('nvrm_entrypoints.c')

libvulkan_nvrm = static_library(
  'vulkan_nvrm',
  [nvrm_files, nvrm_stub_entrypoints],
  include_directories : [
    inc_include, inc_src, inc_mapi, inc_mesa, inc_gallium, inc_gallium_aux,
    include_directories('../common'),
    include_directories('../rm'),
  ],
  c_args : [
    no_override_init_args,
    '-DVK_PROTOTYPES',
  ],
  gnu_symbol_visibility : 'hidden',
  dependencies : [
    idep_vulkan_util,
    idep_vulkan_runtime,
    idep_vulkan_wsi,
    idep_mesautil,
    idep_nir,
    idep_nvidia_common,
    idep_nvidia_rm,
    dep_libdrm,
    dep_thread,
  ],
  build_by_default : true,
)

idep_vulkan_nvrm = declare_dependency(
  link_with : libvulkan_nvrm,
  include_directories : include_directories('.'),
)
