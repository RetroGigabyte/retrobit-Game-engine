#include "PlatformGL.h"
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <vector>
#include <cmath>
#include <iostream>
#include <fstream>
#include <string>
#include <utility>
#include <functional>
#include <map>
#include <array>

#include "Shader.h"
#include "NativeMenu.h"
#include "BlockScript.h"

// ---------------------------------------------------------------------------
// Simple mesh: interleaved position(3) + normal(3)
// ---------------------------------------------------------------------------
struct Mesh {
    unsigned int vao = 0, vbo = 0, ebo = 0;
    int indexCount = 0;

    void upload(const std::vector<float>& verts, const std::vector<unsigned int>& indices) {
        indexCount = (int)indices.size();
        glGenVertexArrays(1, &vao);
        glGenBuffers(1, &vbo);
        glGenBuffers(1, &ebo);

        glBindVertexArray(vao);

        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferData(GL_ARRAY_BUFFER, verts.size() * sizeof(float), verts.data(), GL_STATIC_DRAW);

        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(unsigned int), indices.data(), GL_STATIC_DRAW);

        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)0);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)(3 * sizeof(float)));
        glEnableVertexAttribArray(1);

        glBindVertexArray(0);
    }

    void draw() const {
        glBindVertexArray(vao);
        glDrawElements(GL_TRIANGLES, indexCount, GL_UNSIGNED_INT, 0);
        glBindVertexArray(0);
    }
};

static Mesh makeBox(float sx, float sy, float sz) {
    float x = sx * 0.5f, y = sy * 0.5f, z = sz * 0.5f;
    std::vector<float> v = {
        // pos                normal
        -x,-y, z,  0, 0, 1,   x,-y, z,  0, 0, 1,   x, y, z,  0, 0, 1,  -x, y, z,  0, 0, 1, // front
        -x,-y,-z,  0, 0,-1,  -x, y,-z,  0, 0,-1,   x, y,-z,  0, 0,-1,   x,-y,-z,  0, 0,-1, // back
        -x, y,-z,  0, 1, 0,  -x, y, z,  0, 1, 0,   x, y, z,  0, 1, 0,   x, y,-z,  0, 1, 0, // top
        -x,-y,-z,  0,-1, 0,   x,-y,-z,  0,-1, 0,   x,-y, z,  0,-1, 0,  -x,-y, z,  0,-1, 0, // bottom
         x,-y,-z,  1, 0, 0,   x, y,-z,  1, 0, 0,   x, y, z,  1, 0, 0,   x,-y, z,  1, 0, 0, // right
        -x,-y,-z, -1, 0, 0,  -x,-y, z, -1, 0, 0,  -x, y, z, -1, 0, 0,  -x, y,-z, -1, 0, 0, // left
    };
    std::vector<unsigned int> idx;
    for (unsigned int f = 0; f < 6; f++) {
        unsigned int b = f * 4;
        idx.insert(idx.end(), { b, b+1, b+2, b, b+2, b+3 });
    }
    Mesh m;
    m.upload(v, idx);
    return m;
}

static Mesh makeUVSphere(float radius, int stacks, int slices) {
    std::vector<float> v;
    std::vector<unsigned int> idx;
    for (int i = 0; i <= stacks; i++) {
        float phi = (float)M_PI * i / stacks; // 0..pi
        for (int j = 0; j <= slices; j++) {
            float theta = 2.0f * (float)M_PI * j / slices; // 0..2pi
            glm::vec3 n(sinf(phi) * cosf(theta), cosf(phi), sinf(phi) * sinf(theta));
            glm::vec3 p = n * radius;
            v.insert(v.end(), { p.x, p.y, p.z, n.x, n.y, n.z });
        }
    }
    for (int i = 0; i < stacks; i++) {
        for (int j = 0; j < slices; j++) {
            unsigned int a = i * (slices + 1) + j;
            unsigned int b = a + slices + 1;
            idx.insert(idx.end(), { a, b, a+1, a+1, b, b+1 });
        }
    }
    Mesh m;
    m.upload(v, idx);
    return m;
}

// A flat 1x1 quad in the XY plane facing +Z — used for simple 2D UI overlays
// (title screen buttons) rendered with an orthographic projection.
static Mesh makeQuad() {
    std::vector<float> v = {
        -0.5f,-0.5f, 0.0f,  0, 0, 1,
         0.5f,-0.5f, 0.0f,  0, 0, 1,
         0.5f, 0.5f, 0.0f,  0, 0, 1,
        -0.5f, 0.5f, 0.0f,  0, 0, 1,
    };
    std::vector<unsigned int> idx = { 0, 1, 2, 0, 2, 3 };
    Mesh m;
    m.upload(v, idx);
    return m;
}

// Minimal 5x7 dot-matrix font, just the glyphs used anywhere in the engine's UI
// screens (title screen, spawn menu, block editor). No texture/font-loading
// infrastructure exists yet, so each glyph is drawn as a handful of small quads
// (same technique as the button rectangles) — crude, but it's zero extra
// dependencies and reads fine at title-screen sizes.
// Each row is a 5-bit value, MSB = leftmost column.
static const std::map<char, std::array<uint8_t, 7>> FONT_5X7 = {
    { 'A', {{ 0b01110, 0b10001, 0b10001, 0b11111, 0b10001, 0b10001, 0b10001 }} },
    { 'B', {{ 0b11110, 0b10001, 0b10001, 0b11110, 0b10001, 0b10001, 0b11110 }} },
    { 'D', {{ 0b11110, 0b10001, 0b10001, 0b10001, 0b10001, 0b10001, 0b11110 }} },
    { 'E', {{ 0b11111, 0b10000, 0b10000, 0b11110, 0b10000, 0b10000, 0b11111 }} },
    { 'G', {{ 0b01110, 0b10001, 0b10000, 0b10111, 0b10001, 0b10001, 0b01111 }} },
    { 'H', {{ 0b10001, 0b10001, 0b10001, 0b11111, 0b10001, 0b10001, 0b10001 }} },
    { 'I', {{ 0b01110, 0b00100, 0b00100, 0b00100, 0b00100, 0b00100, 0b01110 }} },
    { 'L', {{ 0b10000, 0b10000, 0b10000, 0b10000, 0b10000, 0b10000, 0b11111 }} },
    { 'M', {{ 0b10001, 0b11011, 0b10101, 0b10101, 0b10001, 0b10001, 0b10001 }} },
    { 'N', {{ 0b10001, 0b11001, 0b10101, 0b10011, 0b10001, 0b10001, 0b10001 }} },
    { 'O', {{ 0b01110, 0b10001, 0b10001, 0b10001, 0b10001, 0b10001, 0b01110 }} },
    { 'P', {{ 0b11110, 0b10001, 0b10001, 0b11110, 0b10000, 0b10000, 0b10000 }} },
    { 'R', {{ 0b11110, 0b10001, 0b10001, 0b11110, 0b10100, 0b10010, 0b10001 }} },
    { 'S', {{ 0b01111, 0b10000, 0b10000, 0b01110, 0b00001, 0b00001, 0b11110 }} },
    { 'T', {{ 0b11111, 0b00100, 0b00100, 0b00100, 0b00100, 0b00100, 0b00100 }} },
    { 'U', {{ 0b10001, 0b10001, 0b10001, 0b10001, 0b10001, 0b10001, 0b01110 }} },
    { 'V', {{ 0b10001, 0b10001, 0b10001, 0b10001, 0b10001, 0b01010, 0b00100 }} },
    { 'W', {{ 0b10001, 0b10001, 0b10001, 0b10101, 0b10101, 0b11011, 0b10001 }} },
    { 'X', {{ 0b10001, 0b10001, 0b01010, 0b00100, 0b01010, 0b10001, 0b10001 }} },
    { 'Y', {{ 0b10001, 0b10001, 0b01010, 0b00100, 0b00100, 0b00100, 0b00100 }} },
    { '0', {{ 0b01110, 0b10001, 0b10011, 0b10101, 0b11001, 0b10001, 0b01110 }} },
    { '1', {{ 0b00100, 0b01100, 0b00100, 0b00100, 0b00100, 0b00100, 0b01110 }} },
    { '2', {{ 0b01110, 0b10001, 0b00001, 0b00010, 0b00100, 0b01000, 0b11111 }} },
    { '3', {{ 0b11110, 0b00001, 0b00001, 0b01110, 0b00001, 0b00001, 0b11110 }} },
    { '4', {{ 0b00010, 0b00110, 0b01010, 0b10010, 0b11111, 0b00010, 0b00010 }} },
    { '5', {{ 0b11111, 0b10000, 0b11110, 0b00001, 0b00001, 0b10001, 0b01110 }} },
    { '6', {{ 0b00110, 0b01000, 0b10000, 0b11110, 0b10001, 0b10001, 0b01110 }} },
    { '7', {{ 0b11111, 0b00001, 0b00010, 0b00100, 0b01000, 0b01000, 0b01000 }} },
    { '8', {{ 0b01110, 0b10001, 0b10001, 0b01110, 0b10001, 0b10001, 0b01110 }} },
    { '9', {{ 0b01110, 0b10001, 0b10001, 0b01111, 0b00001, 0b00010, 0b01100 }} },
    { '-', {{ 0b00000, 0b00000, 0b00000, 0b11111, 0b00000, 0b00000, 0b00000 }} },
    { '+', {{ 0b00000, 0b00100, 0b00100, 0b11111, 0b00100, 0b00100, 0b00000 }} },
    { '.', {{ 0b00000, 0b00000, 0b00000, 0b00000, 0b00000, 0b01100, 0b01100 }} },
};

// A thin circle drawn as a line loop — used for the rotate-tool's ring handles.
// Shares the same pos+normal vertex layout as Mesh (normal is a dummy, unused
// beyond feeding the existing lit shader something reasonable).
struct RingMesh {
    unsigned int vao = 0, vbo = 0;
    int vertexCount = 0;

    void upload(const std::vector<float>& verts) {
        vertexCount = (int)verts.size() / 6;
        glGenVertexArrays(1, &vao);
        glGenBuffers(1, &vbo);

        glBindVertexArray(vao);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferData(GL_ARRAY_BUFFER, verts.size() * sizeof(float), verts.data(), GL_STATIC_DRAW);

        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)0);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)(3 * sizeof(float)));
        glEnableVertexAttribArray(1);
        glBindVertexArray(0);
    }

    void draw() const {
        glBindVertexArray(vao);
        glDrawArrays(GL_LINE_LOOP, 0, vertexCount);
        glBindVertexArray(0);
    }
};

// axis: 0 = ring normal is +X (lies in the YZ plane), 1 = +Y (XZ plane), 2 = +Z (XY plane).
static RingMesh makeRing(float radius, int segments, int axis) {
    std::vector<float> v;
    v.reserve(segments * 6);
    for (int i = 0; i < segments; i++) {
        float a = 2.0f * (float)M_PI * i / segments;
        float ca = cosf(a) * radius, sa = sinf(a) * radius;
        glm::vec3 p = axis == 0 ? glm::vec3(0, ca, sa)
                    : axis == 1 ? glm::vec3(sa, 0, ca)
                                : glm::vec3(ca, sa, 0);
        v.insert(v.end(), { p.x, p.y, p.z, 0.0f, 1.0f, 0.0f });
    }
    RingMesh m;
    m.upload(v);
    return m;
}

// ---------------------------------------------------------------------------
// Collision geometry: world-space triangles for static level props.
// Kept separate from render meshes so collision can generalize to arbitrary
// (including curved) geometry later without touching the renderer.
// ---------------------------------------------------------------------------
struct Tri { glm::vec3 a, b, c; };

static std::vector<Tri> buildBoxTriangles(const glm::vec3& size, const glm::mat4& model) {
    glm::vec3 h = size * 0.5f;
    glm::vec3 local[8] = {
        {-h.x,-h.y,-h.z}, { h.x,-h.y,-h.z}, { h.x, h.y,-h.z}, {-h.x, h.y,-h.z},
        {-h.x,-h.y, h.z}, { h.x,-h.y, h.z}, { h.x, h.y, h.z}, {-h.x, h.y, h.z},
    };
    glm::vec3 w[8];
    for (int i = 0; i < 8; i++) w[i] = glm::vec3(model * glm::vec4(local[i], 1.0f));

    int faces[6][4] = {
        {0,1,2,3}, // back
        {5,4,7,6}, // front
        {4,0,3,7}, // left
        {1,5,6,2}, // right
        {3,2,6,7}, // top
        {4,5,1,0}, // bottom
    };
    std::vector<Tri> tris;
    for (auto& f : faces) {
        tris.push_back({ w[f[0]], w[f[1]], w[f[2]] });
        tris.push_back({ w[f[0]], w[f[2]], w[f[3]] });
    }
    return tris;
}

// Procedural rolling-hills terrain: a flat grid displaced by a simple sine
// height field. Builds both the render mesh (with per-vertex normals from a
// finite-difference gradient) and the matching collision triangles in world
// space, so hills use exactly the same collide-and-slide resolver as props —
// no special-casing needed there. Winding is CCW as seen from above (+Y
// normal) to match the engine's default front-face/culling convention.
static Mesh makeHillTerrain(float halfSize, int segments, float amplitude, float frequency, std::vector<Tri>& outTriangles) {
    auto heightAt = [&](float x, float z) {
        return amplitude * sinf(x * frequency) * cosf(z * frequency);
    };

    int N = segments + 1;
    std::vector<glm::vec3> positions(N * N);
    for (int zi = 0; zi <= segments; zi++) {
        for (int xi = 0; xi <= segments; xi++) {
            float x = -halfSize + (2.0f * halfSize) * xi / segments;
            float z = -halfSize + (2.0f * halfSize) * zi / segments;
            positions[zi * N + xi] = glm::vec3(x, heightAt(x, z), z);
        }
    }

    std::vector<float> verts;
    verts.reserve(N * N * 6);
    const float EPS = 0.1f;
    for (int zi = 0; zi <= segments; zi++) {
        for (int xi = 0; xi <= segments; xi++) {
            const glm::vec3& p = positions[zi * N + xi];
            float hL = heightAt(p.x - EPS, p.z), hR = heightAt(p.x + EPS, p.z);
            float hD = heightAt(p.x, p.z - EPS), hU = heightAt(p.x, p.z + EPS);
            glm::vec3 n = glm::normalize(glm::vec3(hL - hR, 2.0f * EPS, hD - hU));
            verts.insert(verts.end(), { p.x, p.y, p.z, n.x, n.y, n.z });
        }
    }

    std::vector<unsigned int> idx;
    outTriangles.clear();
    for (int zi = 0; zi < segments; zi++) {
        for (int xi = 0; xi < segments; xi++) {
            unsigned int a = zi * N + xi;         // (xi,   zi)
            unsigned int b = (zi + 1) * N + xi;   // (xi,   zi+1)
            unsigned int c = (zi + 1) * N + xi + 1;// (xi+1, zi+1)
            unsigned int d = zi * N + xi + 1;     // (xi+1, zi)
            idx.insert(idx.end(), { a, b, c, a, c, d });
            outTriangles.push_back({ positions[a], positions[b], positions[c] });
            outTriangles.push_back({ positions[a], positions[c], positions[d] });
        }
    }

    Mesh m;
    m.upload(verts, idx);
    return m;
}

// Sonic-style vertical loop: a thin ribbon swept around a circle in the X-Y plane
// (the plane containing the direction of travel and up), width along Z. Parametrized
// so theta=0 sits exactly at ground level with zero slope (dy/dtheta = R*sin(0) = 0),
// meaning it's tangent to the flat ground and needs no separate entry/exit ramp — you
// just run straight onto it. centerX/centerZ is the point on the ground the loop's
// bottom sits at; the loop rises in +Y from there as theta goes 0 -> pi (top) -> 2pi
// (back to the bottom, closing the ring).
//
// Collision needs no special winding: resolveSphereVsTriangles derives its push-out
// normal purely from (sphere center - closest point on triangle), not from triangle
// winding, so this thin ribbon works as real two-sided collision geometry the same
// way the flat ribbon-like hill terrain does — no volume/thickness needed.
static Mesh makeLoopTrack(float centerX, float centerZ, float radius, float halfWidth, int segments, std::vector<Tri>& outTriangles) {
    int N = 2; // just two rows across the track width — a flat ribbon, not a tube
    std::vector<glm::vec3> positions((segments + 1) * N);
    std::vector<glm::vec3> normals((segments + 1) * N);
    for (int i = 0; i <= segments; i++) {
        float theta = 2.0f * (float)M_PI * i / segments;
        float x = centerX + radius * sinf(theta);
        float y = radius - radius * cosf(theta);
        // Points from the loop's circle center toward this point, then flipped —
        // the solid material is on the outward radial side, so the surface normal
        // (facing the player, who runs on the inside) points back toward center.
        glm::vec2 inward = glm::normalize(glm::vec2(-sinf(theta), cosf(theta)));
        for (int w = 0; w < N; w++) {
            float z = centerZ + (w == 0 ? -halfWidth : halfWidth);
            positions[i * N + w] = glm::vec3(x, y, z);
            normals[i * N + w] = glm::vec3(inward.x, inward.y, 0.0f);
        }
    }

    std::vector<float> verts;
    verts.reserve(positions.size() * 6);
    for (size_t i = 0; i < positions.size(); i++) {
        const glm::vec3& p = positions[i];
        const glm::vec3& n = normals[i];
        verts.insert(verts.end(), { p.x, p.y, p.z, n.x, n.y, n.z });
    }

    std::vector<unsigned int> idx;
    outTriangles.clear();
    for (int i = 0; i < segments; i++) {
        unsigned int a = i * N + 0, b = i * N + 1;
        unsigned int c = (i + 1) * N + 1, d = (i + 1) * N + 0;
        idx.insert(idx.end(), { a, b, c, a, c, d });
        outTriangles.push_back({ positions[a], positions[b], positions[c] });
        outTriangles.push_back({ positions[a], positions[c], positions[d] });
    }

    Mesh m;
    m.upload(verts, idx);
    return m;
}

// Closest point on triangle ABC to point p (Ericson, Real-Time Collision Detection).
static glm::vec3 closestPointOnTriangle(const glm::vec3& p, const glm::vec3& a, const glm::vec3& b, const glm::vec3& c) {
    glm::vec3 ab = b - a, ac = c - a, ap = p - a;
    float d1 = glm::dot(ab, ap), d2 = glm::dot(ac, ap);
    if (d1 <= 0.0f && d2 <= 0.0f) return a;

    glm::vec3 bp = p - b;
    float d3 = glm::dot(ab, bp), d4 = glm::dot(ac, bp);
    if (d3 >= 0.0f && d4 <= d3) return b;

    float vc = d1 * d4 - d3 * d2;
    if (vc <= 0.0f && d1 >= 0.0f && d3 <= 0.0f) {
        float v = d1 / (d1 - d3);
        return a + v * ab;
    }

    glm::vec3 cp = p - c;
    float d5 = glm::dot(ab, cp), d6 = glm::dot(ac, cp);
    if (d6 >= 0.0f && d5 <= d6) return c;

    float vb = d5 * d2 - d1 * d6;
    if (vb <= 0.0f && d2 >= 0.0f && d6 <= 0.0f) {
        float w = d2 / (d2 - d6);
        return a + w * ac;
    }

    float va = d3 * d6 - d5 * d4;
    if (va <= 0.0f && (d4 - d3) >= 0.0f && (d5 - d6) >= 0.0f) {
        float w = (d4 - d3) / ((d4 - d3) + (d5 - d6));
        return b + w * (c - b);
    }

    float denom = 1.0f / (va + vb + vc);
    float v = vb * denom, w = vc * denom;
    return a + ab * v + ac * w;
}

// Pushes the sphere out of any penetrating triangle and removes velocity that
// points into the surface (collide-and-slide). Returns true if any contact
// had a mostly-upward normal (i.e. the sphere is standing on something).
static bool resolveSphereVsTriangles(glm::vec3& pos, glm::vec3& vel, float radius, const std::vector<Tri>& tris) {
    bool grounded = false;
    const int ITERATIONS = 4;
    for (int iter = 0; iter < ITERATIONS; iter++) {
        for (const Tri& t : tris) {
            glm::vec3 cp = closestPointOnTriangle(pos, t.a, t.b, t.c);
            glm::vec3 diff = pos - cp;
            float dist = glm::length(diff);
            if (dist < radius && dist > 1e-6f) {
                glm::vec3 n = diff / dist;
                float penetration = radius - dist;
                pos += n * penetration;

                float vn = glm::dot(vel, n);
                if (vn < 0.0f) vel -= n * vn;

                if (n.y > 0.55f) grounded = true;
            }
        }
    }
    return grounded;
}

// Möller–Trumbore ray-triangle intersection. dir need not be normalized;
// outT is the hit distance in units of dir's length.
static bool rayTriangleIntersect(const glm::vec3& orig, const glm::vec3& dir, const Tri& t, float& outT) {
    const float EPS = 1e-6f;
    glm::vec3 e1 = t.b - t.a, e2 = t.c - t.a;
    glm::vec3 pvec = glm::cross(dir, e2);
    float det = glm::dot(e1, pvec);
    if (fabsf(det) < EPS) return false;

    float invDet = 1.0f / det;
    glm::vec3 tvec = orig - t.a;
    float u = glm::dot(tvec, pvec) * invDet;
    if (u < 0.0f || u > 1.0f) return false;

    glm::vec3 qvec = glm::cross(tvec, e1);
    float v = glm::dot(dir, qvec) * invDet;
    if (v < 0.0f || u + v > 1.0f) return false;

    float tt = glm::dot(e2, qvec) * invDet;
    if (tt < EPS) return false;

    outT = tt;
    return true;
}

// Casts from lookTarget toward the ideal camera position and pulls the
// distance in to the nearest obstruction (props + ground plane) so the
// chase camera never clips through level geometry.
static float cameraUnobstructedDistance(const glm::vec3& lookTarget, const glm::vec3& dir, float desiredDist, const std::vector<Tri>& tris) {
    float maxDist = desiredDist;
    for (const Tri& t : tris) {
        float hitT;
        if (rayTriangleIntersect(lookTarget, dir, t, hitT) && hitT < maxDist) {
            maxDist = hitT;
        }
    }
    // ground plane at y = 0
    if (dir.y < -1e-5f) {
        float tPlane = -lookTarget.y / dir.y;
        if (tPlane > 0.0f && tPlane < maxDist) maxDist = tPlane;
    }
    return maxDist;
}

enum class AppState { TITLE, PLAYING, BLOCK_EDITOR };
enum class WorldPreset { PLAYGROUND, HILLS, HILLS_PLUS };

// ---------------------------------------------------------------------------
// Editable level "parts" (Roblox-Studio-style) — movable/rotatable boxes with
// a mesh, size, and color. Position and rotation are what the move/rotate
// tools edit; collision triangles are rebuilt from these each frame.
// ---------------------------------------------------------------------------
struct Part {
    Mesh* mesh;
    glm::vec3 position;
    glm::vec3 size;
    glm::vec3 color;
    glm::quat rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f); // identity

    // Translate+rotate only (no scale) — this is the frame collision/picking
    // math is built in: buildBoxTriangles and rayHitsPart both take `size`
    // directly and apply it themselves, so this must stay scale-free.
    glm::mat4 modelMatrix() const {
        return glm::translate(glm::mat4(1.0f), position) * glm::mat4_cast(rotation);
    }

    // What actually gets drawn: same transform plus a scale by `size`, since the
    // render mesh is a fixed unit cube — this is the only place `size` should
    // resize what you see.
    glm::mat4 renderModelMatrix() const {
        return modelMatrix() * glm::scale(glm::mat4(1.0f), size);
    }
};

// The native File menu / Cmd+S / Cmd+O handlers (see NativeMenu.mm) call plain
// function pointers, so they can't carry state as arguments — these are set
// once at startup instead.
static std::vector<Part>* g_partsForSave = nullptr;
static Mesh* g_partMeshForLoad = nullptr;   // the shared unit-cube mesh new parts should point at
static int* g_selectedPartForLoad = nullptr; // cleared on load since indices may no longer match
static int* g_dragAxisForLoad = nullptr;
static std::vector<BlockInstance>* g_blockScriptForSave = nullptr;
// Loading a level while on the title screen should drop straight into play — but
// only from the title screen; loading mid-game (e.g. Cmd+O while already playing)
// should just swap the level's parts without resetting the player/camera. Reading
// AppState here (rather than only handling this in the title screen's own click
// code) is what makes the native File > Open menu item behave the same way as the
// title screen's own Open button, since both end up calling this function.
static const AppState* g_appStateForLoad = nullptr;
static std::function<void()> g_enterPlayModeFn;
// Saved levels don't carry a ground preset yet (only `parts`), so loading one
// always lands on the flat playground ground it was designed against, even if
// the title screen's Hills preset was active beforehand.
static WorldPreset* g_worldPresetForLoad = nullptr;

// File > New / Cmd+N: unlike Open, this always succeeds locally (no dialog, no
// possible cancel), so it can just unconditionally reset to the Playground preset
// and (re)enter play — from the title screen or mid-game alike.
static std::function<void()> g_newSceneFn;
static void triggerNewScene() {
    if (g_newSceneFn) g_newSceneFn();
}

// Plain-text .retrobitl format: a header, a part count, then one line per part
// (position, size, color, rotation as a quaternion). Deliberately simple/human-
// readable over compact, since there's no tooling to inspect a binary format yet.
static void saveLevelToFile(const char* path) {
    if (!g_partsForSave) return;
    std::ofstream out(path);
    if (!out.is_open()) {
        std::cerr << "Failed to save level to " << path << "\n";
        return;
    }
    out << "RETROBITLEVEL 3\n";
    out << g_partsForSave->size() << "\n";
    for (const Part& p : *g_partsForSave) {
        out << p.position.x << " " << p.position.y << " " << p.position.z << " "
            << p.size.x << " " << p.size.y << " " << p.size.z << " "
            << p.color.x << " " << p.color.y << " " << p.color.z << " "
            << p.rotation.w << " " << p.rotation.x << " " << p.rotation.y << " " << p.rotation.z << "\n";
    }
    // Block-coding script (v2+): each line is a block type index (into BLOCK_DEFS),
    // its 3 params, and (v3+) an indent depth for REPEAT nesting. Guarded by the
    // version bump above so older files (no script section at all, or a v2 script
    // section with no depth column) still load fine — see loadLevelFromFile.
    size_t scriptSize = g_blockScriptForSave ? g_blockScriptForSave->size() : 0;
    out << scriptSize << "\n";
    if (g_blockScriptForSave) {
        for (const BlockInstance& b : *g_blockScriptForSave) {
            out << (int)b.type << " " << b.params[0] << " " << b.params[1] << " " << b.params[2] << " " << b.depth << "\n";
        }
    }
    std::cout << "Saved level to " << path << " (" << g_partsForSave->size() << " parts, "
               << scriptSize << " blocks)\n";
}

// Reads back what saveLevelToFile wrote. Replaces the live parts vector in
// place (rather than swapping the pointer) so g_partsForSave and any other
// references to it stay valid; clears the tool's current selection since part
// indices may no longer mean the same thing.
static void loadLevelFromFile(const char* path) {
    if (!g_partsForSave || !g_partMeshForLoad) return;
    std::ifstream in(path);
    if (!in.is_open()) {
        std::cerr << "Failed to open level file " << path << "\n";
        return;
    }

    std::string header;
    int version = 0;
    in >> header >> version;
    if (header != "RETROBITLEVEL") {
        std::cerr << path << " doesn't look like a .retrobitl file\n";
        return;
    }

    size_t count = 0;
    in >> count;

    std::vector<Part> loaded;
    loaded.reserve(count);
    for (size_t i = 0; i < count; i++) {
        Part p;
        p.mesh = g_partMeshForLoad;
        in >> p.position.x >> p.position.y >> p.position.z
           >> p.size.x >> p.size.y >> p.size.z
           >> p.color.x >> p.color.y >> p.color.z
           >> p.rotation.w >> p.rotation.x >> p.rotation.y >> p.rotation.z;
        if (!in) {
            std::cerr << "Malformed level file " << path << " (part " << i << ")\n";
            return;
        }
        loaded.push_back(p);
    }

    *g_partsForSave = std::move(loaded);
    if (g_selectedPartForLoad) *g_selectedPartForLoad = -1;
    if (g_dragAxisForLoad) *g_dragAxisForLoad = -1;
    if (g_worldPresetForLoad) *g_worldPresetForLoad = WorldPreset::PLAYGROUND;

    // Block script section only exists in version 2+ — a version-1 file simply has
    // nothing left to read here, so an existing block script is just cleared. The
    // depth column (for REPEAT nesting) only exists in version 3+; a v2 file's blocks
    // all load at depth 0 (which is exactly what they meant, since v2 predates REPEAT).
    size_t scriptCount = 0;
    if (version >= 2 && g_blockScriptForSave) {
        in >> scriptCount;
        std::vector<BlockInstance> loadedScript;
        loadedScript.reserve(scriptCount);
        for (size_t i = 0; i < scriptCount; i++) {
            int typeInt = 0;
            BlockInstance b;
            b.depth = 0;
            in >> typeInt >> b.params[0] >> b.params[1] >> b.params[2];
            if (version >= 3) in >> b.depth;
            if (!in || typeInt < 0 || typeInt >= BLOCK_DEF_COUNT) {
                std::cerr << "Malformed level file " << path << " (block " << i << ")\n";
                return;
            }
            b.type = (BlockType)typeInt;
            loadedScript.push_back(b);
        }
        *g_blockScriptForSave = std::move(loadedScript);
    } else if (g_blockScriptForSave) {
        g_blockScriptForSave->clear();
    }

    std::cout << "Loaded level from " << path << " (" << g_partsForSave->size() << " parts, "
               << scriptCount << " blocks)\n";

    if (g_appStateForLoad && *g_appStateForLoad == AppState::TITLE && g_enterPlayModeFn) {
        g_enterPlayModeFn();
    }
}

// Ray vs a part's oriented box, tested in the part's local space (rigid
// transform, so the hit distance is the same in local and world units).
static bool rayHitsPart(const glm::vec3& orig, const glm::vec3& dir, const Part& part, float& outT) {
    glm::mat4 inv = glm::inverse(part.modelMatrix());
    glm::vec3 lo = glm::vec3(inv * glm::vec4(orig, 1.0f));
    glm::vec3 ld = glm::vec3(inv * glm::vec4(dir, 0.0f));
    glm::vec3 h = part.size * 0.5f;

    float tMin = 0.0f, tMax = 1e9f;
    for (int a = 0; a < 3; a++) {
        float o = lo[a], d = ld[a], lim = h[a];
        if (fabsf(d) < 1e-8f) {
            if (o < -lim || o > lim) return false;
        } else {
            float t1 = (-lim - o) / d, t2 = (lim - o) / d;
            if (t1 > t2) std::swap(t1, t2);
            tMin = glm::max(tMin, t1);
            tMax = glm::min(tMax, t2);
            if (tMin > tMax) return false;
        }
    }
    outT = tMin;
    return true;
}

// Closest point on the line (linePoint + t*lineDir) to the ray (rayOrig + s*rayDir),
// returned as t along the line. Used to drag a part along a single gizmo axis.
static float closestParamOnLineToRay(const glm::vec3& rayOrig, const glm::vec3& rayDir, const glm::vec3& linePoint, const glm::vec3& lineDir) {
    float b = glm::dot(rayDir, lineDir);
    glm::vec3 w0 = linePoint - rayOrig;
    float d = glm::dot(rayDir, w0);
    float e = glm::dot(lineDir, w0);
    float denom = 1.0f - b * b;
    if (fabsf(denom) < 1e-6f) return 0.0f; // ray parallel to axis
    return (b * d - e) / denom;
}

// Ray vs plane (point + normal). Returns false if the ray is parallel to the plane.
static bool rayPlaneIntersect(const glm::vec3& orig, const glm::vec3& dir, const glm::vec3& planePoint, const glm::vec3& normal, float& outT) {
    float denom = glm::dot(dir, normal);
    if (fabsf(denom) < 1e-6f) return false;
    outT = glm::dot(planePoint - orig, normal) / denom;
    return true;
}

// Orthonormal in-plane basis (u, v) for the rotation ring around the given world
// axis, chosen so that u x v == the axis direction (keeps rotation sign consistent
// with glm::angleAxis(angle, axis)).
static void ringBasis(int axis, glm::vec3& u, glm::vec3& v) {
    if (axis == 0)      { u = glm::vec3(0, 1, 0); v = glm::vec3(0, 0, 1); }
    else if (axis == 1) { u = glm::vec3(0, 0, 1); v = glm::vec3(1, 0, 0); }
    else                 { u = glm::vec3(1, 0, 0); v = glm::vec3(0, 1, 0); }
}

enum class GizmoMode { MOVE, ROTATE, SCALE };

// ---------------------------------------------------------------------------
// Globals for input handling
// ---------------------------------------------------------------------------
static float g_yaw = -90.0f;   // camera orbit yaw (degrees)
static float g_pitch = -20.0f; // camera orbit pitch
static bool g_firstMouse = true;
static double g_lastX = 0, g_lastY = 0;
static bool g_mouseCaptured = true;
static bool g_editMode = false;
static GizmoMode g_gizmoMode = GizmoMode::MOVE;
static bool g_spawnMenuOpen = false; // edit-mode only, toggled with M
// Play-mode-only flight for the actual player (distinct from edit mode's freecam,
// which already flies) — toggled with F. Full 3D WASD + Space/Shift, gravity off,
// but still collides with the world (not noclip).
static bool g_flightMode = false;
// U-turn (U, flight mode only): snaps g_yaw 180 degrees and flags the main loop to
// reverse playerVel to match — set here since keyCallback can freely touch g_yaw
// (a global) but not playerVel (main()-local), same split as g_toggleBlockEditorRequested.
static bool g_uTurnRequested = false;
// AppState is a main()-local variable (like the rest of the app's per-run state),
// so keyCallback can't switch it directly — same problem g_appStateForLoad/
// g_enterPlayModeFn solve for Open. B just raises this flag; the main loop consumes
// it once per frame and does the actual transition (see enterBlockEditorMode).
static bool g_toggleBlockEditorRequested = false;

void mouseCallback(GLFWwindow* window, double xpos, double ypos) {
    // Camera look tracks mouse movement in both play and edit mode — even with the
    // cursor free for clicking/dragging parts, moving it also turns the camera.
    // Flight mode is the exception: steering is entirely W/S/A/D (see the input
    // block in main()'s loop), so mouse look is disabled there — still track
    // g_lastX/Y though, so look doesn't jump the moment flight mode ends.
    if (g_firstMouse) { g_lastX = xpos; g_lastY = ypos; g_firstMouse = false; }
    float dx = (float)(xpos - g_lastX);
    float dy = (float)(g_lastY - ypos);
    g_lastX = xpos; g_lastY = ypos;

    if (g_flightMode) return;

    float sensitivity = 0.12f;
    g_yaw += dx * sensitivity;
    g_pitch += dy * sensitivity;
    g_pitch = glm::clamp(g_pitch, -60.0f, 70.0f);
}

// GLFW's virtual cursor position can drift while GLFW_CURSOR_DISABLED and doesn't
// reliably re-sync when switching back to NORMAL — re-center it so the very first
// click after unlocking casts a ray from a sane position. Shared by Esc and M.
static void unlockMouseForClicking(GLFWwindow* window) {
    g_mouseCaptured = false;
    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
    g_firstMouse = true;
    int w, h;
    glfwGetWindowSize(window, &w, &h);
    glfwSetCursorPos(window, w * 0.5, h * 0.5);
    g_lastX = w * 0.5;
    g_lastY = h * 0.5;
}

void keyCallback(GLFWwindow* window, int key, int scancode, int action, int mods) {
    if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS) {
        // In edit mode, Esc just toggles the mouse lock (locked = look/fly around,
        // unlocked = click to pick/drag parts) without leaving the move tool.
        // Outside edit mode it's the plain mouse-capture toggle it always was.
        if (g_mouseCaptured) {
            unlockMouseForClicking(window);
        } else {
            g_mouseCaptured = true;
            glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
            g_firstMouse = true;
        }
    }
    if (key == GLFW_KEY_SLASH && action == GLFW_PRESS) {
        g_editMode = !g_editMode;
        g_spawnMenuOpen = false;
        g_mouseCaptured = true; // edit mode starts locked (freecam look/fly); Esc unlocks to pick
        glfwSetInputMode(window, GLFW_CURSOR, g_mouseCaptured ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
        g_firstMouse = true;
    }
    if (key == GLFW_KEY_R && action == GLFW_PRESS && g_editMode) {
        g_gizmoMode = (g_gizmoMode == GizmoMode::ROTATE) ? GizmoMode::MOVE : GizmoMode::ROTATE;
    }
    if (key == GLFW_KEY_T && action == GLFW_PRESS && g_editMode) {
        g_gizmoMode = (g_gizmoMode == GizmoMode::SCALE) ? GizmoMode::MOVE : GizmoMode::SCALE;
    }
    if (key == GLFW_KEY_M && action == GLFW_PRESS && g_editMode) {
        g_spawnMenuOpen = !g_spawnMenuOpen;
        if (g_spawnMenuOpen && g_mouseCaptured) unlockMouseForClicking(window); // so it can be clicked immediately
    }
    if (key == GLFW_KEY_B && action == GLFW_PRESS && g_editMode) {
        g_toggleBlockEditorRequested = true;
    }
    if (key == GLFW_KEY_F && action == GLFW_PRESS && !g_editMode) {
        g_flightMode = !g_flightMode; // edit mode already has its own freecam flight
    }
    if (key == GLFW_KEY_U && action == GLFW_PRESS && g_flightMode) {
        g_yaw += 180.0f;
        if (g_yaw > 180.0f) g_yaw -= 360.0f;
        g_uTurnRequested = true;
    }
#ifdef __APPLE__
    bool fileModifierDown = (mods & GLFW_MOD_SUPER) != 0;   // Cmd on macOS
#else
    bool fileModifierDown = (mods & GLFW_MOD_CONTROL) != 0; // Ctrl elsewhere
#endif
    if (key == GLFW_KEY_N && action == GLFW_PRESS && fileModifierDown) {
        TriggerNew(); // no-op on platforms without a menu bar yet (see src/platform/)
    }
    if (key == GLFW_KEY_S && action == GLFW_PRESS && fileModifierDown) {
        TriggerSaveDialog(); // no-op on platforms without a native dialog yet (see src/platform/)
    }
    if (key == GLFW_KEY_O && action == GLFW_PRESS && fileModifierDown) {
        TriggerOpenDialog(); // no-op on platforms without a native dialog yet (see src/platform/)
    }
}

int main() {
    if (!glfwInit()) {
        std::cerr << "Failed to init GLFW\n";
        return -1;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);

    const int WIDTH = 1280, HEIGHT = 800;
    GLFWwindow* window = glfwCreateWindow(WIDTH, HEIGHT, "RETRObit Engine - Playground", nullptr, nullptr);
    if (!window) {
        std::cerr << "Failed to create window\n";
        glfwTerminate();
        return -1;
    }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);
    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
    glfwSetCursorPosCallback(window, mouseCallback);
    glfwSetKeyCallback(window, keyCallback);

    {
        int fbWidth, fbHeight;
        glfwGetFramebufferSize(window, &fbWidth, &fbHeight);
        glViewport(0, 0, fbWidth, fbHeight);
    }

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);

    Shader shader("shaders/basic.vert", "shaders/basic.frag");
    // Dedicated shader for 2D UI (title screen, spawn menu, block editor): draws a
    // unit quad as a rounded rectangle, with an optional puzzle-connector notch, via
    // signed-distance-field math in the fragment shader — the only way to get real
    // curves/notches out of a quad-only renderer without adding an image-loading
    // dependency for pre-made block sprites.
    Shader uiShader("shaders/ui.vert", "shaders/ui.frag");

    // Ground: used to be a GRID x GRID loop of individual tile boxes (bounded, only
    // ever covered a fixed area — walking past its edge dropped you into the void).
    // Now it's one huge single plane with the checkerboard painted per-fragment in
    // basic.frag from world position (see uUseCheckerboard/uColorB/uCheckerSize), so
    // it reads as an infinite ground without drawing thousands of tile meshes. TILE
    // still sets the checker square size (matches the old tile spacing); GROUND_HALF
    // just needs to be "far enough that the edge is never reached in practice", not
    // a real bound — collision below no longer clamps to any extent either.
    const float TILE = 4.0f;
    const float GROUND_HALF = 4000.0f;
    Mesh groundPlane = makeBox(GROUND_HALF * 2.0f, 0.5f, GROUND_HALF * 2.0f);
    // Real collision geometry for the ground plane above (just 12 triangles — a box's
    // triangle count doesn't grow with its size), merged into propTriangles below only
    // for the Playground preset. Needed because flight mode only ever collided against
    // propTriangles (parts + hills) via resolveSphereVsTriangles, never the analytic
    // y=0 plane (that only runs in the non-flight branch) — so flying low over
    // Playground used to sink straight through the ground with no collision response
    // at all. The plane was thin/mostly invisible as a tile grid, so it went unnoticed;
    // now that the ground is one big rendered box, the clip-through is obvious.
    std::vector<Tri> groundPlaneTriangles = buildBoxTriangles(
        glm::vec3(GROUND_HALF * 2.0f, 0.5f, GROUND_HALF * 2.0f),
        glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, -0.25f, 0.0f)));

    // The actual Sonic-Xtreme signature move: a curved loop, Playground-only. Sits
    // tangent to the ground (see makeLoopTrack) a short run from spawn (SPAWN_POS is
    // the origin) along +X, so running straight ahead carries you right onto it.
    // Radius/width/run-speed haven't been playtested yet — collide-and-slide against
    // this ribbon should carry you around as long as you're moving fast enough when
    // you hit the bottom, same principle as a marble staying in a curved pipe, but the
    // exact numbers (RUN_SPEED = 9, GRAVITY = -28) that make that actually work in
    // practice are unverified.
    const float LOOP_CENTER_X = 30.0f;
    const float LOOP_CENTER_Z = 0.0f;
    const float LOOP_RADIUS = 7.0f;
    const float LOOP_HALF_WIDTH = 3.5f;
    const int LOOP_SEGMENTS = 48;
    std::vector<Tri> loopTriangles;
    Mesh loopMesh = makeLoopTrack(LOOP_CENTER_X, LOOP_CENTER_Z, LOOP_RADIUS, LOOP_HALF_WIDTH, LOOP_SEGMENTS, loopTriangles);

    // Alternate world preset: rolling hills terrain, built once at startup
    // (see makeHillTerrain) rather than regenerated per preset switch. Its
    // triangles feed the same collide-and-slide resolver used for props.
    const float HILL_HALF_SIZE = 60.0f;
    const int HILL_SEGMENTS = 48;
    const float HILL_AMPLITUDE = 3.0f;
    const float HILL_FREQUENCY = 0.12f;
    std::vector<Tri> hillTriangles;
    Mesh hillMesh = makeHillTerrain(HILL_HALF_SIZE, HILL_SEGMENTS, HILL_AMPLITUDE, HILL_FREQUENCY, hillTriangles);

    // Hills+: a bigger version of the above (roughly 4x the area) for more open-air
    // flight-mode room to actually use boost/turning/arena-boundary range without
    // hitting the edge in a few seconds. Segment count scales with half-size to keep
    // roughly the same per-tile resolution rather than stretching the same 48
    // segments over 4x the area (which would make the terrain blocky). Frequency is
    // halved so hills still read as similarly-sized bumps rather than shrinking
    // relative to the bigger map.
    const float HILL_PLUS_HALF_SIZE = 130.0f;
    const int HILL_PLUS_SEGMENTS = 104;
    const float HILL_PLUS_AMPLITUDE = 4.0f;
    const float HILL_PLUS_FREQUENCY = 0.06f;
    std::vector<Tri> hillPlusTriangles;
    Mesh hillPlusMesh = makeHillTerrain(HILL_PLUS_HALF_SIZE, HILL_PLUS_SEGMENTS, HILL_PLUS_AMPLITUDE, HILL_PLUS_FREQUENCY, hillPlusTriangles);

    // Movable parts (Roblox-Studio-style): "/" toggles the move tool that lets
    // you pick one of these and drag ("R" rotates, "T" resizes) via the gizmo.
    // The render mesh is a shared unit cube (or sphere) — actual dimensions always
    // come from Part::size via renderModelMatrix(), so resizing changes what you
    // see, not just the (separately tracked) collision box. Note collision is
    // always an oriented BOX regardless of visual mesh (buildBoxTriangles/
    // rayHitsPart both work purely off Part::size) — a spawned sphere looks round
    // but collides like a box. Fine for a first pass; a true sphere collider would
    // need its own resolver.
    Mesh partUnitBox = makeBox(1.0f, 1.0f, 1.0f);
    Mesh partUnitSphere = makeUVSphere(0.5f, 12, 16); // radius 0.5 so default size=(1,1,1) matches the box's footprint
    std::vector<Part> parts;
    parts.push_back(Part{ &partUnitBox, glm::vec3(14.0f, 0.7f, 0.0f), glm::vec3(6.0f, 1.0f, 10.0f), glm::vec3(0.9f, 0.5f, 0.15f),
        glm::angleAxis(glm::radians(12.0f), glm::vec3(1, 0, 0)) });
    parts.push_back(Part{ &partUnitBox, glm::vec3(-16.0f, 3.0f, -10.0f), glm::vec3(3.0f, 6.0f, 3.0f), glm::vec3(0.85f, 0.2f, 0.35f) });

    g_partsForSave = &parts;
    g_partMeshForLoad = &partUnitBox;

    // Gizmo arrow: unit box, scaled non-uniformly per axis and offset from the
    // part's center so it reads as an elongated handle along that axis.
    Mesh gizmoArrow = makeBox(1.0f, 1.0f, 1.0f);
    const float GIZMO_ARM = 2.5f;
    const float GIZMO_THICK = 0.15f;
    const float GIZMO_GRAB_RADIUS = 0.7f;

    // Rotate-tool rings: one per world axis, world-aligned (not part-local).
    RingMesh ringX = makeRing(GIZMO_ARM, 48, 0);
    RingMesh ringY = makeRing(GIZMO_ARM, 48, 1);
    RingMesh ringZ = makeRing(GIZMO_ARM, 48, 2);
    RingMesh* ringMeshes[3] = { &ringX, &ringY, &ringZ };
    const float ROT_GRAB_TOLERANCE = 0.35f;

    // Scale-tool handles: small cubes sitting on each local +axis face. Local
    // (not world) axes, since size is defined in the part's own space — dragging
    // a handle keeps the opposite face fixed and grows/shrinks toward the handle,
    // same as Roblox's face-resize behavior.
    Mesh scaleHandleMesh = makeBox(1.0f, 1.0f, 1.0f);
    const float SCALE_HANDLE_SIZE = 0.35f;
    const float SCALE_GRAB_TOLERANCE = 0.5f;
    const float SCALE_MIN_SIZE = 0.3f;

    int selectedPart = -1;
    int dragAxis = -1;          // 0=X, 1=Y, 2=Z, -1 = not dragging (shared by all three tools)
    g_selectedPartForLoad = &selectedPart;
    g_dragAxisForLoad = &dragAxis;

    // Block-coding editor (v1): a flat vertical stack of blocks, edited on its own
    // full-screen AppState and executed step-by-step once Run is pressed. See
    // include/BlockScript.h for the data model.
    std::vector<BlockInstance> blockScript;
    bool scriptRunning = false;
    int runningStep = -1;
    float waitTimer = 0.0f;
    std::vector<LoopFrame> loopStack; // innermost active REPEAT is .back()
    int blockDragFromPalette = -1;   // palette index currently being dragged, or -1
    int blockDragFromWorkspace = -1; // workspace index currently being dragged, or -1
    bool blockLeftMouseWasDown = false;
    bool blockEscWasDown = false;
    g_blockScriptForSave = &blockScript;

    SetupNativeFileMenu(&triggerNewScene, &saveLevelToFile, &loadLevelFromFile); // no-op on platforms without a menu bar yet (see src/platform/)
    glm::vec3 dragStartPos(0.0f);
    float dragTcInitial = 0.0f;
    float rotDragStartAngle = 0.0f;
    glm::quat rotDragStartRotation(1.0f, 0.0f, 0.0f, 0.0f);
    glm::vec3 scaleAnchor(0.0f);      // fixed opposite-face point for the axis being resized
    float scaleDragStartSize = 0.0f;
    float scaleDragTcInitial = 0.0f;
    bool leftMouseWasDown = false;
    GizmoMode lastGizmoMode = g_gizmoMode;
    bool spawnKey1WasDown = false, spawnKey2WasDown = false; // shape shortcuts in the spawn menu

    // Spawn-menu button rects (M to open) — two shapes stacked near the top of the
    // screen so they don't overlap the move/rotate/scale gizmo below the cursor.
    const float SPAWN_BTN_X0 = WIDTH * 0.5f - 110.0f, SPAWN_BTN_X1 = WIDTH * 0.5f + 110.0f;
    const float SPAWN_BOX_BTN_Y0 = 80.0f, SPAWN_BOX_BTN_Y1 = 130.0f;
    const float SPAWN_SPHERE_BTN_Y0 = 145.0f, SPAWN_SPHERE_BTN_Y1 = 195.0f;

    // Player: a sphere standing in for Sonic's silhouette
    Mesh player = makeUVSphere(1.0f, 16, 24);
    const float PLAYER_RADIUS = 1.0f;

    const glm::vec3 SPAWN_POS(0.0f, 1.0f, 0.0f);
    const float VOID_Y = -20.0f; // fall this far below the level and you respawn
    glm::vec3 playerPos = SPAWN_POS;
    glm::vec3 playerVel(0.0f);
    bool onGround = true;
    float playerFacing = 0.0f; // yaw in radians, degrees converted

    double lastTime = glfwGetTime();
    bool spaceWasDown = false;
    bool wasFlightMode = false; // zero playerVel on transition so toggling F doesn't carry over odd velocity

    // --- Flight mode extras, StarFox 64 (All-Range Mode)-inspired ---
    // Boost (E) / brake (Q): flight speed isn't fixed anymore, it lerps between a
    // braked floor and a boosted ceiling based on which is held.
    float flightSpeedMul = 1.0f;
    // Camera roll: banks into left/right turning (A/D) like the Arwing tilting into
    // a turn, purely a camera-space rotation around camFront — never touches
    // playerVel/collision, so it can't desync movement from what's on screen.
    float camRoll = 0.0f;
    // Barrel roll: double-tap A or D within BARREL_ROLL_TAP_WINDOW triggers a full
    // cosmetic 360 roll over BARREL_ROLL_DURATION seconds, on top of/overriding the
    // steady-state bank above while it plays.
    bool aWasDownFlight = false, dWasDownFlight = false;
    double lastATapTime = -1000.0, lastDTapTime = -1000.0;
    float barrelRollTimer = 0.0f;   // counts down from BARREL_ROLL_DURATION while active
    float barrelRollDir = 0.0f;     // +1 (rolling right) or -1 (rolling left)
    const float BARREL_ROLL_TAP_WINDOW = 0.3f;
    const float BARREL_ROLL_DURATION = 0.5f;
    // Arena boundary: All-Range Mode isn't infinite freeflight — stray too far from
    // the spawn point and you get pushed back, same as SF64's play-area walls.
    // Scaled per-preset so the bigger Hills+ terrain actually has room to fly around
    // in rather than hitting the same boundary a much smaller map would use.
    const float FLIGHT_ARENA_RADIUS = 160.0f;
    const float FLIGHT_ARENA_RADIUS_HILLS_PLUS = 340.0f;

    glm::vec3 freeCamPos(0.0f);
    glm::vec3 initCamFront(
        cosf(glm::radians(g_yaw)) * cosf(glm::radians(g_pitch)),
        sinf(glm::radians(g_pitch)),
        sinf(glm::radians(g_yaw)) * cosf(glm::radians(g_pitch)));
    glm::vec3 camPos = playerPos + glm::vec3(0, 1.0f, 0) - glm::normalize(initCamFront) * 10.0f + glm::vec3(0, 3.0f, 0);
    bool wasEditMode = false;

    double fpsTimer = lastTime;
    int fpsFrameCount = 0;
    double fpsDisplay = 0.0;
    char titleBuf[192];

    // --- Title screen: pick a world preset (Playground / Hills) or Open a saved
    // level. No on-screen text renderer, so buttons are plain colored rectangles
    // (ortho overlay) labeled with the FONT_5X7 bitmap font, plus keyboard
    // shortcuts (P / H / O) as the accessible/discoverable fallback. ---
    Mesh uiQuad = makeQuad();

    // Shared 2D-overlay primitives (ortho-projected quads) — used by the title
    // screen above and the edit-mode spawn menu below, so they're defined once
    // here rather than duplicated per screen. Both assume the caller has already
    // set up an ortho uView/uProj and disabled depth test + face culling (see
    // either call site for the exact setup).
    auto drawButton = [&](float x0, float y0, float x1, float y1, glm::vec3 color, bool hovered) {
        glm::vec3 c = hovered ? glm::min(color * 1.3f + glm::vec3(0.1f), glm::vec3(1.0f)) : color;
        glm::mat4 model = glm::translate(glm::mat4(1.0f), glm::vec3((x0 + x1) * 0.5f, (y0 + y1) * 0.5f, 0.0f));
        model = glm::scale(model, glm::vec3(x1 - x0, y1 - y0, 1.0f));
        // Re-bind explicitly rather than assuming it's still current — drawRoundedRect/
        // drawBlockShape switch the active program to uiShader, and since glUniform*
        // targets whichever program is currently bound (Shader::setMat4 etc. don't call
        // use() themselves), interleaving them with drawText/drawButton without this
        // would silently set uModel/uColor on the wrong program.
        shader.use();
        shader.setMat4("uModel", model);
        shader.setVec3("uColor", c);
        uiQuad.draw();
    };

    // drawText only takes a CENTER point, not a left edge — callers that need text
    // to start at a specific X (so it doesn't overflow past a block's edge, the bug
    // that caused visible text overlap/ghosting between stacked/adjacent blocks)
    // need to know the width in advance to compute centerX = leftEdge + width/2.
    auto textWidthPx = [&](const std::string& text, float pixelSize) {
        if (text.empty()) return 0.0f;
        int totalCols = 0;
        for (size_t i = 0; i < text.size(); i++) {
            totalCols += 5;
            if (i + 1 < text.size()) totalCols += 1;
        }
        return totalCols * pixelSize;
    };

    // Text is drawn as a grid of small quads from FONT_5X7, built on drawButton
    // above — crude, but it's zero extra dependencies and there's no
    // texture/font-loading infrastructure yet.
    auto drawText = [&](const std::string& text, float centerX, float centerY, float pixelSize, glm::vec3 color) {
        int totalCols = 0;
        for (size_t i = 0; i < text.size(); i++) {
            totalCols += 5;
            if (i + 1 < text.size()) totalCols += 1; // 1-column gap between glyphs
        }
        float totalW = totalCols * pixelSize;
        float totalH = 7.0f * pixelSize;
        float startX = centerX - totalW * 0.5f;
        float startY = centerY - totalH * 0.5f;

        float cursorX = startX;
        for (char ch : text) {
            auto it = FONT_5X7.find(ch);
            if (it != FONT_5X7.end()) {
                const auto& rows = it->second;
                for (int row = 0; row < 7; row++) {
                    for (int col = 0; col < 5; col++) {
                        if (rows[row] & (1 << (4 - col))) {
                            float px0 = cursorX + col * pixelSize;
                            float py0 = startY + row * pixelSize;
                            drawButton(px0, py0, px0 + pixelSize, py0 + pixelSize, color, false);
                        }
                    }
                }
            }
            cursorX += 6.0f * pixelSize; // 5 wide + 1 space
        }
    };

    // Rounded-rect UI primitives (uiShader, SDF-based — see shaders/ui.frag) for
    // anything meant to read as a real button/block rather than flat chrome. Text
    // (drawText above) deliberately stays on the flat drawButton/shader path — tiny
    // per-pixel glyph quads would just look like blobs if each one were independently
    // rounded, and crisp font pixels aren't the part that needs to look "soft."
    // WIDTH/HEIGHT/the ortho projection are the same on every UI screen, so the
    // uProj/uView setup lives in here rather than depending on caller ordering.
    glm::mat4 uiOrtho = glm::ortho(0.0f, (float)WIDTH, (float)HEIGHT, 0.0f);
    auto drawRoundedRect = [&](float x0, float y0, float x1, float y1, glm::vec3 color, bool hovered, float radiusPx) {
        glm::vec3 c = hovered ? glm::min(color * 1.3f + glm::vec3(0.1f), glm::vec3(1.0f)) : color;
        glm::vec2 halfSize((x1 - x0) * 0.5f, (y1 - y0) * 0.5f);
        glm::mat4 model = glm::translate(glm::mat4(1.0f), glm::vec3((x0 + x1) * 0.5f, (y0 + y1) * 0.5f, 0.0f));
        model = glm::scale(model, glm::vec3(x1 - x0, y1 - y0, 1.0f));

        uiShader.use();
        uiShader.setMat4("uModel", model);
        uiShader.setMat4("uView", glm::mat4(1.0f));
        uiShader.setMat4("uProj", uiOrtho);
        uiShader.setVec3("uColor", c);
        uiShader.setVec2("uHalfSizePx", halfSize);
        uiShader.setFloat("uRadiusPx", glm::min(radiusPx, glm::min(halfSize.x, halfSize.y)));
        uiShader.setFloat("uHasTab", 0.0f);
        uiQuad.draw();
    };

    // Same as drawRoundedRect, but with a puzzle-connector notch straddling the top
    // edge — used specifically for block-editor palette entries and workspace blocks,
    // not generic buttons (steppers/Run/trash stay drawRoundedRect, no notch).
    auto drawBlockShape = [&](float x0, float y0, float x1, float y1, glm::vec3 color, bool hovered, bool hasTab) {
        glm::vec3 c = hovered ? glm::min(color * 1.3f + glm::vec3(0.1f), glm::vec3(1.0f)) : color;
        glm::vec2 halfSize((x1 - x0) * 0.5f, (y1 - y0) * 0.5f);
        glm::mat4 model = glm::translate(glm::mat4(1.0f), glm::vec3((x0 + x1) * 0.5f, (y0 + y1) * 0.5f, 0.0f));
        model = glm::scale(model, glm::vec3(x1 - x0, y1 - y0, 1.0f));

        uiShader.use();
        uiShader.setMat4("uModel", model);
        uiShader.setMat4("uView", glm::mat4(1.0f));
        uiShader.setMat4("uProj", uiOrtho);
        uiShader.setVec3("uColor", c);
        uiShader.setVec2("uHalfSizePx", halfSize);
        uiShader.setFloat("uRadiusPx", glm::min(14.0f, glm::min(halfSize.x, halfSize.y)));
        uiShader.setFloat("uHasTab", hasTab ? 1.0f : 0.0f);
        uiShader.setFloat("uTabOffsetFromLeftPx", 30.0f);
        uiQuad.draw();
    };

    AppState appState = AppState::TITLE;
    WorldPreset currentPreset = WorldPreset::PLAYGROUND;
    bool titleLeftMouseWasDown = false;
    bool titlePWasDown = false, titleHWasDown = false, titleOWasDown = false, titleJWasDown = false;

    const float BTN_X0 = WIDTH * 0.5f - 160.0f, BTN_X1 = WIDTH * 0.5f + 160.0f;
    const float PLAYGROUND_BTN_Y0 = HEIGHT * 0.5f - 125.0f, PLAYGROUND_BTN_Y1 = HEIGHT * 0.5f - 75.0f;
    const float HILLS_BTN_Y0 = HEIGHT * 0.5f - 60.0f, HILLS_BTN_Y1 = HEIGHT * 0.5f - 10.0f;
    const float HILLS_PLUS_BTN_Y0 = HEIGHT * 0.5f + 5.0f, HILLS_PLUS_BTN_Y1 = HEIGHT * 0.5f + 55.0f;
    const float OPEN_BTN_Y0 = HEIGHT * 0.5f + 70.0f, OPEN_BTN_Y1 = HEIGHT * 0.5f + 120.0f;

    // Title screen starts with the mouse free so the buttons can be clicked;
    // entering play mode below re-locks it the same way edit mode does.
    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
    g_mouseCaptured = false;
    g_firstMouse = true;

    auto resetToPlaygroundScene = [&]() {
        currentPreset = WorldPreset::PLAYGROUND;
        parts.clear();
        parts.push_back(Part{ &partUnitBox, glm::vec3(14.0f, 0.7f, 0.0f), glm::vec3(6.0f, 1.0f, 10.0f), glm::vec3(0.9f, 0.5f, 0.15f),
            glm::angleAxis(glm::radians(12.0f), glm::vec3(1, 0, 0)) });
        parts.push_back(Part{ &partUnitBox, glm::vec3(-16.0f, 3.0f, -10.0f), glm::vec3(3.0f, 6.0f, 3.0f), glm::vec3(0.85f, 0.2f, 0.35f) });
    };

    auto resetToHillsScene = [&]() {
        currentPreset = WorldPreset::HILLS;
        parts.clear(); // the terrain itself is the content; no boxes needed
    };

    auto resetToHillsPlusScene = [&]() {
        currentPreset = WorldPreset::HILLS_PLUS;
        parts.clear();
    };

    auto enterPlayMode = [&]() {
        playerPos = SPAWN_POS;
        playerVel = glm::vec3(0.0f);
        onGround = true;
        selectedPart = -1;
        dragAxis = -1;
        g_editMode = false;
        g_mouseCaptured = true;
        glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
        g_firstMouse = true;
        camPos = playerPos + glm::vec3(0, 1.0f, 0) - glm::normalize(initCamFront) * 10.0f + glm::vec3(0, 3.0f, 0);
        appState = AppState::PLAYING;
    };

    // Entering the block editor doesn't touch player/camera/parts state at all —
    // it's a separate full-screen UI, not a level reset. Only the mouse needs
    // unlocking (same idiom as the spawn menu) so the workspace is clickable.
    auto enterBlockEditorMode = [&]() {
        unlockMouseForClicking(window);
        appState = AppState::BLOCK_EDITOR;
    };

    // Returns to normal (edit-mode) play without resetting anything — re-locks the
    // mouse the same way enterPlayMode does, but doesn't touch player/camera/parts.
    auto exitBlockEditorMode = [&]() {
        g_mouseCaptured = true;
        glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
        g_firstMouse = true;
        appState = AppState::PLAYING;
    };

    g_appStateForLoad = &appState;
    g_enterPlayModeFn = enterPlayMode;
    g_worldPresetForLoad = &currentPreset;
    g_newSceneFn = [&]() { resetToPlaygroundScene(); enterPlayMode(); };

    while (!glfwWindowShouldClose(window)) {
        double now = glfwGetTime();
        float dt = (float)(now - lastTime);
        dt = std::min(dt, 0.05f); // clamp to avoid huge steps on hitches
        lastTime = now;

        fpsFrameCount++;
        if (now - fpsTimer >= 0.5) {
            fpsDisplay = fpsFrameCount / (now - fpsTimer);
            fpsTimer = now;
            fpsFrameCount = 0;
        }

        glfwPollEvents();
        if (glfwGetKey(window, GLFW_KEY_Q) == GLFW_PRESS) glfwSetWindowShouldClose(window, true);

        // B (see keyCallback) can't switch appState directly since it's a main()-local
        // variable — it just raises this flag, consumed once here.
        if (g_toggleBlockEditorRequested) {
            g_toggleBlockEditorRequested = false;
            if (appState == AppState::PLAYING) {
                enterBlockEditorMode();
            } else if (appState == AppState::BLOCK_EDITOR) {
                exitBlockEditorMode();
            }
        }

        if (appState == AppState::TITLE) {
            bool leftDown = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
            double mx, my;
            glfwGetCursorPos(window, &mx, &my);
            bool clicked = leftDown && !titleLeftMouseWasDown;
            titleLeftMouseWasDown = leftDown;

            bool pDown = glfwGetKey(window, GLFW_KEY_P) == GLFW_PRESS;
            bool hDown = glfwGetKey(window, GLFW_KEY_H) == GLFW_PRESS;
            bool jDown = glfwGetKey(window, GLFW_KEY_J) == GLFW_PRESS; // Hills+ — H is taken
            bool oDown = glfwGetKey(window, GLFW_KEY_O) == GLFW_PRESS;
            bool pPressed = pDown && !titlePWasDown;
            bool hPressed = hDown && !titleHWasDown;
            bool jPressed = jDown && !titleJWasDown;
            bool oPressed = oDown && !titleOWasDown;
            titlePWasDown = pDown;
            titleHWasDown = hDown;
            titleJWasDown = jDown;
            titleOWasDown = oDown;

            bool overPlayground = mx >= BTN_X0 && mx <= BTN_X1 && my >= PLAYGROUND_BTN_Y0 && my <= PLAYGROUND_BTN_Y1;
            bool overHills = mx >= BTN_X0 && mx <= BTN_X1 && my >= HILLS_BTN_Y0 && my <= HILLS_BTN_Y1;
            bool overHillsPlus = mx >= BTN_X0 && mx <= BTN_X1 && my >= HILLS_PLUS_BTN_Y0 && my <= HILLS_PLUS_BTN_Y1;
            bool overOpen = mx >= BTN_X0 && mx <= BTN_X1 && my >= OPEN_BTN_Y0 && my <= OPEN_BTN_Y1;

            if (pPressed || (clicked && overPlayground)) {
                resetToPlaygroundScene();
                enterPlayMode();
            } else if (hPressed || (clicked && overHills)) {
                resetToHillsScene();
                enterPlayMode();
            } else if (jPressed || (clicked && overHillsPlus)) {
                resetToHillsPlusScene();
                enterPlayMode();
            } else if (oPressed || (clicked && overOpen)) {
                // loadLevelFromFile calls enterPlayMode itself on success (since
                // g_appStateForLoad reads TITLE here) — same code path the native
                // File > Open menu item uses, so both behave identically. On
                // platforms without a real dialog yet (see src/platform/), the stub
                // returns false, so OPEN falls back to starting the playground preset
                // instead of silently doing nothing.
                if (!TriggerOpenDialog()) {
                    resetToPlaygroundScene();
                    enterPlayMode();
                }
            }

            snprintf(titleBuf, sizeof(titleBuf), "RETRObit Engine - Title Screen | P = Playground, H = Hills, J = Hills+, O = Open");
            glfwSetWindowTitle(window, titleBuf);

            glClearColor(0.08f, 0.08f, 0.12f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            glDisable(GL_DEPTH_TEST);
            // The top-left-origin ortho projection below (needed so pixel coords match
            // glfwGetCursorPos for hit-testing) has a negative Y scale to flip the axis,
            // which flips triangle winding too — without this, the quads get backface-culled.
            glDisable(GL_CULL_FACE);

            glm::mat4 uiProj = glm::ortho(0.0f, (float)WIDTH, (float)HEIGHT, 0.0f);
            glm::mat4 uiView(1.0f);
            shader.use();
            shader.setMat4("uView", uiView);
            shader.setMat4("uProj", uiProj);
            shader.setVec3("uViewPos", glm::vec3(WIDTH * 0.5f, HEIGHT * 0.5f, 100.0f));
            shader.setVec3("uSunDir", glm::vec3(0.0f, 0.0f, -1.0f)); // flat/full lighting on the quads
            shader.setVec3("uFogColor", glm::vec3(0.08f, 0.08f, 0.12f));
            shader.setFloat("uFogDensity", 0.0f); // no fog on a UI overlay

            drawRoundedRect(BTN_X0, PLAYGROUND_BTN_Y0, BTN_X1, PLAYGROUND_BTN_Y1, glm::vec3(0.25f, 0.75f, 0.35f), overPlayground, 14.0f);
            drawRoundedRect(BTN_X0, HILLS_BTN_Y0, BTN_X1, HILLS_BTN_Y1, glm::vec3(0.55f, 0.45f, 0.15f), overHills, 14.0f);
            drawRoundedRect(BTN_X0, HILLS_PLUS_BTN_Y0, BTN_X1, HILLS_PLUS_BTN_Y1, glm::vec3(0.45f, 0.55f, 0.15f), overHillsPlus, 14.0f);
            drawRoundedRect(BTN_X0, OPEN_BTN_Y0, BTN_X1, OPEN_BTN_Y1, glm::vec3(0.05f, 0.35f, 0.95f), overOpen, 14.0f);

            // pixelSize 4 (not 5, like the old single-button title screen) so
            // "PLAYGROUND" (10 letters) comfortably fits the 320px-wide button.
            glm::vec3 textColor(0.05f, 0.08f, 0.05f);
            float btnCenterX = (BTN_X0 + BTN_X1) * 0.5f;
            drawText("PLAYGROUND", btnCenterX, (PLAYGROUND_BTN_Y0 + PLAYGROUND_BTN_Y1) * 0.5f, 4.0f, textColor);
            drawText("HILLS", btnCenterX, (HILLS_BTN_Y0 + HILLS_BTN_Y1) * 0.5f, 4.0f, textColor);
            drawText("HILLS+", btnCenterX, (HILLS_PLUS_BTN_Y0 + HILLS_PLUS_BTN_Y1) * 0.5f, 4.0f, textColor);
            drawText("OPEN", btnCenterX, (OPEN_BTN_Y0 + OPEN_BTN_Y1) * 0.5f, 4.0f, textColor);

            glEnable(GL_DEPTH_TEST);
            glEnable(GL_CULL_FACE);
            glfwSwapBuffers(window);
            continue;
        }

        // --- Block-coding editor (v1): drag blocks from the palette onto a flat
        // vertical workspace stack, tweak params with +/- steppers (no free-text
        // entry — there's no text-input widget anywhere in the engine yet), press
        // RUN to execute it (switches to AppState::PLAYING; see the script-execution
        // section below the player-physics block). Esc returns without running.
        // Same full-screen-state-with-early-continue shape as AppState::TITLE. ---
        if (appState == AppState::BLOCK_EDITOR) {
            bool leftDown = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
            double mx, my;
            glfwGetCursorPos(window, &mx, &my);
            bool pressed = leftDown && !blockLeftMouseWasDown;
            bool released = !leftDown && blockLeftMouseWasDown;
            blockLeftMouseWasDown = leftDown;

            bool escDown = glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS;
            bool escPressed = escDown && !blockEscWasDown;
            blockEscWasDown = escDown;
            if (escPressed) {
                exitBlockEditorMode();
                continue;
            }

            // Scratch-style top bar the RUN button sits on, and light palette/workspace
            // panel backgrounds behind everything else — see the render section below
            // for the actual panel rects, kept here too since RUN's hit-test needs
            // TOP_BAR_H to place itself within the bar.
            const float TOP_BAR_H = 50.0f;
            const float RUN_BTN_X0 = WIDTH - 160.0f, RUN_BTN_X1 = WIDTH - 20.0f;
            const float RUN_BTN_Y0 = 8.0f, RUN_BTN_Y1 = TOP_BAR_H - 8.0f;
            const float TRASH_X0 = BlockLayout::WORKSPACE_X0;
            const float TRASH_X1 = BlockLayout::WORKSPACE_X0 + BlockLayout::WORKSPACE_BLOCK_W;
            const float TRASH_Y0 = HEIGHT - 70.0f, TRASH_Y1 = HEIGHT - 20.0f;

            bool overRun = mx >= RUN_BTN_X0 && mx <= RUN_BTN_X1 && my >= RUN_BTN_Y0 && my <= RUN_BTN_Y1;
            bool overTrash = mx >= TRASH_X0 && mx <= TRASH_X1 && my >= TRASH_Y0 && my <= TRASH_Y1;

            int paletteHover = hitTestPalette((float)mx, (float)my, BLOCK_DEF_COUNT);
            // Each block's Y position, accounting for the extra space a REPEAT's
            // C-shape closing bar reserves after its body — see the function's
            // comment. Computed once per frame; hit-testing, insertion, and
            // rendering all index into this instead of recomputing it separately.
            std::vector<float> rowY = computeWorkspaceRowY(blockScript);
            int workspaceHover = hitTestWorkspace((float)mx, (float)my, rowY);

            // Per-block param/indent layout, anchored to the FIXED right edge (x1) so
            // steppers and indent buttons line up across different indent depths —
            // only the left edge moves with depth (see workspaceBlockX0/X1's comment).
            // Shared by hit-testing below and rendering further down so they can't drift.
            float workspaceX1 = BlockLayout::workspaceBlockX1();
            float indentBtnsX1 = workspaceX1 - 4.0f;
            float indentBtnsX0 = indentBtnsX1 - 2.0f * BlockLayout::INDENT_BTN_W - 4.0f;
            float paramsRightEdge = indentBtnsX0 - 10.0f;
            // Per-param slot: [0, STEPPER_BTN_W) minus button, [STEPPER_BTN_W,
            // PARAM_PLUS_OFFSET) the value text, [PARAM_PLUS_OFFSET, PARAM_SLOT_W)
            // plus button. Widened from an earlier version where the value slot was
            // only 36px — too narrow for "-5.0"-style text, which silently overlapped
            // (and got visually clipped by) the plus button drawn after it.
            const float PARAM_SLOT_W = 100.0f;
            const float PARAM_PLUS_OFFSET = 76.0f;
            auto paramXFor = [&](int paramCount, int p) {
                return paramsRightEdge - (paramCount - p) * PARAM_SLOT_W;
            };

            // Stepper/indent hit-test on the hovered workspace block, if any — checked
            // before treating the click as a drag-start on the block body.
            int stepperBlockIndex = -1, stepperParamIndex = -1, stepperDelta = 0;
            int indentBlockIndex = -1, indentDelta = 0;
            if (workspaceHover >= 0) {
                const BlockInstance& hoverInst = blockScript[workspaceHover];
                const BlockDef& hoverDef = blockDefFor(hoverInst.type);
                float y0 = rowY[workspaceHover];
                float y1 = y0 + BlockLayout::WORKSPACE_BLOCK_H;
                for (int p = 0; p < hoverDef.paramCount; p++) {
                    float paramX = paramXFor(hoverDef.paramCount, p);
                    float minusX0 = paramX, minusX1 = paramX + BlockLayout::STEPPER_BTN_W;
                    float plusX0 = paramX + PARAM_PLUS_OFFSET, plusX1 = plusX0 + BlockLayout::STEPPER_BTN_W;
                    if (mx >= minusX0 && mx <= minusX1 && my >= y0 && my <= y1) {
                        stepperBlockIndex = workspaceHover; stepperParamIndex = p; stepperDelta = -1;
                    } else if (mx >= plusX0 && mx <= plusX1 && my >= y0 && my <= y1) {
                        stepperBlockIndex = workspaceHover; stepperParamIndex = p; stepperDelta = 1;
                    }
                }
                float outdentX0 = indentBtnsX0, outdentX1 = indentBtnsX0 + BlockLayout::INDENT_BTN_W;
                float indentX0 = outdentX1 + 4.0f, indentX1 = indentX0 + BlockLayout::INDENT_BTN_W;
                if (mx >= outdentX0 && mx <= outdentX1 && my >= y0 && my <= y1) {
                    indentBlockIndex = workspaceHover; indentDelta = -1;
                } else if (mx >= indentX0 && mx <= indentX1 && my >= y0 && my <= y1) {
                    indentBlockIndex = workspaceHover; indentDelta = 1;
                }
            }

            if (pressed) {
                if (overRun) {
                    if (!blockScript.empty()) {
                        scriptRunning = true;
                        runningStep = 0;
                        waitTimer = 0.0f;
                        loopStack.clear();
                        exitBlockEditorMode();
                    }
                } else if (stepperBlockIndex >= 0) {
                    const BlockDef& def = blockDefFor(blockScript[stepperBlockIndex].type);
                    float& val = blockScript[stepperBlockIndex].params[stepperParamIndex];
                    val += def.paramStep[stepperParamIndex] * stepperDelta;
                    val = glm::clamp(val, def.paramMin[stepperParamIndex], def.paramMax[stepperParamIndex]);
                } else if (indentBlockIndex >= 0) {
                    // depth is capped at one deeper than the block above (can't nest inside
                    // a loop that doesn't exist yet) and at BLOCK_MAX_DEPTH either way.
                    int& depth = blockScript[indentBlockIndex].depth;
                    int maxAllowed = indentBlockIndex > 0 ? blockScript[indentBlockIndex - 1].depth + 1 : 0;
                    depth = glm::clamp(depth + indentDelta, 0, glm::min(maxAllowed, BLOCK_MAX_DEPTH));
                } else if (paletteHover >= 0) {
                    blockDragFromPalette = paletteHover;
                } else if (workspaceHover >= 0) {
                    blockDragFromWorkspace = workspaceHover;
                }
            }

            if (released) {
                if (blockDragFromPalette >= 0) {
                    int insertAt = workspaceInsertIndexForY((float)my, rowY);
                    // New blocks land at the same depth as whatever's above the drop point,
                    // so dropping into the middle of an indented run doesn't reset it to 0.
                    int newDepth = insertAt > 0 ? blockScript[insertAt - 1].depth : 0;
                    BlockInstance newInst = BlockInstance::fromDef(BLOCK_DEFS[blockDragFromPalette].type);
                    newInst.depth = newDepth;
                    blockScript.insert(blockScript.begin() + insertAt, newInst);
                    blockDragFromPalette = -1;
                } else if (blockDragFromWorkspace >= 0) {
                    if (overTrash) {
                        blockScript.erase(blockScript.begin() + blockDragFromWorkspace);
                    } else {
                        int insertAt = workspaceInsertIndexForY((float)my, rowY);
                        BlockInstance moved = blockScript[blockDragFromWorkspace];
                        blockScript.erase(blockScript.begin() + blockDragFromWorkspace);
                        if (insertAt > blockDragFromWorkspace) insertAt--;
                        if (insertAt > (int)blockScript.size()) insertAt = (int)blockScript.size();
                        blockScript.insert(blockScript.begin() + insertAt, moved);
                    }
                    blockDragFromWorkspace = -1;
                }
                // blockScript may have just changed size (insert/erase above) — rowY
                // was computed from the pre-mutation script, so the render loops below
                // (which iterate the current blockScript.size()) would index past the
                // end of a now-too-small rowY without this. That's exactly what caused
                // a real segfault: placing a block grew blockScript but not rowY.
                rowY = computeWorkspaceRowY(blockScript);
            }

            snprintf(titleBuf, sizeof(titleBuf), "RETRObit Engine - Block Editor | drag blocks, </> to nest, click RUN | Esc = back");
            glfwSetWindowTitle(window, titleBuf);

            glClearColor(0.08f, 0.08f, 0.12f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            glDisable(GL_DEPTH_TEST);
            glDisable(GL_CULL_FACE);

            glm::mat4 uiProj = glm::ortho(0.0f, (float)WIDTH, (float)HEIGHT, 0.0f);
            glm::mat4 uiView(1.0f);
            shader.use();
            shader.setMat4("uView", uiView);
            shader.setMat4("uProj", uiProj);
            shader.setVec3("uViewPos", glm::vec3(WIDTH * 0.5f, HEIGHT * 0.5f, 100.0f));
            shader.setVec3("uSunDir", glm::vec3(0.0f, 0.0f, -1.0f));
            shader.setVec3("uFogColor", glm::vec3(0.08f, 0.08f, 0.12f));
            shader.setFloat("uFogDensity", 0.0f);

            // palette — real rounded blocks with a puzzle-connector tab, via drawBlockShape.
            // Label text is left-anchored (leftEdge + halfWidth, not the block's raw
            // center) so a long label like "MOVE LAST PART" grows rightward instead of
            // overflowing past the block's own left edge — that overflow was real,
            // visible as text bleeding into whatever sat to its left.
            const float PALETTE_LABEL_PIXEL = 2.0f;
            const float PALETTE_LABEL_LEFT_PAD = 14.0f;
            for (int i = 0; i < BLOCK_DEF_COUNT; i++) {
                float y0 = BlockLayout::paletteEntryY0(i), y1 = BlockLayout::paletteEntryY1(i);
                drawBlockShape(BlockLayout::PALETTE_X0, y0, BlockLayout::PALETTE_X1, y1, BLOCK_DEFS[i].color, paletteHover == i, i > 0);
                float labelX = BlockLayout::PALETTE_X0 + PALETTE_LABEL_LEFT_PAD + textWidthPx(BLOCK_DEFS[i].label, PALETTE_LABEL_PIXEL) * 0.5f;
                drawText(BLOCK_DEFS[i].label, labelX, (y0 + y1) * 0.5f, PALETTE_LABEL_PIXEL, glm::vec3(1.0f));
            }

            // workspace — REPEAT's C-shape (left arm + bottom closing bar) first, drawn
            // under the blocks so the body reads as visually contained within it, then
            // blocks and their tabs/labels/params/indent buttons on top.
            for (int i = 0; i < (int)blockScript.size(); i++) {
                const BlockInstance& inst = blockScript[i];
                if (inst.type != BlockType::REPEAT) continue;
                int bodyEnd = findBodyEnd(blockScript, i);
                if (bodyEnd <= i + 1) continue; // empty body, nothing to draw
                const BlockDef& def = blockDefFor(inst.type);
                float armX0 = BlockLayout::workspaceBlockX0(inst.depth);
                float armY0 = rowY[i] + BlockLayout::WORKSPACE_BLOCK_H;
                float bodyLastY1 = rowY[bodyEnd - 1] + BlockLayout::WORKSPACE_BLOCK_H;
                float closeBarY0 = bodyLastY1;
                float closeBarY1 = closeBarY0 + BlockLayout::CLOSE_BAR_H;
                // left arm (the C's wall) spans from the REPEAT block down to the top
                // of its closing bar/foot
                drawRoundedRect(armX0, armY0, armX0 + BlockLayout::ARM_W, closeBarY0, def.color, false, 4.0f);
                // closing bar (the C's foot) — same width/left edge as the REPEAT block
                // itself, closing the loop off underneath its body
                drawRoundedRect(armX0, closeBarY0, BlockLayout::workspaceBlockX1(), closeBarY1, def.color, false, 8.0f);
            }

            for (int i = 0; i < (int)blockScript.size(); i++) {
                const BlockInstance& inst = blockScript[i];
                const BlockDef& def = blockDefFor(inst.type);
                float y0 = rowY[i], y1 = y0 + BlockLayout::WORKSPACE_BLOCK_H;
                float x0 = BlockLayout::workspaceBlockX0(inst.depth);
                bool dragging = (i == blockDragFromWorkspace);
                // Tab on every block but the very first at depth 0, so a fresh
                // top-level block doesn't show a nub with nothing above it to connect to.
                bool hasTab = (i > 0 || inst.depth > 0);
                drawBlockShape(x0, y0, workspaceX1, y1, def.color, workspaceHover == i || dragging, hasTab);
                // Left-anchored the same way the palette label is — see that loop's comment.
                const float WORKSPACE_LABEL_PIXEL = 2.0f;
                float labelX = x0 + 60.0f + textWidthPx(def.label, WORKSPACE_LABEL_PIXEL) * 0.5f;
                drawText(def.label, labelX, (y0 + y1) * 0.5f, WORKSPACE_LABEL_PIXEL, glm::vec3(1.0f));

                for (int p = 0; p < def.paramCount; p++) {
                    float paramX = paramXFor(def.paramCount, p);
                    bool hoverMinus = (stepperBlockIndex == i && stepperParamIndex == p && stepperDelta < 0);
                    bool hoverPlus = (stepperBlockIndex == i && stepperParamIndex == p && stepperDelta > 0);

                    drawRoundedRect(paramX, y0 + 8.0f, paramX + BlockLayout::STEPPER_BTN_W, y1 - 8.0f, glm::vec3(0.3f), hoverMinus, 6.0f);
                    drawText("-", paramX + BlockLayout::STEPPER_BTN_W * 0.5f, (y0 + y1) * 0.5f, 3.0f, glm::vec3(1.0f));

                    // Value slot is everything between the two buttons — centered here,
                    // not offset by a fixed guess, so it can't run into the + button
                    // regardless of how many digits/minus signs %.1f produces.
                    char valBuf[16];
                    snprintf(valBuf, sizeof(valBuf), "%.1f", inst.params[p]);
                    float valueSlotCenter = paramX + (BlockLayout::STEPPER_BTN_W + PARAM_PLUS_OFFSET) * 0.5f;
                    drawText(valBuf, valueSlotCenter, (y0 + y1) * 0.5f, 2.0f, glm::vec3(0.9f));

                    float plusX0 = paramX + PARAM_PLUS_OFFSET;
                    drawRoundedRect(plusX0, y0 + 8.0f, plusX0 + BlockLayout::STEPPER_BTN_W, y1 - 8.0f, glm::vec3(0.3f), hoverPlus, 6.0f);
                    drawText("+", plusX0 + BlockLayout::STEPPER_BTN_W * 0.5f, (y0 + y1) * 0.5f, 3.0f, glm::vec3(1.0f));
                }

                // </> indent buttons, always present (any block can be nested)
                bool hoverOutdent = (indentBlockIndex == i && indentDelta < 0);
                bool hoverIndent = (indentBlockIndex == i && indentDelta > 0);
                drawRoundedRect(indentBtnsX0, y0 + 8.0f, indentBtnsX0 + BlockLayout::INDENT_BTN_W, y1 - 8.0f, glm::vec3(0.3f), hoverOutdent, 6.0f);
                drawText("-", indentBtnsX0 + BlockLayout::INDENT_BTN_W * 0.5f, (y0 + y1) * 0.5f, 2.5f, glm::vec3(1.0f));
                float indentPlusX0 = indentBtnsX0 + BlockLayout::INDENT_BTN_W + 4.0f;
                drawRoundedRect(indentPlusX0, y0 + 8.0f, indentPlusX0 + BlockLayout::INDENT_BTN_W, y1 - 8.0f, glm::vec3(0.3f), hoverIndent, 6.0f);
                drawText("+", indentPlusX0 + BlockLayout::INDENT_BTN_W * 0.5f, (y0 + y1) * 0.5f, 2.5f, glm::vec3(1.0f));
            }

            // run button
            drawRoundedRect(RUN_BTN_X0, RUN_BTN_Y0, RUN_BTN_X1, RUN_BTN_Y1, glm::vec3(0.2f, 0.8f, 0.3f), overRun, 14.0f);
            drawText("RUN", (RUN_BTN_X0 + RUN_BTN_X1) * 0.5f, (RUN_BTN_Y0 + RUN_BTN_Y1) * 0.5f, 4.0f, glm::vec3(0.05f));

            // trash zone (only meaningful while dragging a workspace block, but always drawn)
            drawRoundedRect(TRASH_X0, TRASH_Y0, TRASH_X1, TRASH_Y1, glm::vec3(0.5f, 0.15f, 0.15f), overTrash && blockDragFromWorkspace >= 0, 10.0f);

            glEnable(GL_DEPTH_TEST);
            glEnable(GL_CULL_FACE);
            glfwSwapBuffers(window);
            continue;
        }

        // --- camera basis from yaw/pitch ---
        glm::vec3 camFront;
        camFront.x = cosf(glm::radians(g_yaw)) * cosf(glm::radians(g_pitch));
        camFront.y = sinf(glm::radians(g_pitch));
        camFront.z = sinf(glm::radians(g_yaw)) * cosf(glm::radians(g_pitch));
        camFront = glm::normalize(camFront);
        glm::vec3 camFlatForward = glm::normalize(glm::vec3(camFront.x, 0, camFront.z));
        glm::vec3 camRight = glm::normalize(glm::cross(camFlatForward, glm::vec3(0, 1, 0)));

        // --- player input (movement relative to camera); frozen while the move tool is open ---
        // Flight mode (F, play-mode only) uses full (non-flattened) camFront plus
        // Space/Shift for up/down, same shape as edit mode's freecam; normal mode
        // stays flattened (no flying) with Space reserved for jump instead.
        if (g_flightMode != wasFlightMode) {
            playerVel = glm::vec3(0.0f); // don't carry momentum across the mode switch
            wasFlightMode = g_flightMode;
        }
        bool justUTurned = false;
        if (g_uTurnRequested) {
            // g_yaw already flipped 180 in keyCallback (so camFront below reflects it
            // this same frame) — flip existing velocity to match instead of leaving it
            // pointed the old way while target velocity catches up over ACCEL time.
            playerVel = -playerVel;
            g_uTurnRequested = false;
            justUTurned = true;
        }

        glm::vec3 moveDir(0.0f);
        if (!g_editMode) {
            if (g_flightMode) {
                // Flight mode's steering is computed straight into playerVel below
                // (forward held constant, vertical/strafe added on top rather than
                // sharing a normalized budget with it) — moveDir isn't used here.
            } else {
                if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) moveDir += camFlatForward;
                if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) moveDir -= camFlatForward;
                if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) moveDir += camRight;
                if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) moveDir -= camRight;
            }
        }

        const float RUN_SPEED = 9.0f;
        const float FLIGHT_SPEED = 14.0f;
        const float ACCEL = 40.0f;
        const float GRAVITY = -28.0f;
        const float JUMP_SPEED = 10.0f;

        if (g_flightMode) {
            // Boost (E) / brake (Q): lerp the speed multiplier toward whichever's
            // held (boost wins if both are somehow held), settle back to 1.0 when
            // neither is — gives flight a StarFox-style speed range instead of one
            // constant velocity.
            // Brake uses C, not Q (Q is the global quit key, line ~1110) and not
            // Left Ctrl (macOS commonly grabs held-Ctrl for Mission Control/Spaces
            // switching, which yanks focus from the app and looks like a crash).
            bool boostDown = glfwGetKey(window, GLFW_KEY_E) == GLFW_PRESS;
            bool brakeDown = glfwGetKey(window, GLFW_KEY_C) == GLFW_PRESS;
            const float BOOST_MUL = 1.8f;
            const float BRAKE_MUL = 0.35f;
            float targetMul = boostDown ? BOOST_MUL : (brakeDown ? BRAKE_MUL : 1.0f);
            flightSpeedMul += (targetMul - flightSpeedMul) * glm::min(6.0f * dt, 1.0f);

            // Barrel roll: detect double-tap A/D (edge-triggered on key-down within
            // BARREL_ROLL_TAP_WINDOW of the previous tap), starts a timed cosmetic roll.
            bool aDownFlight = glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS;
            bool dDownFlight = glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS;
            double nowT = glfwGetTime();
            if (aDownFlight && !aWasDownFlight) {
                if (nowT - lastATapTime < BARREL_ROLL_TAP_WINDOW) {
                    barrelRollTimer = BARREL_ROLL_DURATION;
                    barrelRollDir = -1.0f;
                }
                lastATapTime = nowT;
            }
            if (dDownFlight && !dWasDownFlight) {
                if (nowT - lastDTapTime < BARREL_ROLL_TAP_WINDOW) {
                    barrelRollTimer = BARREL_ROLL_DURATION;
                    barrelRollDir = 1.0f;
                }
                lastDTapTime = nowT;
            }
            aWasDownFlight = aDownFlight;
            dWasDownFlight = dDownFlight;
            if (barrelRollTimer > 0.0f) barrelRollTimer = glm::max(0.0f, barrelRollTimer - dt);

            // Camera bank: steady-state tilt toward whichever of A/D is held (or
            // level when neither is). A barrel roll (if active) adds its own full
            // 360 spin on top of this at the point camRoll is applied to the view.
            float targetRoll = aDownFlight ? 25.0f : (dDownFlight ? -25.0f : 0.0f);
            camRoll += (targetRoll - camRoll) * glm::min(10.0f * dt, 1.0f);

            // Forward speed is held constant regardless of steering — vertical/strafe
            // are added on top rather than sharing a normalized budget with forward,
            // so pressing W/S/A/D no longer robs speed from (and overpowers) the
            // constant-forward motion the way blending everything into one normalized
            // moveDir used to (that made "up" feel wildly fast and forward speed
            // wobble, which is what made it hard to track where you were).
            const float FLIGHT_VERTICAL_SPEED = 14.0f; // was 6 — descending especially felt too slow
            const float FLIGHT_STRAFE_SPEED = 34.0f;   // was 8, then 20 — still not forceful enough
            const float FLIGHT_FAST_ACCEL = 140.0f;    // was 90 (turn-only) — snappier than forward's ACCEL (40)
            glm::vec3 target = camFront * FLIGHT_SPEED * flightSpeedMul;
            bool steering = false;
            if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) { target += glm::vec3(0, 1, 0) * FLIGHT_VERTICAL_SPEED; steering = true; }
            if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) { target -= glm::vec3(0, 1, 0) * FLIGHT_VERTICAL_SPEED; steering = true; }
            if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) { target += camRight * FLIGHT_STRAFE_SPEED; steering = true; }
            if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) { target -= camRight * FLIGHT_STRAFE_SPEED; steering = true; }
            // Steering (W/S/A/D held) blends in faster than forward alone so it
            // responds immediately instead of slowly ramping up like the rest of flight.
            float blendRate = steering ? FLIGHT_FAST_ACCEL : ACCEL;
            playerVel += (target - playerVel) * glm::min(blendRate * dt, 1.0f);
            glm::vec2 flatDir(target.x, target.z);
            if (glm::length(flatDir) > 0.001f) playerFacing = atan2f(flatDir.x, flatDir.y);
        } else {
            camRoll = 0.0f; // no banking outside flight mode
            barrelRollTimer = 0.0f;
            glm::vec3 horizVel(playerVel.x, 0, playerVel.z);
            if (glm::length(moveDir) > 0.001f) {
                moveDir = glm::normalize(moveDir);
                // Sprint (E, normal walk mode only — flight mode already uses E for
                // its own boost): a flat speed multiplier, no separate accel/ramp.
                const float SPRINT_MUL = 1.6f;
                bool sprintDown = glfwGetKey(window, GLFW_KEY_E) == GLFW_PRESS;
                glm::vec3 target = moveDir * RUN_SPEED * (sprintDown ? SPRINT_MUL : 1.0f);
                horizVel += (target - horizVel) * glm::min(ACCEL * dt, 1.0f);
                playerFacing = atan2f(moveDir.x, moveDir.z);
            } else {
                horizVel -= horizVel * glm::min(8.0f * dt, 1.0f);
            }
            playerVel.x = horizVel.x;
            playerVel.z = horizVel.z;
        }

        // rebuild prop collision triangles from current part positions (cheap: a handful of boxes)
        // done every frame regardless of edit mode: parts can move mid-drag, and the
        // chase camera below still needs up-to-date geometry to collide against.
        std::vector<Tri> propTriangles;
        for (const Part& part : parts) {
            std::vector<Tri> t = buildBoxTriangles(part.size, part.modelMatrix());
            propTriangles.insert(propTriangles.end(), t.begin(), t.end());
        }
        // Hills preset: the terrain itself is real (precomputed) triangle geometry,
        // so it rides the same collide-and-slide resolver as props — no flat ground
        // plane exists in this preset, see the branch below.
        if (currentPreset == WorldPreset::HILLS) {
            propTriangles.insert(propTriangles.end(), hillTriangles.begin(), hillTriangles.end());
        } else if (currentPreset == WorldPreset::HILLS_PLUS) {
            propTriangles.insert(propTriangles.end(), hillPlusTriangles.begin(), hillPlusTriangles.end());
        } else if (currentPreset == WorldPreset::PLAYGROUND) {
            // See groundPlaneTriangles' declaration for why this is needed — flight
            // mode has no other way to collide with the Playground ground.
            propTriangles.insert(propTriangles.end(), groundPlaneTriangles.begin(), groundPlaneTriangles.end());
            propTriangles.insert(propTriangles.end(), loopTriangles.begin(), loopTriangles.end());
        }

        // Player physics is fully paused in edit mode — otherwise residual velocity
        // (mid-air jump, sliding) keeps integrating while you're trying to use the
        // gizmo, and since the chase camera follows the player, the whole view would
        // drift/fall out from under you while editing.
        if (!g_editMode) {
            if (g_flightMode) {
                // No gravity, no jump, no ground snap — but still collide with the
                // world (props/hills), so flight is "fly freely" not "clip through
                // everything." onGround has no meaning while flying.
                playerPos += playerVel * dt;
                onGround = false;
                resolveSphereVsTriangles(playerPos, playerVel, PLAYER_RADIUS, propTriangles);

                // Arena boundary (StarFox 64 All-Range Mode-style): flying too far
                // from spawn pushes you back rather than allowing infinite freeflight.
                float arenaRadius = (currentPreset == WorldPreset::HILLS_PLUS) ? FLIGHT_ARENA_RADIUS_HILLS_PLUS : FLIGHT_ARENA_RADIUS;
                glm::vec3 fromSpawn = playerPos - SPAWN_POS;
                float distFromSpawn = glm::length(fromSpawn);
                if (distFromSpawn > arenaRadius) {
                    glm::vec3 inward = -fromSpawn / distFromSpawn;
                    playerPos = SPAWN_POS + fromSpawn * (arenaRadius / distFromSpawn);
                    float outwardSpeed = glm::dot(playerVel, -inward);
                    if (outwardSpeed > 0.0f) playerVel += inward * outwardSpeed; // cancel outward component
                }
            } else {
                bool spaceDown = glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS;
                if (spaceDown && !spaceWasDown && onGround) {
                    playerVel.y = JUMP_SPEED;
                    onGround = false;
                }
                spaceWasDown = spaceDown;

                playerVel.y += GRAVITY * dt;
                playerPos += playerVel * dt;

                // collide-and-slide against props (ramp, pillar, and anything added later)
                onGround = resolveSphereVsTriangles(playerPos, playerVel, PLAYER_RADIUS, propTriangles);

                // analytic ground plane at y = 0 — only in the Playground preset, and
                // now genuinely infinite (unbounded) to match the ground actually being
                // one huge plane rather than a fixed-size tile grid. Hills has no flat
                // plane at all — its terrain triangles (merged into propTriangles above)
                // are what the resolve call just above already collided against.
                if (currentPreset == WorldPreset::PLAYGROUND) {
                    if (playerPos.y - PLAYER_RADIUS < 0.0f) {
                        playerPos.y = PLAYER_RADIUS;
                        if (playerVel.y < 0.0f) playerVel.y = 0.0f;
                        onGround = true;
                    }
                }
            }

            // fallen into the void: gravity just keeps accelerating you down past the
            // level until you cross the threshold, then respawn back at the start —
            // kept as a safety net for flight mode too, in case you fly under the map.
            if (playerPos.y < VOID_Y) {
                playerPos = SPAWN_POS;
                playerVel = glm::vec3(0.0f);
                onGround = true;
            }
        }

        // --- block-coding script execution (one step advances per frame; WAIT holds
        // the current step until its timer elapses; REPEAT pushes/pops loopStack —
        // see include/BlockScript.h's header comment for the depth-based nesting
        // model this walks) ---
        // Runs independently of g_editMode/player-physics gating above — Run (in the
        // block editor) intentionally leaves g_editMode set so B can return to the
        // editor afterward, so this can't reuse that block's guard.
        if (scriptRunning) {
            // Before executing whatever's at runningStep, resolve any loop(s) whose
            // body we've just reached the end of — loop back if iterations remain,
            // otherwise pop and fall through to what comes after. A while-loop since
            // falling out of one loop can immediately land on the end of its parent.
            while (!loopStack.empty() && runningStep == loopStack.back().endIndex) {
                LoopFrame& frame = loopStack.back();
                if (frame.remaining > 0) {
                    frame.remaining--;
                    runningStep = frame.startIndex;
                } else {
                    loopStack.pop_back();
                }
            }

            if (runningStep < 0 || runningStep >= (int)blockScript.size()) {
                scriptRunning = false;
                loopStack.clear();
            } else {
                const BlockInstance& instr = blockScript[runningStep];
                bool advance = true;
                // Fixed offset from the player rather than the freecam — camera-relative
                // spawning would depend on eyePos/camFront, which aren't computed until
                // later in the frame (see the chase-camera section below).
                glm::vec3 spawnPos = playerPos + glm::vec3(0.0f, 2.0f, 3.0f);
                switch (instr.type) {
                    case BlockType::WHEN_START:
                        break; // no-op marker, execution always starts at index 0
                    case BlockType::SPAWN_BOX:
                        parts.push_back(Part{ &partUnitBox, spawnPos, glm::vec3(2.0f, 2.0f, 2.0f), glm::vec3(0.75f, 0.3f, 0.9f) });
                        break;
                    case BlockType::SPAWN_SPHERE:
                        parts.push_back(Part{ &partUnitSphere, spawnPos, glm::vec3(2.0f, 2.0f, 2.0f), glm::vec3(0.75f, 0.3f, 0.9f) });
                        break;
                    case BlockType::MOVE_LAST_PART:
                        if (!parts.empty()) {
                            parts.back().position += glm::vec3(instr.params[0], instr.params[1], instr.params[2]);
                        }
                        break;
                    case BlockType::WAIT:
                        waitTimer += dt;
                        if (waitTimer < instr.params[0]) {
                            advance = false;
                        } else {
                            waitTimer = 0.0f;
                        }
                        break;
                    case BlockType::REPEAT: {
                        int bodyEnd = findBodyEnd(blockScript, runningStep);
                        int count = (int)instr.params[0];
                        if (count > 0 && bodyEnd > runningStep + 1) {
                            loopStack.push_back({ runningStep + 1, bodyEnd, count - 1 });
                            runningStep = runningStep + 1;
                        } else {
                            runningStep = bodyEnd; // zero iterations or an empty body: skip it entirely
                        }
                        advance = false; // runningStep already set explicitly above
                        break;
                    }
                }
                if (advance) {
                    runningStep++;
                }
                if (runningStep >= (int)blockScript.size() && loopStack.empty()) {
                    scriptRunning = false;
                }
            }
        }

        // --- chase camera (with collision so it never clips through geometry) ---
        // Frozen while the move tool is open — see freecam block below, which takes
        // over rendering without touching camPos so play mode resumes exactly where
        // the chase cam left off.
        glm::vec3 lookTarget = playerPos + glm::vec3(0, 1.0f, 0);
        if (!g_editMode) {
            glm::vec3 idealOffset = -camFront * 10.0f + glm::vec3(0, 3.0f, 0);
            float idealDist = glm::length(idealOffset);
            glm::vec3 camDir = idealOffset / idealDist;

            const float CAM_MARGIN = 0.4f;
            const float CAM_MIN_DIST = 1.5f;
            float freeDist = cameraUnobstructedDistance(lookTarget, camDir, idealDist, propTriangles);
            float clampedDist = glm::max(freeDist - CAM_MARGIN, CAM_MIN_DIST);
            glm::vec3 desiredCamPos = lookTarget + camDir * clampedDist;

            if (justUTurned) {
                // Without this, the chase cam would ease from "behind old direction"
                // to "behind new direction" over ~1s, arcing wildly across/through the
                // world since idealOffset flipped 180 instantly — that swing read as
                // the player's position itself getting reset. Cut straight to the new
                // spot instead, same as a hard scene cut.
                camPos = desiredCamPos;
            } else {
                // snap in fast when something's obstructing (avoid clipping), ease out otherwise
                float camLerpRate = (clampedDist < idealDist - 0.01f) ? 20.0f : 6.0f;
                camPos += (desiredCamPos - camPos) * glm::min(camLerpRate * dt, 1.0f);
            }
        }

        // --- freecam (edit mode only): WASD flies, Space/Shift for up/down ---
        glm::vec3 eyePos = camPos;
        glm::vec3 eyeLookTarget = lookTarget;
        if (g_editMode) {
            if (!wasEditMode) freeCamPos = camPos; // start flying from wherever the chase cam was
            wasEditMode = true;

            const float FREECAM_SPEED = 16.0f;
            glm::vec3 flyDir(0.0f);
            if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) flyDir += camFront;
            if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) flyDir -= camFront;
            if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) flyDir += camRight;
            if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) flyDir -= camRight;
            if (glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS) flyDir += glm::vec3(0, 1, 0);
            if (glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS) flyDir -= glm::vec3(0, 1, 0);
            if (glm::length(flyDir) > 0.001f) {
                freeCamPos += glm::normalize(flyDir) * FREECAM_SPEED * dt;
            }

            eyePos = freeCamPos;
            eyeLookTarget = freeCamPos + camFront;
        } else {
            wasEditMode = false;
        }

        // Roll the up vector around camFront for flight-mode banking + barrel rolls
        // (steady bank from camRoll, plus a full 360 spin while a barrel roll plays).
        glm::vec3 camUp(0, 1, 0);
        if (g_flightMode && !g_editMode) {
            float totalRollDeg = camRoll;
            if (barrelRollTimer > 0.0f) {
                float rollProgress = 1.0f - (barrelRollTimer / BARREL_ROLL_DURATION);
                totalRollDeg += barrelRollDir * 360.0f * rollProgress;
            }
            camUp = glm::rotate(glm::mat4(1.0f), glm::radians(totalRollDeg), camFront) * glm::vec4(camUp, 0.0f);
        }
        glm::mat4 view = glm::lookAt(eyePos, eyeLookTarget, camUp);
        glm::mat4 proj = glm::perspective(glm::radians(65.0f), (float)WIDTH / HEIGHT, 0.1f, 300.0f);

        // --- Roblox-Studio-style move/rotate tool ("/" toggles the tool, "R" swaps mode) ---
        // Picking only makes sense with the cursor unlocked (a real, visible screen
        // position) — while locked, mouse movement is just freecam look, same as play mode.
        bool leftMouseDown = false;
        if (g_gizmoMode != lastGizmoMode) {
            dragAxis = -1; // switching move<->rotate mid-drag would otherwise reinterpret the axis
            lastGizmoMode = g_gizmoMode;
        }
        if (g_editMode && !g_mouseCaptured && g_spawnMenuOpen) {
            // Spawn menu takes over clicks/shortcuts entirely while open — no
            // gizmo interaction happens underneath it.
            leftMouseDown = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
            bool clicked = leftMouseDown && !leftMouseWasDown;

            double mx, my;
            glfwGetCursorPos(window, &mx, &my);
            bool overBox = mx >= SPAWN_BTN_X0 && mx <= SPAWN_BTN_X1 && my >= SPAWN_BOX_BTN_Y0 && my <= SPAWN_BOX_BTN_Y1;
            bool overSphere = mx >= SPAWN_BTN_X0 && mx <= SPAWN_BTN_X1 && my >= SPAWN_SPHERE_BTN_Y0 && my <= SPAWN_SPHERE_BTN_Y1;

            bool key1Down = glfwGetKey(window, GLFW_KEY_1) == GLFW_PRESS;
            bool key2Down = glfwGetKey(window, GLFW_KEY_2) == GLFW_PRESS;
            bool key1Pressed = key1Down && !spawnKey1WasDown;
            bool key2Pressed = key2Down && !spawnKey2WasDown;
            spawnKey1WasDown = key1Down;
            spawnKey2WasDown = key2Down;

            Mesh* spawnMesh = nullptr;
            if ((clicked && overBox) || key1Pressed) spawnMesh = &partUnitBox;
            else if ((clicked && overSphere) || key2Pressed) spawnMesh = &partUnitSphere;

            if (spawnMesh) {
                // Spawn a few units in front of wherever the freecam is looking,
                // floored so it doesn't land underground/inside the terrain.
                glm::vec3 spawnPos = eyePos + camFront * 8.0f;
                spawnPos.y = glm::max(spawnPos.y, 1.0f);
                parts.push_back(Part{ spawnMesh, spawnPos, glm::vec3(2.0f, 2.0f, 2.0f), glm::vec3(0.75f, 0.3f, 0.9f) });
                selectedPart = (int)parts.size() - 1;
                dragAxis = -1;
                g_gizmoMode = GizmoMode::MOVE; // land in Move so the new part can be repositioned immediately
                g_spawnMenuOpen = false;
            }
        } else if (g_editMode && !g_mouseCaptured) {
            leftMouseDown = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;

            double mx, my;
            glfwGetCursorPos(window, &mx, &my);
            float ndcX = (2.0f * (float)mx) / WIDTH - 1.0f;
            float ndcY = 1.0f - (2.0f * (float)my) / HEIGHT;
            glm::vec4 rayClip(ndcX, ndcY, -1.0f, 1.0f);
            glm::vec4 rayEye = glm::inverse(proj) * rayClip;
            rayEye = glm::vec4(rayEye.x, rayEye.y, -1.0f, 0.0f);
            glm::vec3 rayDir = glm::normalize(glm::vec3(glm::inverse(view) * rayEye));

            static const glm::vec3 AXIS_DIR[3] = { glm::vec3(1,0,0), glm::vec3(0,1,0), glm::vec3(0,0,1) };

            if (g_gizmoMode == GizmoMode::MOVE) {
                if (leftMouseDown && !leftMouseWasDown) {
                    // fresh click: try grabbing a gizmo handle on the selected part first
                    dragAxis = -1;
                    if (selectedPart >= 0) {
                        const Part& sp = parts[selectedPart];
                        float bestT = 1e9f;
                        for (int a = 0; a < 3; a++) {
                            float tc = closestParamOnLineToRay(eyePos, rayDir, sp.position, AXIS_DIR[a]);
                            glm::vec3 closest = sp.position + AXIS_DIR[a] * tc;
                            float distToRay = glm::length(closest - (eyePos + rayDir * glm::dot(closest - eyePos, rayDir)));
                            if (distToRay < GIZMO_GRAB_RADIUS && tc > -0.3f && tc < GIZMO_ARM + 0.3f) {
                                float distAlongRay = glm::dot(closest - eyePos, rayDir);
                                if (distAlongRay < bestT) {
                                    bestT = distAlongRay;
                                    dragAxis = a;
                                }
                            }
                        }
                    }
                    if (dragAxis >= 0) {
                        dragStartPos = parts[selectedPart].position;
                        dragTcInitial = closestParamOnLineToRay(eyePos, rayDir, dragStartPos, AXIS_DIR[dragAxis]);
                    } else {
                        // no handle grabbed: pick a part under the cursor
                        float bestT = 1e9f;
                        int hit = -1;
                        for (int i = 0; i < (int)parts.size(); i++) {
                            float t;
                            if (rayHitsPart(eyePos, rayDir, parts[i], t) && t < bestT) {
                                bestT = t;
                                hit = i;
                            }
                        }
                        selectedPart = hit;
                    }
                } else if (leftMouseDown && dragAxis >= 0 && selectedPart >= 0) {
                    float tc = closestParamOnLineToRay(eyePos, rayDir, dragStartPos, AXIS_DIR[dragAxis]);
                    parts[selectedPart].position = dragStartPos + AXIS_DIR[dragAxis] * (tc - dragTcInitial);
                } else if (!leftMouseDown) {
                    dragAxis = -1;
                }
            } else if (g_gizmoMode == GizmoMode::ROTATE) {
                if (leftMouseDown && !leftMouseWasDown) {
                    // fresh click: try grabbing a rotation ring on the selected part
                    dragAxis = -1;
                    if (selectedPart >= 0) {
                        const Part& sp = parts[selectedPart];
                        float bestT = 1e9f;
                        for (int a = 0; a < 3; a++) {
                            float t;
                            if (!rayPlaneIntersect(eyePos, rayDir, sp.position, AXIS_DIR[a], t) || t <= 0.0f) continue;
                            glm::vec3 hit = eyePos + rayDir * t;
                            float distFromCenter = glm::length(hit - sp.position);
                            if (fabsf(distFromCenter - GIZMO_ARM) < ROT_GRAB_TOLERANCE && t < bestT) {
                                bestT = t;
                                dragAxis = a;
                            }
                        }
                    }
                    if (dragAxis >= 0) {
                        const Part& sp = parts[selectedPart];
                        glm::vec3 u, v;
                        ringBasis(dragAxis, u, v);
                        float t;
                        rayPlaneIntersect(eyePos, rayDir, sp.position, AXIS_DIR[dragAxis], t);
                        glm::vec3 rel = (eyePos + rayDir * t) - sp.position;
                        rotDragStartAngle = atan2f(glm::dot(rel, v), glm::dot(rel, u));
                        rotDragStartRotation = sp.rotation;
                    } else {
                        // no ring grabbed: pick a part under the cursor, same as move mode
                        float bestT = 1e9f;
                        int hit = -1;
                        for (int i = 0; i < (int)parts.size(); i++) {
                            float t;
                            if (rayHitsPart(eyePos, rayDir, parts[i], t) && t < bestT) {
                                bestT = t;
                                hit = i;
                            }
                        }
                        selectedPart = hit;
                    }
                } else if (leftMouseDown && dragAxis >= 0 && selectedPart >= 0) {
                    // rotation pivots in place, so use the part's live (unchanged) position
                    const glm::vec3& pivot = parts[selectedPart].position;
                    glm::vec3 u, v;
                    ringBasis(dragAxis, u, v);
                    float t;
                    if (rayPlaneIntersect(eyePos, rayDir, pivot, AXIS_DIR[dragAxis], t) && t > 0.0f) {
                        glm::vec3 rel = (eyePos + rayDir * t) - pivot;
                        float angle = atan2f(glm::dot(rel, v), glm::dot(rel, u));
                        float delta = angle - rotDragStartAngle;
                        parts[selectedPart].rotation = glm::normalize(glm::angleAxis(delta, AXIS_DIR[dragAxis]) * rotDragStartRotation);
                    }
                } else if (!leftMouseDown) {
                    dragAxis = -1;
                }
            } else { // GizmoMode::SCALE
                if (leftMouseDown && !leftMouseWasDown) {
                    // fresh click: try grabbing a resize handle on the selected part.
                    // Handles sit on the part's LOCAL +axis faces (rotated with the part),
                    // since size is defined in local space.
                    dragAxis = -1;
                    if (selectedPart >= 0) {
                        const Part& sp = parts[selectedPart];
                        float bestT = 1e9f;
                        for (int a = 0; a < 3; a++) {
                            glm::vec3 axisWorldDir = glm::normalize(sp.rotation * AXIS_DIR[a]);
                            float handleParam = sp.size[a] * 0.5f;
                            float tc = closestParamOnLineToRay(eyePos, rayDir, sp.position, axisWorldDir);
                            glm::vec3 closest = sp.position + axisWorldDir * tc;
                            float distToRay = glm::length(closest - (eyePos + rayDir * glm::dot(closest - eyePos, rayDir)));
                            if (distToRay < GIZMO_GRAB_RADIUS && fabsf(tc - handleParam) < SCALE_GRAB_TOLERANCE) {
                                float distAlongRay = glm::dot(closest - eyePos, rayDir);
                                if (distAlongRay < bestT) {
                                    bestT = distAlongRay;
                                    dragAxis = a;
                                }
                            }
                        }
                    }
                    if (dragAxis >= 0) {
                        const Part& sp = parts[selectedPart];
                        glm::vec3 axisWorldDir = glm::normalize(sp.rotation * AXIS_DIR[dragAxis]);
                        scaleDragStartSize = sp.size[dragAxis];
                        scaleAnchor = sp.position - axisWorldDir * (scaleDragStartSize * 0.5f); // opposite face, held fixed
                        scaleDragTcInitial = closestParamOnLineToRay(eyePos, rayDir, scaleAnchor, axisWorldDir);
                    } else {
                        // no handle grabbed: pick a part under the cursor, same as the other modes
                        float bestT = 1e9f;
                        int hit = -1;
                        for (int i = 0; i < (int)parts.size(); i++) {
                            float t;
                            if (rayHitsPart(eyePos, rayDir, parts[i], t) && t < bestT) {
                                bestT = t;
                                hit = i;
                            }
                        }
                        selectedPart = hit;
                    }
                } else if (leftMouseDown && dragAxis >= 0 && selectedPart >= 0) {
                    Part& sp = parts[selectedPart];
                    glm::vec3 axisWorldDir = glm::normalize(sp.rotation * AXIS_DIR[dragAxis]);
                    float tc = closestParamOnLineToRay(eyePos, rayDir, scaleAnchor, axisWorldDir);
                    float newSize = glm::max(scaleDragStartSize + (tc - scaleDragTcInitial), SCALE_MIN_SIZE);
                    sp.size[dragAxis] = newSize;
                    sp.position = scaleAnchor + axisWorldDir * (newSize * 0.5f);
                } else if (!leftMouseDown) {
                    dragAxis = -1;
                }
            }
        } else if (!g_editMode) {
            // fully leaving the tool clears selection; merely being locked (flying
            // around) keeps whatever was selected so you can resume dragging after Esc
            selectedPart = -1;
            dragAxis = -1;
        }
        leftMouseWasDown = leftMouseDown;

        // Live debug readout in the title bar (edit mode has no on-screen UI yet,
        // so this is the fastest way to see what the picker/gizmo is doing).
        if (g_editMode) {
            const char* axisName = dragAxis == 0 ? "X" : dragAxis == 1 ? "Y" : dragAxis == 2 ? "Z" : "-";
            const char* modeName = g_gizmoMode == GizmoMode::MOVE ? "MOVE(R=rotate,T=scale)"
                                  : g_gizmoMode == GizmoMode::ROTATE ? "ROTATE(R=move,T=scale)"
                                                                      : "SCALE(R=rotate,T=move)";
            if (g_spawnMenuOpen) {
                snprintf(titleBuf, sizeof(titleBuf),
                    "RETRObit Engine - EDIT MODE [SPAWN MENU] click BOX or SPHERE, or press M to cancel | %.0f FPS",
                    fpsDisplay);
            } else {
                snprintf(titleBuf, sizeof(titleBuf),
                    "RETRObit Engine - EDIT MODE [%s] %s | selected=%d drag=%s LMB=%s M=spawn | %.0f FPS",
                    g_mouseCaptured ? "LOCKED - fly/look" : "UNLOCKED - click to pick",
                    modeName, selectedPart, axisName, leftMouseDown ? "down" : "up", fpsDisplay);
            }
        } else if (g_flightMode) {
            snprintf(titleBuf, sizeof(titleBuf), "RETRObit Engine - Playground [FLIGHT MODE - always forward, W/S up/down, A/D strafe, E boost, C brake, AA/DD barrel roll, U u-turn] | %.0f FPS", fpsDisplay);
        } else {
            snprintf(titleBuf, sizeof(titleBuf), "RETRObit Engine - Playground | %.0f FPS", fpsDisplay);
        }
        glfwSetWindowTitle(window, titleBuf);

        // --- render ---
        glm::vec3 fogColor(0.55f, 0.75f, 0.95f);
        glClearColor(fogColor.r, fogColor.g, fogColor.b, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        shader.use();
        shader.setMat4("uView", view);
        shader.setMat4("uProj", proj);
        shader.setVec3("uViewPos", eyePos);
        shader.setVec3("uSunDir", glm::normalize(glm::vec3(-0.4f, -1.0f, -0.3f)));
        shader.setVec3("uFogColor", fogColor);
        shader.setFloat("uFogDensity", 0.012f);

        // ground: one huge checkerboard plane (Playground, infinite-looking — see
        // basic.frag) or the rolling hill mesh (Hills/Hills+)
        if (currentPreset == WorldPreset::PLAYGROUND) {
            shader.setMat4("uModel", glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, -0.25f, 0.0f)));
            shader.setVec3("uColor", glm::vec3(0.25f, 0.75f, 0.35f));
            shader.setVec3("uColorB", glm::vec3(0.20f, 0.65f, 0.30f));
            shader.setFloat("uCheckerSize", TILE);
            shader.setFloat("uUseCheckerboard", 1.0f);
            groundPlane.draw();
            shader.setFloat("uUseCheckerboard", 0.0f); // every other draw this frame uses flat uColor

            shader.setMat4("uModel", glm::mat4(1.0f)); // loopMesh's vertices are already world-space
            shader.setVec3("uColor", glm::vec3(0.6f, 0.6f, 0.65f)); // steel-grey track
            loopMesh.draw();
        } else {
            shader.setMat4("uModel", glm::mat4(1.0f));
            shader.setVec3("uColor", glm::vec3(0.35f, 0.65f, 0.25f)); // grassy green
            if (currentPreset == WorldPreset::HILLS_PLUS) {
                hillPlusMesh.draw();
            } else {
                hillMesh.draw();
            }
        }

        // movable parts (ramp, pillar, ...) — brighten the selected one
        for (int i = 0; i < (int)parts.size(); i++) {
            const Part& part = parts[i];
            shader.setMat4("uModel", part.renderModelMatrix());
            glm::vec3 c = part.color;
            if (i == selectedPart) c = glm::min(c * 1.6f + glm::vec3(0.15f), glm::vec3(1.0f));
            shader.setVec3("uColor", c);
            part.mesh->draw();
        }

        // move/rotate gizmo on the selected part
        if (selectedPart >= 0) {
            glDisable(GL_DEPTH_TEST);
            const Part& sp = parts[selectedPart];
            static const glm::vec3 AXIS_DIR[3] = { glm::vec3(1,0,0), glm::vec3(0,1,0), glm::vec3(0,0,1) };
            static const glm::vec3 AXIS_COLOR[3] = { glm::vec3(1,0.2f,0.2f), glm::vec3(0.2f,1,0.2f), glm::vec3(0.2f,0.4f,1) };

            if (g_gizmoMode == GizmoMode::MOVE) {
                for (int a = 0; a < 3; a++) {
                    glm::vec3 scale = glm::vec3(GIZMO_THICK);
                    scale[a] = GIZMO_ARM;
                    glm::vec3 center = sp.position + AXIS_DIR[a] * (GIZMO_ARM * 0.5f);
                    glm::mat4 model = glm::translate(glm::mat4(1.0f), center);
                    model = glm::scale(model, scale);
                    shader.setMat4("uModel", model);
                    glm::vec3 c = AXIS_COLOR[a];
                    if (a == dragAxis) c = glm::vec3(1.0f, 1.0f, 0.3f);
                    shader.setVec3("uColor", c);
                    gizmoArrow.draw();
                }
            } else if (g_gizmoMode == GizmoMode::ROTATE) {
                // rotation rings are world-aligned (not part-local) — just centered on the part
                glLineWidth(2.5f);
                glm::mat4 model = glm::translate(glm::mat4(1.0f), sp.position);
                shader.setMat4("uModel", model);
                for (int a = 0; a < 3; a++) {
                    glm::vec3 c = AXIS_COLOR[a];
                    if (a == dragAxis) c = glm::vec3(1.0f, 1.0f, 0.3f);
                    shader.setVec3("uColor", c);
                    ringMeshes[a]->draw();
                }
                glLineWidth(1.0f);
            } else {
                // scale handles sit on each LOCAL +axis face, rotated with the part
                for (int a = 0; a < 3; a++) {
                    glm::vec3 axisWorldDir = glm::normalize(sp.rotation * AXIS_DIR[a]);
                    glm::vec3 handlePos = sp.position + axisWorldDir * (sp.size[a] * 0.5f);
                    glm::mat4 model = glm::translate(glm::mat4(1.0f), handlePos);
                    model = glm::scale(model, glm::vec3(SCALE_HANDLE_SIZE));
                    shader.setMat4("uModel", model);
                    glm::vec3 c = AXIS_COLOR[a];
                    if (a == dragAxis) c = glm::vec3(1.0f, 1.0f, 0.3f);
                    shader.setVec3("uColor", c);
                    scaleHandleMesh.draw();
                }
            }
            glEnable(GL_DEPTH_TEST);
        }

        // player
        {
            glm::mat4 model = glm::translate(glm::mat4(1.0f), playerPos);
            model = glm::rotate(model, playerFacing, glm::vec3(0, 1, 0));
            shader.setMat4("uModel", model);
            shader.setVec3("uColor", glm::vec3(0.05f, 0.35f, 0.95f)); // Sonic blue
            player.draw();
        }

        // spawn menu ("M" in edit mode): same ortho-overlay technique as the title
        // screen (see its comment for why GL_CULL_FACE has to be disabled here too)
        if (g_editMode && g_spawnMenuOpen) {
            glDisable(GL_DEPTH_TEST);
            glDisable(GL_CULL_FACE);

            glm::mat4 spawnUiProj = glm::ortho(0.0f, (float)WIDTH, (float)HEIGHT, 0.0f);
            glm::mat4 spawnUiView(1.0f);
            shader.use();
            shader.setMat4("uView", spawnUiView);
            shader.setMat4("uProj", spawnUiProj);
            shader.setVec3("uViewPos", glm::vec3(WIDTH * 0.5f, HEIGHT * 0.5f, 100.0f));
            shader.setVec3("uSunDir", glm::vec3(0.0f, 0.0f, -1.0f));
            shader.setVec3("uFogColor", glm::vec3(0.08f, 0.08f, 0.12f));
            shader.setFloat("uFogDensity", 0.0f);

            double smx, smy;
            glfwGetCursorPos(window, &smx, &smy);
            bool overBoxNow = smx >= SPAWN_BTN_X0 && smx <= SPAWN_BTN_X1 && smy >= SPAWN_BOX_BTN_Y0 && smy <= SPAWN_BOX_BTN_Y1;
            bool overSphereNow = smx >= SPAWN_BTN_X0 && smx <= SPAWN_BTN_X1 && smy >= SPAWN_SPHERE_BTN_Y0 && smy <= SPAWN_SPHERE_BTN_Y1;

            drawRoundedRect(SPAWN_BTN_X0, SPAWN_BOX_BTN_Y0, SPAWN_BTN_X1, SPAWN_BOX_BTN_Y1, glm::vec3(0.75f, 0.3f, 0.9f), overBoxNow, 12.0f);
            drawRoundedRect(SPAWN_BTN_X0, SPAWN_SPHERE_BTN_Y0, SPAWN_BTN_X1, SPAWN_SPHERE_BTN_Y1, glm::vec3(0.75f, 0.3f, 0.9f), overSphereNow, 12.0f);

            glm::vec3 spawnTextColor(0.05f, 0.05f, 0.08f);
            float spawnBtnCenterX = (SPAWN_BTN_X0 + SPAWN_BTN_X1) * 0.5f;
            drawText("BOX", spawnBtnCenterX, (SPAWN_BOX_BTN_Y0 + SPAWN_BOX_BTN_Y1) * 0.5f, 4.0f, spawnTextColor);
            drawText("SPHERE", spawnBtnCenterX, (SPAWN_SPHERE_BTN_Y0 + SPAWN_SPHERE_BTN_Y1) * 0.5f, 3.0f, spawnTextColor);

            glEnable(GL_DEPTH_TEST);
            glEnable(GL_CULL_FACE);
        }

        glfwSwapBuffers(window);
    }

    glfwTerminate();
    return 0;
}
