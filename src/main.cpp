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

// Minimal 5x7 dot-matrix font, just the glyphs the title screen needs
// ("PLAYGROUND", "HILLS", "OPEN"). No texture/font-loading infrastructure
// exists yet, so each glyph is drawn as a handful of small quads (same
// technique as the button rectangles) — crude, but it's zero extra
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
    { 'N', {{ 0b10001, 0b11001, 0b10101, 0b10011, 0b10001, 0b10001, 0b10001 }} },
    { 'O', {{ 0b01110, 0b10001, 0b10001, 0b10001, 0b10001, 0b10001, 0b01110 }} },
    { 'P', {{ 0b11110, 0b10001, 0b10001, 0b11110, 0b10000, 0b10000, 0b10000 }} },
    { 'R', {{ 0b11110, 0b10001, 0b10001, 0b11110, 0b10100, 0b10010, 0b10001 }} },
    { 'S', {{ 0b01111, 0b10000, 0b10000, 0b01110, 0b00001, 0b00001, 0b11110 }} },
    { 'U', {{ 0b10001, 0b10001, 0b10001, 0b10001, 0b10001, 0b10001, 0b01110 }} },
    { 'W', {{ 0b10001, 0b10001, 0b10001, 0b10101, 0b10101, 0b11011, 0b10001 }} },
    { 'X', {{ 0b10001, 0b10001, 0b01010, 0b00100, 0b01010, 0b10001, 0b10001 }} },
    { 'Y', {{ 0b10001, 0b10001, 0b01010, 0b00100, 0b00100, 0b00100, 0b00100 }} },
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

enum class AppState { TITLE, PLAYING };
enum class WorldPreset { PLAYGROUND, HILLS };

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
    out << "RETROBITLEVEL 1\n";
    out << g_partsForSave->size() << "\n";
    for (const Part& p : *g_partsForSave) {
        out << p.position.x << " " << p.position.y << " " << p.position.z << " "
            << p.size.x << " " << p.size.y << " " << p.size.z << " "
            << p.color.x << " " << p.color.y << " " << p.color.z << " "
            << p.rotation.w << " " << p.rotation.x << " " << p.rotation.y << " " << p.rotation.z << "\n";
    }
    std::cout << "Saved level to " << path << " (" << g_partsForSave->size() << " parts)\n";
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
    std::cout << "Loaded level from " << path << " (" << g_partsForSave->size() << " parts)\n";

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

void mouseCallback(GLFWwindow* window, double xpos, double ypos) {
    // Camera look tracks mouse movement in both play and edit mode — even with the
    // cursor free for clicking/dragging parts, moving it also turns the camera.
    if (g_firstMouse) { g_lastX = xpos; g_lastY = ypos; g_firstMouse = false; }
    float dx = (float)(xpos - g_lastX);
    float dy = (float)(g_lastY - ypos);
    g_lastX = xpos; g_lastY = ypos;

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

    // Ground: checkerboard of flat tiles for that early-3D playground look
    const int GRID = 16;
    const float TILE = 4.0f;
    Mesh tile = makeBox(TILE, 0.5f, TILE);

    // Alternate world preset: rolling hills terrain, built once at startup
    // (see makeHillTerrain) rather than regenerated per preset switch. Its
    // triangles feed the same collide-and-slide resolver used for props.
    const float HILL_HALF_SIZE = 60.0f;
    const int HILL_SEGMENTS = 48;
    const float HILL_AMPLITUDE = 3.0f;
    const float HILL_FREQUENCY = 0.12f;
    std::vector<Tri> hillTriangles;
    Mesh hillMesh = makeHillTerrain(HILL_HALF_SIZE, HILL_SEGMENTS, HILL_AMPLITUDE, HILL_FREQUENCY, hillTriangles);

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
        shader.setMat4("uModel", model);
        shader.setVec3("uColor", c);
        uiQuad.draw();
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

    AppState appState = AppState::TITLE;
    WorldPreset currentPreset = WorldPreset::PLAYGROUND;
    bool titleLeftMouseWasDown = false;
    bool titlePWasDown = false, titleHWasDown = false, titleOWasDown = false;

    const float BTN_X0 = WIDTH * 0.5f - 160.0f, BTN_X1 = WIDTH * 0.5f + 160.0f;
    const float PLAYGROUND_BTN_Y0 = HEIGHT * 0.5f - 90.0f, PLAYGROUND_BTN_Y1 = HEIGHT * 0.5f - 40.0f;
    const float HILLS_BTN_Y0 = HEIGHT * 0.5f - 25.0f, HILLS_BTN_Y1 = HEIGHT * 0.5f + 25.0f;
    const float OPEN_BTN_Y0 = HEIGHT * 0.5f + 40.0f, OPEN_BTN_Y1 = HEIGHT * 0.5f + 90.0f;

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

        if (appState == AppState::TITLE) {
            bool leftDown = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
            double mx, my;
            glfwGetCursorPos(window, &mx, &my);
            bool clicked = leftDown && !titleLeftMouseWasDown;
            titleLeftMouseWasDown = leftDown;

            bool pDown = glfwGetKey(window, GLFW_KEY_P) == GLFW_PRESS;
            bool hDown = glfwGetKey(window, GLFW_KEY_H) == GLFW_PRESS;
            bool oDown = glfwGetKey(window, GLFW_KEY_O) == GLFW_PRESS;
            bool pPressed = pDown && !titlePWasDown;
            bool hPressed = hDown && !titleHWasDown;
            bool oPressed = oDown && !titleOWasDown;
            titlePWasDown = pDown;
            titleHWasDown = hDown;
            titleOWasDown = oDown;

            bool overPlayground = mx >= BTN_X0 && mx <= BTN_X1 && my >= PLAYGROUND_BTN_Y0 && my <= PLAYGROUND_BTN_Y1;
            bool overHills = mx >= BTN_X0 && mx <= BTN_X1 && my >= HILLS_BTN_Y0 && my <= HILLS_BTN_Y1;
            bool overOpen = mx >= BTN_X0 && mx <= BTN_X1 && my >= OPEN_BTN_Y0 && my <= OPEN_BTN_Y1;

            if (pPressed || (clicked && overPlayground)) {
                resetToPlaygroundScene();
                enterPlayMode();
            } else if (hPressed || (clicked && overHills)) {
                resetToHillsScene();
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

            snprintf(titleBuf, sizeof(titleBuf), "RETRObit Engine - Title Screen | P = Playground, H = Hills, O = Open");
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

            drawButton(BTN_X0, PLAYGROUND_BTN_Y0, BTN_X1, PLAYGROUND_BTN_Y1, glm::vec3(0.25f, 0.75f, 0.35f), overPlayground);
            drawButton(BTN_X0, HILLS_BTN_Y0, BTN_X1, HILLS_BTN_Y1, glm::vec3(0.55f, 0.45f, 0.15f), overHills);
            drawButton(BTN_X0, OPEN_BTN_Y0, BTN_X1, OPEN_BTN_Y1, glm::vec3(0.05f, 0.35f, 0.95f), overOpen);

            // pixelSize 4 (not 5, like the old single-button title screen) so
            // "PLAYGROUND" (10 letters) comfortably fits the 320px-wide button.
            glm::vec3 textColor(0.05f, 0.08f, 0.05f);
            float btnCenterX = (BTN_X0 + BTN_X1) * 0.5f;
            drawText("PLAYGROUND", btnCenterX, (PLAYGROUND_BTN_Y0 + PLAYGROUND_BTN_Y1) * 0.5f, 4.0f, textColor);
            drawText("HILLS", btnCenterX, (HILLS_BTN_Y0 + HILLS_BTN_Y1) * 0.5f, 4.0f, textColor);
            drawText("OPEN", btnCenterX, (OPEN_BTN_Y0 + OPEN_BTN_Y1) * 0.5f, 4.0f, textColor);

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
        glm::vec3 moveDir(0.0f);
        if (!g_editMode) {
            if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) moveDir += camFlatForward;
            if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) moveDir -= camFlatForward;
            if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) moveDir += camRight;
            if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) moveDir -= camRight;
        }

        const float RUN_SPEED = 9.0f;
        const float ACCEL = 40.0f;
        const float GRAVITY = -28.0f;
        const float JUMP_SPEED = 10.0f;

        glm::vec3 horizVel(playerVel.x, 0, playerVel.z);
        if (glm::length(moveDir) > 0.001f) {
            moveDir = glm::normalize(moveDir);
            glm::vec3 target = moveDir * RUN_SPEED;
            horizVel += (target - horizVel) * glm::min(ACCEL * dt, 1.0f);
            playerFacing = atan2f(moveDir.x, moveDir.z);
        } else {
            horizVel -= horizVel * glm::min(8.0f * dt, 1.0f);
        }
        playerVel.x = horizVel.x;
        playerVel.z = horizVel.z;

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
        }

        // Player physics is fully paused in edit mode — otherwise residual velocity
        // (mid-air jump, sliding) keeps integrating while you're trying to use the
        // gizmo, and since the chase camera follows the player, the whole view would
        // drift/fall out from under you while editing.
        if (!g_editMode) {
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

            // analytic ground plane at y = 0 — only in the Playground preset. Bounded
            // to the actual tile area (not infinite) so walking off the edge of the
            // checkerboard really does drop you into the void instead of hitting an
            // invisible floor everywhere. Hills has no flat plane at all — its terrain
            // triangles (merged into propTriangles above) are what the resolve call
            // just above already collided against.
            if (currentPreset == WorldPreset::PLAYGROUND) {
                const float GROUND_HALF_EXTENT = (GRID + 0.5f) * TILE;
                bool overGround = fabsf(playerPos.x) <= GROUND_HALF_EXTENT && fabsf(playerPos.z) <= GROUND_HALF_EXTENT;
                if (overGround && playerPos.y - PLAYER_RADIUS < 0.0f) {
                    playerPos.y = PLAYER_RADIUS;
                    if (playerVel.y < 0.0f) playerVel.y = 0.0f;
                    onGround = true;
                }
            }

            // fallen into the void: gravity just keeps accelerating you down past the
            // level until you cross the threshold, then respawn back at the start
            if (playerPos.y < VOID_Y) {
                playerPos = SPAWN_POS;
                playerVel = glm::vec3(0.0f);
                onGround = true;
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

            // snap in fast when something's obstructing (avoid clipping), ease out otherwise
            float camLerpRate = (clampedDist < idealDist - 0.01f) ? 20.0f : 6.0f;
            camPos += (desiredCamPos - camPos) * glm::min(camLerpRate * dt, 1.0f);
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

        glm::mat4 view = glm::lookAt(eyePos, eyeLookTarget, glm::vec3(0, 1, 0));
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

        // ground: checkerboard tiles (Playground) or the rolling hill mesh (Hills)
        if (currentPreset == WorldPreset::PLAYGROUND) {
            for (int i = -GRID; i <= GRID; i++) {
                for (int j = -GRID; j <= GRID; j++) {
                    glm::mat4 model = glm::translate(glm::mat4(1.0f), glm::vec3(i * TILE, -0.25f, j * TILE));
                    shader.setMat4("uModel", model);
                    bool even = ((i + j) % 2 + 2) % 2 == 0;
                    glm::vec3 c = even ? glm::vec3(0.25f, 0.75f, 0.35f) : glm::vec3(0.20f, 0.65f, 0.30f);
                    shader.setVec3("uColor", c);
                    tile.draw();
                }
            }
        } else {
            shader.setMat4("uModel", glm::mat4(1.0f));
            shader.setVec3("uColor", glm::vec3(0.35f, 0.65f, 0.25f)); // grassy green
            hillMesh.draw();
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

            drawButton(SPAWN_BTN_X0, SPAWN_BOX_BTN_Y0, SPAWN_BTN_X1, SPAWN_BOX_BTN_Y1, glm::vec3(0.75f, 0.3f, 0.9f), overBoxNow);
            drawButton(SPAWN_BTN_X0, SPAWN_SPHERE_BTN_Y0, SPAWN_BTN_X1, SPAWN_SPHERE_BTN_Y1, glm::vec3(0.75f, 0.3f, 0.9f), overSphereNow);

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
