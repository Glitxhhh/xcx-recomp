// DLSS: NVIDIA DLSS on the game's scene (XCX_UPSCALER = dlss). Ported from TotK Recomp's runtime/vk_dlaa.c, which this
// fork shares its Vulkan back end with; the frame analysis below is XCX's own.
//
// NGX is loaded at run time from the driver's NGX core: on Windows _nvngx.dll, found through
// HKLM\SOFTWARE\NVIDIA Corporation\Global\NGXCore; on Linux libnvidia-ngx.so.1, which the NVIDIA driver installs on the
// library path. NGX loads the DLSS model from the folder of the executable: nvngx_dlss.dll on Windows,
// libnvidia-ngx-dlss.so.<version> on Linux. No NVIDIA SDK code or header is compiled in: the few NGX types used are
// declared here from the SDK's documented ABI.
//
// The core's parameter object is a C++ class whose virtual functions are called through its vtable: the Set overloads
// in slots 0-7 and the Get overloads in 8-15. MSVC (the Windows core) lays out each group of overloads in reverse
// declaration order, the Itanium C++ ABI (the Linux core, built with GCC) in declaration order. vtable_ok checks the
// layout with a set-then-get round trip before anything relies on it.
//
// Where DLSS runs in XCX's frame, the jitter and the motion vectors: see the sections below.
#ifdef _WIN32
#include <windows.h>
#endif
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#include "cfg.h"
#include "host.h"
#include "platform/platform.h"
#include "vk.h"
#include "vk_dlss.h"
#include "vk_exec.h"
#include "dlss_mv_spv.h"

typedef int NgxResult;   // NVSDK_NGX_Result: 1 success, 0xBAD00000 | n failures
#define NGX_OK(r) (((unsigned)(r) & 0xFFF00000u) != 0xBAD00000u)
#define NGX_VERSION_API 0x15
#define NGX_FEATURE_SUPERSAMPLING 1
#define NGX_PERFQ_DLAA 5   // NVSDK_NGX_PerfQuality_Value: 0 performance, 1 balanced, 2 quality, 3 ultra performance
typedef struct NgxParam NgxParam;
typedef struct NgxHandle NgxHandle;
typedef struct { void *slot[17]; } NgxParamVtbl;
struct NgxParam { const NgxParamVtbl *vt; };
typedef struct {   // NVSDK_NGX_ImageViewInfo_VK
    VkImageView view;
    VkImage image;
    VkImageSubresourceRange range;
    VkFormat format;
    unsigned width, height;
} NgxImageView;
typedef struct {   // NVSDK_NGX_Resource_VK
    union { NgxImageView img; struct { VkBuffer buffer; unsigned bytes; } buf; } r;
    int type;      // 0 image view, 1 buffer
    _Bool rw;
} NgxResourceVK;

typedef NgxResult (*PFN_Init)(unsigned long long app, const wchar_t *path, VkInstance, VkPhysicalDevice, VkDevice,
                              PFN_vkGetInstanceProcAddr, PFN_vkGetDeviceProcAddr, int version, const void *info);
typedef NgxResult (*PFN_ReqExt)(unsigned *ni, const char ***ie, unsigned *nd, const char ***de);
typedef NgxResult (*PFN_Params)(NgxParam **);
typedef NgxResult (*PFN_DestroyParams)(NgxParam *);
typedef NgxResult (*PFN_Create)(VkCommandBuffer, int feature, NgxParam *, NgxHandle **);
typedef NgxResult (*PFN_Evaluate)(VkCommandBuffer, const NgxHandle *, const NgxParam *, void *callback);
typedef NgxResult (*PFN_Release)(NgxHandle *);

static struct {
    void *core;   // the NGX core library (an HMODULE on Windows)
    PFN_Init init;
    PFN_ReqExt req_ext;
    PFN_Params caps, alloc_params;
    PFN_DestroyParams destroy_params;
    PFN_Create create;
    PFN_Evaluate evaluate;
    PFN_Release release;
    int tried, loaded, available;
} N;

typedef void (*SetI)(NgxParam *, const char *, int);
typedef void (*SetU)(NgxParam *, const char *, unsigned);
typedef void (*SetF)(NgxParam *, const char *, float);
typedef void (*SetP)(NgxParam *, const char *, void *);
typedef NgxResult (*GetI)(const NgxParam *, const char *, int *);
typedef NgxResult (*GetU)(const NgxParam *, const char *, unsigned *);
typedef NgxResult (*GetF)(const NgxParam *, const char *, float *);
typedef NgxResult (*GetP)(const NgxParam *, const char *, void **);
// The SDK declares Set(u64), Set(float), Set(double), Set(u32), Set(int), Set(ID3D11Resource *), Set(ID3D12Resource *),
// Set(void *), then the Get overloads in the same order, then Reset.
#ifdef _WIN32
enum { S_P = 0, S_D12, S_D11, S_I, S_U32, S_D, S_F, S_U64, G_P, G_D12, G_D11, G_I, G_U32, G_D, G_F, G_U64 };
#else
enum { S_U64 = 0, S_F, S_D, S_U32, S_I, S_D11, S_D12, S_P, G_U64, G_F, G_D, G_U32, G_I, G_D11, G_D12, G_P };
#endif
static void p_seti(NgxParam *p, const char *n, int v) { ((SetI)p->vt->slot[S_I])(p, n, v); }
static void p_setu(NgxParam *p, const char *n, unsigned v) { ((SetU)p->vt->slot[S_U32])(p, n, v); }
static void p_setf(NgxParam *p, const char *n, float v) { ((SetF)p->vt->slot[S_F])(p, n, v); }
static void p_setp(NgxParam *p, const char *n, void *v) { ((SetP)p->vt->slot[S_P])(p, n, v); }
static int p_geti(const NgxParam *p, const char *n, int *v) { return NGX_OK(((GetI)p->vt->slot[G_I])(p, n, v)); }
static int p_getu(const NgxParam *p, const char *n, unsigned *v) { return NGX_OK(((GetU)p->vt->slot[G_U32])(p, n, v)); }
static int p_getf(const NgxParam *p, const char *n, float *v) { return NGX_OK(((GetF)p->vt->slot[G_F])(p, n, v)); }
static int p_getp(const NgxParam *p, const char *n, void **v) { return NGX_OK(((GetP)p->vt->slot[G_P])(p, n, v)); }

#ifdef _WIN32
#define DLSS_MODEL "nvngx_dlss.dll"
#else
#define DLSS_MODEL "libnvidia-ngx-dlss.so.<version>"
#endif

static int is_dlss(void) { return !strcmp(cfg_value("XCX_UPSCALER"), "dlss"); }

// NGX is brought up only when upscaler = dlss at start: its device extensions can only be asked for at device creation,
// and loading the driver's NGX core on every NVIDIA machine would cost start time and change the device for everyone
// else. Chosen while the game runs, dlss applies after a restart.
int vk_dlss_wanted(void) {
    static int wanted = -1;
    if (wanted < 0) wanted = is_dlss();
    return wanted;
}

static int core_load(void) {
    if (N.tried) return N.loaded;
    N.tried = 1;
#ifdef _WIN32
    char dir[MAX_PATH] = {0}, path[MAX_PATH + 32];
    DWORD n = sizeof dir;
    if (RegGetValueA(HKEY_LOCAL_MACHINE, "SOFTWARE\\NVIDIA Corporation\\Global\\NGXCore", "FullPath", RRF_RT_REG_SZ,
                     NULL, dir, &n) != ERROR_SUCCESS) {
        rt_log("dlss: the driver has no NGX core (registry NGXCore missing): DLSS off");
        return 0;
    }
    snprintf(path, sizeof path, "%s\\_nvngx.dll", dir);
    N.core = LoadLibraryA(path);
    if (!N.core) {
        rt_log("dlss: cannot load %s (error %lu): DLSS off", path, GetLastError());
        return 0;
    }
#define FN(f, name) N.f = (void *)GetProcAddress((HMODULE)N.core, name)
#else
    const char *path = "libnvidia-ngx.so.1";   // the dynamic loader finds it where the driver put it
    N.core = plat_lib_open(path);
    if (!N.core) {
        rt_log("dlss: cannot load %s (not an NVIDIA driver, or one without NGX): DLSS off", path);
        return 0;
    }
#define FN(f, name) N.f = plat_lib_sym(N.core, name)
#endif
    FN(init, "NVSDK_NGX_VULKAN_Init_Ext2");
    FN(req_ext, "NVSDK_NGX_VULKAN_RequiredExtensions");
    FN(caps, "NVSDK_NGX_VULKAN_GetCapabilityParameters");
    FN(alloc_params, "NVSDK_NGX_VULKAN_AllocateParameters");
    FN(destroy_params, "NVSDK_NGX_VULKAN_DestroyParameters");
    FN(create, "NVSDK_NGX_VULKAN_CreateFeature");
    FN(evaluate, "NVSDK_NGX_VULKAN_EvaluateFeature");
    FN(release, "NVSDK_NGX_VULKAN_ReleaseFeature");
#undef FN
    if (!N.init || !N.req_ext || !N.caps || !N.alloc_params || !N.create || !N.evaluate || !N.release) {
        rt_log("dlss: the NGX core %s lacks Vulkan entry points: DLSS off", path);
        return 0;
    }
    N.loaded = 1;
    rt_log("dlss: NGX core %s", path);
    return 1;
}

static int listed(const char *const *list, uint32_t n, const char *s) {
    for (uint32_t i = 0; i < n; i++)
        if (!strcmp(list[i], s)) return 1;
    return 0;
}

void vk_dlss_instance_exts(const char **ext, uint32_t *n, uint32_t max) {
    unsigned ni = 0, nd = 0;
    const char **ie = NULL, **de = NULL;
    if (!vk_dlss_wanted() || !core_load() || !NGX_OK(N.req_ext(&ni, &ie, &nd, &de))) return;
    for (unsigned i = 0; i < ni; i++)
        if (!listed(ext, *n, ie[i]) && *n < max) ext[(*n)++] = ie[i];
}

void vk_dlss_device_exts(const VkExtensionProperties *have, uint32_t nhave, const char **ext, uint32_t *n,
                         uint32_t max) {
    unsigned ni = 0, nd = 0;
    const char **ie = NULL, **de = NULL;
    if (!vk_dlss_wanted() || !core_load() || !NGX_OK(N.req_ext(&ni, &ie, &nd, &de))) return;
    for (unsigned i = 0; i < nd; i++) {
        int ok = 0;
        for (uint32_t k = 0; k < nhave && !ok; k++) ok = !strcmp(have[k].extensionName, de[i]);
        if (!ok) rt_log("dlss: the device lacks %s, which NGX asks for", de[i]);
        else if (!listed(ext, *n, de[i]) && *n < max) ext[(*n)++] = de[i];
    }
}

static int vtable_ok(void) {
    NgxParam *p = NULL;
    if (!NGX_OK(N.alloc_params(&p)) || !p) return 0;
    int i = 0;
    unsigned u = 0;
    float f = 0;
    void *v = NULL;
    p_seti(p, "XCX.Test.I", -12345);
    p_setu(p, "XCX.Test.U", 4000000000u);
    p_setf(p, "XCX.Test.F", 1.25f);
    p_setp(p, "XCX.Test.P", (void *)p);
    int ok = p_geti(p, "XCX.Test.I", &i) && i == -12345 && p_getu(p, "XCX.Test.U", &u) && u == 4000000000u &&
             p_getf(p, "XCX.Test.F", &f) && f == 1.25f && p_getp(p, "XCX.Test.P", &v) && v == (void *)p;
    if (N.destroy_params) N.destroy_params(p);
    if (!ok) rt_log("dlss: NGX parameter layout check failed (i=%d u=%u f=%g p=%p): DLSS off", i, u, f, v);
    return ok;
}

#ifndef _WIN32
// UTF-8 to UTF-32 wchar_t, without the C library's locale (the runtime never sets one, so mbstowcs would stop at the
// first non-ASCII byte). Returns 0 on malformed input or when out has no room.
static int utf8_to_wide(const char *in, wchar_t *out, size_t cap) {
    const unsigned char *u = (const unsigned char *)in;
    size_t n = 0;
    while (*u) {
        unsigned c = *u++, more = c < 0x80 ? 0 : c >= 0xF0 ? 3 : c >= 0xE0 ? 2 : c >= 0xC0 ? 1 : 4;
        if (more == 4) return 0;
        if (more) c &= 0x3F >> more;
        for (unsigned k = 0; k < more; k++, u++) {
            if ((*u & 0xC0) != 0x80) return 0;
            c = c << 6 | (*u & 0x3F);
        }
        if (n + 1 >= cap) return 0;
        out[n++] = (wchar_t)c;
    }
    out[n] = 0;
    return 1;
}
#endif

void vk_dlss_init(void) {
    if (!vk_dlss_wanted() || !core_load()) return;
#ifdef _WIN32
    wchar_t wdir[MAX_PATH];
    DWORD len = GetModuleFileNameW(NULL, wdir, MAX_PATH);
    while (len && wdir[len - 1] != L'\\') len--;
    wdir[len] = 0;
#else
    // The executable's folder, where NGX looks for libnvidia-ngx-dlss.so and writes its logs, as 32-bit code points.
    char dir[PLAT_PATH_MAX];
    wchar_t wdir[PLAT_PATH_MAX];
    char *cut = plat_exe_path(dir, sizeof dir) ? plat_last_sep(dir) : NULL;
    if (!cut || !utf8_to_wide((cut[1] = 0, dir), wdir, PLAT_PATH_MAX)) {
        rt_log("dlss: cannot name the executable's folder for NGX: DLSS off");
        return;
    }
#endif
    // 231313132: the application id NVIDIA's DLSS samples use for builds without an NVIDIA-issued id
    const VkCtx *c = vk_ctx();
    NgxResult r = N.init(231313132ull, wdir, c->instance, c->phys, c->device, vkGetInstanceProcAddr,
                         vkGetDeviceProcAddr, NGX_VERSION_API, NULL);
    if (!NGX_OK(r)) {
        rt_log("dlss: NGX init failed (%08x): DLSS off", (unsigned)r);
        return;
    }
    if (!vtable_ok()) return;
    NgxParam *caps = NULL;
    int avail = 0, driver = 0;
    if (NGX_OK(N.caps(&caps)) && caps) {
        p_geti(caps, "SuperSampling.Available", &avail);
        p_geti(caps, "SuperSampling.NeedsUpdatedDriver", &driver);
    }
    N.available = avail != 0;
    rt_log("dlss: NGX up; DLSS %s%s", avail ? "available" : "NOT available (is " DLSS_MODEL " next to the executable?)",
           driver ? "; the driver needs an update" : "");
}

int vk_dlss_available(void) { return vk_dlss_wanted() && N.available; }

// ---------------------------------------------------------------- where DLSS runs in XCX's frame
// Measured with XCX_DUMP in New Los Angeles at 1920x1080, render scale 100 and 50 (2026-11-04):
// - The G-buffer pass draws into seven targets of the scene size with a D32F_S8 depth; target 5 is the R11G11B10F
//   light target. Lighting, the sky, water and the transparents then draw into the light target (sometimes with one or
//   two RGBA8 targets beside it) with the scene depth attached. Off-screen particles at a quarter size get a D16 depth
//   of their own.
// - The first pass with the light target as its only target and no depth composites those particles back in; the
//   bloom, the game's flash filter and the tonemap follow, all in place in the light target (the tonemap leaves display
//   values, gamma encoded, in it).
// - One full-screen draw (FXAA 3.11 console, guest fs 3cfc53b328b8d79d) reads the light target into an RGBA8 target
//   of the output size, in a pass of its own. That draw is also the game's scaling: below 100 % the scene targets are
//   the scaled size (960x544 at 50 %, sides rounded up to a multiple of 8) and FXAA samples them up bilinearly.
//   The interface is then drawn into the same RGBA8 target in passes of its own.
// So: the G-buffer arms the frame with the light target and the scene depth; the scene's draws (with that depth) are
// jittered until the first light-only pass; DLSS runs when the scaling pass begins, on the post-processed light target
// (LDR), writes the output-sized picture into the pass's target, and the pass's first draw, the FXAA, is skipped. The
// interface is never jittered nor passed through DLSS. Bloom and the tonemap take the jittered picture as it is.
#define SCENE_MIN_W 320u
#define CAM_SIZE 0x300u   // the scene camera block, vertex stage c3
#define CAM_VP 0x70u      // its rows 7-10: the view-projection (rows 3-6 the projection, rows 0-2 the view)

static struct {
    VkExecTarget color, depth;   // the scene's light target and depth this frame (valid with gbuf)
    int gbuf, post, done, up;    // this frame: G-buffer seen, post passes begun, DLSS ran, its pass's draw skipped
    uint32_t up_pass;            // pass_serial of the scaling pass DLSS wrote
    struct Size {                // a DLSS feature and its work images per scene and output size
        uint32_t w, h, ow, oh;
        VkImage out, mv;
        VmaAllocation out_mem, mv_mem;
        VkImageView out_view, mv_view;
        NgxHandle *feat;
        uint64_t used;
    } sizes[3], *last;
    VkImage last_color;
    NgxParam *params;
    int failed, reset, on;
    uint64_t evals, frames_missed, mv_frames, no_cam, holds;
    // jitter
    float jx, jy, ux, uy;        // the offset the next scene draws take / this frame's took (render pixels)
    uint32_t jframe;
    int jit_ok, jit_used, want, hold;
    // camera
    double cam[16], cam_prev[16];   // this and last frame's view-projection, rows
    int cam_ok, prev_ok;
    // motion-vector pass
    VkDescriptorSetLayout mv_dsl;
    VkPipelineLayout mv_layout;
    VkPipeline mv_pipe;
    VkSampler mv_samp;
    int mv_ok;                   // -1 not built, 0 failed, 1 ready
    uint64_t last_eval_ns;
} D = {.mv_ok = -1};

static int dl_active(void) { return N.available && !D.failed && D.on; }

static void dl_fail(const char *what, NgxResult r) {
    rt_log("dlss: %s failed (%08x): DLSS off for this run", what, (unsigned)r);
    D.failed = 1;
}

static int image2d(uint32_t w, uint32_t h, VkFormat f, VkImageUsageFlags usage, VkImage *img, VmaAllocation *mem,
                   VkImageView *view) {
    const VkCtx *c = vk_ctx();
    VkImageCreateInfo ii = {.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO, .imageType = VK_IMAGE_TYPE_2D, .format = f,
                            .extent = {w, h, 1}, .mipLevels = 1, .arrayLayers = 1, .samples = VK_SAMPLE_COUNT_1_BIT,
                            .tiling = VK_IMAGE_TILING_OPTIMAL, .usage = usage,
                            .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED};
    VmaAllocationCreateInfo ai = {.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE};
    if (vmaCreateImage(c->vma, &ii, &ai, img, mem, NULL) != VK_SUCCESS) return 0;
    VkImageViewCreateInfo vi = {.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO, .image = *img,
                                .viewType = VK_IMAGE_VIEW_TYPE_2D, .format = f,
                                .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}};
    return vkCreateImageView(c->device, &vi, NULL, view) == VK_SUCCESS;
}

static void free_size(struct Size *e) {
    const VkCtx *c = vk_ctx();
    vkDeviceWaitIdle(c->device);   // the evicted images may still be in flight
    if (e->out_view) vkDestroyImageView(c->device, e->out_view, NULL);
    if (e->mv_view) vkDestroyImageView(c->device, e->mv_view, NULL);
    if (e->out) vmaDestroyImage(c->vma, e->out, e->out_mem);
    if (e->mv) vmaDestroyImage(c->vma, e->mv, e->mv_mem);
    if (e->feat) N.release(e->feat);
    memset(e, 0, sizeof *e);
}

static void full_barrier(VkCommandBuffer cb) {
    VkMemoryBarrier2 mb = {.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2,
                           .srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                           .srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT,
                           .dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                           .dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT};
    VkDependencyInfo di = {.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO, .memoryBarrierCount = 1, .pMemoryBarriers = &mb};
    vkCmdPipelineBarrier2(cb, &di);
}

// The entry for a scene and output size: DLSS's output (RGBA16F, the output size) and the motion vectors (RG16F, the
// scene size, render pixels), both kept in GENERAL like every image of the back end. The least recently used entry
// goes for a new size (the menu's render scale, the game's dynamic resolution).
static struct Size *size_for(VkCommandBuffer cb, uint32_t w, uint32_t h, uint32_t ow, uint32_t oh) {
    struct Size *e = NULL, *lru = &D.sizes[0];
    for (int i = 0; i < 3; i++) {
        if (D.sizes[i].w == w && D.sizes[i].h == h && D.sizes[i].ow == ow && D.sizes[i].oh == oh) return &D.sizes[i];
        if (!D.sizes[i].out && !e) e = &D.sizes[i];
        if (D.sizes[i].used < lru->used) lru = &D.sizes[i];
    }
    if (!e) {
        if (D.last == lru) D.last = NULL;
        free_size(e = lru);
    }
    VkImageUsageFlags u = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                          VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    if (!image2d(ow, oh, VK_FORMAT_R16G16B16A16_SFLOAT, u, &e->out, &e->out_mem, &e->out_view) ||
        !image2d(w, h, VK_FORMAT_R16G16_SFLOAT, u, &e->mv, &e->mv_mem, &e->mv_view)) {
        free_size(e);
        return NULL;
    }
    VkImageMemoryBarrier2 ib[2];
    VkImage im[2] = {e->out, e->mv};
    for (int i = 0; i < 2; i++)
        ib[i] = (VkImageMemoryBarrier2){.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
                                        .srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                                        .dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                                        .dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT,
                                        .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED, .newLayout = VK_IMAGE_LAYOUT_GENERAL,
                                        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                                        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .image = im[i],
                                        .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}};
    VkDependencyInfo di = {.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO, .imageMemoryBarrierCount = 2,
                           .pImageMemoryBarriers = ib};
    vkCmdPipelineBarrier2(cb, &di);
    e->w = w, e->h = h, e->ow = ow, e->oh = oh;
    return e;
}

static NgxResourceVK res_image(VkImageView v, VkImage img, VkFormat f, uint32_t w, uint32_t h, VkImageAspectFlags a,
                               int rw) {
    NgxResourceVK r;
    memset(&r, 0, sizeof r);
    r.r.img = (NgxImageView){v, img, {a, 0, 1, 0, 1}, f, w, h};
    r.type = 0;
    r.rw = rw;
    return r;
}

// ---------------------------------------------------------------- camera motion vectors
// The scene camera block is the vertex stage's c3 of 0x300 bytes that the scene's draws share (one address for nearly
// every G-buffer draw in a frame): rows 0-2 the view (3x4), rows 3-6 an OpenGL projection (row 6 is (0, 0, -1, 0)),
// rows 7-10 their product, the view-projection. vk_dlss_draw copies rows 7-10 out of the replay's uniform snapshot
// (vk_usnap.c, as of the submit call) at the first scene draw of a frame. mv_pass takes each pixel's depth through
// last frame's view-projection times the inverse of this frame's, computed here in double precision (shaders/dlss_mv.comp).
static int mv_init(void) {
    if (D.mv_ok >= 0) return D.mv_ok;
    D.mv_ok = 0;
    const VkCtx *c = vk_ctx();
    VkDescriptorSetLayoutBinding b[2] = {{0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, NULL},
                                         {1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, NULL}};
    VkDescriptorSetLayoutCreateInfo dci = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
                                           .flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR,
                                           .bindingCount = 2, .pBindings = b};
    VkPushConstantRange pr = {VK_SHADER_STAGE_COMPUTE_BIT, 0, 80};
    VkSamplerCreateInfo si = {.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO, .magFilter = VK_FILTER_NEAREST,
                              .minFilter = VK_FILTER_NEAREST, .mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
                              .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
                              .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
                              .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE};
    VkShaderModuleCreateInfo mi = {.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, .codeSize = sizeof dlss_mv_spv,
                                   .pCode = dlss_mv_spv};
    VkShaderModule m = VK_NULL_HANDLE;
    if (vkCreateDescriptorSetLayout(c->device, &dci, NULL, &D.mv_dsl) != VK_SUCCESS) goto fail;
    VkPipelineLayoutCreateInfo li = {.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, .setLayoutCount = 1,
                                     .pSetLayouts = &D.mv_dsl, .pushConstantRangeCount = 1, .pPushConstantRanges = &pr};
    if (vkCreatePipelineLayout(c->device, &li, NULL, &D.mv_layout) != VK_SUCCESS ||
        vkCreateSampler(c->device, &si, NULL, &D.mv_samp) != VK_SUCCESS ||
        vkCreateShaderModule(c->device, &mi, NULL, &m) != VK_SUCCESS)
        goto fail;
    VkComputePipelineCreateInfo ci = {.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
                                      .stage = {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                                                .stage = VK_SHADER_STAGE_COMPUTE_BIT, .module = m, .pName = "main"},
                                      .layout = D.mv_layout};
    VkResult r = vkCreateComputePipelines(c->device, VK_NULL_HANDLE, 1, &ci, NULL, &D.mv_pipe);
    vkDestroyShaderModule(c->device, m, NULL);
    if (r != VK_SUCCESS) goto fail;
    return D.mv_ok = 1;
fail:
    rt_log("dlss: the motion-vector pass could not be set up: DLSS runs with zero motion");
    return 0;
}

static int inv4(const double m[16], double o[16]) {
    double a[4][8];
    for (int r = 0; r < 4; r++)
        for (int k = 0; k < 8; k++) a[r][k] = k < 4 ? m[r * 4 + k] : (k - 4 == r);
    for (int col = 0; col < 4; col++) {
        int piv = col;
        for (int r = col + 1; r < 4; r++)
            if (fabs(a[r][col]) > fabs(a[piv][col])) piv = r;
        if (fabs(a[piv][col]) < 1e-300) return 0;
        for (int k = 0; k < 8; k++) { double t = a[col][k]; a[col][k] = a[piv][k]; a[piv][k] = t; }
        double d = a[col][col];
        for (int k = 0; k < 8; k++) a[col][k] /= d;
        for (int r = 0; r < 4; r++)
            if (r != col) {
                double f = a[r][col];
                for (int k = 0; k < 8; k++) a[r][k] -= f * a[col][k];
            }
    }
    for (int r = 0; r < 4; r++)
        for (int k = 0; k < 4; k++) o[r * 4 + k] = a[r][k + 4];
    return 1;
}

// 0: no motion this frame (no camera, or none last frame): the caller clears the vectors and DLSS starts a new history.
static int mv_pass(VkExec *ex, struct Size *e, VkImageView zv, float jx, float jy) {
    int had_prev = D.prev_ok;
    D.prev_ok = D.cam_ok;
    double ci[16], m[16];
    int have = D.cam_ok && had_prev && inv4(D.cam, ci);
    if (have)
        for (int r = 0; r < 4; r++)
            for (int k = 0; k < 4; k++) {
                double v = 0;
                for (int j = 0; j < 4; j++) v += D.cam_prev[r * 4 + j] * ci[j * 4 + k];
                m[r * 4 + k] = v;
            }
    if (D.cam_ok) memcpy(D.cam_prev, D.cam, sizeof D.cam);
    else D.no_cam++;
    if (!have || !mv_init()) return 0;
    VkCommandBuffer cb = ex->cb;
    VkDescriptorImageInfo zi = {D.mv_samp, zv, VK_IMAGE_LAYOUT_GENERAL};
    VkDescriptorImageInfo oi = {VK_NULL_HANDLE, e->mv_view, VK_IMAGE_LAYOUT_GENERAL};
    VkWriteDescriptorSet wr[2] = {
        {.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstBinding = 0, .descriptorCount = 1,
         .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, .pImageInfo = &zi},
        {.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstBinding = 1, .descriptorCount = 1,
         .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, .pImageInfo = &oi}};
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, D.mv_pipe);
    rec_push(vk_pipeline_push_fn(), cb, VK_PIPELINE_BIND_POINT_COMPUTE, D.mv_layout, 0, 2, wr);
    float pc[20];
    for (int i = 0; i < 16; i++) pc[i] = (float)m[i];
    pc[16] = (float)e->w, pc[17] = (float)e->h, pc[18] = jx, pc[19] = jy;
    vkCmdPushConstants(cb, D.mv_layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof pc, pc);
    vkCmdDispatch(cb, (e->w + 7) / 8, (e->h + 7) / 8, 1);
    full_barrier(cb);
    ex->recorded += 5;
    D.mv_frames++;
    return 1;
}

// ---------------------------------------------------------------- jitter
// The scene's draws (every draw with the scene depth until the post passes) have their viewport moved by a sub-pixel
// Halton(2,3) offset, which shifts what they rasterise as a jittered projection would. Full-screen draws (at most six
// vertices, no depth write: the lighting, sky and composite passes) are left where they are, so they read the
// G-buffer pixel for pixel. Jitter only follows a frame DLSS ran on: a jittered frame that DLSS does not resolve shakes
// on screen, so a missed DLSS point stops the jitter for a second.
static float halton(uint32_t i, uint32_t b) {
    float f = 1, r = 0;
    for (; i; i /= b) f /= (float)b, r += f * (float)(i % b);
    return r;
}

// NVIDIA's DLSS guide asks for 8 x (output / scene)^2 phases, 16 at least.
static void next_jitter(uint32_t w, uint32_t ow) {
    static int nj = -1;
    if (nj < 0) nj = !rt_knob("DLSS_JITTER", "1");   // XCX_DLSS_JITTER=0: no jitter (test)
    double r = w ? (double)ow / w : 1.0;
    uint32_t phases = (uint32_t)ceil(8.0 * r * r - 1e-6);
    if (phases < 16) phases = 16;
    if (phases > 72) phases = 72;
    uint32_t i = D.jframe++ % phases + 1;
    D.jx = halton(i, 2) - 0.5f, D.jy = halton(i, 3) - 0.5f;
    D.jit_ok = !D.hold && !nj;
}

void vk_dlss_viewport(VkViewport *v) {
    if (!D.want) return;
    v->x += D.jx, v->y += D.jy;
    D.ux = D.jx, D.uy = D.jy, D.jit_used = 1;
}

static int depth32(VkFormat f) { return f == VK_FORMAT_D32_SFLOAT || f == VK_FORMAT_D32_SFLOAT_S8_UINT; }

void vk_dlss_draw(VkExec *ex, int32_t count) {
    int want = 0;
    if (dl_active() && D.gbuf && !D.post && !D.done && ex->depth.image && ex->depth.image == D.depth.image) {
        const p_bind_buf *b = &ex->ubo[0][0];   // vertex c3: the replay keeps user banks from c3 on
        if (!D.cam_ok && b->size == CAM_SIZE && b->addr) {
            const float *f = (const float *)vk_usnap_bytes(b->addr, CAM_SIZE);
            if (!f) f = (const float *)(uintptr_t)b->addr;   // no snapshot: guest memory as it is now
            const float *proj_w = f + 6 * 4, *vp = f + CAM_VP / 4;
            if (proj_w[0] == 0 && proj_w[1] == 0 && proj_w[2] == -1.0f && proj_w[3] == 0) {
                for (int i = 0; i < 16; i++) D.cam[i] = vp[i];
                D.cam_ok = 1;
            }
        }
        int fullscreen = count <= 6 && !ex->pipe.ds.dw;
        want = D.jit_ok && !fullscreen;
    }
    if (want != D.want) D.want = want, ex->dirty |= VK_EXEC_DIRTY_VIEWPORT;
}

// The model per mode, as NVIDIA recommends for DLSS 4.5 (310.x; NVSDK_NGX_DLSS_Hint_Render_Preset 11 K, 12 L, 13 M):
// K for DLAA, Quality and Balanced, M for Performance, L for Ultra Performance. XCX_DLSS_PRESET=<a..m> takes one model
// in every mode (read when a feature is created).
static int preset_value(void) {
    const char *v = cfg_get("XCX_DLSS_PRESET");
    int c = v && v[0] && !v[1] ? v[0] | 0x20 : 0;
    return c >= 'a' && c <= 'm' ? c - 'a' + 1 : 0;
}

static int perf_quality(uint32_t w, uint32_t ow) {
    if (w >= ow) return NGX_PERFQ_DLAA;
    double k = (double)w / ow;
    return k >= 0.66 ? 2 : k >= 0.57 ? 1 : k >= 0.49 ? 0 : 3;
}

static void evaluate(VkExec *ex, const VkExecTarget *dst) {
    const VkExecTarget *t = &D.color, *z = &D.depth;
    uint32_t w = t->width, h = t->height, ow = dst->width, oh = dst->height;
    VkCommandBuffer cb = ex->cb;
    vk_exec_barrier(ex);   // what drew the scene is done (no pass is open here)
    struct Size *e = size_for(cb, w, h, ow, oh);
    if (!e) return dl_fail("work image creation", 0);
    if (e != D.last) D.reset = 1;
    if (!D.params && !NGX_OK(N.alloc_params(&D.params))) return dl_fail("parameter allocation", 0);
    NgxParam *p = D.params;
    VkImageView zv = vk_exec_target_view(z->img, z->format, VK_IMAGE_VIEW_TYPE_2D, VK_IMAGE_ASPECT_DEPTH_BIT, z->level,
                                         z->base_layer, 1, 0);
    float jx = D.jit_used ? D.ux : 0.0f, jy = D.jit_used ? D.uy : 0.0f;
    static int no_mv = -1;
    if (no_mv < 0) no_mv = !rt_knob("DLSS_MV", "1");   // XCX_DLSS_MV=0: zero motion (test)
    if (!mv_pass(ex, e, zv, jx, jy) || no_mv) {
        VkClearColorValue zero = {{0, 0, 0, 0}};
        VkImageSubresourceRange all = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        vkCmdClearColorImage(cb, e->mv, VK_IMAGE_LAYOUT_GENERAL, &zero, 1, &all);
        full_barrier(cb);
        if (!no_mv) D.reset = 1;
    }
    VkImageView cv = vk_exec_target_view(t->img, t->format, VK_IMAGE_VIEW_TYPE_2D, VK_IMAGE_ASPECT_COLOR_BIT, t->level,
                                         t->base_layer, 1, 0);
    rec_drain();   // NGX records into the command buffer itself: the recorder thread must have made every call so far
    if (!e->feat) {
        int preset = preset_value();
        p_setu(p, "CreationNodeMask", 1);
        p_setu(p, "VisibilityNodeMask", 1);
        p_setu(p, "Width", w);
        p_setu(p, "Height", h);
        p_setu(p, "OutWidth", ow);
        p_setu(p, "OutHeight", oh);
        p_seti(p, "PerfQualityValue", perf_quality(w, ow));
        // MVLowRes: motion vectors at the render size. Not IsHDR: DLSS reads the tonemapped, gamma-encoded picture.
        p_seti(p, "DLSS.Feature.Create.Flags", 1 << 1);
        p_seti(p, "DLSS.Enable.Output.Subrects", 0);
        int pk = preset ? preset : 11, pm = preset ? preset : 13, pl = preset ? preset : 12;
        p_setu(p, "DLSS.Hint.Render.Preset.DLAA", (unsigned)pk);
        p_setu(p, "DLSS.Hint.Render.Preset.UltraQuality", (unsigned)pk);
        p_setu(p, "DLSS.Hint.Render.Preset.Quality", (unsigned)pk);
        p_setu(p, "DLSS.Hint.Render.Preset.Balanced", (unsigned)pk);
        p_setu(p, "DLSS.Hint.Render.Preset.Performance", (unsigned)pm);
        p_setu(p, "DLSS.Hint.Render.Preset.UltraPerformance", (unsigned)pl);
        NgxResult r = N.create(cb, NGX_FEATURE_SUPERSAMPLING, p, &e->feat);
        if (!NGX_OK(r) || !e->feat) return dl_fail("DLSS feature creation", r);
        D.reset = 1;
        int q = perf_quality(w, ow), pr = q == 0 ? pm : q == 3 ? pl : pk;
        rt_log("dlss: feature created at %ux%u -> %ux%u, quality value %d, preset %c (scene format %d, depth format %d)",
               w, h, ow, oh, q, 'A' + pr - 1, (int)t->format, (int)z->format);
    }
    if (D.last_color != t->image) D.reset = 1, D.last_color = t->image;
    NgxResourceVK rc = res_image(cv, t->image, t->format, w, h, VK_IMAGE_ASPECT_COLOR_BIT, 0);
    NgxResourceVK ro = res_image(e->out_view, e->out, VK_FORMAT_R16G16B16A16_SFLOAT, ow, oh, VK_IMAGE_ASPECT_COLOR_BIT, 1);
    NgxResourceVK rz = res_image(zv, z->image, z->format, w, h, VK_IMAGE_ASPECT_DEPTH_BIT, 0);
    NgxResourceVK rm = res_image(e->mv_view, e->mv, VK_FORMAT_R16G16_SFLOAT, w, h, VK_IMAGE_ASPECT_COLOR_BIT, 0);
    p_setp(p, "Color", &rc);
    p_setp(p, "Output", &ro);
    p_setp(p, "Depth", &rz);
    p_setp(p, "MotionVectors", &rm);
    p_setf(p, "Jitter.Offset.X", jx);
    p_setf(p, "Jitter.Offset.Y", jy);
    p_setf(p, "Sharpness", 0.0f);
    p_seti(p, "Reset", D.reset);
    p_setf(p, "MV.Scale.X", 1.0f);
    p_setf(p, "MV.Scale.Y", 1.0f);
    p_setu(p, "DLSS.Render.Subrect.Dimensions.Width", w);
    p_setu(p, "DLSS.Render.Subrect.Dimensions.Height", h);
    p_setf(p, "DLSS.Pre.Exposure", 1.0f);
    p_setf(p, "DLSS.Exposure.Scale", 1.0f);
    uint64_t now = plat_now_ns();
    double dt = D.last_eval_ns ? (now - D.last_eval_ns) / 1e6 : 0;
    D.last_eval_ns = now;
    p_setf(p, "FrameTimeDeltaInMsec", dt > 0 && dt < 100 ? (float)dt : 16.67f);
    NgxResult r = N.evaluate(cb, e->feat, p, NULL);
    if (!NGX_OK(r)) return dl_fail("DLSS evaluation", r);
    D.reset = 0;
    D.last = e, e->used = D.evals + 1;
    full_barrier(cb);
    // XCX_DLSS_MVDEBUG=1 (test): the motion vectors in place of the picture, x in red and y in green, render pixels
    static int mvdebug = -1;
    if (mvdebug < 0) mvdebug = rt_knob("DLSS_MVDEBUG", "0");
    VkImageBlit b = {.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
                     .srcOffsets = {{0, 0, 0}, {(int32_t)(mvdebug ? w : ow), (int32_t)(mvdebug ? h : oh), 1}},
                     .dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, dst->level, dst->base_layer, 1},
                     .dstOffsets = {{0, 0, 0}, {(int32_t)ow, (int32_t)oh, 1}}};
    vkCmdBlitImage(cb, mvdebug ? e->mv : e->out, VK_IMAGE_LAYOUT_GENERAL, dst->image, VK_IMAGE_LAYOUT_GENERAL, 1, &b,
                   VK_FILTER_LINEAR);
    full_barrier(cb);
    ex->recorded += 4;
    vk_exec_draw_new_cb();   // NGX bound pipelines and descriptor sets of its own: the next draw binds everything again
    next_jitter(w, ow);
    if (++D.evals == 1 || D.evals == 300 || D.evals % 3600 == 0)
        rt_log("dlss: %llu frames (scene %ux%u, output %ux%u; %llu frames without the DLSS point, %llu with camera "
               "motion, %llu without a camera, %llu jitter holds)",
               (unsigned long long)D.evals, w, h, ow, oh, (unsigned long long)D.frames_missed,
               (unsigned long long)D.mv_frames, (unsigned long long)D.no_cam, (unsigned long long)D.holds);
}

void vk_dlss_pass(VkExec *ex) {
    if (!dl_active() || D.done) return;
    const VkExecTarget *c = &ex->color[0], *z = &ex->depth;
    // The G-buffer: five targets or more with a 32-bit depth; its R11G11B10F target of the depth's size is the light
    // target. A later G-buffer in the frame (a menu's model view) replaces an earlier one.
    if (ex->pass_ncolor >= 5 && z->image && depth32(z->format) && z->width >= SCENE_MIN_W && z->samples <= 1) {
        for (uint32_t i = 0; i < ex->pass_ncolor; i++) {
            const VkExecTarget *t = &ex->color[i];
            if (t->image && t->format == VK_FORMAT_B10G11R11_UFLOAT_PACK32 && t->width == z->width &&
                t->height == z->height && !t->is3d && t->samples <= 1) {
                if (D.gbuf && D.color.image != t->image) D.cam_ok = 0;   // another view: its own camera
                D.color = *t, D.depth = *z, D.gbuf = 1, D.post = 0;
                break;
            }
        }
        return;
    }
    if (!D.gbuf) return;
    if (!D.post) {   // the first pass into the light target alone, without depth: the scene is drawn
        if (ex->pass_ncolor == 1 && !z->image && c->image == D.color.image) D.post = 1;
        return;
    }
    // the game's scaling pass: one colour target of the output size, no depth
    unsigned o[2], s[2];
    res_sizes(o, s);
    if (ex->pass_ncolor == 1 && c->image && !z->image && c->image != D.color.image && c->width == o[0] &&
        c->height == o[1] && c->samples <= 1 && !c->is3d && !(c->img && (c->img->aspect & VK_IMAGE_ASPECT_DEPTH_BIT))) {
        D.done = 1;
        if (D.color.width > c->width || D.color.height > c->height) {   // supersampling: DLSS only scales up
            static int told;
            if (!told++) rt_log("dlss: the scene (%ux%u) is larger than the output (%ux%u): DLSS skipped",
                                D.color.width, D.color.height, c->width, c->height);
            return;
        }
        evaluate(ex, c);
        if (!D.failed) D.up = 1, D.up_pass = ex->pass_serial + 1;   // vk_exec_pass_begin counts this pass after us
    }
}

// The scaling pass's first draw is the game's FXAA, which would draw the scene at its own size over DLSS's picture.
int vk_dlss_skip_draw(VkExec *ex) {
    if (D.up != 1 || ex->pass_serial != D.up_pass) return 0;
    D.up = 2;
    return 1;
}

void vk_dlss_frame(void) {
    int on = N.available && is_dlss();
    if (!N.available && is_dlss() && !vk_dlss_wanted()) {
        static int told;
        if (!told++) rt_log("dlss: upscaler = dlss takes effect after a restart");
    }
    if (on != D.on) {
        rt_log("dlss: %s", on ? "on" : "off");
        D.on = on;
        D.reset = 1, D.prev_ok = D.cam_ok = 0, D.jit_ok = D.jit_used = D.want = 0, D.hold = 0;
        D.gbuf = D.post = D.done = D.up = 0;
        return;
    }
    // Upscaling: textures are sampled for the output's resolution, not the scene's (NVIDIA's DLSS guide: a mip bias of
    // log2(render / output)), so their detail survives the scaling. The ratio is that of the size DLSS last ran at;
    // DLAA (1:1) and DLSS off give 0.
    static float bias;
    float b = dl_active() && D.last && D.last->ow ? log2f((float)D.last->w / (float)D.last->ow) : 0.0f;
    if (b > -0.01f) b = 0.0f;
    if (fabsf(b - bias) > 0.01f) {
        bias = b;
        vk_sampler_dlss_bias(b);
        rt_log("dlss: texture mip bias %.2f", b);
    }
    if (!dl_active()) return;
    if (!D.done) D.frames_missed++, D.prev_ok = 0;
    if (D.jit_used && !D.done) D.hold = 60, D.holds++;   // jittered but not resolved: that frame shook
    else if (D.hold) D.hold--;
    if (!D.done) D.jit_ok = 0;
    D.gbuf = D.post = D.done = D.up = D.jit_used = D.cam_ok = 0;
}
