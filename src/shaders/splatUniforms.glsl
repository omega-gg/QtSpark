// SPARK v2.3.1: src/SparkRenderer.ts (makeUniforms), src/shaders/splatVertex.glsl and
//               src/shaders/splatFragment.glsl uniform declarations
//
// NOTE: One std140 block shared by splat.vert and splat.frag, mirrored by WSplatUniforms in
//       WSplatRenderer.cpp. It has no instance name so the upstream bodies use the same names.
//       Booleans are ints, and the upstream names are macros on top of them.
//
// NOTE: Uniforms of cut features are not declared: enableExtSplats, enableCovSplats,
//       renderToViewBasis, enable2DGS, time, deltaTime, debugFlag, near, far.

layout(std140, binding = 0) uniform buf
{
    // NOTE: three.js projectionMatrix, in OpenGL convention.
    mat4 projectionMatrix;

    // NOTE: QRhi::clipSpaceCorrMatrix(), applied to gl_Position only.
    mat4 clipSpaceCorrMatrix;

    vec4  renderToViewQuat;
    vec3  renderToViewPos;
    float maxStdDev;

    vec2  renderSize;
    float minPixelRadius;
    float maxPixelRadius;

    float minAlpha;
    float blurAmount;
    float preBlurAmount;
    float focalDistance;

    float apertureAngle;
    float clipXY;
    float focalAdjustment;
    float falloff;

    int isOrthographicValue;
    int lodInflateValue;
    int encodeLinearValue;
    int disableFalloffValue;

    // NOTE: Splat only, the empty quad culling, see WSplatRenderOptions::tightBounds.
    int tightBoundsValue;
};

#define isOrthographic (isOrthographicValue != 0)
#define lodInflate     (lodInflateValue     != 0)
#define encodeLinear   (encodeLinearValue   != 0)
#define disableFalloff (disableFalloffValue != 0)
#define tightBounds    (tightBoundsValue    != 0)
