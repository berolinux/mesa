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
