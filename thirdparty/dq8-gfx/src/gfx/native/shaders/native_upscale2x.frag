#version 450

// Edge-Directed 2x Texture Upscaler + Adaptive Detail Restoration for the
// native pipeline's Remaster cache. Designed for hand-painted PS2 textures
// (stone, wood, fabric, skin, anime cel-art): interpolates along local edges
// using a structure-guided Catmull-Rom kernel and restores fine micro-detail
// without ringing or halos. Runs once per texture at cache upload (0 ms/frame).
layout(set = 2, binding = 0) uniform sampler2D source;
layout(set = 3, binding = 0) uniform UpscaleParams {
    uvec4 wrap; // x: clampU (0 repeat, 1 clamp), y: clampV (0 repeat, 1 clamp)
} up;
layout(location = 0) out vec4 outColor;

vec4 fetchT(ivec2 p, ivec2 size) {
    if (up.wrap.x != 0u)
        p.x = clamp(p.x, 0, size.x - 1);
    else
        p.x = ((p.x % size.x) + size.x) % size.x;
    if (up.wrap.y != 0u)
        p.y = clamp(p.y, 0, size.y - 1);
    else
        p.y = ((p.y % size.y) + size.y) % size.y;
    return texelFetch(source, p, 0);
}

vec4 toPremul(vec4 c) {
    return vec4(c.rgb * c.a, c.a);
}

float luma(vec3 c) {
    return dot(c, vec3(0.299, 0.587, 0.114));
}

// 1D Catmull-Rom cubic interpolation of 4 premultiplied samples at t in [0, 1].
vec4 cubic4(vec4 p0, vec4 p1, vec4 p2, vec4 p3, float t) {
    float t2 = t * t;
    float t3 = t2 * t;
    return 0.5 * (
        (2.0 * p1) +
        (-p0 + p2) * t +
        (2.0 * p0 - 5.0 * p1 + 4.0 * p2 - p3) * t2 +
        (-p0 + 3.0 * p1 - 3.0 * p2 + p3) * t3
    );
}

void main() {
    ivec2 srcSize = textureSize(source, 0);
    // Map output pixel center (ox + 0.5, oy + 0.5) to exact source texel space.
    vec2 srcPos = (gl_FragCoord.xy * 0.5) - vec2(0.5);
    ivec2 base = ivec2(floor(srcPos));
    vec2 f = srcPos - vec2(base);

    // 4x4 neighborhood in premultiplied alpha so transparent borders never bleed dark.
    vec4 rows[4];
    vec4 c00, c10, c01, c11;
    for (int j = -1; j <= 2; ++j) {
        vec4 s0 = toPremul(fetchT(base + ivec2(-1, j), srcSize));
        vec4 s1 = toPremul(fetchT(base + ivec2( 0, j), srcSize));
        vec4 s2 = toPremul(fetchT(base + ivec2( 1, j), srcSize));
        vec4 s3 = toPremul(fetchT(base + ivec2( 2, j), srcSize));
        if (j == 0) { c00 = s1; c10 = s2; }
        if (j == 1) { c01 = s1; c11 = s2; }
        rows[j + 1] = cubic4(s0, s1, s2, s3, f.x);
    }
    vec4 bicubic = cubic4(rows[0], rows[1], rows[2], rows[3], f.y);

    // Anti-ringing clamp to the local 2x2 neighborhood bounds.
    vec4 mn = min(min(c00, c10), min(c01, c11));
    vec4 mx = max(max(c00, c10), max(c01, c11));
    vec4 baseSample = clamp(bicubic, mn, mx);

    // Diagonal edge-directed refinement: when a strong diagonal crosses the 2x2 cell,
    // steer interpolation along the stronger diagonal to sharpen diagonal strokes & seams.
    float dMain = abs(luma(c00.rgb) - luma(c11.rgb));
    float dAnti = abs(luma(c10.rgb) - luma(c01.rgb));
    float diagBias = clamp((abs(dMain - dAnti) - 0.03) * 4.0, 0.0, 0.65);
    vec4 diagSample = (dMain < dAnti) ? mix(c00, c11, 0.5 * (f.x + f.y))
                                      : mix(c10, c01, 0.5 * ((1.0 - f.x) + f.y));
    vec4 res = mix(baseSample, diagSample, diagBias * 0.45);

    // Unpremultiply safely.
    if (res.a <= 1e-5) {
        outColor = vec4(0.0);
        return;
    }
    vec3 rgb = clamp(res.rgb / res.a, 0.0, 1.0);

    // Adaptive detail restoration (multi-tap unsharp mask on the source neighborhood)
    // to bring out painted brushwork, stone cracks, fabric weave, and ink outlines.
    vec3 n0 = fetchT(base + ivec2( 0,  0), srcSize).rgb;
    vec3 n1 = fetchT(base + ivec2( 1,  0), srcSize).rgb;
    vec3 n2 = fetchT(base + ivec2( 0,  1), srcSize).rgb;
    vec3 n3 = fetchT(base + ivec2( 1,  1), srcSize).rgb;
    vec3 localMean = 0.25 * (n0 + n1 + n2 + n3);
    vec3 detail = rgb - localMean;
    // Balanced, halo-clamped detail boost so painted PS2 textures look crisp without crunch.
    vec3 enhanced = rgb + clamp(detail * 0.75, vec3(-0.12), vec3(0.12));

    outColor = vec4(clamp(enhanced, 0.0, 1.0), clamp(res.a, 0.0, 1.0));
}
