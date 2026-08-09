#pragma once

// Pure data model for RETRObit's block-coding editor. Deliberately has no engine
// coupling (no Shader, no GLFW, no globals) — main.cpp owns rendering (via the
// shared drawButton/drawText lambdas) and execution against the live `parts`
// vector; this header just describes what a block IS and provides layout/
// hit-test math so main.cpp's input handling doesn't need to duplicate geometry
// logic inline.
//
// Storage is still a FLAT array (`std::vector<BlockInstance>`), not a tree —
// nesting (REPEAT) is expressed via each block's `depth` (like Python's
// indentation), not by containment. A REPEAT block's body is "every immediately
// following block one depth deeper, until depth drops back to its own or
// lower" (see findBodyEnd). This is a deliberate simplification over a real
// Scratch-style containment tree: depth is edited with per-block indent/outdent
// buttons rather than drag-into-the-C-shape, since implementing true spatial
// drop-into-container detection is a meaningfully bigger UI problem. It still
// gives genuine nested/repeated execution, just a cruder editing interaction.

#include <glm/glm.hpp>
#include <vector>

enum class BlockType {
    WHEN_START,
    SPAWN_BOX,
    SPAWN_SPHERE,
    MOVE_LAST_PART,
    WAIT,
    REPEAT,
};

struct BlockDef {
    BlockType type;
    const char* label;
    glm::vec3 color;
    int paramCount;
    float paramMin[3];
    float paramMax[3];
    float paramDefault[3];
    float paramStep[3];
};

// One entry per BlockType, in declaration order — BLOCK_DEFS[(int)type] must stay
// valid, so keep this array's order in sync with the enum above.
// Colors match Scratch's actual per-category palette (Events yellow, Looks purple,
// Motion blue, Control orange) rather than arbitrary picks — including WAIT and
// REPEAT sharing the same orange, since Scratch's real Control category does that too.
static const BlockDef BLOCK_DEFS[] = {
    { BlockType::WHEN_START,     "WHEN START",     glm::vec3(1.00f, 0.75f, 0.00f), 0, {0,0,0}, {0,0,0}, {0,0,0}, {0,0,0} }, // Events
    { BlockType::SPAWN_BOX,      "SPAWN BOX",      glm::vec3(0.60f, 0.40f, 1.00f), 0, {0,0,0}, {0,0,0}, {0,0,0}, {0,0,0} }, // Looks
    { BlockType::SPAWN_SPHERE,   "SPAWN SPHERE",   glm::vec3(0.60f, 0.40f, 1.00f), 0, {0,0,0}, {0,0,0}, {0,0,0}, {0,0,0} }, // Looks
    { BlockType::MOVE_LAST_PART, "MOVE LAST PART", glm::vec3(0.30f, 0.59f, 1.00f), 3, {-5,-5,-5}, {5,5,5}, {1,0,0}, {0.5f,0.5f,0.5f} }, // Motion
    { BlockType::WAIT,           "WAIT",           glm::vec3(1.00f, 0.67f, 0.10f), 1, {0,0,0}, {10,0,0}, {1,0,0}, {0.5f,0,0} }, // Control
    { BlockType::REPEAT,         "REPEAT",         glm::vec3(1.00f, 0.67f, 0.10f), 1, {0,0,0}, {50,0,0}, {2,0,0}, {1,0,0} }, // Control
};
static const int BLOCK_DEF_COUNT = sizeof(BLOCK_DEFS) / sizeof(BLOCK_DEFS[0]);

inline const BlockDef& blockDefFor(BlockType type) {
    return BLOCK_DEFS[(int)type];
}

static const int BLOCK_MAX_DEPTH = 3;

struct BlockInstance {
    BlockType type;
    float params[3];
    int depth = 0; // indent level — see the file header comment for what this means

    static BlockInstance fromDef(BlockType type) {
        const BlockDef& def = blockDefFor(type);
        BlockInstance inst;
        inst.type = type;
        for (int i = 0; i < 3; i++) inst.params[i] = def.paramDefault[i];
        inst.depth = 0;
        return inst;
    }
};

// The index just past a REPEAT (or any container) block's body: the first index
// after `index` whose depth is <= the block at `index`'s own depth, or
// `script.size()` if the body runs to the end. Also valid to call on a
// non-container block — it'll just return index+1 or wherever depth next drops,
// which callers that only care about containers won't do.
inline int findBodyEnd(const std::vector<BlockInstance>& script, int index) {
    int baseDepth = script[index].depth;
    for (int j = index + 1; j < (int)script.size(); j++) {
        if (script[j].depth <= baseDepth) return j;
    }
    return (int)script.size();
}

// One active REPEAT's execution state, on a stack in the interpreter (main.cpp) so
// nested REPEATs work — innermost loop is stack.back(). `endIndex` is that loop's
// findBodyEnd(); reaching it either loops back to `startIndex` (if iterations
// remain) or pops the frame and falls through to whatever comes after the loop.
struct LoopFrame {
    int startIndex;
    int endIndex;
    int remaining;
};

// --- Layout constants (shared by rendering and hit-testing so they can never drift apart) ---
namespace BlockLayout {
    constexpr float PALETTE_X0 = 20.0f;
    constexpr float PALETTE_X1 = 220.0f;
    constexpr float PALETTE_ENTRY_H = 50.0f;
    constexpr float PALETTE_ENTRY_GAP = 10.0f;
    constexpr float PALETTE_TOP = 60.0f;

    constexpr float WORKSPACE_X0 = 260.0f;
    constexpr float WORKSPACE_BLOCK_W = 620.0f; // fits "MOVE LAST PART" (longest label) + 3 widened param slots without overlap
    constexpr float WORKSPACE_BLOCK_H = 50.0f;
    constexpr float WORKSPACE_TOP = 60.0f;
    // Blocks stack flush (no gap) so consecutive blocks visually connect — this is
    // what makes the tab (drawBlockShape's notch) actually read as plugging into the
    // block above, the way Scratch's blocks do, instead of floating in a gap.

    constexpr float STEPPER_BTN_W = 24.0f;
    constexpr float INDENT_W = 28.0f;        // per depth level, how far a block's body shifts right
    constexpr float INDENT_BTN_W = 20.0f;    // the </> depth buttons at the right edge of each block
    constexpr float ARM_W = 22.0f;           // width of a REPEAT's left "arm" (the C-shape's left wall)
    constexpr float CLOSE_BAR_H = 26.0f;     // height of a REPEAT's bottom "foot" (the C-shape's closing bar)

    inline float workspaceBlockX0(int depth) {
        return WORKSPACE_X0 + depth * INDENT_W;
    }
    // Indented blocks give up width to the indent, not to the right edge — this is
    // deliberately depth-independent (no parameter) so the whole stack stays
    // visually right-aligned (params/indent buttons line up at any depth).
    inline float workspaceBlockX1() {
        return WORKSPACE_X0 + WORKSPACE_BLOCK_W;
    }

    inline float paletteEntryY0(int index) {
        return PALETTE_TOP + index * (PALETTE_ENTRY_H + PALETTE_ENTRY_GAP);
    }
    inline float paletteEntryY1(int index) {
        return paletteEntryY0(index) + PALETTE_ENTRY_H;
    }
}

// Returns the palette index under (x,y), or -1. `count` is normally BLOCK_DEF_COUNT.
inline int hitTestPalette(float x, float y, int count) {
    using namespace BlockLayout;
    if (x < PALETTE_X0 || x > PALETTE_X1) return -1;
    for (int i = 0; i < count; i++) {
        if (y >= paletteEntryY0(i) && y <= paletteEntryY1(i)) return i;
    }
    return -1;
}

// Each block's top-Y in the workspace. Not a simple index*height formula (like
// paletteEntryY0 above) because a REPEAT's C-shape needs extra vertical space
// reserved right after its body for the closing bar (BlockLayout::CLOSE_BAR_H) —
// this walks the script once and accounts for that, including multiple closing
// bars stacking up where nested REPEATs end at the same point. Computed once per
// frame by the caller and passed into hitTestWorkspace/workspaceInsertIndexForY/
// rendering below, rather than each recomputing it, so they can't disagree.
inline std::vector<float> computeWorkspaceRowY(const std::vector<BlockInstance>& script) {
    std::vector<float> ys(script.size());
    float y = BlockLayout::WORKSPACE_TOP;
    for (int i = 0; i < (int)script.size(); i++) {
        ys[i] = y;
        y += BlockLayout::WORKSPACE_BLOCK_H;
        for (int j = i; j >= 0; j--) {
            if (script[j].type == BlockType::REPEAT && findBodyEnd(script, j) == i + 1) {
                y += BlockLayout::CLOSE_BAR_H;
            }
        }
    }
    return ys;
}

// Returns the workspace block index under (x,y), or -1.
inline int hitTestWorkspace(float x, float y, const std::vector<float>& rowY) {
    using namespace BlockLayout;
    if (x < WORKSPACE_X0 || x > WORKSPACE_X0 + WORKSPACE_BLOCK_W) return -1;
    for (int i = 0; i < (int)rowY.size(); i++) {
        if (y >= rowY[i] && y <= rowY[i] + WORKSPACE_BLOCK_H) return i;
    }
    return -1;
}

// Given a Y position (e.g. where a dragged block was released), returns the stack
// index it should be inserted at — clamped to [0, rowY.size()].
inline int workspaceInsertIndexForY(float y, const std::vector<float>& rowY) {
    using namespace BlockLayout;
    for (int i = 0; i < (int)rowY.size(); i++) {
        if (y < rowY[i] + WORKSPACE_BLOCK_H * 0.5f) return i;
    }
    return (int)rowY.size();
}
