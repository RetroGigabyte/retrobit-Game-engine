#version 410 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal; // unused — reused vertex layout from the 3D Mesh format

uniform mat4 uModel;
uniform mat4 uView; // always identity for UI, kept for symmetry with the 3D shader's call sites
uniform mat4 uProj;

out vec2 vLocalPos; // -0.5..0.5, the unit quad's own space, before uModel scales it to a real pixel rect

void main() {
    vLocalPos = aPos.xy;
    gl_Position = uProj * uView * uModel * vec4(aPos, 1.0);
}
