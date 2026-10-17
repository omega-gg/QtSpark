#version 440

// SPARK v2.3.1: three/addons/postprocessing/Pass.js (FullScreenQuad)
//
// NOTE: A single triangle covering the whole target, generated from gl_VertexIndex so no vertex
//       buffer is needed. The scissor restricts it to the rows being generated.

void main()
{
    vec2 position = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);

    gl_Position = vec4(position * 2.0 - 1.0, 0.0, 1.0);
}
