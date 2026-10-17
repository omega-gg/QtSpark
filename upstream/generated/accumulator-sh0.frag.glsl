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