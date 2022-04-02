#version 460

// Hardcoded array of vertex positions for our triangle
const vec4 positions[3] = vec4[](
    vec4( 0.0, +1.0, 0.0, 1.0),
    vec4(-1.0, -1.0, 0.0, 1.0),
    vec4(+1.0, -1.0, 0.0, 1.0)
);

// Hardcoded array of vertex colors for our triangle
const vec4 colors[3] = vec4[](
    vec4(1.0, 0.0, 0.0, 1.0),
    vec4(0.0, 1.0, 0.0, 1.0),
    vec4(0.0, 0.0, 1.0, 1.0)
);

layout (location = 0) out vec4 outColor;

void main()
{
    gl_Position = positions[gl_VertexID];
    outColor = colors[gl_VertexID];
}
