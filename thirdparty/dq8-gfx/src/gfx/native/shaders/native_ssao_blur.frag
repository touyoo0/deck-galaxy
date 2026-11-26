#version 450

// Depth-aware bilateral blur for half-res SSAO, also used on the final vertical
// pass to multiply AO cleanly onto m_scene (via multiplicative blend state).
layout(set = 2, binding = 0) uniform sampler2D aoTex;
layout(set = 2, binding = 1) uniform sampler2D depthTex;
layout(set = 3, binding = 0) uniform BlurParams {
    vec4 dir; // xy: step in UV (horizontal or vertical)
} blur;
layout(location = 0) out vec4 outColor;

void main() {
    vec2 size = vec2(textureSize(aoTex, 0));
    // Note: on the vertical composite pass targeting full-res m_scene, gl_FragCoord
    // is in full-res pixels; blur.dir.zw holds the target reciprocal size!
    vec2 uv = gl_FragCoord.xy * blur.dir.zw;

    float dCenter = texture(depthTex, uv).r;
    if (dCenter <= 0.0005) {
        outColor = vec4(1.0);
        return;
    }

    const float weights[3] = float[3](0.40, 0.24, 0.06);
    float sum = texture(aoTex, uv).r * weights[0];
    float wSum = weights[0];

    for (int i = 1; i <= 2; ++i) {
        vec2 off = blur.dir.xy * float(i);
        vec2 uv1 = clamp(uv + off, vec2(0.0), vec2(1.0));
        vec2 uv2 = clamp(uv - off, vec2(0.0), vec2(1.0));

        float d1 = texture(depthTex, uv1).r;
        float d2 = texture(depthTex, uv2).r;

        float w1 = weights[i] * exp(-abs(d1 - dCenter) * 400.0);
        float w2 = weights[i] * exp(-abs(d2 - dCenter) * 400.0);

        sum += texture(aoTex, uv1).r * w1 + texture(aoTex, uv2).r * w2;
        wSum += w1 + w2;
    }

    float ao = clamp(sum / max(wSum, 1e-4), 0.52, 1.0);
    outColor = vec4(ao, ao, ao, 1.0);
}
