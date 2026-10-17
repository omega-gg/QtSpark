// SPARK v2.3.1: src/PackedSplats.ts (defineEvalPackedSH1 / 2 / 3)
//
// NOTE: The GLSL bodies of the three upstream template strings, verbatim and unindented the
//       same way Spark's unindent() does. Included by accumulate.frag.

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
