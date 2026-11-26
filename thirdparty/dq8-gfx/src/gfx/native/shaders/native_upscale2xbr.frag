#version 450

// 2xBR Edge-Directed 2D / UI / Icon / Font Upscaler (adapted from Hyllian's
// xBR algorithm, MIT License) with premultiplied-alpha awareness for PS2
// transparent UI sprites, item icons, portraits, menu borders and fonts.
// Runs once per texture at cache upload (0 ms/frame).
layout(set = 2, binding = 0) uniform sampler2D source;
layout(set = 3, binding = 0) uniform Upscale2dParams {
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

vec4 fetchP(ivec2 p, ivec2 size) {
    return texelFetch(source, wrapCoord(p, size), 0);
}

// Perceptual YUV + alpha distance so bitmap fonts and transparent icons
// detect edges cleanly even when RGB is uniform white/black.
float df(vec4 a, vec4 b) {
    vec3 d = abs(a.rgb * a.a - b.rgb * b.a);
    float y = dot(d, vec3(0.299, 0.587, 0.114));
    float u = dot(d, vec3(-0.14713, -0.28886, 0.436));
    float v = dot(d, vec3(0.615, -0.51499, -0.10001));
    float da = abs(a.a - b.a);
    return 48.0 * y + 7.0 * abs(u) + 6.0 * abs(v) + 48.0 * da;
}

vec4 blendPremul(vec4 a, vec4 b, float t) {
    vec4 pa = vec4(a.rgb * a.a, a.a);
    vec4 pb = vec4(b.rgb * b.a, b.a);
    vec4 pm = mix(pa, pb, t);
    if (pm.a <= 1e-5)
        return vec4(0.0);
    return vec4(clamp(pm.rgb / pm.a, 0.0, 1.0), clamp(pm.a, 0.0, 1.0));
}

// Evaluate one corner of the 2x2 sub-pixel block around center E using a
// canonical orientation where the target sub-pixel is bottom-right (toward F & H).
vec4 evalCorner(
    vec4 E,
    vec4 B, vec4 D, vec4 F, vec4 H,
    vec4 A, vec4 C, vec4 G, vec4 I,
    vec4 F4, vec4 H5, vec4 I4, vec4 I5
) {
    // Weighted edge comparison across the two competing diagonals:
    // e = diagonal through E (C-E-G), i = perpendicular diagonal (F-H).
    float e = df(E, C) + df(E, G) + df(I, H5) + df(I, F4) + 4.0 * df(H, F);
    float i = df(H, D) + df(H, I5) + df(F, I4) + df(F, B) + 4.0 * df(E, I);

    if (e >= i)
        return E;

    // Require E to differ from both F and H (avoid blurring flat gradients).
    float dEF = df(E, F);
    float dEH = df(E, H);
    if (dEF < 1.5 && dEH < 1.5)
        return E;

    // Pick whichever orthogonal neighbor (F or H) is closer to E.
    vec4 px = (dEF <= dEH) ? F : H;

    // Never bleed empty transparent atlas space (a ~= 0) into any visible PS2
    // pixel (opaque 0x80 = 0.502 or semi-transparent HUD/gradient strips), or
    // 9-slice borders and 1px stretched HUD backdrops become half-transparent.
    if (E.a >= 0.05 && px.a < 0.05)
        return E;

    // Determine edge slope (normal 45-deg vs 2:1 shallow/steep) for smooth anti-aliasing.
    float ke = df(F, G);
    float ki = df(H, C);
    bool left  = (2.0 * ke <= ki) && (df(E, G) > 1.0) && (df(D, G) > 1.0);
    bool up    = (ke >= 2.0 * ki) && (df(E, C) > 1.0) && (df(B, C) > 1.0);

    float w = 0.5;
    if (left && up)
        w = 0.75;
    else if (left || up)
        w = 0.625;
    else if ((df(E, F) + df(E, H)) > (df(E, B) + df(E, D)) * 0.5)
        w = 0.5;
    else
        w = 0.375;

    return blendPremul(E, px, w);
}

void main() {
    ivec2 sz = textureSize(source, 0);
    ivec2 outPix = ivec2(gl_FragCoord.xy);
    ivec2 p = clamp(outPix >> 1, ivec2(0), sz - ivec2(1));
    ivec2 sub = outPix & 1;

    // Fetch 5x5 neighborhood around source pixel p.
    vec4 B1 = fetchP(p + ivec2( 0, -2), sz);
    vec4 A0 = fetchP(p + ivec2(-2, -1), sz);
    vec4 A  = fetchP(p + ivec2(-1, -1), sz);
    vec4 B  = fetchP(p + ivec2( 0, -1), sz);
    vec4 C  = fetchP(p + ivec2( 1, -1), sz);
    vec4 C4 = fetchP(p + ivec2( 2, -1), sz);
    vec4 D0 = fetchP(p + ivec2(-2,  0), sz);
    vec4 D  = fetchP(p + ivec2(-1,  0), sz);
    vec4 E  = fetchP(p + ivec2( 0,  0), sz);
    vec4 F  = fetchP(p + ivec2( 1,  0), sz);
    vec4 F4 = fetchP(p + ivec2( 2,  0), sz);
    vec4 G0 = fetchP(p + ivec2(-2,  1), sz);
    vec4 G  = fetchP(p + ivec2(-1,  1), sz);
    vec4 H  = fetchP(p + ivec2( 0,  1), sz);
    vec4 I  = fetchP(p + ivec2( 1,  1), sz);
    vec4 I4 = fetchP(p + ivec2( 2,  1), sz);
    vec4 H5 = fetchP(p + ivec2( 0,  2), sz);
    vec4 I5 = fetchP(p + ivec2( 1,  2), sz);
    vec4 G5 = fetchP(p + ivec2(-1,  2), sz);
    vec4 A1 = fetchP(p + ivec2(-1, -2), sz);
    vec4 C1 = fetchP(p + ivec2( 1, -2), sz);

    // Symmetrically orient the 5x5 grid for the current 2x2 sub-pixel quadrant.
    if (sub.x == 1 && sub.y == 1) {
        // Bottom-right (canonical)
        outColor = evalCorner(E, B, D, F, H, A, C, G, I, F4, H5, I4, I5);
    } else if (sub.x == 0 && sub.y == 1) {
        // Bottom-left (mirror X)
        outColor = evalCorner(E, B, F, D, H, C, A, I, G, D0, H5, G0, G5);
    } else if (sub.x == 1 && sub.y == 0) {
        // Top-right (mirror Y)
        outColor = evalCorner(E, H, D, F, B, G, I, A, C, F4, B1, C4, C1);
    } else {
        // Top-left (mirror X and Y)
        outColor = evalCorner(E, H, F, D, B, I, G, C, A, D0, B1, A0, A1);
    }
}
