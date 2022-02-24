#version 460

layout (std140, binding = 0) uniform Transformation {
    mat4 model_mat;
    mat4 view_mat;
    mat4 projection_mat;
} u;

layout (location = 0) in vec3 inPos;
layout (location = 1) in vec3 inAttrib;

layout (location = 0) out vec3 outAttrib;

void main()
{
    gl_Position = vec4(inPos, 1.0) * u.model_mat * u.view_mat * u.projection_mat;
    outAttrib = inAttrib;
}