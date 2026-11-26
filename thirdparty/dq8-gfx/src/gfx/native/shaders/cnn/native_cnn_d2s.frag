#version 450
// Derived from Anime4K-v3.2-Upscale-CNN-x2-(M) (Pass 8: Depth-to-Space 2x)
// Combines the learned 2x2 sub-pixel CNN residual with a premultiplied-alpha
// Catmull-Rom base sample so transparent PS2 cutouts stay halo-free.
// MIT License

// Copyright (c) 2019-2021 bloc97
// All rights reserved.

// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:

// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.

// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

layout(set = 2, binding = 0) uniform sampler2D source;
layout(set = 2, binding = 1) uniform sampler2D convLast;
layout(set = 3, binding = 0) uniform CnnParams {
    uvec4 wrap; // x: clampU, y: clampV
} up;
layout(location = 0) out vec4 outColor;

ivec2 wrapCoord(ivec2 p, ivec2 size) {
    if (up.wrap.x != 0u)
        p.x = clamp(p.x, 0, size.x - 1);
    else
        p.x = ((p.x % size.x) + size.x) % size.x;
    if (up.wrap.y != 0u)
        p.y = clamp(p.y, 0, size.y - 1);
    else
        p.y = ((p.y % size.y) + size.y) % size.y;
    return p;
}

vec4 fetchSrc(ivec2 p, ivec2 size) {
    return texelFetch(source, wrapCoord(p, size), 0);
}

vec4 toPremul(vec4 c) {
    return vec4(c.rgb * c.a, c.a);
}

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
    ivec2 outPix = ivec2(gl_FragCoord.xy);
    ivec2 srcPix = clamp(outPix >> 1, ivec2(0), srcSize - ivec2(1));
    ivec2 sub = outPix & 1;

    // Premultiplied Catmull-Rom 2x base (prevents dark fringes on transparent edges).
    vec2 srcPos = (gl_FragCoord.xy * 0.5) - vec2(0.5);
    ivec2 base = ivec2(floor(srcPos));
    vec2 f = srcPos - vec2(base);

    vec4 rows[4];
    vec4 c00, c10, c01, c11;
    for (int j = -1; j <= 2; ++j) {
        vec4 s0 = toPremul(fetchSrc(base + ivec2(-1, j), srcSize));
        vec4 s1 = toPremul(fetchSrc(base + ivec2( 0, j), srcSize));
        vec4 s2 = toPremul(fetchSrc(base + ivec2( 1, j), srcSize));
        vec4 s3 = toPremul(fetchSrc(base + ivec2( 2, j), srcSize));
        if (j == 0) { c00 = s1; c10 = s2; }
        if (j == 1) { c01 = s1; c11 = s2; }
        rows[j + 1] = cubic4(s0, s1, s2, s3, f.x);
    }
    vec4 bicubic = cubic4(rows[0], rows[1], rows[2], rows[3], f.y);
    vec4 mn = min(min(c00, c10), min(c01, c11));
    vec4 mx = max(max(c00, c10), max(c01, c11));
    vec4 res = clamp(bicubic, mn, mx);

    if (res.a <= 1e-5) {
        outColor = vec4(0.0);
        return;
    }
    vec3 rgb = clamp(res.rgb / res.a, 0.0, 1.0);

    // Learned CNN 2x2 sub-pixel residual from conv_last.
    vec4 resVec = texelFetch(convLast, srcPix, 0);
    float residual = resVec[sub.y * 2 + sub.x];
    // Attenuate residual on near-transparent alpha borders to avoid edge halos.
    float alphaWeight = smoothstep(0.05, 0.35, res.a);
    rgb = clamp(rgb + vec3(residual * alphaWeight), 0.0, 1.0);

    outColor = vec4(rgb, clamp(res.a, 0.0, 1.0));
}
