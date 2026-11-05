// DLSS (vk_dlss.c): NVIDIA DLSS on the game's scene, upscaler = dlss. DLAA when the scene is drawn at the game
// resolution, DLSS Quality, Balanced or Performance at a smaller render scale.
#pragma once
#include "vk.h"

typedef struct VkExec VkExec;

int vk_dlss_wanted(void);   // upscaler = dlss at start (the device may still lack DLSS)
// vk_device.c: NGX's instance and device extensions join the lists (room for max names); vk_dlss_init after the device
// exists brings NGX up and checks that DLSS is available.
void vk_dlss_instance_exts(const char **ext, uint32_t *n, uint32_t max);
void vk_dlss_device_exts(const VkExtensionProperties *have, uint32_t nhave, const char **ext, uint32_t *n, uint32_t max);
void vk_dlss_init(void);
int vk_dlss_available(void);   // NGX came up with DLSS (after vk_dlss_init)
// vk_exec.c vk_exec_pass_begin, once the pass's targets are resolved and before its barrier: notes the scene's targets
// and, at the game's scaling pass, runs DLSS into that pass's target.
void vk_dlss_pass(VkExec *ex);
// vk_exec_draw.c prepare_draw, before the fixed state is recorded: notes the scene camera and decides whether the draw
// is jittered (marks the viewport dirty when that changes); vk_dlss_viewport moves a recorded viewport by the jitter.
void vk_dlss_draw(VkExec *ex, int32_t count);
void vk_dlss_viewport(VkViewport *v);
// vk_exec_draw.c prepare_draw: 1 skips the draw (the game's FXAA scaling draw, whose target DLSS already wrote).
int vk_dlss_skip_draw(VkExec *ex);
void vk_dlss_frame(void);   // each present (vk_backend.c present_image)
