// HDR output (XCX_HDR=1): an HDR10 swapchain (Rec. 2020 primaries, PQ) and a game picture with highlights above the
// SDR white. Settings: XCX_HDR (read at start), XCX_HDR_PEAK_NITS (the display's peak), XCX_HDR_GAME_NITS (the
// picture's white, "paper white"), XCX_HDR_UI_NITS (the HUD's and this menu's white) and XCX_HDR_HUE (how far bright
// colours turn towards white as in SDR), the four read when the settings change. With XCX_HDR=0 nothing here runs and
// the present is what it was.
//
// The game's picture (docs/architecture.md, "HDR"). The game's tonemap pass scales the r11g11b10f scene by its exposure
// gain, clamps it to 0..1 and grades it through a 16x16x16 LUT, writing the result back into the same image; FXAA then
// draws that into the 8-bit window texture it presents, and the HUD goes on top. So:
// 1. vk_hdr_shader_swap: the tonemap's fragment shader becomes shaders/hdr_tonemap.frag, which keeps the game's
//    result within the SDR white and, above it, writes highlights up to the display's peak into the float image. It is
//    recognised by its constant and resources, not by a hash, so another translator still finds it; its SPIR-V is
//    replaced in memory only.
// 2. vk_hdr_draw / vk_hdr_present_source: a draw into a float target that samples that same image and a 3D texture
//    is the tonemap; the next draw into a presented window texture ties that image to it. A window texture drawn
//    without a tonemap since the last present (menus, movies) has none and stays SDR at the HUD's white. The present
//    is queued before the next frame's work (vk_backend.c), so the float image is read before the game draws into it
//    again.
// 3. vk_hdr_encode: on the present, the crop rectangle goes to PQ code values in an RGBA16F image (shaders/hdr_encode.comp)
//    that FSR 1 or the blit scale into the swapchain. Where the window texture shows the scene, what the float image
//    holds above the SDR white is added back; the HUD stays at the HUD's white. vk_hdr_ui_*: the in-game menu draws
//    into an RGBA8 image and shaders/hdr_ui.frag blends it onto the swapchain at the HUD's white.
// Adapted from TotK Recomp's runtime/vk_hdr.c (github.com/valentderah/totk-pc-recomp, MIT, see THIRD_PARTY_NOTICES.md):
// the settings, the swapchain, the shader swap by names and bindings, the present passes and the menu pass are its;
// the tonemap match, the tonemap and the merge of the 8-bit picture with the float scene are XCX's. TotK Recomp's
// tonemap follows RenoDX's TotK mod by souperman9 and Musa on ShortFuse's RenoDX (github.com/clshortfuse/renodx, MIT).
#include "vk.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cfg.h"
#include "gfx.h"
#include "hdr_encode_spv.h"
#include "hdr_fullscreen_spv.h"
#include "hdr_tonemap_spv.h"
#include "hdr_ui_spv.h"
#include "host.h"
#include "nvn_api.h"
#include "vk_exec.h"
#include "vk_rec.h"
#include "platform/platform.h"

#define SLOTS 8

// ---- settings

static int g_wanted = -1;

int vk_hdr_wanted(void) {
    if (g_wanted < 0) g_wanted = cfg_bool("XCX_HDR");
    return g_wanted;
}

void vk_hdr_off(void) { g_wanted = 0; }

// The nits and the hue, read again when the settings change. The replay (every draw's support buffer) and the present
// thread read them: an update fills the snapshot not in use and then publishes it.
typedef struct { float peak, game, ui, hue; } Nits;
static struct { unsigned seen; int cur; Nits snap[2]; PlatMutex lock; } N = {.seen = ~0u, .lock = PLAT_MUTEX_INIT};

static Nits nits(void) {
    unsigned v = cfg_version();
    if (__atomic_load_n(&N.seen, __ATOMIC_ACQUIRE) != v) {
        plat_mutex_lock(&N.lock);
        if (N.seen != v) {
            Nits *n = &N.snap[!N.cur];
            n->peak = fmaxf((float)cfg_double("XCX_HDR_PEAK_NITS"), 80.0f);
            n->game = fmaxf((float)cfg_double("XCX_HDR_GAME_NITS"), 80.0f);
            n->ui = fmaxf((float)cfg_double("XCX_HDR_UI_NITS"), 80.0f);
            n->hue = fminf(fmaxf((float)cfg_double("XCX_HDR_HUE"), 0.0f), 100.0f) / 100.0f;
            __atomic_store_n(&N.cur, !N.cur, __ATOMIC_RELEASE);
            __atomic_store_n(&N.seen, v, __ATOMIC_RELEASE);
        }
        plat_mutex_unlock(&N.lock);
    }
    return N.snap[__atomic_load_n(&N.cur, __ATOMIC_ACQUIRE)];
}

static volatile int32_t g_active;   // the swapchain is HDR10 (gfx_present.c vk_hdr_set_active)

void vk_hdr_set_active(int on) { plat_xchg32(&g_active, on); }
int vk_hdr_active(void) { return plat_cas32(&g_active, 0, 0); }

void vk_hdr_support(float out[4]) {
    Nits n = nits();
    out[0] = n.game / n.ui;                              // the picture's white against the HUD's
    out[1] = n.peak > n.game ? n.peak / n.game : 1.0f;   // the peak against the picture's white
    out[2] = n.hue;                                      // hue correction of highlights (hdr_tonemap.frag)
    out[3] = vk_hdr_active() ? 1.0f : 0.0f;              // 0: the game's own tonemap, for an SDR swapchain
}

// ---- 1. the tonemap shader

enum { SPV_OP_NAME = 5, SPV_OP_TYPE_FLOAT = 22, SPV_OP_TYPE_IMAGE = 25, SPV_OP_CONSTANT = 43, SPV_OP_DECORATE = 71,
       SPV_DEC_BINDING = 33, SPV_DEC_SET = 34, SPV_DIM_3D = 2 };

// The variables of the game's shader, in the order of hdr_tonemap.frag's bindings 0..3. The translator names a block's
// type and its variable alike, so a name can stand for several ids: the one with a binding is the variable.
static const char *const tm_names[4] = {"support_buffer", "fp_t_tcb_8", "fp_t_tcb_A", "fp_c3"};

static int name_is(const uint32_t *a, uint32_t len, const char *s) {   // OpName operands after the id
    size_t n = strlen(s);
    return (len - 2) * 4 >= n + 1 && !memcmp(a + 1, s, n + 1);
}

void vk_hdr_shader_swap(uint32_t **spirv, uint64_t *words) {
    if (!vk_hdr_wanted() || !*spirv || *words < 5) return;
    const uint32_t *w = *spirv;
    uint64_t n = *words;
    // the game's tonemap: the 1/2.2 power of its LUT input, a 3D texture (the LUT) and the four resources by name
    const uint32_t inv_gamma = 0x3ee8ba2eu;   // 0.454545438f
    uint32_t float_type = 0, ids[4][4] = {{0}}, nids[4] = {0}, bind[4], set[4];
    int has_const = 0, has_3d = 0, has_bind[4] = {0};
    for (uint64_t i = 5; i < n;) {
        uint32_t op = w[i] & 0xffff, len = w[i] >> 16;
        if (!len || i + len > n) return;
        const uint32_t *a = w + i + 1;
        if (op == SPV_OP_TYPE_FLOAT && len == 3 && a[1] == 32) float_type = a[0];
        else if (op == SPV_OP_CONSTANT && len == 4 && a[0] == float_type && float_type && a[2] == inv_gamma) has_const = 1;
        else if (op == SPV_OP_TYPE_IMAGE && len >= 9 && a[2] == SPV_DIM_3D) has_3d = 1;
        else if (op == SPV_OP_NAME && len >= 3)
            for (int k = 0; k < 4; k++)
                if (name_is(a, len, tm_names[k]) && nids[k] < 4) ids[k][nids[k]++] = a[0];
        i += len;
    }
    if (!has_const || !has_3d) return;
    for (int k = 0; k < 4; k++)
        if (!nids[k]) return;
    for (uint64_t i = 5; i < n;) {   // the bindings and sets of those variables
        uint32_t op = w[i] & 0xffff, len = w[i] >> 16;
        const uint32_t *a = w + i + 1;
        if (op == SPV_OP_DECORATE && len == 4)
            for (int k = 0; k < 4; k++)
                for (uint32_t j = 0; j < nids[k]; j++)
                    if (a[0] == ids[k][j]) {
                        if (a[1] == SPV_DEC_BINDING) bind[k] = a[2], has_bind[k] |= 1;
                        else if (a[1] == SPV_DEC_SET) set[k] = a[2], has_bind[k] |= 2;
                    }
        i += len;
    }
    for (int k = 0; k < 4; k++)
        if (has_bind[k] != 3) return;
    uint64_t m = sizeof hdr_tonemap_spv / 4;
    uint32_t *out = malloc(sizeof hdr_tonemap_spv);
    if (!out) return;
    memcpy(out, hdr_tonemap_spv, sizeof hdr_tonemap_spv);
    // ours declares binding k for tm_names[k] in set 0: each gets the game's binding and set
    uint32_t bound = out[3], *ours = calloc(bound ? bound : 1, sizeof *ours);   // id -> our binding + 1
    if (!ours) {
        free(out);
        return;
    }
    for (uint64_t i = 5; i < m;) {
        uint32_t op = out[i] & 0xffff, len = out[i] >> 16;
        uint32_t *a = out + i + 1;
        if (op == SPV_OP_DECORATE && len == 4 && a[1] == SPV_DEC_BINDING && a[2] < 4) {
            if (a[0] < bound) ours[a[0]] = a[2] + 1;
            a[2] = bind[a[2]];
        }
        i += len;
    }
    for (uint64_t i = 5; i < m;) {
        uint32_t op = out[i] & 0xffff, len = out[i] >> 16;
        uint32_t *a = out + i + 1;
        if (op == SPV_OP_DECORATE && len == 4 && a[1] == SPV_DEC_SET && a[0] < bound && ours[a[0]]) a[2] = set[ours[a[0]] - 1];
        i += len;
    }
    free(ours);
    free(*spirv);
    *spirv = out;
    *words = m;
    static volatile int32_t swaps;
    rt_log("hdr: the game's tonemap shader runs as shaders/hdr_tonemap.frag (%d so far)", plat_inc32(&swaps));
}

// ---- 2. the tonemapped picture under the window texture

int vk_hdr_track;                 // vk_exec_draw.c calls vk_hdr_draw: set at the first present with XCX_HDR=1
static PlatMutex rec_lock = PLAT_MUTEX_INIT;
static struct { VkImage win, scene; VkFormat fmt; } recs[4];   // per presented window image: the picture under it
static int nrecs;
static VkImage g_tm;              // the image the last tonemap since the last present wrote, or VK_NULL_HANDLE
static VkFormat g_tm_fmt;
static VkImage g_use;             // the present thread's: the scene of the present it records (vk_hdr_use_scene)
static VkFormat g_use_fmt;

static int is_float(VkFormat f) {
    return f == VK_FORMAT_B10G11R11_UFLOAT_PACK32 || f == VK_FORMAT_R16G16B16A16_SFLOAT ||
           f == VK_FORMAT_R32G32B32A32_SFLOAT;
}

// The tonemap: one draw into a float target that samples a 2D texture of that same image and a 3D texture.
static int is_tonemap(const VkExec *ex, const VkPipeInfo *info, const VkExecTarget *t) {
    if (!is_float(t->format)) return 0;
    const VkPipeBinding *tex[2];
    int ntex = 0;
    for (uint32_t i = 0; i < info->n_bindings; i++) {   // the shape first, without looking textures up
        const VkPipeBinding *bd = &info->bindings[i];
        if (bd->kind != GFX_RES_TEX || bd->stage != GFX_STAGE_FS || !bd->res) continue;
        if (ntex == 2 || (bd->count && bd->count != 1) || bd->res->separate) return 0;
        tex[ntex++] = bd;
    }
    if (ntex != 2) return 0;
    int self = 0, lut = 0;
    for (int i = 0; i < 2; i++) {
        int slot = tex[i]->res->slot, type = tex[i]->res->type & 0x7ff;   // Ryujinx SamplerType: 3 2D, 4 3D
        if ((type != 3 && type != 4) || slot < 0 || slot >= 128 || !ex->tex[1][slot]) return 0;
        const VkTexImage *s = vk_texture_by_id(NVN_HANDLE_TEX(ex->tex[1][slot]), NULL, NULL);
        if (!s) return 0;
        if (type == 3) self |= s->image == t->image;
        else lut |= s->width == 16 && s->height == 16 && s->depth == 16;
    }
    return self && lut;
}

void vk_hdr_draw(VkExec *ex, const VkPipeInfo *info) {
    const VkExecTarget *t = &ex->color[0];
    if (!t->image) return;
    if (is_tonemap(ex, info, t)) {
        plat_mutex_lock(&rec_lock);
        g_tm = t->image, g_tm_fmt = t->format;
        plat_mutex_unlock(&rec_lock);
        return;
    }
    int known = 0;
    for (int i = 0; i < 4; i++)   // unlocked first look: most draws go elsewhere (a stale slot only costs the lock below)
        known |= __atomic_load_n(&recs[i].win, __ATOMIC_RELAXED) == t->image;
    if (!known) return;
    plat_mutex_lock(&rec_lock);
    for (int i = 0; i < nrecs; i++)
        if (recs[i].win == t->image) recs[i].scene = g_tm, recs[i].fmt = g_tm_fmt;
    plat_mutex_unlock(&rec_lock);
}

void vk_hdr_present_source(const VkTexImage *win, VkImage *scene, VkFormat *format) {
    *scene = VK_NULL_HANDLE, *format = VK_FORMAT_UNDEFINED;
    if (!vk_hdr_wanted()) return;
    vk_hdr_track = 1;
    plat_mutex_lock(&rec_lock);
    int r = -1;
    for (int i = 0; i < nrecs; i++)
        if (recs[i].win == win->image) r = i;
    if (r < 0 && nrecs < 4) r = nrecs++, recs[r].win = win->image, recs[r].scene = VK_NULL_HANDLE;
    // kept until the next draw into the window texture: a present again without new draws shows the same picture
    if (r >= 0 && recs[r].scene && vk_hdr_active()) *scene = recs[r].scene, *format = recs[r].fmt;
    g_tm = VK_NULL_HANDLE;
    plat_mutex_unlock(&rec_lock);
    static int logged;
    if (!logged && *scene) logged = 1, rt_log("hdr: the present adds the highlights of the game's float picture");
}

void vk_hdr_use_scene(VkImage scene, VkFormat format) {
    plat_mutex_lock(&rec_lock);
    g_use = scene, g_use_fmt = format;
    plat_mutex_unlock(&rec_lock);
}

void vk_hdr_image_gone(VkImage image) {   // a texture image went away: forget it as window or scene
    if (!vk_hdr_track) return;   // no records before the first HDR present (vk_hdr_present_source): no lock per image
    plat_mutex_lock(&rec_lock);
    for (int i = 0; i < nrecs; i++) {
        if (recs[i].scene == image) recs[i].scene = VK_NULL_HANDLE;
        if (recs[i].win == image) recs[i] = recs[--nrecs], recs[nrecs] = (typeof(recs[0])){0}, i--;
    }
    if (g_tm == image) g_tm = VK_NULL_HANDLE;
    if (g_use == image) g_use = VK_NULL_HANDLE;
    plat_mutex_unlock(&rec_lock);
}

// ---- swapchain

// With XCX_HDR=1, the HDR10 format among the surface's (10-bit UNORM, ST 2084): 1 with *out set, else 0 and the
// reason logged once.
int vk_hdr_surface_format(const VkSurfaceFormatKHR *f, uint32_t n, VkSurfaceFormatKHR *out) {
    if (!vk_hdr_wanted()) return 0;
    static int logged;
    if (!logged) {
        char b[1024];
        int o = 0;
        for (uint32_t i = 0; i < n && o < (int)sizeof b - 32; i++)
            o += snprintf(b + o, sizeof b - (size_t)o, " %d/%d", (int)f[i].format, (int)f[i].colorSpace);
        rt_log("hdr: surface formats (VkFormat/VkColorSpaceKHR):%s", b);
    }
    for (int pass = 0; pass < 2; pass++)
        for (uint32_t i = 0; i < n; i++)
            if (f[i].colorSpace == VK_COLOR_SPACE_HDR10_ST2084_EXT &&
                f[i].format == (pass ? VK_FORMAT_A2R10G10B10_UNORM_PACK32 : VK_FORMAT_A2B10G10R10_UNORM_PACK32)) {
                *out = f[i];
                if (!logged) rt_log("hdr: HDR10 swapchain (format %d)", (int)f[i].format);
                logged = 1;
                return 1;
            }
    if (!logged)
        rt_log("hdr: the display offers no HDR10 format, so the picture stays SDR (Windows: turn on \"Use HDR\" in the "
               "display settings; Linux: a Wayland compositor with colour management and HDR on)");
    logged = 1;
    return 0;
}

// The display's peak and the picture's white for sc; again when the settings change (gfx_present.c calls it on every
// present to an HDR10 swapchain, with new = 1 for a new swapchain).
void vk_hdr_metadata(VkSwapchainKHR sc, int new_sc) {
    static unsigned sent = ~0u;
    VkCtx *c = vk_ctx();
    if (!c->has_hdr_metadata || (!new_sc && cfg_version() == sent)) return;
    sent = cfg_version();
    PFN_vkSetHdrMetadataEXT set = (PFN_vkSetHdrMetadataEXT)vkGetDeviceProcAddr(c->device, "vkSetHdrMetadataEXT");
    if (!set) return;
    Nits n = nits();
    VkHdrMetadataEXT m = {.sType = VK_STRUCTURE_TYPE_HDR_METADATA_EXT,
                          .displayPrimaryRed = {0.708f, 0.292f}, .displayPrimaryGreen = {0.170f, 0.797f},
                          .displayPrimaryBlue = {0.131f, 0.046f}, .whitePoint = {0.3127f, 0.3290f},
                          .maxLuminance = n.peak, .minLuminance = 0.0f, .maxContentLightLevel = n.peak,
                          .maxFrameAverageLightLevel = n.game};
    set(c->device, 1, &sc, &m);
}

// ---- 3. present passes

static void barrier(VkCommandBuffer cb, VkImage img, VkImageLayout from, VkImageLayout to, VkPipelineStageFlags2 ss,
                    VkAccessFlags2 sa, VkPipelineStageFlags2 ds, VkAccessFlags2 da) {
    VkImageMemoryBarrier2 b = {.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2, .srcStageMask = ss, .srcAccessMask = sa,
                               .dstStageMask = ds, .dstAccessMask = da, .oldLayout = from, .newLayout = to,
                               .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                               .image = img, .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}};
    VkDependencyInfo d = {.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO, .imageMemoryBarrierCount = 1,
                          .pImageMemoryBarriers = &b};
    vkCmdPipelineBarrier2(cb, &d);
}

typedef struct { VkImage img; VmaAllocation alloc; VkImageView view; uint32_t w, h; } Img;

static void img_free(VkCtx *c, Img *m) {
    if (m->view) vkDestroyImageView(c->device, m->view, NULL);
    if (m->img) vmaDestroyImage(c->vma, m->img, m->alloc);
    *m = (Img){0};
}

// m at w x h in fmt; another size waits for the device first (earlier presents may still read the old image).
static int img_ensure(VkCtx *c, Img *m, uint32_t w, uint32_t h, VkFormat fmt, VkImageUsageFlags usage) {
    if (m->img && m->w == w && m->h == h) return 1;
    if (m->img) {
        vk_queue_lock();
        vkDeviceWaitIdle(c->device);
        vk_queue_unlock();
        img_free(c, m);
    }
    VkImageCreateInfo ii = {.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO, .imageType = VK_IMAGE_TYPE_2D, .format = fmt,
                            .extent = {w, h, 1}, .mipLevels = 1, .arrayLayers = 1, .samples = VK_SAMPLE_COUNT_1_BIT,
                            .tiling = VK_IMAGE_TILING_OPTIMAL, .usage = usage};
    VmaAllocationCreateInfo ai = {.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE};
    if (vmaCreateImage(c->vma, &ii, &ai, &m->img, &m->alloc, NULL) != VK_SUCCESS) {
        m->img = VK_NULL_HANDLE;
        return 0;
    }
    VkImageViewCreateInfo vi = {.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO, .image = m->img,
                                .viewType = VK_IMAGE_VIEW_TYPE_2D, .format = fmt,
                                .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}};
    if (vkCreateImageView(c->device, &vi, NULL, &m->view) != VK_SUCCESS) {
        m->view = VK_NULL_HANDLE;
        img_free(c, m);
        return 0;
    }
    m->w = w, m->h = h;
    return 1;
}

static struct {
    int tried, ok;
    PFN_vkCmdPushDescriptorSetKHR push;
    VkSampler sampler, linear;      // linear: the scene, which may differ in size from the window texture
    VkDescriptorSetLayout dsl_enc, dsl_ui;
    VkPipelineLayout enc_layout, ui_layout;
    VkPipeline enc;
    VkPipeline ui;                  // for ui_fmt
    VkFormat ui_fmt;
    Img out;                        // PQ code values of the crop, RGBA16F
    Img menu;                       // the in-game menu, RGBA8
    VkImageView src_view[SLOTS], scene_view[SLOTS];
} H;

static VkShaderModule module(VkCtx *c, const uint32_t *code, size_t size) {
    VkShaderModuleCreateInfo mi = {.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, .codeSize = size, .pCode = code};
    VkShaderModule m = VK_NULL_HANDLE;
    return vkCreateShaderModule(c->device, &mi, NULL, &m) == VK_SUCCESS ? m : VK_NULL_HANDLE;
}

static VkDescriptorSetLayout set_layout(VkCtx *c, const VkDescriptorSetLayoutBinding *b, uint32_t n) {
    VkDescriptorSetLayoutCreateInfo ci = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
                                          .flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR,
                                          .bindingCount = n, .pBindings = b};
    VkDescriptorSetLayout l = VK_NULL_HANDLE;
    return vkCreateDescriptorSetLayout(c->device, &ci, NULL, &l) == VK_SUCCESS ? l : VK_NULL_HANDLE;
}

static VkPipelineLayout pipe_layout(VkCtx *c, VkDescriptorSetLayout *dsl, VkShaderStageFlags st, uint32_t pc) {
    VkPushConstantRange pr = {st, 0, pc};
    VkPipelineLayoutCreateInfo li = {.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, .setLayoutCount = 1,
                                     .pSetLayouts = dsl, .pushConstantRangeCount = 1, .pPushConstantRanges = &pr};
    VkPipelineLayout l = VK_NULL_HANDLE;
    return vkCreatePipelineLayout(c->device, &li, NULL, &l) == VK_SUCCESS ? l : VK_NULL_HANDLE;
}

static int init(VkCtx *c) {
    H.tried = 1;
    H.push = (PFN_vkCmdPushDescriptorSetKHR)vkGetDeviceProcAddr(c->device, "vkCmdPushDescriptorSetKHR");
    VkDescriptorSetLayoutBinding enc[3] = {
        {.binding = 0, .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, .descriptorCount = 1,
         .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT},
        {.binding = 1, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, .descriptorCount = 1,
         .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT},
        {.binding = 2, .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, .descriptorCount = 1,
         .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT}};
    VkDescriptorSetLayoutBinding ui = {.binding = 0, .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                                       .descriptorCount = 1, .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT};
    VkSamplerCreateInfo si = {.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO, .magFilter = VK_FILTER_NEAREST,
                              .minFilter = VK_FILTER_NEAREST, .mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
                              .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
                              .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
                              .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, .maxLod = 0};
    VkSamplerCreateInfo li = si;
    li.magFilter = li.minFilter = VK_FILTER_LINEAR;
    if (!H.push || vkCreateSampler(c->device, &si, NULL, &H.sampler) != VK_SUCCESS ||
        vkCreateSampler(c->device, &li, NULL, &H.linear) != VK_SUCCESS ||
        !(H.dsl_enc = set_layout(c, enc, 3)) || !(H.dsl_ui = set_layout(c, &ui, 1)) ||
        !(H.enc_layout = pipe_layout(c, &H.dsl_enc, VK_SHADER_STAGE_COMPUTE_BIT, 48)) ||
        !(H.ui_layout = pipe_layout(c, &H.dsl_ui, VK_SHADER_STAGE_FRAGMENT_BIT, 16))) {
        rt_log("hdr: the present passes could not be created; the picture goes out unconverted");
        return 0;
    }
    VkShaderModule m = module(c, hdr_encode_spv, sizeof hdr_encode_spv);
    VkComputePipelineCreateInfo ci = {.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
                                      .stage = {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                                                .stage = VK_SHADER_STAGE_COMPUTE_BIT, .module = m, .pName = "main"},
                                      .layout = H.enc_layout};
    VkResult r = m ? vkCreateComputePipelines(c->device, VK_NULL_HANDLE, 1, &ci, NULL, &H.enc) : VK_ERROR_UNKNOWN;
    if (m) vkDestroyShaderModule(c->device, m, NULL);
    if (r != VK_SUCCESS) {
        rt_log("hdr: the present passes could not be created; the picture goes out unconverted");
        return 0;
    }
    return H.ok = 1;
}

static VkImageView view_of(VkCtx *c, VkImageView *v, VkImage img, VkFormat fmt) {   // *v replaced by a view of img
    if (*v) vkDestroyImageView(c->device, *v, NULL);
    VkImageViewCreateInfo vi = {.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO, .image = img,
                                .viewType = VK_IMAGE_VIEW_TYPE_2D, .format = fmt,
                                .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}};
    if (vkCreateImageView(c->device, &vi, NULL, v) != VK_SUCCESS) *v = VK_NULL_HANDLE;
    return *v;
}

VkImage vk_hdr_encode(VkCommandBuffer cb, unsigned slot, VkImage src, VkFormat fmt, const int crop[4]) {
    VkCtx *c = vk_ctx();
    if (!H.tried) init(c);
    if (!H.ok || !img_ensure(c, &H.out, (uint32_t)crop[2], (uint32_t)crop[3], VK_FORMAT_R16G16B16A16_SFLOAT,
                             VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT))
        return VK_NULL_HANDLE;
    slot %= SLOTS;
    if (!view_of(c, &H.src_view[slot], src, fmt)) return VK_NULL_HANDLE;
    plat_mutex_lock(&rec_lock);
    VkImage scene = g_use;
    VkFormat scene_fmt = g_use_fmt;
    plat_mutex_unlock(&rec_lock);
    if (scene && !view_of(c, &H.scene_view[slot], scene, scene_fmt)) scene = VK_NULL_HANDLE;
    Nits n = nits();
    // an sRGB format decodes itself through the view; 8-bit UNORM holds sRGB codes the shader decodes
    int decode = fmt == VK_FORMAT_R8G8B8A8_UNORM || fmt == VK_FORMAT_B8G8R8A8_UNORM;
    const VkPipelineStageFlags2 CS = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, ALL = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    barrier(cb, src, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL, ALL, VK_ACCESS_2_MEMORY_WRITE_BIT, CS,
            VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
    if (scene)
        barrier(cb, scene, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL, ALL, VK_ACCESS_2_MEMORY_WRITE_BIT, CS,
                VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
    barrier(cb, H.out.img, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL, ALL, 0, CS,
            VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
    VkDescriptorImageInfo ii = {H.sampler, H.src_view[slot], VK_IMAGE_LAYOUT_GENERAL},
                          oi = {VK_NULL_HANDLE, H.out.view, VK_IMAGE_LAYOUT_GENERAL},
                          si = {H.linear, scene ? H.scene_view[slot] : H.src_view[slot], VK_IMAGE_LAYOUT_GENERAL};
    VkWriteDescriptorSet w[3] = {
        {.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstBinding = 0, .descriptorCount = 1,
         .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, .pImageInfo = &ii},
        {.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstBinding = 1, .descriptorCount = 1,
         .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, .pImageInfo = &oi},
        {.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstBinding = 2, .descriptorCount = 1,
         .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, .pImageInfo = &si}};
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, H.enc);
    rec_push(H.push, cb, VK_PIPELINE_BIND_POINT_COMPUTE, H.enc_layout, 0, 3, w);
    // without a scene (a menu or movie frame) the window texture is the picture, at the HUD's white
    struct { int32_t offset[2], size[2]; float p[4], q[4]; } pc = {
        {crop[0], crop[1]}, {crop[2], crop[3]}, {n.ui, n.peak, decode ? 1.0f : 0.0f, n.game / n.ui},
        {scene ? 1.0f : 0.0f, 0, 0, 0}};
    vkCmdPushConstants(cb, H.enc_layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof pc, &pc);
    vkCmdDispatch(cb, ((uint32_t)crop[2] + 7) / 8, ((uint32_t)crop[3] + 7) / 8, 1);
    barrier(cb, src, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL, CS, 0, ALL, 0);
    if (scene) barrier(cb, scene, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL, CS, 0, ALL, 0);
    barrier(cb, H.out.img, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL, CS, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT, ALL,
            VK_ACCESS_2_MEMORY_READ_BIT);
    return H.out.img;
}

// The in-game menu's target: an RGBA8 image of the swapchain's size in COLOR_ATTACHMENT_OPTIMAL (its old content
// dropped); the caller renders the menu into the view with a clear to 0.
VkImageView vk_hdr_ui_target(VkCommandBuffer cb, uint32_t w, uint32_t h) {
    VkCtx *c = vk_ctx();
    if (!H.tried) init(c);
    if (!H.ok || !img_ensure(c, &H.menu, w, h, VK_FORMAT_R8G8B8A8_UNORM,
                             VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT))
        return VK_NULL_HANDLE;
    barrier(cb, H.menu.img, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, 0, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
            VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
    return H.menu.view;
}

static int ui_pipeline(VkCtx *c, VkFormat fmt) {
    if (H.ui && H.ui_fmt == fmt) return 1;
    if (H.ui) {   // another swapchain format: earlier presents may still use the old pipeline
        vk_queue_lock();
        vkDeviceWaitIdle(c->device);
        vk_queue_unlock();
        vkDestroyPipeline(c->device, H.ui, NULL);
        H.ui = VK_NULL_HANDLE;
    }
    VkShaderModule vs = module(c, hdr_fullscreen_spv, sizeof hdr_fullscreen_spv),
                   fs = module(c, hdr_ui_spv, sizeof hdr_ui_spv);
    VkPipelineShaderStageCreateInfo st[2] = {
        {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = VK_SHADER_STAGE_VERTEX_BIT, .module = vs,
         .pName = "main"},
        {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = VK_SHADER_STAGE_FRAGMENT_BIT, .module = fs,
         .pName = "main"}};
    VkPipelineVertexInputStateCreateInfo vi = {.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    VkPipelineInputAssemblyStateCreateInfo ia = {.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
                                                 .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST};
    VkPipelineViewportStateCreateInfo vp = {.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
                                            .viewportCount = 1, .scissorCount = 1};
    VkPipelineRasterizationStateCreateInfo rs = {.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
                                                 .polygonMode = VK_POLYGON_MODE_FILL, .cullMode = VK_CULL_MODE_NONE,
                                                 .lineWidth = 1.0f};
    VkPipelineMultisampleStateCreateInfo ms = {.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
                                               .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT};
    VkPipelineColorBlendAttachmentState ba = {
        .blendEnable = VK_TRUE, .srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA,
        .dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA, .colorBlendOp = VK_BLEND_OP_ADD,
        .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE, .dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
        .alphaBlendOp = VK_BLEND_OP_ADD, .colorWriteMask = 0xf};
    VkPipelineColorBlendStateCreateInfo cb = {.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
                                              .attachmentCount = 1, .pAttachments = &ba};
    VkDynamicState dyn[2] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo ds = {.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
                                           .dynamicStateCount = 2, .pDynamicStates = dyn};
    VkPipelineRenderingCreateInfo ri = {.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
                                        .colorAttachmentCount = 1, .pColorAttachmentFormats = &fmt};
    VkGraphicsPipelineCreateInfo gi = {.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO, .pNext = &ri,
                                       .stageCount = 2, .pStages = st, .pVertexInputState = &vi,
                                       .pInputAssemblyState = &ia, .pViewportState = &vp, .pRasterizationState = &rs,
                                       .pMultisampleState = &ms, .pColorBlendState = &cb, .pDynamicState = &ds,
                                       .layout = H.ui_layout};
    VkResult r = vs && fs ? vkCreateGraphicsPipelines(c->device, VK_NULL_HANDLE, 1, &gi, NULL, &H.ui) : VK_ERROR_UNKNOWN;
    if (vs) vkDestroyShaderModule(c->device, vs, NULL);
    if (fs) vkDestroyShaderModule(c->device, fs, NULL);
    if (r != VK_SUCCESS) {
        H.ui = VK_NULL_HANDLE;
        rt_log("hdr: the menu pass could not be created; the menu is not shown");
        return 0;
    }
    H.ui_fmt = fmt;
    return 1;
}

// The menu image onto dst (a swapchain image in COLOR_ATTACHMENT_OPTIMAL, view dst_view of format fmt).
void vk_hdr_ui_compose(VkCommandBuffer cb, VkImageView dst_view, VkFormat fmt, uint32_t w, uint32_t h) {
    VkCtx *c = vk_ctx();
    if (!H.ok || !H.menu.img || !ui_pipeline(c, fmt)) return;
    barrier(cb, H.menu.img, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
            VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
    VkRenderingAttachmentInfo ca = {.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO, .imageView = dst_view,
                                    .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                                    .loadOp = VK_ATTACHMENT_LOAD_OP_LOAD, .storeOp = VK_ATTACHMENT_STORE_OP_STORE};
    VkRenderingInfo ri = {.sType = VK_STRUCTURE_TYPE_RENDERING_INFO, .renderArea = {{0, 0}, {w, h}}, .layerCount = 1,
                          .colorAttachmentCount = 1, .pColorAttachments = &ca};
    vkCmdBeginRendering(cb, &ri);
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, H.ui);
    VkViewport v = {0, 0, (float)w, (float)h, 0, 1};
    VkRect2D s = {{0, 0}, {w, h}};
    vkCmdSetViewport(cb, 0, 1, &v);
    vkCmdSetScissor(cb, 0, 1, &s);
    VkDescriptorImageInfo ii = {H.sampler, H.menu.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkWriteDescriptorSet wr = {.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstBinding = 0, .descriptorCount = 1,
                               .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, .pImageInfo = &ii};
    rec_push(H.push, cb, VK_PIPELINE_BIND_POINT_GRAPHICS, H.ui_layout, 0, 1, &wr);
    float pc[4] = {nits().ui, 0, 0, 0};
    vkCmdPushConstants(cb, H.ui_layout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof pc, pc);
    vkCmdDraw(cb, 3, 1, 0, 0);
    vkCmdEndRendering(cb);
}

void vk_hdr_release(void) {
    VkCtx *c = vk_ctx();
    for (int i = 0; i < SLOTS; i++) {
        if (H.src_view[i]) vkDestroyImageView(c->device, H.src_view[i], NULL), H.src_view[i] = VK_NULL_HANDLE;
        if (H.scene_view[i]) vkDestroyImageView(c->device, H.scene_view[i], NULL), H.scene_view[i] = VK_NULL_HANDLE;
    }
    img_free(c, &H.out);
    img_free(c, &H.menu);
    if (H.enc) vkDestroyPipeline(c->device, H.enc, NULL);
    if (H.ui) vkDestroyPipeline(c->device, H.ui, NULL);
    if (H.enc_layout) vkDestroyPipelineLayout(c->device, H.enc_layout, NULL);
    if (H.ui_layout) vkDestroyPipelineLayout(c->device, H.ui_layout, NULL);
    if (H.dsl_enc) vkDestroyDescriptorSetLayout(c->device, H.dsl_enc, NULL);
    if (H.dsl_ui) vkDestroyDescriptorSetLayout(c->device, H.dsl_ui, NULL);
    if (H.sampler) vkDestroySampler(c->device, H.sampler, NULL);
    if (H.linear) vkDestroySampler(c->device, H.linear, NULL);
    memset(&H, 0, sizeof H);
}

// PQ code (0..1023) of a 10-bit channel to the sRGB byte of the same colour at the HUD's white (menu test shots and
// flight thumbnails of an HDR10 swapchain).
static float pq_nits(float e) {
    const float m1 = 0.1593017578125f, m2 = 78.84375f, c1 = 0.8359375f, c2 = 18.8515625f, c3 = 18.6875f;
    float p = powf(e, 1.0f / m2);
    return 10000.0f * powf(fmaxf(p - c1, 0.0f) / (c2 - c3 * p), 1.0f / m1);
}

void vk_hdr_to_srgb8(const uint8_t *in, uint8_t *out, size_t px, int a2r) {
    float ui = nits().ui;
    // Rec. 2020 to Rec. 709 (the inverse of ITU-R BT.2087's matrix)
    static const float m[9] = {1.6605f, -0.5876f, -0.0728f, -0.1246f, 1.1329f, -0.0083f, -0.0182f, -0.1006f, 1.1187f};
    for (size_t i = 0; i < px; i++) {
        uint32_t v;
        memcpy(&v, in + 4 * i, 4);
        float c[3] = {(float)(v & 1023), (float)(v >> 10 & 1023), (float)(v >> 20 & 1023)};   // A2B10G10R10: R low
        if (a2r) { float t = c[0]; c[0] = c[2]; c[2] = t; }
        for (int k = 0; k < 3; k++) c[k] = pq_nits(c[k] / 1023.0f) / ui;
        for (int k = 0; k < 3; k++) {
            float l = fminf(fmaxf(m[3 * k] * c[0] + m[3 * k + 1] * c[1] + m[3 * k + 2] * c[2], 0.0f), 1.0f);
            float s = l <= 0.0031308f ? 12.92f * l : 1.055f * powf(l, 1.0f / 2.4f) - 0.055f;
            out[4 * i + k] = (uint8_t)(s * 255.0f + 0.5f);
        }
        out[4 * i + 3] = 255;
    }
}
