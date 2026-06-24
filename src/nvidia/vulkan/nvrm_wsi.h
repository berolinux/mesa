/*
 * Copyright 2026 - Open NVIDIA userspace driver project
 * SPDX-License-Identifier: MIT
 */
#ifndef NVRM_WSI_H
#define NVRM_WSI_H

#include "nvrm_private.h"

VkResult nvrm_init_wsi(struct nvrm_physical_device *pdev);
void nvrm_finish_wsi(struct nvrm_physical_device *pdev);

#endif /* NVRM_WSI_H */
