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
int vk_dlss_on(void);       // DLSS runs (NGX up, upscaler = dlss), as of the last present
// A draw of the scene (with its depth, between the G-buffer and the post passes) on a frame DLSS runs on: the draws the
// motion passes below look at.
int vk_dlss_scene_draw(const VkExec *ex);

// ---- per-object motion vectors (ported from TotK Recomp, see vk_exec_draw.c "per-object motion" and vk_mvprev.c)
// vk_mvsurvey.c (XCX_MV_SURVEY): notes the uniform blocks of each scene draw, beside vk_dlss_draw; each present
// compares them with the previous frames' and logs a summary now and then.
void vk_mvsurvey_draw(VkExec *ex);
void vk_mvsurvey_frame(void);
void vk_mvprev_draw(VkExec *ex);   // vk_mvprev.c: notes the scene draw's object and bone blocks
// vk_exec_draw.c: the moving scene draws are listed again during the replay with their vertex shader's last-frame
// variant; vk_dlss.c mv_pass records them over the camera motion into ex->cb (and leaves every binding of the replay
// to be set again; draws whose camera block is not cam are left out).
// vk_dlss_prev_camera: last frame's camera block and this frame's jitter, 0 when there is no history.
int vk_mvobj_execute(VkExec *ex, uint64_t cam, VkImageView mv, VkImageView depth, VkFormat depth_fmt, uint32_t w,
                     uint32_t h);
int vk_dlss_prev_camera(VkDescriptorBufferInfo *out, float jitter[2]);
// vk_mvcheck.c (XCX_MV_CHECK, test): mv_pass hands it the motion image after the camera pass and, after the object
// pass, the motion image again and the scene light target it ran on; it measures how well the vectors fit the picture.
void vk_mvcheck_camera(VkCommandBuffer cb, VkImage mv, uint32_t w, uint32_t h, float jx, float jy);
void vk_mvcheck_final(VkCommandBuffer cb, uint64_t fence, VkImage mv, VkImage cimg, VkFormat cfmt, uint32_t level,
                      uint32_t layer, uint32_t w, uint32_t h, float jx, float jy);
const char *vk_mvobj_frame_masks(void);   // vk_exec_draw.c: the last motion list, per position and changed banks
void vk_mvcheck_output(VkCommandBuffer cb, VkImage out, uint32_t ow, uint32_t oh);   // after DLSS on a check frame
