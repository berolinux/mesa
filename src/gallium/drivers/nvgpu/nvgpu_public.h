/*
 * Copyright 2026 - Open NVIDIA userspace driver project
 * SPDX-License-Identifier: MIT
 */
#ifndef NVGPU_PUBLIC_H
#define NVGPU_PUBLIC_H

struct pipe_screen;
struct sw_winsys;
struct pipe_screen_config;

struct pipe_screen *nvgpu_screen_create(int fd,
                                        const struct pipe_screen_config *config,
                                        struct sw_winsys *winsys);

#endif
