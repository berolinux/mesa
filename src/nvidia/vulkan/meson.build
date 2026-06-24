# SPDX-License-Identifier: MIT
# Vulkan driver "nvrm" - open NVIDIA RM-backed implementation
#
# Dispatch tables are hand-maintained in nvrm_entrypoints.c (vk_*_dispatch_table
# for vk_device_init).  Optional generated weak stubs (nvrm_entrypoints_gen.c)
# are produced for header/reference but NOT linked into the static archive to
# avoid duplicate nvrm_*_entrypoints symbols with the hand tables.
#
# Shared ICD: links implementation sources directly (not link_whole of static
# that also pulls idep_vulkan_runtime duplicates of vk_icd* from instance.c).

nvrm_files = files(
  'nvrm_instance.c',
  'nvrm_device.c',
  'nvrm_cmd_buffer.c',
  'nvrm_memory.c',
  'nvrm_pipeline.c',
  'nvrm_wsi.c',
  'nvrm_entrypoints.c',
)

# Generated weak entrypoint header/stubs (available for future GetProcAddr work).
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

nvrm_inc = [
  inc_include, inc_src, inc_mesa, inc_gallium, inc_gallium_aux,
  include_directories('../../compiler'),
  include_directories('../common'),
  include_directories('../rm'),
  include_directories('../compiler'),
]

nvrm_deps = [
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
]

nvrm_c_args = [
  no_override_init_args,
  '-DVK_PROTOTYPES',
]

# Static library for in-tree linking / unit builds (hand entrypoints only;
# no vk_icd* symbols — those come from idep_vulkan_runtime when linked).
libvulkan_nvrm = static_library(
  'vulkan_nvrm',
  [nvrm_files, nvrm_entrypoints[0]],  # header dep only; not the _gen.c object
  include_directories : nvrm_inc,
  c_args : nvrm_c_args,
  gnu_symbol_visibility : 'hidden',
  dependencies : nvrm_deps,
  build_by_default : true,
)

idep_vulkan_nvrm = declare_dependency(
  link_with : libvulkan_nvrm,
  include_directories : include_directories('.'),
)

# Shared ICD: hand entrypoints only (not gen.c — same symbol names as hand
# dispatch tables).  NVRM_ICD_BUILD enables vk_icd* exports for the loader.
libvulkan_nvrm_shared = shared_library(
  'vulkan_nvrm',
  [nvrm_files, nvrm_entrypoints[0]],
  include_directories : nvrm_inc,
  c_args : nvrm_c_args + ['-DNVRM_ICD_BUILD=1'],
  link_args : [ld_args_build_id],
  gnu_symbol_visibility : 'hidden',
  dependencies : nvrm_deps,
  install : true,
)

nvrm_icd_file_name = libname_prefix + 'vulkan_nvrm.' + libname_suffix

nvrm_icd = custom_target(
  input : [vk_icd_gen, vk_api_xml],
  output : 'nvrm_icd.' + vulkan_manifest_suffix,
  command : [
    prog_python, '@INPUT0@',
    '--api-version', '1.3', '--xml', '@INPUT1@',
    '--sizeof-pointer', sizeof_pointer,
    '--icd-lib-path', vulkan_icd_lib_path,
    '--icd-filename', nvrm_icd_file_name,
    '--out', '@OUTPUT@',
  ],
  build_by_default : true,
  install_dir : with_vulkan_icd_dir,
  install_tag : 'runtime',
  install : true,
)

_nvrm_dev_icdname = 'nvrm_devenv_icd.@0@.json'.format(host_machine.cpu())
_nvrm_dev_icd = custom_target(
  input : [vk_icd_gen, vk_api_xml],
  output : _nvrm_dev_icdname,
  command : [
    prog_python, '@INPUT0@',
    '--api-version', '1.3', '--xml', '@INPUT1@',
    '--sizeof-pointer', sizeof_pointer,
    '--icd-lib-path', meson.current_build_dir(),
    '--icd-filename', nvrm_icd_file_name,
    '--out', '@OUTPUT@',
  ],
  build_by_default : true,
)

devenv.append('VK_DRIVER_FILES', _nvrm_dev_icd.full_path())
