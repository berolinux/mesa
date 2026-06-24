/* SPDX-License-Identifier: MIT
 *
 * Copyright 2026 - Open NVIDIA userspace driver project
 *
 * Gallium video codec (NVDEC bitstream decode) for nvgpu.
 */
#ifndef NVGPU_VIDEO_H
#define NVGPU_VIDEO_H

#include "pipe/p_video_codec.h"
#include "pipe/p_video_state.h"

struct pipe_context;
struct pipe_screen;

struct pipe_video_codec *
nvgpu_create_video_codec(struct pipe_context *context,
                         const struct pipe_video_codec *templ);

struct pipe_video_buffer *
nvgpu_create_video_buffer(struct pipe_context *context,
                          const struct pipe_video_buffer *tmpl);

int
nvgpu_screen_get_video_param(struct pipe_screen *pscreen,
                             enum pipe_video_profile profile,
                             enum pipe_video_entrypoint entrypoint,
                             enum pipe_video_cap param);

bool
nvgpu_screen_is_video_format_supported(struct pipe_screen *pscreen,
                                       enum pipe_format format,
                                       enum pipe_video_profile profile,
                                       enum pipe_video_entrypoint entrypoint);

#endif /* NVGPU_VIDEO_H */
