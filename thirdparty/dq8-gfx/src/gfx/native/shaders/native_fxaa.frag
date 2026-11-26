#version 450

// FXAA 3.11 Quality pass for the native pipeline's Remaster mode.
// Smooths 3D geometric edges, cel-shaded outlines and alpha-tested foliage
// while preserving fine UI/text detail.
layout(set = 2, binding = 0) uniform sampler2D source;
layout(location = 0) out vec4 outColor;

float luma(vec3 rgb) {
    return dot(rgb, vec3(0.299, 0.587, 0.114));
}

void main() {
    vec2 size = vec2(textureSize(source, 0));
    vec2 rcp = 1.0 / size;
    vec2 uv = gl_FragCoord.xy * rcp;

    vec4 center = texture(source, uv);
    vec3 rgbM = center.rgb;
    float lumaM = luma(rgbM);

    float lumaN = luma(texture(source, uv + vec2( 0.0, -rcp.y)).rgb);
    float lumaS = luma(texture(source, uv + vec2( 0.0,  rcp.y)).rgb);
    float lumaW = luma(texture(source, uv + vec2(-rcp.x,  0.0)).rgb);
    float lumaE = luma(texture(source, uv + vec2( rcp.x,  0.0)).rgb);

    float rangeMin = min(lumaM, min(min(lumaN, lumaS), min(lumaW, lumaE)));
    float rangeMax = max(lumaM, max(max(lumaN, lumaS), max(lumaW, lumaE)));
    float range = rangeMax - rangeMin;

    // Skip low-contrast areas (preserves flat cel-shaded surfaces and saves GPU).
    if (range < max(0.0312, rangeMax * 0.0833)) {
        outColor = center;
        return;
    }

    float lumaNW = luma(texture(source, uv + vec2(-rcp.x, -rcp.y)).rgb);
    float lumaNE = luma(texture(source, uv + vec2( rcp.x, -rcp.y)).rgb);
    float lumaSW = luma(texture(source, uv + vec2(-rcp.x,  rcp.y)).rgb);
    float lumaSE = luma(texture(source, uv + vec2( rcp.x,  rcp.y)).rgb);

    float lumaNS = lumaN + lumaS;
    float lumaWE = lumaW + lumaE;
    float lumaNWSW = lumaNW + lumaSW;
    float lumaNENE = lumaNE + lumaSE;
    float lumaNWNE = lumaNW + lumaNE;
    float lumaSWSE = lumaSW + lumaSE;

    float edgeHorz = abs(-2.0 * lumaW + lumaNWSW) + abs(-2.0 * lumaM + lumaNS) * 2.0 + abs(-2.0 * lumaE + lumaNENE);
    float edgeVert = abs(-2.0 * lumaN + lumaNWNE) + abs(-2.0 * lumaM + lumaWE) * 2.0 + abs(-2.0 * lumaS + lumaSWSE);
    bool horzSpan = edgeHorz >= edgeVert;

    float lengthSign = horzSpan ? rcp.y : rcp.x;
    float luma1 = horzSpan ? lumaN : lumaW;
    float luma2 = horzSpan ? lumaS : lumaE;
    float grad1 = abs(luma1 - lumaM);
    float grad2 = abs(luma2 - lumaM);
    bool is1Steepest = grad1 >= grad2;
    float gradientScaled = 0.25 * max(grad1, grad2);

    float lumaLocalAvg;
    if (is1Steepest) {
        lengthSign = -lengthSign;
        lumaLocalAvg = 0.5 * (luma1 + lumaM);
    } else {
        lumaLocalAvg = 0.5 * (luma2 + lumaM);
    }

    vec2 currentUv = uv;
    if (horzSpan)
        currentUv.y += lengthSign * 0.5;
    else
        currentUv.x += lengthSign * 0.5;

    vec2 offset = horzSpan ? vec2(rcp.x, 0.0) : vec2(0.0, rcp.y);
    vec2 uv1 = currentUv - offset;
    vec2 uv2 = currentUv + offset;

    float lumaEnd1 = luma(texture(source, uv1).rgb) - lumaLocalAvg;
    float lumaEnd2 = luma(texture(source, uv2).rgb) - lumaLocalAvg;
    bool reached1 = abs(lumaEnd1) >= gradientScaled;
    bool reached2 = abs(lumaEnd2) >= gradientScaled;
    bool reachedBoth = reached1 && reached2;

    if (!reached1) uv1 -= offset;
    if (!reached2) uv2 += offset;

    const float steps[6] = float[6](1.0, 1.5, 2.0, 2.0, 4.0, 8.0);
    if (!reachedBoth) {
        for (int i = 0; i < 6; ++i) {
            if (!reached1)
                lumaEnd1 = luma(texture(source, uv1).rgb) - lumaLocalAvg;
            if (!reached2)
                lumaEnd2 = luma(texture(source, uv2).rgb) - lumaLocalAvg;
            reached1 = abs(lumaEnd1) >= gradientScaled;
            reached2 = abs(lumaEnd2) >= gradientScaled;
            reachedBoth = reached1 && reached2;
            if (reachedBoth)
                break;
            if (!reached1) uv1 -= offset * steps[i];
            if (!reached2) uv2 += offset * steps[i];
        }
    }

    float dist1 = horzSpan ? (uv.x - uv1.x) : (uv.y - uv1.y);
    float dist2 = horzSpan ? (uv2.x - uv.x) : (uv2.y - uv.y);
    bool dir1 = dist1 < dist2;
    float distFinal = min(dist1, dist2);
    float edgeLength = dist1 + dist2;
    float pixelOffset = -distFinal / edgeLength + 0.5;

    bool isLumaMSmaller = lumaM < lumaLocalAvg;
    bool correctVariation = ((dir1 ? lumaEnd1 : lumaEnd2) < 0.0) != isLumaMSmaller;
    float finalOffset = correctVariation ? pixelOffset : 0.0;

    // Gentle subpixel aliasing removal (0.50 cap keeps cel-shading & UI crisp).
    float lumaAvg = (1.0 / 12.0) * (2.0 * (lumaNS + lumaWE) + lumaNWSW + lumaNENE);
    float subpixel1 = clamp(abs(lumaAvg - lumaM) / range, 0.0, 1.0);
    float subpixel2 = (-2.0 * subpixel1 + 3.0) * subpixel1 * subpixel1;
    float subpixelOffset = subpixel2 * subpixel2 * 0.50;

    finalOffset = max(finalOffset, subpixelOffset);

    vec2 finalUv = uv;
    if (horzSpan)
        finalUv.y += finalOffset * lengthSign;
    else
        finalUv.x += finalOffset * lengthSign;

    outColor = vec4(texture(source, finalUv).rgb, center.a);
}
