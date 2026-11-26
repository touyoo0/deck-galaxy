#version 450

// The native pipeline's mesh draw (Rogue Galaxy: added): the game's texture
// function, alpha test and fog, in its own units (0x80 is 1.0 for colors
// and alphas). Blending is the pipeline's: the alpha written out is As /
// 0x80, the blend's source alpha and what a target stores (clamped at 1).
layout(set = 2, binding = 0) uniform sampler2D image;
layout(set = 2, binding = 1) uniform sampler2D palette;
layout(set = 3, binding = 0) uniform Material {
    // x: 0 untextured, 1 RGBA, 2 Index8 through the palette, 3 a target
    //    (its alpha stored as A / 0x80, the blend's factor)
    // y: the texture function (0 modulate, 1 decal, 2 highlight, 3 highlight2)
    // z: the texel's alpha is used (TCC)
    // w: the palette's entries
    uvec4 mode;
    // x: the alpha test (0 always ... the Compare enum), y: its reference,
    // z: whether a failing pixel is discarded, w: fog on
    uvec4 test;
    // x, y: the size the uvs are normalized to (the game's 2^TW, 2^TH);
    // z, w: the texture's own, which an upload may leave smaller
    vec4 size;
    // x: a channel shuffle (1: alpha's bits 2-7 from green's), z: clamp u,
    // w: clamp v (y unused)
    vec4 blend;
    vec4 fogColor; // 0-1
    // x: the texel's alpha replaced by this (0-255) when not negative (a
    // CT24 read); y: then 0 where the texel is black (AEM); z: the
    // destination alpha test, 0 none, 1 drawn where the target's alpha is
    // below 0x80, 2 where at least (read from `palette`, the target's copy)
    vec4 texa;
    // The GS's CLAMP: x, y the u region (min, max), z, w the v region;
    // wrap.x, .y the modes (0 repeat, 1 clamp, 2 region clamp, 3 region
    // repeat).
    uvec4 region;
    uvec4 wrap;
} material;

int wrapAxis(int a, int size, uint mode, uint lo, uint hi, bool clampOld) {
    if (mode == 2u)
        a = clamp(a, int(lo), int(hi));
    else if (mode == 3u)
        a = (a & int(lo)) | int(hi);
    else
        return clampOld ? clamp(a, 0, size - 1) : int(mod(float(a), float(size)));
    return clamp(a, 0, size - 1);
}

float wrapAxisF(float t, float size, uint mode, uint lo, uint hi, bool clampOld) {
    if (mode == 2u)
        return clamp(t, float(lo) + 0.25, float(hi) + 0.75);
    if (mode == 3u) {
        int a = int(floor(t));
        int w = clamp((a & int(lo)) | int(hi), 0, int(size) - 1);
        return float(w) + clamp(fract(t), 0.125, 0.875);
    }
    return clampOld ? clamp(t, 0.25, size - 0.25) : mod(t, size);
}

layout(location = 0) in vec2 inUv;
layout(location = 1) in vec4 inColor;
layout(location = 2) in float inFog;
layout(location = 0) out vec4 outColor;

bool passes(uint compare, float a, float ref) {
    switch (compare) {
    case 0u: return true;
    case 1u: return false;
    case 2u: return a < ref;
    case 3u: return a <= ref;
    case 4u: return a == ref;
    case 5u: return a >= ref;
    case 6u: return a > ref;
    default: return a != ref;
    }
}

void main() {
#ifndef DQ8_NO_DISCARD
    if (material.texa.z > 0.0) {
        // Stored as A / 0x80: bit 7 set is 1.0.
        bool high = texelFetch(palette, ivec2(gl_FragCoord.xy), 0).a >= 0.999;
        if (high != (material.texa.z > 1.5))
            discard;
    }
#endif
    // The vertex color: exact 8-bit integer truncation in Original mode, or
    // smooth float interpolation in Remaster mode (material.blend.y != 0) to
    // eliminate color banding on skies, skin, walls and fog.
    // material.blend.y == 2.0 marks 3D textured surfaces eligible for 0-cost
    // dFdx/dFdy micro-relief bump shading.
    bool remaster = material.blend.y > 0.5;
    bool remaster3d = material.blend.y > 1.5;
    vec4 f = remaster ? clamp(inColor, 0.0, 255.0) : clamp(floor(inColor), 0.0, 255.0);
    vec4 c = f;
    if (material.mode.x != 0u) {
        vec4 t;
        vec2 texel = inUv * material.size.xy; // in the game's texels
        // A region (a tile of a sheet) is fetched as the GS addresses it.
        bool region = material.mode.x != 3u && (material.wrap.x >= 2u || material.wrap.y >= 2u);
        if (material.mode.x == 2u || (region && !remaster)) {
            // Fetched, not filtered: an index is not a color.
            ivec2 size = ivec2(material.size.zw);
            ivec2 at = ivec2(floor(texel));
            at.x = wrapAxis(at.x, size.x, material.wrap.x, material.region.x, material.region.y, material.blend.z != 0.0);
            at.y = wrapAxis(at.y, size.y, material.wrap.y, material.region.z, material.region.w, material.blend.w != 0.0);
            if (material.mode.x == 2u) {
                int index = int(texelFetch(image, at, 0).r * 255.0 + 0.5);
                t = texelFetch(palette, ivec2(min(index, int(material.mode.w) - 1), 0), 0);
            } else {
                t = texelFetch(image, at, 0);
            }
        } else if (region) {
            // Remaster mode with a 2x/4x upscaled atlas: stay strictly inside the tile region.
            vec2 wrapped;
            wrapped.x = wrapAxisF(texel.x, material.size.z, material.wrap.x, material.region.x, material.region.y, material.blend.z != 0.0);
            wrapped.y = wrapAxisF(texel.y, material.size.w, material.wrap.y, material.region.z, material.region.w, material.blend.w != 0.0);
            t = texture(image, wrapped / material.size.zw);
        } else {
            t = texture(image, texel / material.size.zw);
        }
        bool target = material.mode.x == 3u;
        t = remaster ? vec4(t.rgb * 255.0, t.a * (target ? 128.0 : 255.0))
                     : vec4(floor(t.rgb * 255.0 + 0.5), floor(t.a * (target ? 128.0 : 255.0) + 0.5));
        // Zero-cost procedural micro-relief (bump) on 3D painted textures using
        // hardware screen-space derivatives (0 extra texture fetches).
        if (remaster3d && material.mode.x == 1u) {
            float lum = dot(t.rgb, vec3(0.299, 0.587, 0.114)) / 255.0;
            vec2 grad = vec2(dFdx(lum), dFdy(lum));
            float relief = clamp(dot(grad, vec2(-0.55, -0.83)) * 1.8, -0.08, 0.10);
            t.rgb = clamp(t.rgb * (1.0 + relief), 0.0, 255.0);
        }
        if (material.texa.x >= 0.0)
            t.a = (material.texa.y != 0.0 && t.rgb == vec3(0.0)) ? 0.0 : material.texa.x;
        if (material.blend.x == 1.0)
            t.a = float((uint(t.g + 0.5) & 0xFCu) | (uint(t.a + 0.5) & 3u));
        bool tcc = material.mode.z != 0u;
        uint fn = material.mode.y;
        if (fn == 1u) {
            c.rgb = t.rgb;
            c.a = tcc ? t.a : f.a;
        } else {
            c.rgb = remaster ? (f.rgb * t.rgb / 128.0) : floor(f.rgb * t.rgb / 128.0);
            if (fn >= 2u)
                c.rgb += f.a;
            if (fn == 0u)
                c.a = tcc ? (remaster ? (f.a * t.a / 128.0) : floor(f.a * t.a / 128.0)) : f.a;
            else if (fn == 2u)
                c.a = tcc ? t.a + f.a : f.a;
            else
                c.a = tcc ? t.a : f.a;
        }
        c = min(c, 255.0);
    }
#ifndef DQ8_NO_DISCARD
    if (!passes(material.test.x, remaster ? floor(c.a + 0.5) : c.a, float(material.test.y)) && material.test.z != 0u)
        discard;
#endif
    if (material.test.w != 0u) {
        float k = (remaster ? clamp(inFog, 0.0, 255.0) : clamp(floor(inFog), 0.0, 255.0)) / 255.0;
        c.rgb = mix(material.fogColor.rgb * 255.0, c.rgb, k);
    }
    float outAlpha = (material.wrap.z != 0u) ? max(c.a, 128.0) : c.a;
    outColor = vec4(c.rgb / 255.0, outAlpha / 128.0);
}
