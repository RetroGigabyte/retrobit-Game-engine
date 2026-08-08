#version 410 core
in vec3 vNormal;
in vec3 vWorldPos;

uniform vec3 uColor;
uniform vec3 uViewPos;
uniform vec3 uSunDir;
uniform vec3 uFogColor;
uniform float uFogDensity;

out vec4 FragColor;

void main() {
    vec3 N = normalize(vNormal);
    vec3 L = normalize(-uSunDir);

    float diff = max(dot(N, L), 0.0);
    float ambient = 0.45;
    vec3 lit = uColor * (ambient + diff * 0.75);

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
