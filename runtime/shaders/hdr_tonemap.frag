#version 450
// The game's tonemap pass for HDR output (runtime/vk_hdr.c vk_hdr_shader_swap puts it in place of the translated
// shader when XCX_HDR=1). Same inputs as the game's: fp_t_tcb_8 (the scene, r11g11b10f, which the pass also writes),
// fp_t_tcb_A (the 16x16x16 grading LUT) and c3[1] (x: the exposure gain, z and w: the LUT's scale and bias, 15/16 and
// 1/32). The game scales the scene by the gain, clamps it to 0..1, takes it to the power 1/2.2 and looks the result up
// in the LUT, whose output is the SDR picture's code value. FXAA then draws it into the 8-bit window texture.
// Here a colour whose largest channel stays within 1 gets exactly that; above 1 the LUT grades the colour divided by
// its largest channel and the result is scaled back up, so the grade's hue and contrast hold while highlights keep
// going; a roll-off on the largest channel stops at the display's peak. Values above 1 are written as sRGB codes past
// 1, which the r11g11b10f target keeps; the 8-bit window texture clips them, and the present (shaders/hdr_encode.comp)
// adds what lies above 1 back. 1.0 is the picture's white (XCX_HDR_GAME_NITS).
// Hue correction (hdr.z, XCX_HDR_HUE): the game's per-channel clamp turns bright saturated colours towards white (the
// sun, flames, lit armour); the extended curve keeps their ratios, so they stay saturated. Above the white the colour
// moves towards the game's own SDR result at the HDR luminance, by up to hdr.z.
// hdr.w is 0 while the swapchain is not HDR10: then the pass is the game's.
// Approach after TotK Recomp's shaders/hdr_tonemap.frag (MIT), itself after RenoDX's TotK mod by souperman9 and Musa
// (github.com/clshortfuse/renodx, MIT); the code is ours.
// Bindings are those of the game's translated shader: vk_hdr.c rewrites them from the original's names.
layout(location = 0) in vec4 uv;
layout(location = 0) out vec4 color;

layout(set = 0, binding = 0, std140) uniform SupportBuffer {
    vec4 pad[88];
    vec4 hdr;   // byte 1408 (vk_exec_draw.c support_fill): game white / HUD white, peak / game white, hue, HDR10 on
} support_buffer;
layout(set = 0, binding = 1) uniform sampler2D fp_t_tcb_8;
layout(set = 0, binding = 2) uniform sampler3D fp_t_tcb_A;
layout(set = 0, binding = 3, std140) uniform C3 { vec4 v[4096]; } fp_c3;

float maxc(vec3 c) { return max(c.r, max(c.g, c.b)); }

vec3 srgb_decode(vec3 c) {
    return mix(c / 12.92, pow((c + 0.055) / 1.055, vec3(2.4)), greaterThan(c, vec3(0.04045)));
}

vec3 srgb_encode(vec3 l) {   // past 1 as well: the same curve continued
    return mix(l * 12.92, 1.055 * pow(l, vec3(1.0 / 2.4)) - 0.055, greaterThan(l, vec3(0.0031308)));
}

vec3 grade(vec3 e) {   // the game's: e is the scene at the exposure gain; the LUT's code value out
    vec4 k = fp_c3.v[1];
    vec3 g = exp2(log2(abs(clamp(e, 0.0, 1.0))) * 0.454545438);
    return texture(fp_t_tcb_A, g * k.z + k.w).rgb;
}

void main() {
    vec3 e = texture(fp_t_tcb_8, uv.xy).rgb * fp_c3.v[1].x;
    vec3 sdr = grade(e);
    float m = maxc(e);
    float peak = support_buffer.hdr.y;
    if (support_buffer.hdr.w < 0.5 || m <= 1.0 || peak <= 1.0) {
        color = vec4(sdr, 1.0);
        return;
    }
    vec3 c = srgb_decode(grade(e / m)) * m;

    vec3 s = srgb_decode(sdr);
    const vec3 Y = vec3(0.2126, 0.7152, 0.0722);   // Rec. 709 luminance
    float yc = dot(c, Y), ys = dot(s, Y), hue = clamp(support_buffer.hdr.z, 0.0, 1.0);
    if (hue > 0.0 && ys > 1e-5) c = mix(c, s * (yc / ys), hue * smoothstep(1.0, 2.0, m));

    // roll-off of the largest channel from half the peak (not below the white) to the peak
    float top = maxc(c), knee = max(0.5 * peak, 1.0);
    if (top > knee) {
        float r = peak - knee;
        c *= (r > 0.0 ? knee + r * (1.0 - exp(-(top - knee) / r)) : knee) / top;
    }
    color = vec4(srgb_encode(max(c, vec3(0.0))), 1.0);
}
