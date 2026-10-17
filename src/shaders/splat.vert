#version 440
#extension GL_GOOGLE_include_directive : require

// SPARK v2.3.1: src/shaders/splatVertex.glsl
//
// NOTE: The upstream body for the PackedSplats path. Every change is tagged "SPLAT:":
//       - Uniforms live in splatUniforms.glsl, textures and varyings have explicit bindings.
//       - gl_InstanceID is gl_InstanceIndex (Vulkan GLSL, the base instance is always 0).
//       - The ExtSplats, CovSplats and 2DGS branches are cut (not used by PackedSplats).
//       - three.js logdepthbuf chunks are cut.
//       - gl_Position is multiplied by clipSpaceCorrMatrix so the math stays in OpenGL clip space.
//       - Empty quad culling (tightBounds), the output is unchanged.

precision highp float;
precision highp int;
precision highp usampler2DArray;

#include "splatDefines.glsl"
#include "splatUniforms.glsl"

// SPLAT: three.js 'position' attribute of SplatGeometry, the quad corners (-1,-1,0)..(1,1,0).
layout(location = 0) in vec3 position;

layout(location = 0) out vec4 vRgba;
layout(location = 1) out vec2 vSplatUv;
layout(location = 2) out vec3 vNdc;
layout(location = 3) flat out uint vSplatIndex;
layout(location = 4) flat out float adjustedStdDev;

layout(binding = 1) uniform highp usampler2D ordering;
layout(binding = 2) uniform highp usampler2DArray extSplats;

void main() {
    // Default to outside the frustum so it's discarded if we return early
    gl_Position = vec4(0.0, 0.0, 2.0, 1.0);

    // SPLAT: gl_InstanceID is gl_InstanceIndex.
    ivec2 orderingCoord = ivec2((gl_InstanceIndex >> 2) & 4095, gl_InstanceIndex >> 14);
    uint splatIndex = texelFetch(ordering, orderingCoord, 0)[gl_InstanceIndex & 3];
    if (splatIndex == 0xffffffffu) {
        // Special value reserved for "no splat"
        return;
    }

    ivec3 texCoord = splatTexCoord(int(splatIndex));
    vec3 center, scales;
    vec4 quaternion, rgba;
    mat3 cov3D;
    bvec3 zeroScales = bvec3(false);

    // SPLAT: The enableExtSplats and enableCovSplats branches are cut.
    {
        uvec4 packedData = texelFetch(extSplats, texCoord, 0);
        {
            unpackSplatEncoding(packedData, center, scales, quaternion, rgba, vec4(0.0, 1.0, LN_SCALE_MIN, LN_SCALE_MAX));
            zeroScales = equal(scales, vec3(0.0));
            if (all(zeroScales)) {
                return;
            }
        }

        rgba.a *= 2.0;
        if ((rgba.a == 0.0) || (rgba.a < minAlpha)) {
            return;
        }
    }

    // Match the reference 3DGS rasterizer (graphdeco-inria/diff-gaussian-rasterization),
    // which clamps the SH-evaluated color to positive
    rgba.rgb = max(rgba.rgb, vec3(0.0));

    adjustedStdDev = maxStdDev;
    if (rgba.a > 1.0) {
        // Stretch 1..2 to 1..5
        rgba.a = min(rgba.a * 4.0 - 3.0, 5.0);

        if (lodInflate) {
            // Adjust size to componsate for loss of opacity
            float opacity = exp((rgba.a * rgba.a - 1.0) / 2.718281828459045);
            float rescale = pow(opacity, 1.0 / 3.0);
            scales *= rescale;
            rgba.a = 1.0;
        }

        // Expand the maximum std dev to approximately cover the larger range
        adjustedStdDev = maxStdDev + 0.7 * (rgba.a - 1.0);
    }

    // Compute the view space center of the splat
    // SPLAT: The enableCovSplats basis is cut.
    vec3 viewCenter = quatVec(renderToViewQuat, center) + renderToViewPos;

    // Discard splats behind the camera
    if (viewCenter.z >= 0.0) {
        return;
    }

    // Compute the clip space center of the splat
    vec4 clipCenter = projectionMatrix * vec4(viewCenter, 1.0);

    // Discard splats outside near/far planes
    if (abs(clipCenter.z) >= clipCenter.w) {
        return;
    }

    // Discard splats more than clipXY times outside the XY frustum
    float clip = clipXY * clipCenter.w;
    if (abs(clipCenter.x) > clip || abs(clipCenter.y) > clip) {
        return;
    }

    vRgba = rgba;
    vSplatUv = position.xy * adjustedStdDev;

    // Record the splat index for entropy
    vSplatIndex = splatIndex;

    {
        // Compute view space quaternion of splat
        vec4 viewQuaternion = quatQuat(renderToViewQuat, quaternion);

        // SPLAT: The enable2DGS branch is cut.

        // Compute the 3D covariance matrix of the splat
        mat3 RS = scaleQuaternionToMatrix(scales, viewQuaternion);
        cov3D = RS * transpose(RS);
    }

    // Compute the Jacobian of the splat's projection at its center
    vec2 scaledRenderSize = renderSize * focalAdjustment;
    vec2 focal = 0.5 * scaledRenderSize * vec2(projectionMatrix[0][0], projectionMatrix[1][1]);

    mat3 J;
    if (isOrthographic) {
        J = mat3(
            focal.x, 0.0, 0.0,
            0.0, focal.y, 0.0,
            0.0, 0.0, 0.0
        );
    } else {
        float invZ = 1.0 / viewCenter.z;
        vec2 J1 = focal * invZ;
        vec2 J2 = -(J1 * viewCenter.xy) * invZ;
        J = mat3(
            J1.x, 0.0, J2.x,
            0.0, J1.y, J2.y,
            0.0, 0.0, 0.0
        );
    }

    // Compute the 2D covariance by projecting the 3D covariance
    // and picking out the XY plane components.
    mat3 cov2D = transpose(J) * cov3D * J;
    float a = cov2D[0][0];
    float d = cov2D[1][1];
    float b = cov2D[0][1];

    // Optionally pre-blur the splat to match non-antialias optimized splats
    a += preBlurAmount;
    d += preBlurAmount;

    float fullBlurAmount = blurAmount;
    if ((focalDistance > 0.0) && (apertureAngle > 0.0)) {
        float focusRadius = maxPixelRadius;
        if (viewCenter.z < 0.0) {
            float focusBlur = abs((-viewCenter.z - focalDistance) / viewCenter.z);
            float apertureRadius = focal.x * tan(0.5 * apertureAngle);
            focusRadius = focusBlur * apertureRadius;
        }
        fullBlurAmount = clamp(sqr(focusRadius), blurAmount, sqr(maxPixelRadius));
    }

    // Do convolution with a 0.5-pixel Gaussian for anti-aliasing: sqrt(0.3) ~= 0.5
    float detOrig = a * d - b * b;
    a += fullBlurAmount;
    d += fullBlurAmount;
    float det = a * d - b * b;

    // Compute anti-aliasing intensity scaling factor
    float blurAdjust = sqrt(max(0.0, detOrig / det));
    rgba.a *= blurAdjust;
    if (rgba.a < minAlpha) {
        return;
    }
    vRgba.a = rgba.a;

    // Compute the eigenvalue and eigenvectors of the 2D covariance matrix
    float eigenAvg = 0.5 * (a + d);
    float eigenDelta = sqrt(max(0.0, eigenAvg * eigenAvg - det));
    float eigen1 = eigenAvg + eigenDelta;
    float eigen2 = eigenAvg - eigenDelta;

    vec2 eigenVec1 = (abs(b) > 0.001) ? normalize(vec2(b, eigen1 - a))
        : ((a >= d) ? vec2(1.0, 0.0) : vec2(0.0, 1.0));
    vec2 eigenVec2 = vec2(eigenVec1.y, -eigenVec1.x);

    float scale1 = min(maxPixelRadius, adjustedStdDev * sqrt(eigen1));
    float scale2 = min(maxPixelRadius, adjustedStdDev * sqrt(eigen2));
    if (scale1 < minPixelRadius && scale2 < minPixelRadius) {
        return;
    }

    // SPLAT: Empty quad culling. A quad whose bounding box holds no pixel center produces no
    //        fragment, so it is culled before the rasterizer: most splats of large scenes are
    //        below a pixel. The box is grown by 1/64 pixel, coarser than the rasterizer snapping.
    if (tightBounds) {
        vec2 extent = (abs(eigenVec1) * scale1 + abs(eigenVec2) * scale2)
            * (renderSize / scaledRenderSize);
        vec2 centerPixel = ((clipCenter.xy / clipCenter.w) * 0.5 + 0.5) * renderSize;
        vec2 first = ceil(centerPixel - extent - 0.5 - 1.0 / 64.0);
        vec2 last = floor(centerPixel + extent - 0.5 + 1.0 / 64.0);
        if (any(lessThan(last, first))) {
            return;
        }
    }

    // Compute the NDC coordinates for the ellipsoid's diagonal axes.
    vec2 pixelOffset = position.x * eigenVec1 * scale1 + position.y * eigenVec2 * scale2;
    vec2 ndcOffset = (2.0 / scaledRenderSize) * pixelOffset;

    // Compute NDC center of the splat
    vec3 ndcCenter = clipCenter.xyz / clipCenter.w;
    vec3 ndc = vec3(ndcCenter.xy + ndcOffset, ndcCenter.z);

    vNdc = ndc;
    // SPLAT: Converted to the backend clip space.
    gl_Position = clipSpaceCorrMatrix * vec4(ndc.xy * clipCenter.w, clipCenter.zw);

    // SPLAT: #include <logdepthbuf_vertex> is cut.
}
