#version 450
// Derived from Anime4K-v3.2-Upscale-CNN-x2-(M) (Pass 0: Conv 4x3x3x3)
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

layout(set = 2, binding = 0) uniform sampler2D texIn;
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

vec4 fetchT(ivec2 p, ivec2 size) {
    return texelFetch(texIn, wrapCoord(p, size), 0);
}

void main() {
    ivec2 sz = textureSize(texIn, 0);
    ivec2 base = ivec2(gl_FragCoord.xy);
    vec4 result = mat4(-0.010995803, 0.077095956, -0.043992598, 0.06048717, 0.1164834, -0.11689607, 0.072985925, -0.078805886, 0.01182932, 0.054985743, -0.09018186, 0.044907484, 0.0, 0.0, 0.0, 0.0) * fetchT(base + ivec2(-1, -1), sz);
    result += mat4(0.1813623, -0.14752422, 0.025720436, -0.17639883, 0.15697388, 0.10445984, -0.1843076, 0.5264643, 0.047516696, -0.097305484, 0.09740847, -0.29619336, 0.0, 0.0, 0.0, 0.0) * fetchT(base + ivec2(-1, 0), sz);
    result += mat4(-0.014534763, 0.09486465, 0.046173926, 0.039391946, 0.09609376, -0.060574662, 0.042200956, -0.3269777, 0.051006425, 0.059818447, 0.04366627, 0.17699827, 0.0, 0.0, 0.0, 0.0) * fetchT(base + ivec2(-1, 1), sz);
    result += mat4(0.04268535, -0.08152529, 0.10577459, -0.036936995, -0.051562306, 0.054872766, 0.09194519, 0.0025066638, -0.01073954, 0.00064474024, 0.10038221, 0.02131141, 0.0, 0.0, 0.0, 0.0) * fetchT(base + ivec2(0, -1), sz);
    result += mat4(-0.51751363, -0.40028602, 0.3469574, 0.5933738, -0.91357684, -0.67692596, 0.57815677, 0.39809322, -0.16341521, -0.27169713, 0.12232366, 0.4318641, 0.0, 0.0, 0.0, 0.0) * fetchT(base + ivec2(0, 0), sz);
    result += mat4(0.12601124, -0.06263236, -0.45907676, -0.41514075, 0.3330334, -0.1929565, -0.6333532, -0.6552794, -0.045809917, 0.046351526, -0.26173338, -0.30252662, 0.0, 0.0, 0.0, 0.0) * fetchT(base + ivec2(0, 1), sz);
    result += mat4(0.0030332592, 0.012103107, 0.010537323, -0.02038607, 0.095558085, 0.097704545, 0.083433494, 0.026790185, 0.01943357, -0.061712462, -0.00015703632, -0.032268334, 0.0, 0.0, 0.0, 0.0) * fetchT(base + ivec2(1, -1), sz);
    result += mat4(0.016870102, 0.5215812, -0.11525501, 0.027527615, -0.09045733, 0.61310345, -0.1575268, 0.1905386, 0.020172214, 0.3503187, -0.08209157, -0.051328037, 0.0, 0.0, 0.0, 0.0) * fetchT(base + ivec2(1, 0), sz);
    result += mat4(0.005494087, -0.010656317, 0.07682753, -0.08116042, -0.03934524, 0.16589017, 0.101483546, -0.066603065, 0.03494657, -0.07885597, 0.074227594, 0.0016264897, 0.0, 0.0, 0.0, 0.0) * fetchT(base + ivec2(1, 1), sz);
    result += vec4(0.014463938, -0.0031906287, 0.007015422, -0.003888468);
    outColor = result;
}
