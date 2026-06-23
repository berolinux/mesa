# NVIDIA open userspace driver for Mesa

This tree implements an open-source replacement for the NVIDIA proprietary
userspace driver, talking to the same kernel module (`nvidia.ko`,
`nvidia-modeset.ko`, `nvidia-drm.ko`, `nvidia-uvm.ko`) and firmware that the
binary driver uses.

## Layout (mirrors `src/amd/`)

| Path | Role |
|------|------|
| `common/` | Shared GPU info, class IDs, pushbuffer helpers |
| `rm/` | Resource Manager client (wraps/extends libdrm_nvidia) |
| `compiler/` | NIR -> NVIDIA ISA compiler (SM50+) |
| `winsys/` | DRM/RM winsys for Gallium |
| `vulkan/` | Vulkan driver (`nvrm` / codename TBD) |
| `../gallium/drivers/nvgpu/` | Gallium3D pipe driver |

## Canonical references (do NOT trust nouveau/nvk without verification)

1. `open-gpu-kernel-modules/` - ioctl ABI, class headers, control commands
2. `NVIDIA-Linux-x86_64-610.43.02/` - proprietary userspace (disassemble for
   command buffer formats, object init sequences, compiler behavior)
3. `open-gpu-doc/` - supplementary, may be outdated

## Build

```
meson setup build -Dgallium-drivers=nvgpu -Dvulkan-drivers=nvrm \
  -Dgallium-rusticl=false
```

Requires libdrm built with `-Dnvidia=enabled` (libdrm_nvidia).
