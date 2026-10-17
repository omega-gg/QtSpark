#version 440
#extension GL_GOOGLE_include_directive : require

// SPARK v2.3.1: src/shaders/computeUvec4_Vec4.glsl (template)
//               src/SplatAccumulator.ts           (prepareProgramMaterial, packed path)
//               src/SplatMesh.ts                  (constructGenerator)
//               src/PackedSplats.ts               (fetchSplat, evaluatePackedSH)
//               src/dyno/splats.ts                (defineGsplat, readPackedArray, TransformGsplat)
//               src/dyno/output.ts                (OutputPackedSplat, OutputSplatDepth)
//
// NOTE: Spark generates this program at runtime with its dyno graph. This is the same program
//       expanded by hand for a SplatMesh reading PackedSplats, with no LoD, skinning, modifiers or
//       edits. Each dyno node is a function named after it. The generated upstream source is kept
//       in upstream/generated/ for resync diffs.

precision highp float;
precision highp int;

#include "splatDefines.glsl"
#include "packedSH.glsl"

//-------------------------------------------------------------------------------------------------
// Uniforms

layout(std140, binding = 0) uniform buf
{
    // SPARK: computeUvec4_Vec4.glsl
    int targetLayer;
    int targetBase;
    int targetCount;

    // SPARK: DynoPackedSplats
    int numSplats;
    vec4 rgbMinMaxLnScaleMinMax;

    // SPARK: PackedSplats.dynoShMax (xyz) and dynoNumSh
    vec4 shMax;
    int  numSh;
    int  lodOpacity;

    // SPARK: SplatAccumulator.sortRadialUniform
    int sortRadial;

    // SPARK: SplatMesh context.transform (SplatTransformer), translate.xyz and scale in w
    vec4 rotate;
    vec4 translateScale;

    // SPARK: SplatMesh context.recolor (rgb, opacity)
    vec4 recolor;

    // SPARK: SplatMesh context.viewToObject.translate, the camera position in object space
    vec4 viewOrigin;

    // SPARK: SplatAccumulator.viewCenterUniform / viewDirUniform (world space)
    vec4 viewCenter;
    vec4 viewDir;
} ubuf;

layout(binding = 1) uniform highp usampler2DArray packedSplats;
layout(binding = 2) uniform highp usampler2DArray sh1Texture;
layout(binding = 3) uniform highp usampler2DArray sh2Texture;
layout(binding = 4) uniform highp usampler2DArray sh3Texture;

layout(location = 0) out uvec4 target;
layout(location = 1) out vec4  target3;

//-------------------------------------------------------------------------------------------------
// SPARK: defineGsplat

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

//-------------------------------------------------------------------------------------------------
// SPARK: defineReadPackedArray
// SPLAT: 'gsplat' is inout (out upstream), HLSL rejects an out parameter left unwritten.

bool readPackedArray(int numSplats, vec4 rgbMinMaxLnScaleMinMax, int index, inout Gsplat gsplat) {
    if ((index >= 0) && (index < numSplats)) {
        uvec4 packedData = texelFetch(packedSplats, splatTexCoord(index), 0);
        unpackSplatEncoding(packedData, gsplat.center, gsplat.scales, gsplat.quaternion, gsplat.rgba, rgbMinMaxLnScaleMinMax);
        return true;
    } else {
        return false;
    }
}

//-------------------------------------------------------------------------------------------------
// SPARK: ReadPackedSplat

Gsplat readPackedSplat(int index) {
    // SPLAT: Zero initialized, HLSL rejects reading an uninitialized struct.
    Gsplat gsplat = Gsplat(vec3(0.0), 0u, vec3(0.0), 0, vec4(0.0, 0.0, 0.0, 1.0), vec4(0.0));
    gsplat.flags = 0u;
    if (readPackedArray(ubuf.numSplats, ubuf.rgbMinMaxLnScaleMinMax, index, gsplat)) {
        if (ubuf.lodOpacity != 0) {
            gsplat.rgba.a = 2.0 * gsplat.rgba.a;
        }
        bool zeroSize = all(equal(gsplat.scales, vec3(0.0, 0.0, 0.0)));
        gsplat.flags = zeroSize ? 0u : GSPLAT_FLAG_ACTIVE;
    }
    gsplat.index = index;
    return gsplat;
}

//-------------------------------------------------------------------------------------------------
// SPARK: evaluatePackedSH

vec3 evaluatePackedSH(ivec3 coord, vec3 viewDir, int numSh, vec3 shMax) {
    vec3 rgb = vec3(0.0);
    if (numSh >= 1) {
        vec3 sh1Rgb = evaluatePackedSH1(texelFetch(sh1Texture, coord, 0).rg, viewDir, shMax.x);
        rgb += sh1Rgb;
        if (numSh >= 2) {
            vec3 sh2Rgb = evaluatePackedSH2(texelFetch(sh2Texture, coord, 0), viewDir, shMax.y);
            rgb += sh2Rgb;
            if (numSh >= 3) {
                vec3 sh3Rgb = evaluatePackedSH3(texelFetch(sh3Texture, coord, 0), viewDir, shMax.z);
                rgb += sh3Rgb;
            }
        }
    }
    return rgb;
}

//-------------------------------------------------------------------------------------------------
// SPARK: PackedSplats.fetchSplat

Gsplat fetchSplat(int index, vec3 viewOrigin) {
    Gsplat gsplat = readPackedSplat(index);

    // NOTE: hasRgbDir() is numSh > 0, folded into the uniform.
    if (ubuf.numSh > 0) {
        vec3 viewDir = normalize(gsplat.center - viewOrigin);
        vec3 rgb = evaluatePackedSH(splatTexCoord(index), viewDir, ubuf.numSh, ubuf.shMax.xyz);
        gsplat.rgba.rgb = rgb + gsplat.rgba.rgb;
    }
    return gsplat;
}

//-------------------------------------------------------------------------------------------------
// SPARK: TransformGsplat (scale, rotate, translate)

Gsplat transformGsplat(Gsplat inGsplat, float scale, vec4 rotate, vec3 translate) {
    Gsplat gsplat = inGsplat;
    if (isGsplatActive(gsplat.flags)) {
        gsplat.center *= scale;
        gsplat.center = quatVec(rotate, gsplat.center);
        gsplat.center += translate;
        gsplat.scales *= scale;
        gsplat.quaternion = quatQuat(rotate, gsplat.quaternion);
    }
    return gsplat;
}

//-------------------------------------------------------------------------------------------------
// SPARK: SplatMesh.constructGenerator

Gsplat generator(int index) {
    // Read a Gsplat from the SplatSource
    Gsplat gsplat = fetchSplat(index, ubuf.viewOrigin.xyz);

    // Transform from object to world-space
    gsplat = transformGsplat(gsplat, ubuf.translateScale.w, ubuf.rotate, ubuf.translateScale.xyz);

    // Apply any global recoloring and opacity
    gsplat.rgba = ubuf.recolor * gsplat.rgba;

    return gsplat;
}

//-------------------------------------------------------------------------------------------------
// SPARK: OutputPackedSplat

void outputPackedSplat(Gsplat gsplat, vec4 rgbMinMaxLnScaleMinMax) {
    if (isGsplatActive(gsplat.flags)) {
        target = packSplatEncoding(gsplat.center, gsplat.scales, gsplat.quaternion, gsplat.rgba, rgbMinMaxLnScaleMinMax);
    } else {
        target = uvec4(0u, 0u, 0u, 0u);
    }
}

//-------------------------------------------------------------------------------------------------
// SPARK: OutputSplatDepth

void outputSplatDepth(Gsplat gsplat, vec3 viewCenter, vec3 viewDir, bool sortRadial) {
    float metric = 1.0 / 0.0;
    if (isGsplatActive(gsplat.flags)) {
        vec3 center = gsplat.center - viewCenter;
        if (sortRadial) {
            metric = length(center);
        } else {
            float bias = 100.0; // reduce popping
            metric = dot(center, viewDir) + bias;
        }
    }
    target3 = floatToVec4(metric);
}

//-------------------------------------------------------------------------------------------------
// SPARK: SplatAccumulator.prepareProgramMaterial (packed, non cov path)

void produceSplat(int _index) {
    Gsplat gsplat = generator(_index);

    // NOTE: The accumulator stores centers relative to the view origin and opacity * 0.5 (the
    //       expanded LoD opacity encoding). splatVertex.glsl undoes both.
    Gsplat accumSplat = gsplat;
    accumSplat.center = gsplat.center - ubuf.viewCenter.xyz;
    accumSplat.rgba.a = gsplat.rgba.a * 0.5;

    outputPackedSplat(accumSplat, vec4(0.0, 1.0, LN_SCALE_MIN, LN_SCALE_MAX));

    // NOTE: The depth metric reads the world space Gsplat (generator.outputs.gsplat).
    outputSplatDepth(gsplat, ubuf.viewCenter.xyz, ubuf.viewDir.xyz, ubuf.sortRadial != 0);
}

//-------------------------------------------------------------------------------------------------
// SPARK: computeUvec4_Vec4.glsl main

void main() {
    int targetIndex = int(uint(ubuf.targetLayer) << SPLAT_TEX_LAYER_BITS) + int(uint(gl_FragCoord.y) << SPLAT_TEX_WIDTH_BITS) + int(gl_FragCoord.x);
    int index = targetIndex - ubuf.targetBase;

    // Initial target to "null" splat
    target = uvec4(0u, 0u, 0u, 0u);

    // Initialize depthTarget to +infinity
    target3 = floatToVec4(1.0 / 0.0);

    if ((index >= 0) && (index < ubuf.targetCount)) {
        produceSplat(index);
    }
}
