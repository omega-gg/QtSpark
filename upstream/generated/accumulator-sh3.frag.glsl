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

#include <splatDefines>

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

uniform PackedSplats packedSplats_15;

uniform usampler2D lodIndices_17;

uniform int value_18;

uniform bool value_19;

uniform int value_20;

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

uniform vec3 value_27;

uniform int numSh_28;

uniform usampler2DArray sh1_29;

uniform usampler2DArray sh2_30;

uniform usampler2DArray sh3_31;

uniform vec3 shMax_32;

vec3 evaluatePackedSH1(uvec2 packedData, vec3 viewDir, float sh1Max) {
  // Extract sint7 values packed into 2 x uint32
  vec3 sh1_0 = vec3(ivec3(
    int(packedData.x << 25u) >> 25,
    int(packedData.x << 18u) >> 25,
    int(packedData.x << 11u) >> 25
  ));
  vec3 sh1_1 = vec3(ivec3(
    int(packedData.x << 4u) >> 25,
    int((packedData.x >> 3u) | (packedData.y << 29u)) >> 25,
    int(packedData.y << 22u) >> 25
  ));
  vec3 sh1_2 = vec3(ivec3(
    int(packedData.y << 15u) >> 25,
    int(packedData.y << 8u) >> 25,
    int(packedData.y << 1u) >> 25
  ));

  vec3 rgb = sh1_0 * (-0.4886025 * viewDir.y)
    + sh1_1 * (0.4886025 * viewDir.z)
    + sh1_2 * (-0.4886025 * viewDir.x);
  return rgb * (sh1Max / 63.0);
}

vec3 evaluatePackedSH2(uvec4 packedData, vec3 viewDir, float sh2Max) {
  // Extract sint8 values packed into 4 x uint32
  vec3 sh2_0 = vec3(ivec3(
    int(packedData.x << 24u) >> 24,
    int(packedData.x << 16u) >> 24,
    int(packedData.x << 8u) >> 24
  ));
  vec3 sh2_1 = vec3(ivec3(
    int(packedData.x) >> 24,
    int(packedData.y << 24u) >> 24,
    int(packedData.y << 16u) >> 24
  ));
  vec3 sh2_2 = vec3(ivec3(
    int(packedData.y << 8u) >> 24,
    int(packedData.y) >> 24,
    int(packedData.z << 24u) >> 24
  ));
  vec3 sh2_3 = vec3(ivec3(
    int(packedData.z << 16u) >> 24,
    int(packedData.z << 8u) >> 24,
    int(packedData.z) >> 24
  ));
  vec3 sh2_4 = vec3(ivec3(
    int(packedData.w << 24u) >> 24,
    int(packedData.w << 16u) >> 24,
    int(packedData.w << 8u) >> 24
  ));

  vec3 rgb = sh2_0 * (1.0925484 * viewDir.x * viewDir.y)
    + sh2_1 * (-1.0925484 * viewDir.y * viewDir.z)
    + sh2_2 * (0.3153915 * (2.0 * viewDir.z * viewDir.z - viewDir.x * viewDir.x - viewDir.y * viewDir.y))
    + sh2_3 * (-1.0925484 * viewDir.x * viewDir.z)
    + sh2_4 * (0.5462742 * (viewDir.x * viewDir.x - viewDir.y * viewDir.y));
  return rgb * (sh2Max / 127.0);
}

vec3 evaluatePackedSH3(uvec4 packedData, vec3 viewDir, float sh3Max) {
  // Extract sint6 values packed into 4 x uint32
  vec3 sh3_0 = vec3(ivec3(
    int(packedData.x << 26u) >> 26,
    int(packedData.x << 20u) >> 26,
    int(packedData.x << 14u) >> 26
  ));
  vec3 sh3_1 = vec3(ivec3(
    int(packedData.x << 8u) >> 26,
    int(packedData.x << 2u) >> 26,
    int((packedData.x >> 4u) | (packedData.y << 28u)) >> 26
  ));
  vec3 sh3_2 = vec3(ivec3(
    int(packedData.y << 22u) >> 26,
    int(packedData.y << 16u) >> 26,
    int(packedData.y << 10u) >> 26
  ));
  vec3 sh3_3 = vec3(ivec3(
    int(packedData.y << 4u) >> 26,
    int((packedData.y >> 2u) | (packedData.z << 30u)) >> 26,
    int(packedData.z << 24u) >> 26
  ));
  vec3 sh3_4 = vec3(ivec3(
    int(packedData.z << 18u) >> 26,
    int(packedData.z << 12u) >> 26,
    int(packedData.z << 6u) >> 26
  ));
  vec3 sh3_5 = vec3(ivec3(
    int(packedData.z) >> 26,
    int(packedData.w << 26u) >> 26,
    int(packedData.w << 20u) >> 26
  ));
  vec3 sh3_6 = vec3(ivec3(
    int(packedData.w << 14u) >> 26,
    int(packedData.w << 8u) >> 26,
    int(packedData.w << 2u) >> 26
  ));

  float xx = viewDir.x * viewDir.x;
  float yy = viewDir.y * viewDir.y;
  float zz = viewDir.z * viewDir.z;
  float xy = viewDir.x * viewDir.y;
  float yz = viewDir.y * viewDir.z;
  float zx = viewDir.z * viewDir.x;

  vec3 rgb = sh3_0 * (-0.5900436 * viewDir.y * (3.0 * xx - yy))
    + sh3_1 * (2.8906114 * xy * viewDir.z) +
    + sh3_2 * (-0.4570458 * viewDir.y * (4.0 * zz - xx - yy))
    + sh3_3 * (0.3731763 * viewDir.z * (2.0 * zz - 3.0 * xx - 3.0 * yy))
    + sh3_4 * (-0.4570458 * viewDir.x * (4.0 * zz - xx - yy))
    + sh3_5 * (1.4453057 * viewDir.z * (xx - yy))
    + sh3_6 * (-0.5900436 * viewDir.x * (xx - 3.0 * yy));
  return rgb * (sh3Max / 31.0);
}

uniform float value_34;

uniform vec4 value_35;

uniform vec3 value_36;

uniform vec4 value_38;

uniform vec3 value_5;

uniform vec3 value_9;

uniform bool value_10;

void produceSplat(int _index) {
    // DynoBlock(_index) => ()
    {
        // DynoBlock(_index) => (gsplat_2)
        Gsplat gsplat_2;
        {
            // DynoPackedSplats() => (packedSplats_15)
            
            // DynoUsampler2D() => (lodIndices_17)
            
            // DynoInt() => (value_18)
            
            // DynoBool() => (value_19)
            
            // DynoInt() => (value_20)
            
            // Dyno(lodIndices_17, _index, value_18, value_19, value_20) => (index_16)
            int index_16;
            {
                int index = _index;
                if (value_20 < 0) {
                  if (index >= value_18) {
                    return;
                  }
                  if (value_19) {
                    ivec2 lodIndexCoord = ivec2((index >> 2) & 4095, index >> 14);
                    uint splatIndex = texelFetch(lodIndices_17, lodIndexCoord, 0)[index & 3];
                    index_16 = int(splatIndex);
                  } else {
                    index_16 = index;
                  }
                } else {
                  int start = value_20 << 16;
                  if (index >= 65536) {
                    return;
                  }
                  index_16 = start + index;
                }
            }
            
            // ReadPackedSplat(packedSplats_15, index_16) => (gsplat_14)
            Gsplat gsplat_14;
            {
                gsplat_14.flags = 0u;
                if (readPackedArray(packedSplats_15.textureArray, packedSplats_15.numSplats, packedSplats_15.rgbMinMaxLnScaleMinMax, index_16, gsplat_14)) {
                  if (packedSplats_15.lodOpacity) {
                    gsplat_14.rgba.a = 2.0 * gsplat_14.rgba.a;
                  }
                  bool zeroSize = all(equal(gsplat_14.scales, vec3(0.0, 0.0, 0.0)));
                  gsplat_14.flags = zeroSize ? 0u : GSPLAT_FLAG_ACTIVE;
                }
                gsplat_14.index = index_16;
            }
            
            // SplatTexCoord(index_16) => (coord_23)
            ivec3 coord_23;
            {
                coord_23 = splatTexCoord(index_16);
            }
            
            // SplitGsplat(gsplat_14) => (center_26)
            vec3 center_26;
            {
                center_26 = gsplat_14.center;
            }
            
            // DynoVec3() => (value_27)
            
            // Sub(center_26, value_27) => (difference_25)
            vec3 difference_25;
            {
                difference_25 = center_26 - value_27;
            }
            
            // Normalize(difference_25) => (normalize_24)
            vec3 normalize_24;
            {
                normalize_24 = normalize(difference_25);
            }
            
            // DynoInt() => (numSh_28)
            
            // DynoUsampler2DArray() => (sh1_29)
            
            // DynoUsampler2DArray() => (sh2_30)
            
            // DynoUsampler2DArray() => (sh3_31)
            
            // DynoVec3() => (shMax_32)
            
            // Dyno(coord_23, normalize_24, numSh_28, sh1_29, sh2_30, sh3_31, shMax_32) => (rgb_22)
            vec3 rgb_22;
            {
                vec3 rgb = vec3(0.0);
                if (numSh_28 >= 1) {
                  vec3 sh1Rgb = evaluatePackedSH1(texelFetch(sh1_29, coord_23, 0).rg, normalize_24, shMax_32.x);
                  rgb += sh1Rgb;
                if (numSh_28 >= 2) {
                  vec3 sh2Rgb = evaluatePackedSH2(texelFetch(sh2_30, coord_23, 0), normalize_24, shMax_32.y);
                  rgb += sh2Rgb;
                if (numSh_28 >= 3) {
                  vec3 sh3Rgb = evaluatePackedSH3(texelFetch(sh3_31, coord_23, 0), normalize_24, shMax_32.z);
                  rgb += sh3Rgb;
                }
                }
                }
                rgb_22 = rgb;
            }
            
            // SplitGsplat(gsplat_14) => (rgb_33)
            vec3 rgb_33;
            {
                rgb_33 = gsplat_14.rgba.rgb;
            }
            
            // Add(rgb_22, rgb_33) => (sum_21)
            vec3 sum_21;
            {
                sum_21 = rgb_22 + rgb_33;
            }
            
            // CombineGsplat(gsplat_14, sum_21) => (gsplat_13)
            Gsplat gsplat_13;
            {
                gsplat_13.flags = gsplat_14.flags;
                gsplat_13.index = gsplat_14.index;
                gsplat_13.center = gsplat_14.center;
                gsplat_13.scales = gsplat_14.scales;
                gsplat_13.quaternion = gsplat_14.quaternion;
                gsplat_13.rgba = gsplat_14.rgba;
                gsplat_13.rgba.rgb = sum_21;
            }
            
            // DynoFloat() => (value_34)
            
            // DynoVec4() => (value_35)
            
            // DynoVec3() => (value_36)
            
            // TransformGsplat(gsplat_13, value_34, value_35, value_36) => (gsplat_12)
            Gsplat gsplat_12;
            {
                gsplat_12 = gsplat_13;
                if (isGsplatActive(gsplat_12.flags)) {
                    gsplat_12.center *= value_34;
                    gsplat_12.center = quatVec(value_35, gsplat_12.center);
                    gsplat_12.center += value_36;
                    gsplat_12.scales *= value_34;
                    gsplat_12.quaternion = quatQuat(value_35, gsplat_12.quaternion);
                }
            }
            
            // DynoVec4() => (value_38)
            
            // SplitGsplat(gsplat_12) => (rgba_39)
            vec4 rgba_39;
            {
                rgba_39 = gsplat_12.rgba;
            }
            
            // Mul(value_38, rgba_39) => (product_37)
            vec4 product_37;
            {
                product_37 = value_38 * rgba_39;
            }
            
            // CombineGsplat(gsplat_12, product_37) => (gsplat_2)
            {
                gsplat_2.flags = gsplat_12.flags;
                gsplat_2.index = gsplat_12.index;
                gsplat_2.center = gsplat_12.center;
                gsplat_2.scales = gsplat_12.scales;
                gsplat_2.quaternion = gsplat_12.quaternion;
                gsplat_2.rgba = product_37;
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