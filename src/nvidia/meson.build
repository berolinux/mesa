# SPDX-License-Identifier: MIT
# Top-level NVIDIA open driver tree (mirrors src/amd structure)
#
# Talks to the proprietary NVIDIA kernel module (nvidia.ko / nvidia-drm.ko)
# via libdrm_nvidia, NOT the nouveau kernel driver.

dep_libdrm_nvidia = dependency('libdrm_nvidia', required : false)
if not dep_libdrm_nvidia.found()
  dep_libdrm_nvidia = cc.find_library('drm_nvidia', required : false)
endif

subdir('common')
subdir('compiler')
subdir('rm')

if with_nvrm_vk
  subdir('vulkan')
endif

# Mesa-linked full host smoke (encode + compiler G2 via NV_SMOKE_SELFTEST_HAVE_NV_NIR).
# Built after compiler so idep_nvidia_compiler / idep_nir are available.
if meson.can_run_host_binaries() or true
  nvidia_smoke_full = executable(
    'nvidia_smoke_full',
    files(
      'tools/nvidia_smoke_full.c',
      'common/nv_smoke_selftest.c',
    ),
    include_directories : [
      inc_include, inc_src, inc_mesa, inc_gallium, inc_gallium_aux,
      include_directories('common'),
      include_directories('compiler'),
      include_directories('rm'),
      include_directories('../compiler'),
    ],
    c_args : [
      no_override_init_args,
      '-DNV_SMOKE_SELFTEST_HAVE_NV_NIR=1',
      '-DHAVE_NIR=1',
    ],
    dependencies : [idep_nvidia_compiler, idep_nir, idep_mesautil],
    install : false,
    build_by_default : false,
  )
  test('nvidia_smoke_full', nvidia_smoke_full,
       suite : ['nvidia'],
       is_parallel : false)
endif

# Standalone HW smoke CLI (links idep_nvidia_rm + common; needs nvidia.ko + libdrm_nvidia at runtime)
nvidia_smoke_hw_cli = executable(
  'nvidia_smoke_hw_cli',
  files('tools/nvidia_smoke_hw_cli.c'),
  include_directories : [
    inc_include, inc_src, inc_mesa, inc_gallium, inc_gallium_aux,
    include_directories('common'),
    include_directories('rm'),
  ],
  c_args : [no_override_init_args],
  dependencies : [idep_nvidia_rm, idep_nvidia_common, idep_mesautil] +
                 (dep_libdrm_nvidia.found() ? [dep_libdrm_nvidia] : []),
  install : false,
  build_by_default : false,
)
