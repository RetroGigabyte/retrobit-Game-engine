#version 410 core
in vec2 vLocalPos;
out vec4 FragColor;

uniform vec3 uColor;
uniform vec2 uHalfSizePx;         // half-width/half-height of the rect, in pixels
uniform float uRadiusPx;          // corner radius, in pixels
uniform float uHasTab;            // 0 or 1 — draws a small rounded notch straddling the top edge
uniform float uTabOffsetFromLeftPx; // where the tab's center sits, measured from the rect's left edge

// Inigo Quilez's rounded-box SDF: distance from p to the boundary of a
// halfSize-sized box with corner radius r, negative inside.
float roundedBoxSDF(vec2 p, vec2 halfSize, float r) {
    vec2 q = abs(p) - halfSize + r;
    return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - r;
}

void main() {
    // vLocalPos is -0.5..0.5; recover real pixel-space position (origin at rect
    // center) so the SDF math below works in the same units as uHalfSizePx/uRadiusPx.
    vec2 p = vLocalPos * uHalfSizePx * 2.0;

    float d = roundedBoxSDF(p, uHalfSizePx, uRadiusPx);

    if (uHasTab > 0.5) {
        // A small rounded bump unioned onto the top edge — the closest
        // approximation of Scratch's puzzle-piece connector achievable without
        // real concave geometry or a texture atlas.
        vec2 tabHalfSize = vec2(min(uHalfSizePx.x * 0.18, 14.0), 6.0);
        vec2 tabCenter = vec2(-uHalfSizePx.x + uTabOffsetFromLeftPx, -uHalfSizePx.y);
        float tabD = roundedBoxSDF(p - tabCenter, tabHalfSize, 3.0);
        d = min(d, tabD);
    }

    // ~1.5px soft edge for antialiasing instead of a hard cutoff.
    float alpha = 1.0 - smoothstep(-1.5, 1.5, d);
    if (alpha <= 0.001) discard;
    FragColor = vec4(uColor, alpha);
}
