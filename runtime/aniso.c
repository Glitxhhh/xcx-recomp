// XCX_ANISO: forced anisotropic filtering. The setting is read at start and again when the configuration changes (live):
// the game builds its NVN samplers with its own maximum anisotropy, and NVN queries (nvnSamplerGetMaxAnisotropy) still
// answer that value; only the Vulkan sampler of a mipmapped, linearly minified texture takes the forced level instead
// (vk_texture.c sampler_for). 0 keeps every sampler as the game made it; the menu offers 2, 4, 8 and 16.
#include "cfg.h"
#include "host.h"
#include "vk.h"

static unsigned aniso_cfg_seen;
static int aniso_cfg_seen_valid;
static int aniso_level_seen = -1;

void rt_aniso_update(void) {
    unsigned version = cfg_version();
    if (aniso_cfg_seen_valid && version == aniso_cfg_seen) return;
    aniso_cfg_seen = version;
    aniso_cfg_seen_valid = 1;

    int level = cfg_int("XCX_ANISO");
    if (level < 2) level = 0;   // "0" and "1" keep the game's own anisotropy
    if (level == aniso_level_seen) return;
    aniso_level_seen = level;
    vk_sampler_aniso((float)level);
    if (level) rt_log("aniso: forcing %dx on mipmapped textures", level);
    else rt_log("aniso: the game's own values");
}

void rt_aniso_init(void) {
    aniso_cfg_seen_valid = 0;
    aniso_level_seen = -1;
    rt_aniso_update();
}
