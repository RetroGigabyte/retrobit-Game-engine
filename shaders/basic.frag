#version 410 core
in vec3 vNormal;
in vec3 vWorldPos;

uniform vec3 uColor;
uniform vec3 uViewPos;
uniform vec3 uSunDir;
uniform vec3 uFogColor;
uniform float uFogDensity;

// Infinite checkerboard ground: instead of drawing hundreds of individual tile
// meshes (only ever covers a fixed, bounded area), the Playground ground is now one
// huge single quad and this shader paints the checker pattern per-fragment from
// world position, so it reads as infinite regardless of how far the plane actually
// extends. uColorB is the alternate tile color; uColor is the base (even) one.
uniform float uUseCheckerboard;
uniform vec3 uColorB;
uniform float uCheckerSize;

out vec4 FragColor;

void main() {
    vec3 N = normalize(vNormal);
    vec3 L = normalize(-uSunDir);

    float diff = max(dot(N, L), 0.0);
    float ambient = 0.45;
    vec3 baseColor = uColor;
    if (uUseCheckerboard > 0.5) {
        float cx = floor(vWorldPos.x / uCheckerSize);
        float cz = floor(vWorldPos.z / uCheckerSize);
        float parity = mod(cx + cz, 2.0);
        baseColor = mix(uColor, uColorB, parity);
    }
    vec3 lit = baseColor * (ambient + diff * 0.75);

    // cheap rim light for that shiny early-3D look
    vec3 V = normalize(uViewPos - vWorldPos);
    float rim = pow(1.0 - max(dot(N, V), 0.0), 3.0) * 0.25;
    lit += rim;

    float dist = length(uViewPos - vWorldPos);
    float fog = 1.0 - exp(-uFogDensity * dist * uFogDensity * dist);
    fog = clamp(fog, 0.0, 1.0);

    vec3 finalColor = mix(lit, uFogColor, fog);
    FragColor = vec4(finalColor, 1.0);
}
