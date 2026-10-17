#version 300 es
#define SHADER_TYPE RawShaderMaterial
#define SHADER_NAME 
precision highp float;
precision highp int;
precision highp sampler2D;
precision highp usampler2D;
precision highp isampler2D;
precision highp sampler2DArray;
precision highp usampler2DArray;
precision highp isampler2DArray;
precision highp sampler3D;
precision highp usampler3D;
precision highp isampler3D;

const float LN_SCALE_MIN = -12.0;
const float LN_SCALE_MAX = 9.0;

const uint SPLAT_TEX_WIDTH_BITS = 11u;
const uint SPLAT_TEX_HEIGHT_BITS = 11u;
const uint SPLAT_TEX_DEPTH_BITS = 11u;
const uint SPLAT_TEX_LAYER_BITS = SPLAT_TEX_WIDTH_BITS + SPLAT_TEX_HEIGHT_BITS;

const uint SPLAT_TEX_WIDTH = 1u << SPLAT_TEX_WIDTH_BITS;
const uint SPLAT_TEX_HEIGHT = 1u << SPLAT_TEX_HEIGHT_BITS;
const uint SPLAT_TEX_DEPTH = 1u << SPLAT_TEX_DEPTH_BITS;

const uint SPLAT_TEX_WIDTH_MASK = SPLAT_TEX_WIDTH - 1u;
const uint SPLAT_TEX_HEIGHT_MASK = SPLAT_TEX_HEIGHT - 1u;
const uint SPLAT_TEX_DEPTH_MASK = SPLAT_TEX_DEPTH - 1u;

const uint F16_INF = 0x7c00u;
const float PI = 3.1415926535897932384626433832795;

const float INFINITY = 1.0 / 0.0;
const float NEG_INFINITY = -INFINITY;

float sqr(float x) {
    return x * x;
}

float pow4(float x) {
    float x2 = x * x;
    return x2 * x2;
}

float pow8(float x) {
    float x4 = pow4(x);
    return x4 * x4;
}

vec3 srgbToLinear(vec3 rgb) {
    return pow(rgb, vec3(2.2));
}

vec3 linearToSrgb(vec3 rgb) {
    return pow(rgb, vec3(1.0 / 2.2));
}

uint encodeQuatOctXy88R8(vec4 q) {
    
    if (q.w < 0.0) {
        q = -q;
    }
    
    float theta = 2.0 * acos(q.w);
    float halfTheta = theta * 0.5;
    float s = sin(halfTheta);
    
    vec3 axis = (abs(s) < 1e-6) ? vec3(1.0, 0.0, 0.0) : q.xyz / s;
    
    
    
    float sum = abs(axis.x) + abs(axis.y) + abs(axis.z);
    vec2 p = vec2(axis.x, axis.y) / sum;
    
    if (axis.z < 0.0) {
        float oldPx = p.x;
        p.x = (1.0 - abs(p.y)) * (p.x >= 0.0 ? 1.0 : -1.0);
        p.y = (1.0 - abs(oldPx)) * (p.y >= 0.0 ? 1.0 : -1.0);
    }
    
    float u_f = p.x * 0.5 + 0.5;
    float v_f = p.y * 0.5 + 0.5;
    
    uint quantU = uint(clamp(round(u_f * 255.0), 0.0, 255.0));
    uint quantV = uint(clamp(round(v_f * 255.0), 0.0, 255.0));
    
    
    
    uint angleInt = uint(clamp(round((theta / 3.14159265359) * 255.0), 0.0, 255.0));
    
    
    return (angleInt << 16u) | (quantV << 8u) | quantU;
}

vec4 decodeQuatOctXy88R8(uint encoded) {
    
    uint quantU = encoded & uint(0xFFu);               
    uint quantV = (encoded >> 8u) & uint(0xFFu);         
    uint angleInt = encoded >> 16u;                      

    
    float u_f = float(quantU) / 255.0;
    float v_f = float(quantV) / 255.0;
    vec2 f = vec2(u_f * 2.0 - 1.0, v_f * 2.0 - 1.0);

    vec3 axis = vec3(f.xy, 1.0 - abs(f.x) - abs(f.y));
    float t = max(-axis.z, 0.0);
    axis.x += (axis.x >= 0.0) ? -t : t;
    axis.y += (axis.y >= 0.0) ? -t : t;
    axis = normalize(axis);
    
    
    float theta = (float(angleInt) / 255.0) * 3.14159265359;
    float halfTheta = theta * 0.5;
    float s = sin(halfTheta);
    float w = cos(halfTheta);
    
    return vec4(axis * s, w);
}

uint encodeQuatOctXy1010R12(vec4 q) {
    
    if (q.w < 0.0) {
        q = -q;
    }
    
    float halfTheta = acos(q.w);
    float theta = 2.0 * halfTheta;
    float s = sin(halfTheta);
    
    vec3 axis = (abs(s) < 1e-6) ? vec3(1.0, 0.0, 0.0) : q.xyz / s;
    
    
    
    float sum = abs(axis.x) + abs(axis.y) + abs(axis.z);
    vec2 p = vec2(axis.x, axis.y) / sum;
    
    if (axis.z < 0.0) {
        float oldPx = p.x;
        p.x = (1.0 - abs(p.y)) * (p.x >= 0.0 ? 1.0 : -1.0);
        p.y = (1.0 - abs(oldPx)) * (p.y >= 0.0 ? 1.0 : -1.0);
    }
    
    float u_f = p.x * 0.5 + 0.5;
    float v_f = p.y * 0.5 + 0.5;
    
    uint quantU = uint(clamp(round(u_f * 1023.0), 0.0, 1023.0));
    uint quantV = uint(clamp(round(v_f * 1023.0), 0.0, 1023.0));
    
    
    
    uint angleInt = uint(clamp(round((theta / PI) * 4095.0), 0.0, 4095.0));
    
    
    return (angleInt << 20u) | (quantV << 10u) | quantU;
}

vec4 decodeQuatOctXy1010R12(uint encoded) {
    
    uint quantU = encoded & uint(0x3FFu);               
    uint quantV = (encoded >> 10u) & uint(0x3FFu);         
    uint angleInt = encoded >> 20u;                      

    
    float u_f = float(quantU) / 1023.0;
    float v_f = float(quantV) / 1023.0;
    vec2 f = vec2(u_f * 2.0 - 1.0, v_f * 2.0 - 1.0);

    vec3 axis = vec3(f.xy, 1.0 - abs(f.x) - abs(f.y));
    float t = max(-axis.z, 0.0);
    axis.x += (axis.x >= 0.0) ? -t : t;
    axis.y += (axis.y >= 0.0) ? -t : t;
    axis = normalize(axis);
    
    
    float theta = (float(angleInt) / 4095.0) * PI;
    float halfTheta = theta * 0.5;
    float s = sin(halfTheta);
    float w = cos(halfTheta);
    
    return vec4(axis * s, w);
}

uvec4 packSplatEncoding(
    vec3 center, vec3 scales, vec4 quaternion, vec4 rgba, vec4 rgbMinMaxLnScaleMinMax
) {
    float rgbMin = rgbMinMaxLnScaleMinMax.x;
    float rgbMax = rgbMinMaxLnScaleMinMax.y;
    vec3 encRgb = (rgba.rgb - vec3(rgbMin)) / (rgbMax - rgbMin);
    uvec4 uRgba = uvec4(round(clamp(vec4(encRgb, rgba.a) * 255.0, 0.0, 255.0)));

    uint uQuat = encodeQuatOctXy88R8(quaternion);
    
    
    uvec3 uQuat3 = uvec3(uQuat & 0xffu, (uQuat >> 8u) & 0xffu, (uQuat >> 16u) & 0xffu);

    
    float lnScaleMin = rgbMinMaxLnScaleMinMax.z;
    float lnScaleMax = rgbMinMaxLnScaleMinMax.w;
    float lnScaleScale = 254.0 / (lnScaleMax - lnScaleMin);
    uvec3 uScales = uvec3(
        (scales.x == 0.0) ? 0u : uint(round(clamp((log(scales.x) - lnScaleMin) * lnScaleScale, 0.0, 254.0))) + 1u,
        (scales.y == 0.0) ? 0u : uint(round(clamp((log(scales.y) - lnScaleMin) * lnScaleScale, 0.0, 254.0))) + 1u,
        (scales.z == 0.0) ? 0u : uint(round(clamp((log(scales.z) - lnScaleMin) * lnScaleScale, 0.0, 254.0))) + 1u
    );

    
    uint word0 = uRgba.r | (uRgba.g << 8u) | (uRgba.b << 16u) | (uRgba.a << 24u);
    uint word1 = packHalf2x16(center.xy);
    uint word2 = packHalf2x16(vec2(center.z, 0.0)) | (uQuat3.x << 16u) | (uQuat3.y << 24u);
    uint word3 = uScales.x | (uScales.y << 8u) | (uScales.z << 16u) | (uQuat3.z << 24u);
    return uvec4(word0, word1, word2, word3);
}

uvec4 packSplat(vec3 center, vec3 scales, vec4 quaternion, vec4 rgba) {
    return packSplatEncoding(center, scales, quaternion, rgba, vec4(0.0, 1.0, LN_SCALE_MIN, LN_SCALE_MAX));
}

void unpackSplatEncoding(uvec4 packedData, out vec3 center, out vec3 scales, out vec4 quaternion, out vec4 rgba, vec4 rgbMinMaxLnScaleMinMax) {
    uint word0 = packedData.x, word1 = packedData.y, word2 = packedData.z, word3 = packedData.w;

    uvec4 uRgba = uvec4(word0 & 0xffu, (word0 >> 8u) & 0xffu, (word0 >> 16u) & 0xffu, (word0 >> 24u) & 0xffu);
    float rgbMin = rgbMinMaxLnScaleMinMax.x;
    float rgbMax = rgbMinMaxLnScaleMinMax.y;
    rgba = (vec4(uRgba) / 255.0);
    rgba.rgb = rgba.rgb * (rgbMax - rgbMin) + rgbMin;

    center = vec4(
        unpackHalf2x16(word1),
        unpackHalf2x16(word2 & 0xffffu)
    ).xyz;

    uvec3 uScales = uvec3(word3 & 0xffu, (word3 >> 8u) & 0xffu, (word3 >> 16u) & 0xffu);
    float lnScaleMin = rgbMinMaxLnScaleMinMax.z;
    float lnScaleMax = rgbMinMaxLnScaleMinMax.w;
    float lnScaleScale = (lnScaleMax - lnScaleMin) / 254.0;
    scales = vec3(
        (uScales.x == 0u) ? 0.0 : exp(lnScaleMin + float(uScales.x - 1u) * lnScaleScale),
        (uScales.y == 0u) ? 0.0 : exp(lnScaleMin + float(uScales.y - 1u) * lnScaleScale),
        (uScales.z == 0u) ? 0.0 : exp(lnScaleMin + float(uScales.z - 1u) * lnScaleScale)
    );

    uint uQuat = ((word2 >> 16u) & 0xFFFFu) | ((word3 >> 8u) & 0xFF0000u);
    quaternion = decodeQuatOctXy88R8(uQuat);
    
    
}

void unpackSplat(uvec4 packedData, out vec3 center, out vec3 scales, out vec4 quaternion, out vec4 rgba) {
    unpackSplatEncoding(packedData, center, scales, quaternion, rgba, vec4(0.0, 1.0, LN_SCALE_MIN, LN_SCALE_MAX));
}

uvec4 packSplatCovEncoding(
    vec3 center, vec4 rgba, vec3 xxyyzz, vec3 xyxzyz, vec4 rgbMinMaxLnScaleMinMax
) {
    float rgbMin = rgbMinMaxLnScaleMinMax.x;
    float rgbMax = rgbMinMaxLnScaleMinMax.y;
    vec3 encRgb = (rgba.rgb - vec3(rgbMin)) / (rgbMax - rgbMin);
    uvec4 uRgba = uvec4(round(clamp(vec4(encRgb, rgba.a) * 255.0, 0.0, 255.0)));

    float lnScaleMin = rgbMinMaxLnScaleMinMax.z;
    float lnScaleMax = rgbMinMaxLnScaleMinMax.w;
    float diagScale = 255.0 / (2.0 * (lnScaleMax - lnScaleMin));
    uvec3 uXxyyzz = uvec3(round(clamp((log(xxyyzz) - 2.0 * lnScaleMin) * diagScale, 0.0, 255.0)));

    vec3 xyxzyzCor = vec3(
        clamp(xyxzyz.x / sqrt(xxyyzz.x * xxyyzz.y), -1.0, 1.0),
        clamp(xyxzyz.y / sqrt(xxyyzz.x * xxyyzz.z), -1.0, 1.0),
        clamp(xyxzyz.z / sqrt(xxyyzz.y * xxyyzz.z), -1.0, 1.0)
    );
    ivec3 iXyxzyzCor = ivec3(round(xyxzyzCor * 127.0));

    
    uint word0 = uRgba.r | (uRgba.g << 8u) | (uRgba.b << 16u) | (uRgba.a << 24u);
    uint word1 = packHalf2x16(center.xy);
    uint word2 = packHalf2x16(vec2(center.z, 0.0)) |
        ((uint(iXyxzyzCor.y) & 0xffu) << 16u) |
        ((uint(iXyxzyzCor.z) & 0xffu) << 24u);
    uint word3 =
        uXxyyzz.x | (uXxyyzz.y << 8u) | (uXxyyzz.z << 16u) |
        ((uint(iXyxzyzCor.x) & 0xffu) << 24u);
    return uvec4(word0, word1, word2, word3);
}

void unpackSplatCovEncoding(uvec4 packedData, out vec3 center, out vec4 rgba, out vec3 xxyyzz, out vec3 xyxzyz, vec4 rgbMinMaxLnScaleMinMax) {
    uint word0 = packedData.x, word1 = packedData.y, word2 = packedData.z, word3 = packedData.w;

    uvec4 uRgba = uvec4(word0 & 0xffu, (word0 >> 8u) & 0xffu, (word0 >> 16u) & 0xffu, (word0 >> 24u) & 0xffu);
    float rgbMin = rgbMinMaxLnScaleMinMax.x;
    float rgbMax = rgbMinMaxLnScaleMinMax.y;
    rgba = (vec4(uRgba) / 255.0);
    rgba.rgb = rgba.rgb * (rgbMax - rgbMin) + rgbMin;

    center = vec3(
        unpackHalf2x16(word1),
        unpackHalf2x16(word2 & 0xffffu).x
    );

    uvec3 uXxyyzz = uvec3(word3 & 0xffu, (word3 >> 8u) & 0xffu, (word3 >> 16u) & 0xffu);
    ivec3 iXyxzyzCor = ivec3(int(word3) >> 24, int(word2 << 8u) >> 24, int(word2) >> 24);

    float lnScaleMin = rgbMinMaxLnScaleMinMax.z;
    float lnScaleMax = rgbMinMaxLnScaleMinMax.w;
    float diagScale = 2.0 * (lnScaleMax - lnScaleMin) / 255.0;
    xxyyzz = exp(2.0 * lnScaleMin + vec3(uXxyyzz) * diagScale);

    vec3 xyxzyzCor = vec3(iXyxzyzCor) / 127.0;
    xyxzyz = xyxzyzCor * vec3(
        sqrt(xxyyzz.x * xxyyzz.y),
        sqrt(xxyyzz.x * xxyyzz.z),
        sqrt(xxyyzz.y * xxyyzz.z)
    );
}

void packSplatExtCov(
    out uvec4 packedData, out uvec4 packedData2,
    vec3 center, vec4 rgba, vec3 xxyyzz, vec3 xyxzyz
) {
    packedData.x = floatBitsToUint(center.x);
    packedData.y = floatBitsToUint(center.y);
    packedData.z = floatBitsToUint(center.z);
    packedData.w = packHalf2x16(vec2(rgba.a, rgba.b));
    packedData2.x = packHalf2x16(rgba.rg);

    vec3 xyxzyzCor = vec3(
        clamp(xyxzyz.x / sqrt(xxyyzz.x * xxyyzz.y), -1.0, 1.0),
        clamp(xyxzyz.y / sqrt(xxyyzz.x * xxyyzz.z), -1.0, 1.0),
        clamp(xyxzyz.z / sqrt(xxyyzz.y * xxyyzz.z), -1.0, 1.0)
    );
    xyxzyzCor = sign(xyxzyzCor) * clamp(log(abs(xyxzyzCor)), -100.0, -0.0000001);
    xxyyzz = log(xxyyzz);

    packedData2.y = packHalf2x16(vec2(xxyyzz.x, xxyyzz.y));
    packedData2.z = packHalf2x16(vec2(xxyyzz.z, xyxzyzCor.x));
    packedData2.w = packHalf2x16(vec2(xyxzyzCor.y, xyxzyzCor.z));
}

void unpackSplatExtCov(
    uvec4 packedData, uvec4 packedData2,
    out vec3 center, out vec4 rgba, out vec3 xxyyzz, out vec3 xyxzyz
) {
    center.x = uintBitsToFloat(packedData.x);
    center.y = uintBitsToFloat(packedData.y);
    center.z = uintBitsToFloat(packedData.z);

    vec2 ab = unpackHalf2x16(packedData.w);
    vec2 rg = unpackHalf2x16(packedData2.x);
    rgba = vec4(rg, ab.y, ab.x);

    vec2 xxyy = unpackHalf2x16(packedData2.y);
    vec2 zzxy = unpackHalf2x16(packedData2.z);
    vec2 xzyz = unpackHalf2x16(packedData2.w);
    xxyyzz = exp(vec3(xxyy.x, xxyy.y, zzxy.x));
    xyxzyz = vec3(zzxy.y, xzyz.x, xzyz.y);
    xyxzyz = -sign(xyxzyz) * exp(-abs(xyxzyz));
    xyxzyz *= vec3(
        sqrt(xxyyzz.x * xxyyzz.y),
        sqrt(xxyyzz.x * xxyyzz.z),
        sqrt(xxyyzz.y * xxyyzz.z)
    );
}

void packSplatExt(
    out uvec4 packedData, out uvec4 packedData2,
    vec3 center, vec3 scales, vec4 quaternion, vec4 rgba
) {
    packedData.x = floatBitsToUint(center.x);
    packedData.y = floatBitsToUint(center.y);
    packedData.z = floatBitsToUint(center.z);
    packedData.w = packHalf2x16(vec2(rgba.a, 0.0));

    packedData2.x = packHalf2x16(rgba.rg);
    packedData2.y = packHalf2x16(vec2(rgba.b, log(scales.x)));
    packedData2.z = packHalf2x16(log(scales.yz));
    packedData2.w = encodeQuatOctXy1010R12(quaternion);
}

vec4 unpackSplatExtCenterAlpha(uvec4 packedData) {
    return vec4(
        uintBitsToFloat(packedData.x),
        uintBitsToFloat(packedData.y),
        uintBitsToFloat(packedData.z),
        unpackHalf2x16(packedData.w).x
    );
}

float unpackSplatExtAlpha(uvec4 packedData) {
    return unpackHalf2x16(packedData.w).x;
}

void unpackSplatExt(
    uvec4 packedData, uvec4 packedData2,
    out vec3 center, out vec3 scales, out vec4 quaternion, out vec4 rgba
) {
    center.x = uintBitsToFloat(packedData.x);
    center.y = uintBitsToFloat(packedData.y);
    center.z = uintBitsToFloat(packedData.z);
    rgba.a = unpackHalf2x16(packedData.w).x;

    rgba.rg = unpackHalf2x16(packedData2.x);
    vec2 split = unpackHalf2x16(packedData2.y);
    rgba.b = split.x;
    scales.x = exp(split.y);
    scales.yz = exp(unpackHalf2x16(packedData2.z));
    quaternion = decodeQuatOctXy1010R12(packedData2.w);
}

uint encodeExtRgb(vec3 rgb) {
    vec3 absRgb = abs(rgb);
    float maxAbs = max(absRgb.r, max(absRgb.g, absRgb.b));

    int base = clamp(int(floor(log2(maxAbs))) + 15, 0, 31);
    float divisor = exp2(float(base - 15)) / 255.0;

    uvec3 uRgb = uvec3(round(clamp(absRgb / divisor, 0.0, 255.0)));
    uint expSigns = (uint(base) << 3u) | ((rgb.r < 0.0 ? 0x1u : 0u) | (rgb.g < 0.0 ? 0x2u : 0u) | (rgb.b < 0.0 ? 0x4u : 0u));
    return uRgb.r | (uRgb.g << 8u) | (uRgb.b << 16u) | (expSigns << 24u);
}

vec3 decodeExtRgb(uint encoded) {
    uint biasedBase = (encoded >> 27u) & 0x1fu;
    float divisor = exp2(float(int(biasedBase) - 15)) / 255.0;

    vec3 rgb = vec3(uvec3(encoded & 0xffu, (encoded >> 8u) & 0xffu, (encoded >> 16u) & 0xffu));
    rgb *= divisor;

    return vec3(
        ((encoded & 0x1000000u) != 0u) ? -rgb.r : rgb.r,
        ((encoded & 0x2000000u) != 0u) ? -rgb.g : rgb.g,
        ((encoded & 0x4000000u) != 0u) ? -rgb.b : rgb.b
    );
}

vec3 quatVec(vec4 q, vec3 v) {
    
    vec3 t = 2.0 * cross(q.xyz, v);
    return v + q.w * t + cross(q.xyz, t);
}

vec4 quatQuat(vec4 q1, vec4 q2) {
    return vec4(
        q1.w * q2.x + q1.x * q2.w + q1.y * q2.z - q1.z * q2.y,
        q1.w * q2.y - q1.x * q2.z + q1.y * q2.w + q1.z * q2.x,
        q1.w * q2.z + q1.x * q2.y - q1.y * q2.x + q1.z * q2.w,
        q1.w * q2.w - q1.x * q2.x - q1.y * q2.y - q1.z * q2.z
    );
}

mat3 quaternionToMatrix(vec4 q) {
    return mat3(
        (1.0 - 2.0 * (q.y * q.y + q.z * q.z)),
        (2.0 * (q.x * q.y + q.w * q.z)),
        (2.0 * (q.x * q.z - q.w * q.y)),
        (2.0 * (q.x * q.y - q.w * q.z)),
        (1.0 - 2.0 * (q.x * q.x + q.z * q.z)),
        (2.0 * (q.y * q.z + q.w * q.x)),
        (2.0 * (q.x * q.z + q.w * q.y)),
        (2.0 * (q.y * q.z - q.w * q.x)),
        (1.0 - 2.0 * (q.x * q.x + q.y * q.y))
    );
}

mat3 scaleQuaternionToMatrix(vec3 s, vec4 q) {
    
    return mat3(
        s.x * (1.0 - 2.0 * (q.y * q.y + q.z * q.z)),
        s.x * (2.0 * (q.x * q.y + q.w * q.z)),
        s.x * (2.0 * (q.x * q.z - q.w * q.y)),
        s.y * (2.0 * (q.x * q.y - q.w * q.z)),
        s.y * (1.0 - 2.0 * (q.x * q.x + q.z * q.z)),
        s.y * (2.0 * (q.y * q.z + q.w * q.x)),
        s.z * (2.0 * (q.x * q.z + q.w * q.y)),
        s.z * (2.0 * (q.y * q.z - q.w * q.x)),
        s.z * (1.0 - 2.0 * (q.x * q.x + q.y * q.y))
    );
}

vec4 slerp(vec4 q1, vec4 q2, float t) {
    
    float cosHalfTheta = dot(q1, q2);

    
    if (abs(cosHalfTheta) >= 0.999) {
        return q1;
    }
    
    
    
    if (cosHalfTheta < 0.0) {
        q2 = -q2;
        cosHalfTheta = -cosHalfTheta;
    }

    
    float halfTheta = acos(cosHalfTheta);
    float sinHalfTheta = sqrt(1.0 - cosHalfTheta * cosHalfTheta);

    
    float ratioA = sin((1.0 - t) * halfTheta) / sinHalfTheta;
    float ratioB = sin(t * halfTheta) / sinHalfTheta;

    
    return q1 * ratioA + q2 * ratioB;
}

ivec3 splatTexCoord(int index) {
    uint x = uint(index) & SPLAT_TEX_WIDTH_MASK;
    uint y = (uint(index) >> SPLAT_TEX_WIDTH_BITS) & SPLAT_TEX_HEIGHT_MASK;
    uint z = uint(index) >> SPLAT_TEX_LAYER_BITS;
    return ivec3(x, y, z);
}

ivec3 pagedSplatTexCoord(int index) {
    return ivec3(index & 255, (index >> 8) & 255, index >> 16);
}

vec4 uintToVec4(uint u32) {
    uvec4 bytes = uvec4(
        u32 & 0xFFu,
        (u32 >> 8u) & 0xFFu,
        (u32 >> 16u) & 0xFFu,
        (u32 >> 24u) & 0xFFu
    );
    return vec4(bytes) / 255.0;
}

vec4 floatToVec4(float f) {
    uint u32 = floatBitsToUint(f);
    return uintToVec4(u32);
}

vec3 debugColorHue(uint i) {
    
    float hue = fract(float(i) * 0.61803398875);
    
    vec3 rgb = clamp(abs(mod(hue*6.0 + vec3(0.0,4.0,2.0), 6.0) - 3.0) - 1.0, 0.0, 1.0);
    return mix(vec3(1.0), rgb, 0.85); 
}

uniform uint targetLayer;
uniform int targetBase;
uniform int targetCount;

layout(location = 0) out uvec4 target;
layout(location = 1) out vec4 target3;

struct PackedSplats {
  usampler2DArray textureArray;
  int numSplats;
  vec4 rgbMinMaxLnScaleMinMax;
  bool lodOpacity;
};

uniform PackedSplats packedSplats_14;

uniform usampler2D lodIndices_16;

uniform int value_17;

uniform bool value_18;

uniform int value_19;

struct Gsplat {
  vec3 center;
  uint flags;
  vec3 scales;
  int index;
  vec4 quaternion;
  vec4 rgba;
};
const uint GSPLAT_FLAG_ACTIVE = 1u << 0u;

bool isGsplatActive(uint flags) {
  return (flags & GSPLAT_FLAG_ACTIVE) != 0u;
}

bool readPackedArray(usampler2DArray texture, int numSplats, vec4 rgbMinMaxLnScaleMinMax, int index, out Gsplat gsplat) {
  if ((index >= 0) && (index < numSplats)) {
    uvec4 packedData = texelFetch(texture, splatTexCoord(index), 0);
    unpackSplatEncoding(packedData, gsplat.center, gsplat.scales, gsplat.quaternion, gsplat.rgba, rgbMinMaxLnScaleMinMax);
    return true;
  } else {
    return false;
  }
}

uniform float value_20;

uniform vec4 value_21;

uniform vec3 value_22;

uniform vec4 value_24;

uniform vec3 value_5;

uniform vec3 value_9;

uniform bool value_10;

void produceSplat(int _index) {
    // DynoBlock(_index) => ()
    {
        // DynoBlock(_index) => (gsplat_2)
        Gsplat gsplat_2;
        {
            // DynoPackedSplats() => (packedSplats_14)
            
            // DynoUsampler2D() => (lodIndices_16)
            
            // DynoInt() => (value_17)
            
            // DynoBool() => (value_18)
            
            // DynoInt() => (value_19)
            
            // Dyno(lodIndices_16, _index, value_17, value_18, value_19) => (index_15)
            int index_15;
            {
                int index = _index;
                if (value_19 < 0) {
                  if (index >= value_17) {
                    return;
                  }
                  if (value_18) {
                    ivec2 lodIndexCoord = ivec2((index >> 2) & 4095, index >> 14);
                    uint splatIndex = texelFetch(lodIndices_16, lodIndexCoord, 0)[index & 3];
                    index_15 = int(splatIndex);
                  } else {
                    index_15 = index;
                  }
                } else {
                  int start = value_19 << 16;
                  if (index >= 65536) {
                    return;
                  }
                  index_15 = start + index;
                }
            }
            
            // ReadPackedSplat(packedSplats_14, index_15) => (gsplat_13)
            Gsplat gsplat_13;
            {
                gsplat_13.flags = 0u;
                if (readPackedArray(packedSplats_14.textureArray, packedSplats_14.numSplats, packedSplats_14.rgbMinMaxLnScaleMinMax, index_15, gsplat_13)) {
                  if (packedSplats_14.lodOpacity) {
                    gsplat_13.rgba.a = 2.0 * gsplat_13.rgba.a;
                  }
                  bool zeroSize = all(equal(gsplat_13.scales, vec3(0.0, 0.0, 0.0)));
                  gsplat_13.flags = zeroSize ? 0u : GSPLAT_FLAG_ACTIVE;
                }
                gsplat_13.index = index_15;
            }
            
            // DynoFloat() => (value_20)
            
            // DynoVec4() => (value_21)
            
            // DynoVec3() => (value_22)
            
            // TransformGsplat(gsplat_13, value_20, value_21, value_22) => (gsplat_12)
            Gsplat gsplat_12;
            {
                gsplat_12 = gsplat_13;
                if (isGsplatActive(gsplat_12.flags)) {
                    gsplat_12.center *= value_20;
                    gsplat_12.center = quatVec(value_21, gsplat_12.center);
                    gsplat_12.center += value_22;
                    gsplat_12.scales *= value_20;
                    gsplat_12.quaternion = quatQuat(value_21, gsplat_12.quaternion);
                }
            }
            
            // DynoVec4() => (value_24)
            
            // SplitGsplat(gsplat_12) => (rgba_25)
            vec4 rgba_25;
            {
                rgba_25 = gsplat_12.rgba;
            }
            
            // Mul(value_24, rgba_25) => (product_23)
            vec4 product_23;
            {
                product_23 = value_24 * rgba_25;
            }
            
            // CombineGsplat(gsplat_12, product_23) => (gsplat_2)
            {
                gsplat_2.flags = gsplat_12.flags;
                gsplat_2.index = gsplat_12.index;
                gsplat_2.center = gsplat_12.center;
                gsplat_2.scales = gsplat_12.scales;
                gsplat_2.quaternion = gsplat_12.quaternion;
                gsplat_2.rgba = product_23;
            }
        }
        
        // SplitGsplat(gsplat_2) => (center_4)
        vec3 center_4;
        {
            center_4 = gsplat_2.center;
        }
        
        // DynoVec3() => (value_5)
        
        // Sub(center_4, value_5) => (difference_3)
        vec3 difference_3;
        {
            difference_3 = center_4 - value_5;
        }
        
        // SplitGsplat(gsplat_2) => (opacity_7)
        float opacity_7;
        {
            opacity_7 = gsplat_2.rgba.a;
        }
        
        // Mul(opacity_7, 0.5) => (product_6)
        float product_6;
        {
            product_6 = opacity_7 * 0.5;
        }
        
        // CombineGsplat(gsplat_2, difference_3, product_6) => (gsplat_1)
        Gsplat gsplat_1;
        {
            gsplat_1.flags = gsplat_2.flags;
            gsplat_1.index = gsplat_2.index;
            gsplat_1.center = difference_3;
            gsplat_1.scales = gsplat_2.scales;
            gsplat_1.quaternion = gsplat_2.quaternion;
            gsplat_1.rgba = gsplat_2.rgba;
            gsplat_1.rgba.a = product_6;
        }
        
        // OutputPackedSplat(gsplat_1, vec4(0.0, 1.0, -12.0, 9.0)) => ()
        {
            if (isGsplatActive(gsplat_1.flags)) {
              target = packSplatEncoding(gsplat_1.center, gsplat_1.scales, gsplat_1.quaternion, gsplat_1.rgba, vec4(0.0, 1.0, -12.0, 9.0));
            } else {
              target = uvec4(0u, 0u, 0u, 0u);
            }
        }
        
        // DynoVec3() => (value_9)
        
        // DynoBool() => (value_10)
        
        // OutputSplatDepth(gsplat_2, value_5, value_9, value_10) => ()
        {
            float metric = 1.0 / 0.0;
            if (isGsplatActive(gsplat_2.flags)) {
              vec3 center = gsplat_2.center - value_5;
              if (value_10) {
                metric = length(center);
              } else {
                float bias = 100.0; // reduce popping
                metric = dot(center, value_9) + bias;
              }
            }
            target3 = floatToVec4(metric);
        }
    }
}

void main() {
    int targetIndex = int(targetLayer << SPLAT_TEX_LAYER_BITS) + int(uint(gl_FragCoord.y) << SPLAT_TEX_WIDTH_BITS) + int(gl_FragCoord.x);
    int index = targetIndex - targetBase;

    
    target = uvec4(0u, 0u, 0u, 0u);

    
    target3 = floatToVec4(1.0 / 0.0);

    if ((index >= 0) && (index < targetCount)) {
        produceSplat(index);
    }
}