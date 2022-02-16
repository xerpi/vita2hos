#version 460

layout (location = 0) in vec3 inPos;
layout (location = 1) in vec3 inAttrib;

layout (location = 0) out vec3 outAttrib;

void main()
{
    gl_Position = vec4(inPos, 1.0);
    outAttrib = inAttrib;
}