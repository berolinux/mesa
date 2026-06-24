# SPDX-License-Identifier: MIT
# Vulkan driver "nvrm" - open NVIDIA RM-backed implementation
#
# Entrypoint tables: generated weak stubs via vk_entrypoints_gen (prefix nvrm)
# plus hand-maintained constructor in nvrm_entrypoints.c that assigns the
# functions we implement.  Shared ICD wiring follows once dispatch is complete.

nvrm_files = files(
  'nvrm_instance.c',
  'nvrm_device.c',
  'nvrm_cmd_buffer.c',
  'nvrm_memory.c',
  'nvrm_pipeline.c',
  'nvrm_wsi.c',
)

# Generated weak entrypoint prototypes/stubs (like asahi/hk, radv, etc.)
nvrm_entrypoints = custom_target(
  'nvrm_entrypoints',
  input : [vk_entrypoints_gen, vk_api_xml],
  output : ['nvrm_entrypoints.h', 'nvrm_entrypoints_gen.c'],
  command : [
    prog_python, '@INPUT0@', '--xml', '@INPUT1@', '--proto', '--weak',
    '--out-h', '@OUTPUT0@', '--out-c', '@OUTPUT1@', '--prefix', 'nvrm',
    '--beta', with_vulkan_beta.to_string(),
  ],
  depend_files : vk_entrypoints_gen_depend_files,
)

# Hand-maintained dispatch table init (fills generated weak slots we implement).
nvrm_stub_entrypoints = files('nvrm_entrypoints.c')

libvulkan_nvrm = static_library(
  'vulkan_nvrm',
  [nvrm_files, nvrm_entrypoints, nvrm_stub_entrypoints],
  include_directories : [
    inc_include, inc_src, inc_mesa, inc_gallium, inc_gallium_aux,
    include_directories('../../compiler'),
    include_directories('../common'),
    include_directories('../rm'),
    include_directories('../compiler'),
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
    idep_vulkan_wsi_headers,
    idep_mesautil,
    idep_nir,
    idep_nvidia_common,
    idep_nvidia_rm,
    idep_nvidia_compiler,
    dep_libdrm,
    dep_thread,
  ],
  build_by_default : true,
)

idep_vulkan_nvrm = declare_dependency(
  link_with : libvulkan_nvrm,
  include_directories : include_directories('.'),
)
