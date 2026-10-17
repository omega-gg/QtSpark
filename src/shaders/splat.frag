#version 440
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_control_flow_attributes : require

// SPARK v2.3.1: src/shaders/splatFragment.glsl
//
// NOTE: The upstream body. Every change is tagged "SPLAT:":
//       - Uniforms live in splatUniforms.glsl, varyings have explicit locations.
//       - PREMULTIPLIED_ALPHA is always defined (SparkRenderer premultipliedAlpha: true).
//       - three.js logdepthbuf chunks are cut.
//       - [[dont_flatten]] on the falloff and encodeLinear branches. The math is unchanged, but
//         fxc keeps real branches instead of evaluating both sides. On D3D this halves the cost
//         of the fragment shader.

precision highp float;
precision highp int;

#include "splatDefines.glsl"
#include "splatUniforms.glsl"

// SPLAT: SparkRenderer uses premultipliedAlpha: true by default.
#define PREMULTIPLIED_ALPHA

layout(location = 0) out vec4 fragColor;

layout(location = 0) in vec4 vRgba;
layout(location = 1) in vec2 vSplatUv;
layout(location = 2) in vec3 vNdc;
layout(location = 3) flat in uint vSplatIndex;
layout(location = 4) flat in float adjustedStdDev;

void main() {
    vec4 rgba = vRgba;

    float z2 = dot(vSplatUv, vSplatUv);
    if (z2 > (adjustedStdDev * adjustedStdDev)) {
        discard;
    }

    if (false) {
    // if (debugFlag) {
        float a = rgba.a;
        float shifted = sqrt(z2) - max(0.0, a - 1.0);
        float exponent = -0.5 * max(1.0, a) * sqr(max(0.0, shifted));
        float min1a = min(1.0, a);
        rgba.a = mix(min1a, min1a * exp(exponent), falloff);
    } else {
        // New falloff function, more or less equivalent
        // SPLAT: [[dont_flatten]]
        [[dont_flatten]] if (rgba.a <= 1.0) {
            rgba.a = mix(rgba.a, rgba.a * exp(-0.5 * z2), falloff);
        } else {
            float a = exp((rgba.a*rgba.a - 1.0) / 2.718281828459045);
            float alpha = 1.0 - pow(1.0 - exp(-0.5 * z2), a);
            rgba.a = mix(1.0, alpha, falloff);
        }
    }

    if (rgba.a < minAlpha) {
        discard;
    }
    // SPLAT: [[dont_flatten]]
    [[dont_flatten]] if (encodeLinear) {
        rgba.rgb = srgbToLinear(rgba.rgb);
    }

    #ifdef PREMULTIPLIED_ALPHA
        fragColor = vec4(rgba.rgb * rgba.a, rgba.a);
    #else
        fragColor = rgba;
    #endif

    // SPLAT: #include <logdepthbuf_fragment> is cut.
}
