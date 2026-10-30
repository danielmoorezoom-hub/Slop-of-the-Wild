// Video > Clarity: Cemu's "Clarity" for BOTW (cemu_graphic_packs, BreathOfTheWild/Enhancements, shader
// 37040a485a29d54e: Clarity GFX by Jamie and Kiri with Crementif; presets by Serfrost; CC0 1.0). On the Wii U it
// replaces the game's HDR -> LDR composite; here it replaces the same pass of the Switch build: the translated
// fragment shader that adds the bloom chain to the HDR scene and tone maps it (BOTW's 1 - exp(-x) curve, recognised
// by its constants: the 10000 clamp, the 0.2989 luma weight and -log2(e)). The replacement keeps that shader's
// interface (t0: HDR scene, t1: bloom, both at the same coordinates; Tint at binding 19) and runs Clarity's chain:
// bloom scaled down, fake HDR, luma sharpening, Uncharted 2 tone map, levels, Technicolor, Technicolor 2, BOTW
// vibrance, filmic pass, lift / gamma / gain, vibrance.
#include <stdio.h>
#include <string.h>

#include "../../settings.h"
#include "../maxwell/shader.h"

typedef struct {
    float bloom, hdr_power, radius1, radius2;
    int luma_sharpen;
    float sharp_strength, sharp_clamp, offset_bias, gamma, sat_factor;
    int black, white;
    float lift, lgg_gamma, gain, vibrance, vib_b;
    int tech;
    float tech_power, tech_negative, tech_strength;
    float t2_r, t2_g, t2_b, t2_brightness, t2_strength, t2_saturation;
    float f_contrast, f_bleach, f_saturation, f_strength, f_fade, f_linearization, f_base_curve, f_base_gamma, f_effect_gamma;
} ClarityPreset;

// 1: Serfrost's Preset (Recommended, Cemu's 10), 2: Dim Display (9), 3: Bright Display (8)
static const ClarityPreset k_presets[4] = {
    {0},
    {0.30f, 1.15f, 0.793f, 0.87f, 1, 0.45f, 0.085f, 1.0f, 1.0f, 0.07f, 5, 210, 1.027f, 0.83f, 1.10f, 0.45f, 1.3f,
     1, 6.0f, 0.88f, 0.30f, -0.12f, -0.25f, -0.15f, 0.4f, 1.0f, 0.90f,
     1.03f, 0.0f, -0.35f, 0.60f, 0.10f, 1.0f, 1.5f, 1.0f, 0.68f},
    {0.70f, 1.20f, 0.793f, 0.87f, 0, 0.45f, 0.85f, 1.0f, 1.05f, 0.07f, 1, 235, 1.027f, 0.83f, 1.10f, 0.45f, 1.0f,
     0, 5.0f, 0.88f, 0.10f, -0.09f, -0.09f, -0.00f, 0.4f, 1.2f, 1.15f,
     1.00f, 0.0f, -0.17f, 0.48f, 0.08f, 1.0f, 1.0f, 1.0f, 1.68f},
    {0.40f, 1.20f, 0.793f, 0.87f, 0, 0.45f, 0.85f, 1.0f, 0.98f, 0.07f, 2, 300, 1.027f, 0.83f, 1.10f, 0.15f, 1.0f,
     0, 5.0f, 0.88f, 0.10f, -0.09f, -0.09f, -0.00f, 0.4f, 1.2f, 1.10f,
     1.00f, 0.0f, -0.17f, 0.48f, 0.08f, 1.0f, 1.0f, 1.0f, 1.68f},
};

static const char k_body[] =
    "layout(std140, set = 1, binding = 19) uniform Tint { vec4 ltint[2]; };\n"
    "layout(set = 1, binding = 32) uniform sampler2D t0;\n"
    "layout(set = 1, binding = 33) uniform sampler2D t1;\n"
    "layout(location = 0) in vec4 v0;\n"
    "layout(location = 0) out vec4 c0;\n"
    "vec3 hdr_tap(vec2 p) { return min(texture(t0, p).rgb, vec3(10000.0)); }\n"
    "vec3 HDRPass(vec2 pos, vec2 pxy) {\n"
    "  vec3 color = hdr_tap(pos);\n"
    "  vec3 s1 = vec3(0.0), s2 = vec3(0.0);\n"
    "  const vec2 o[8] = vec2[8](vec2(1.5,-1.5), vec2(-1.5,-1.5), vec2(1.5,1.5), vec2(-1.5,1.5), vec2(0.0,-2.5), vec2(0.0,2.5), vec2(-2.5,0.0), vec2(2.5,0.0));\n"
    "  for (int i = 0; i < 8; i++) { s1 += hdr_tap(pos + o[i] * RADIUS1 * pxy); s2 += hdr_tap(pos + o[i] * RADIUS2 * pxy); }\n"
    "  s1 *= 0.005; s2 *= 0.010;\n"
    "  vec3 HDR = (color + (s2 - s1)) * (RADIUS2 - RADIUS1);\n"
    "  return pow(abs(HDR + color), vec3(abs(HDR_POWER))) + HDR;\n"
    "}\n"
    "float LumaSharpen(vec2 pos, vec2 pxy) {\n"
    "  vec3 ori = hdr_tap(pos);\n"
    "  vec3 blur = hdr_tap(pos + vec2(pxy.x, -pxy.y) * 0.5 * OFFSET_BIAS) + hdr_tap(pos + vec2(-pxy.x, -pxy.y) * 0.5 * OFFSET_BIAS)\n"
    "            + hdr_tap(pos + vec2(pxy.x, pxy.y) * 0.5 * OFFSET_BIAS) + hdr_tap(pos + vec2(-pxy.x, pxy.y) * 0.5 * OFFSET_BIAS);\n"
    "  blur *= 0.25;\n"
    "  vec3 sharp = ori - blur;\n"
    "  vec4 k = vec4(vec3(0.2126, 0.7152, 0.0722) * SHARP_STRENGTH * (0.5 / SHARP_CLAMP), 0.5);\n"
    "  float l = clamp(dot(vec4(sharp, 1.0), k), 0.0, 1.0);\n"
    "  return (SHARP_CLAMP * 2.0) * l - SHARP_CLAMP;\n"
    "}\n"
    "vec3 Uncharted2(vec3 color) {\n"
    "  const float A = 0.15, B = 0.50, C = 0.10, D = 0.20, E = 0.02, F = 0.30, W = 11.2;\n"
    "  color *= 2.0;\n"
    "  color = ((color * (A * color + C * B) + D * E) / (color * (A * color + B) + D * F)) - E / F;\n"
    "  float white = ((W * (A * W + C * B) + D * E) / (W * (A * W + B) + D * F)) - E / F;\n"
    "  return pow(max(color / white, vec3(0.0)), vec3(1.0 / GAMMA));\n"
    "}\n"
    "vec3 Levels(vec3 c) {\n"
    "  float bp = float(BLACK) / 255.0, wp = 255.0 / float(WHITE - BLACK);\n"
    "  return c * wp - bp * wp;\n"
    "}\n"
    "vec3 Technicolor(vec3 tcol) {\n"
    "  const vec3 cyan = vec3(0.0, 1.30, 1.0), magenta = vec3(1.0, 0.0, 1.05), yellow = vec3(1.6, 1.6, 0.05);\n"
    "  const vec2 redorange = vec2(1.05, 0.620), green = vec2(0.30, 1.0), magenta2 = vec2(1.0, 1.05);\n"
    "  float k = 1.0 / (TECH_NEGATIVE * TECH_POWER);\n"
    "  vec3 r = vec3(dot(redorange, tcol.rg * k)) + cyan, g = vec3(dot(green, tcol.rg * k)) + magenta, b = vec3(dot(magenta2, tcol.rb * k)) + yellow;\n"
    "  return mix(tcol, r * g * b, TECH_STRENGTH);\n"
    "}\n"
    "vec3 Technicolor2(vec3 source) {\n"
    "  vec3 cs = vec3(T2_R, T2_G, T2_B);\n"
    "  vec3 temp = 1.0 - source;\n"
    "  vec3 target = temp.grg, target2 = temp.bbr;\n"
    "  vec3 temp2 = source * target * target2;\n"
    "  temp = temp2 * cs;\n"
    "  temp2 *= T2_BRIGHTNESS;\n"
    "  target = temp.grg; target2 = temp.bbr;\n"
    "  temp = source - target + temp2;\n"
    "  temp2 = temp - target2;\n"
    "  vec3 color = mix(source, temp2, T2_STRENGTH);\n"
    "  return mix(vec3(dot(color, vec3(0.333))), color, T2_SATURATION);\n"
    "}\n"
    "vec3 BotWVibrance(vec3 color) {\n"
    "  float avg = (color.r + color.g + color.b) / 3.0, maxc = max(color.r, max(color.g, color.b));\n"
    "  float w = 1.0 - pow(1.0 - 2.0 * avg, 2.0);\n"
    "  return mix(vec3(maxc), color, 1.0 + w * SAT_FACTOR);\n"
    "}\n"
    "float filmic_ch(float D, float B) { return D < 0.5 ? (2.0 * D - 1.0) * (B - B * B) + B : (2.0 * D - 1.0) * (sqrt(B) - B) + B; }\n"
    "vec3 FilmPass(vec3 inputColor) {\n"
    "  vec3 B = clamp(inputColor, 0.0, 1.0);\n"
    "  B = pow(B, vec3(F_LINEARIZATION));\n"
    "  B = mix(vec3(0.0), B, F_CONTRAST);\n"
    "  const vec3 LumCoeff = vec3(0.2126, 0.7152, 0.0722);\n"
    "  vec3 D = vec3(dot(B, LumCoeff));\n"
    "  B = pow(B, vec3(1.0 / F_BASE_GAMMA));\n"
    "  float y = 1.0 / (1.0 + exp(0.5)), v = 1.0 / (1.0 + exp(F_BASE_CURVE / 2.0));\n"
    "  D = (1.0 / (1.0 + exp(-1.0 * (D - 0.5))) - y) / (1.0 - 2.0 * y);\n"
    "  D = pow(max(D, vec3(0.0)), vec3(1.0 / F_EFFECT_GAMMA));\n"
    "  D = abs(mix(D, 1.0 - D, F_BLEACH));\n"
    "  vec3 C = vec3(filmic_ch(D.r, B.r), filmic_ch(D.g, B.g), filmic_ch(D.b, B.b));\n"
    "  vec3 F = mix(B, C, F_STRENGTH);\n"
    "  F = (1.0 / (1.0 + exp(-F_BASE_CURVE * (F - 0.5))) - v) / (1.0 - 2.0 * v);\n"
    "  float S = F_SATURATION, Fd = F_FADE;\n"
    "  vec3 iF = F;\n"
    "  F.r = iF.r * (1.0 - S) + iF.g * S + iF.b * S;\n"
    "  F.g = iF.r * S + iF.g * ((1.0 - Fd) - S) + iF.b * (Fd + S);\n"
    "  F.b = iF.r * S + iF.g * (Fd + S) + iF.b * ((1.0 - Fd) - S);\n"
    "  float N = dot(F, LumCoeff);\n"
    "  vec3 Cn = N < 0.5 ? (2.0 * N - 1.0) * (F - F * F) + F : (2.0 * N - 1.0) * (sqrt(max(F, vec3(0.0))) - F) + F;\n"
    "  Cn = pow(max(Cn, vec3(0.0)), vec3(1.0 / F_LINEARIZATION));\n"
    "  return mix(B, Cn, F_STRENGTH);\n"
    "}\n"
    "vec3 LiftGammaGain(vec3 color) {\n"
    "  color = color * (1.5 - 0.5 * LGG_LIFT) + 0.5 * LGG_LIFT - 0.5;\n"
    "  color = clamp(color, 0.0, 1.0) * LGG_GAIN;\n"
    "  return clamp(pow(color, vec3(1.0 / LGG_GAMMA)), 0.0, 1.0);\n"
    "}\n"
    "vec3 Vibrance(vec3 color) {\n"
    "  float luma = dot(vec3(0.2126, 0.7152, 0.0722), color);\n"
    "  float sat = max(color.r, max(color.g, color.b)) - min(color.r, min(color.g, color.b));\n"
    "  vec3 k = vec3(1.0, 1.0, VIB_B) * VIBRANCE;\n"
    "  return mix(vec3(luma), color, 1.0 + (k * (1.0 - (sign(k) * sat))));\n"
    "}\n"
    "void main() {\n"
    "  vec2 uv = v0.xy;\n"
    "  vec2 pxy = 1.0 / vec2(textureSize(t0, 0));\n"
    "  vec3 bloom = texture(t1, uv).rgb * BLOOM;\n"
    "  vec3 color = HDRPass(uv, pxy);\n"
    "#if LUMA_SHARPEN\n"
    "  color += vec3(LumaSharpen(uv, pxy));\n"
    "#endif\n"
    "  color += bloom;\n"
    "  color = Uncharted2(color);\n"
    "  color = Levels(color);\n"
    "#if TECH\n"
    "  color = Technicolor(color);\n"
    "#endif\n"
    "  color = Technicolor2(color);\n"
    "  color = BotWVibrance(color);\n"
    "  color = FilmPass(color);\n"
    "  color = LiftGammaGain(color);\n"
    "  color = Vibrance(color);\n"
    "  c0 = vec4(clamp(color, 0.0, 1.0), 0.0);\n"
    "  if (ltint[0].w > 0.0f) { vec3 g = c0.rgb; float l = dot(g, vec3(0.299f, 0.587f, 0.114f));\n"
    "    c0.rgb = mix(g, ltint[0].rgb * pow(l, ltint[1].w) + ltint[1].rgb * l, ltint[0].w); }\n"
    "}\n";

// The source to compile instead of src (BOTW's tone-mapping composite with Clarity on), else src
const char *vk_clarity_override(const char *src, int stage) {
    int p = g_settings.clarity;
    if (p <= 0 || p > 3 || stage != STAGE_FRAGMENT || !src) return src;
    if (!strstr(src, "0x461c4000u") || !strstr(src, "0x3e99096cu") || !strstr(src, "0xbfb8aa3bu") ||
        !strstr(src, "binding = 33) uniform sampler2D t1;") || strstr(src, "binding = 34)"))
        return src;
    static char out[4][16384];
    if (!out[p][0]) {
        const ClarityPreset *c = &k_presets[p];
        int n = snprintf(out[p], sizeof out[p],
            "#version 460\n// Clarity (Cemu graphic packs, CC0): Serfrost preset %d\n"
            "#define BLOOM %.4f\n#define HDR_POWER %.4f\n#define RADIUS1 %.4f\n#define RADIUS2 %.4f\n#define LUMA_SHARPEN %d\n"
            "#define SHARP_STRENGTH %.4f\n#define SHARP_CLAMP %.4f\n#define OFFSET_BIAS %.4f\n#define GAMMA %.4f\n"
            "#define SAT_FACTOR %.4f\n#define BLACK %d\n#define WHITE %d\n#define LGG_LIFT %.4f\n#define LGG_GAMMA %.4f\n"
            "#define LGG_GAIN %.4f\n#define VIBRANCE %.4f\n#define VIB_B %.4f\n#define TECH %d\n#define TECH_POWER %.4f\n"
            "#define TECH_NEGATIVE %.4f\n#define TECH_STRENGTH %.4f\n#define T2_R %.4f\n#define T2_G %.4f\n#define T2_B %.4f\n"
            "#define T2_BRIGHTNESS %.4f\n#define T2_STRENGTH %.4f\n#define T2_SATURATION %.4f\n#define F_CONTRAST %.4f\n"
            "#define F_BLEACH %.4f\n#define F_SATURATION %.4f\n#define F_STRENGTH %.4f\n#define F_FADE %.4f\n"
            "#define F_LINEARIZATION %.4f\n#define F_BASE_CURVE %.4f\n#define F_BASE_GAMMA %.4f\n#define F_EFFECT_GAMMA %.4f\n",
            p, c->bloom, c->hdr_power, c->radius1, c->radius2, c->luma_sharpen, c->sharp_strength, c->sharp_clamp,
            c->offset_bias, c->gamma, c->sat_factor, c->black, c->white, c->lift, c->lgg_gamma, c->gain, c->vibrance,
            c->vib_b, c->tech, c->tech_power, c->tech_negative, c->tech_strength, c->t2_r, c->t2_g, c->t2_b,
            c->t2_brightness, c->t2_strength, c->t2_saturation, c->f_contrast, c->f_bleach, c->f_saturation,
            c->f_strength, c->f_fade, c->f_linearization, c->f_base_curve, c->f_base_gamma, c->f_effect_gamma);
        if (n < 0 || (size_t)n + sizeof k_body >= sizeof out[p]) return src;
        memcpy(out[p] + n, k_body, sizeof k_body);
    }
    static int logged[4];
    if (!logged[p]) { logged[p] = 1; fprintf(stderr, "[clarity] Serfrost preset %d replaces the tone-mapping composite\n", p); }
    return out[p];
}
