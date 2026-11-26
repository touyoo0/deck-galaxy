#version 450

// AMD FidelityFX CAS (Contrast Adaptive Sharpening) + subtle Remaster
// vibrance/contrast polish for Steam Deck LCD/OLED displays.
layout(set = 2, binding = 0) uniform sampler2D source;
layout(set = 3, binding = 0) uniform CasParams {
    vec4 params; // x: sharpness [0..1], y: vibrance [0..0.2], z: contrast [0..0.1]
} cas;
layout(location = 0) out vec4 outColor;

void main() {
    ivec2 size = textureSize(source, 0);
    ivec2 p = ivec2(gl_FragCoord.xy);
    ivec2 hi = size - ivec2(1);

    vec4 center = texelFetch(source, clamp(p, ivec2(0), hi), 0);
    vec3 b = texelFetch(source, clamp(p + ivec2( 0, -1), ivec2(0), hi), 0).rgb;
    vec3 d = texelFetch(source, clamp(p + ivec2(-1,  0), ivec2(0), hi), 0).rgb;
    vec3 e = center.rgb;
    vec3 f = texelFetch(source, clamp(p + ivec2( 1,  0), ivec2(0), hi), 0).rgb;
    vec3 h = texelFetch(source, clamp(p + ivec2( 0,  1), ivec2(0), hi), 0).rgb;

    vec3 mnRGB = min(min(min(d, e), min(f, b)), h);
    vec3 mxRGB = max(max(max(d, e), max(f, b)), h);

    vec3 rcpMRGB = 1.0 / max(mxRGB, vec3(1e-5));
    vec3 ampRGB = clamp(min(mnRGB, 2.0 - mxRGB) * rcpMRGB, 0.0, 1.0);
    ampRGB = sqrt(ampRGB);

    float peak = mix(-0.125, -0.2, clamp(cas.params.x, 0.0, 1.0));
    vec3 wRGB = ampRGB * peak;
    vec3 rcpWeightRGB = 1.0 / (1.0 + 4.0 * wRGB);

    vec3 color = clamp(((b + d + f + h) * wRGB + e) * rcpWeightRGB, 0.0, 1.0);

    // Subtle perceptual vibrance (boosts muted tones more than already-saturated colors).
    if (cas.params.y > 0.0) {
        float luma = dot(color, vec3(0.2126, 0.7152, 0.0722));
        float maxC = max(color.r, max(color.g, color.b));
        float minC = min(color.r, min(color.g, color.b));
        float sat = maxC - minC;
        color = clamp(mix(vec3(luma), color, 1.0 + cas.params.y * (1.0 - sat)), 0.0, 1.0);
    }

    // Gentle S-curve micro-contrast without crushing blacks or clipping highlights.
    if (cas.params.z > 0.0) {
        vec3 s = color * color * (3.0 - 2.0 * color);
        color = mix(color, s, cas.params.z);
    }

    outColor = vec4(color, center.a);
}
