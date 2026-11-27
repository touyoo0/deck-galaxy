#version 450

// Symmetric paired half-resolution SSAO for Rogue Galaxy's reversed float depth.
// Sampling opposite pairs (+off, -off) cancels out on flat sloped floors and
// walls (zero overall darkening) and only darkens true concave corners/crevices.
layout(set = 2, binding = 0) uniform sampler2D depthTex;
layout(location = 0) out vec4 outColor;

float linearize(float d) {
    return 1.0 / max(d, 1e-4);
}

void main() {
    vec2 fullSize = vec2(textureSize(depthTex, 0));
    vec2 halfSize = max(fullSize * 0.5, vec2(1.0));
    vec2 uv = gl_FragCoord.xy / halfSize;
    vec2 texel = 1.0 / fullSize;

    float dCenter = texture(depthTex, uv).r;
    if (dCenter <= 0.0005) {
        outColor = vec4(1.0);
        return;
    }

    float zCenter = linearize(dCenter);

    ivec2 ip = ivec2(gl_FragCoord.xy);
    float angle = float(((ip.x & 3) + (ip.y & 3) * 4) * 11 % 16) * (6.2831853 / 16.0);
    float ca = cos(angle), sa = sin(angle);
    mat2 rot = mat2(ca, -sa, sa, ca);

    // 6 opposite direction pairs (12 taps total) across inner & outer rings.
    const vec2 dirs[6] = vec2[6](
        vec2( 0.35,  0.10), vec2(-0.15,  0.34), vec2( 0.24, -0.28),
        vec2( 0.80,  0.22), vec2(-0.35,  0.76), vec2( 0.56, -0.62)
    );

    float radiusPx = clamp(96.0 / pow(zCenter, 0.35), 10.0, 28.0);
    vec2 radius = texel * radiusPx;

    float occ = 0.0;
    for (int i = 0; i < 6; ++i) {
        vec2 off = (rot * dirs[i]) * radius;
        float d1 = texture(depthTex, clamp(uv + off, vec2(0.0), vec2(1.0))).r;
        float d2 = texture(depthTex, clamp(uv - off, vec2(0.0), vec2(1.0))).r;
        if (d1 <= 0.0005 || d2 <= 0.0005)
            continue;
        float z1 = linearize(d1);
        float z2 = linearize(d2);
        // On a flat sloped surface, (zCenter - z1) + (zCenter - z2) ~= 0.
        // In a concave corner or contact crease, both sides are closer -> positive sum.
        float delta1 = (zCenter - z1) / max(zCenter, 1.0);
        float delta2 = (zCenter - z2) / max(zCenter, 1.0);
        float range1 = smoothstep(0.22, 0.02, abs(delta1));
        float range2 = smoothstep(0.22, 0.02, abs(delta2));
        float pairConcave = (delta1 * range1 + delta2 * range2) * 0.5;
        occ += smoothstep(0.006, 0.045, pairConcave);
    }

    float ao = 1.0 - clamp((occ / 6.0) * 1.15, 0.0, 0.32);
    outColor = vec4(ao, ao, ao, 1.0);
}
