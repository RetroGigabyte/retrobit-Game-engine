# RETRObit Engine

A custom native C++ 3D game engine — general-purpose, not a Sonic game. The current
test scene (checkerboard playground, sphere player, run/jump physics) is built to
exercise and validate late-Sonic-Xtreme-era vibes (bright saturated colors, simple fog,
curved/ramped playgrounds, fast movement) as a proving ground for the engine's
rendering, collision, and editor systems — but that's demo content, not the engine
itself. The engine should stay capable of building other kinds of games. Scratch/
TurboWarp-style block coding is underway — see "Block coding" below for the v1 state.

**Current state note:** `main.cpp` doesn't yet separate "engine" from "this demo" —
renderer, collision, and the Part/gizmo editor are general-purpose, but they're
interleaved in one file with the hardcoded playground/player/physics-tuning. No action
taken on this yet; flagging it so future work doesn't assume the demo content is
load-bearing engine design.

## Stack

- Language: C++17
- Build: CMake
- Windowing/input: GLFW (via Homebrew)
- Math: GLM (via Homebrew)
- Graphics: OpenGL 4.1 core profile, via `include/PlatformGL.h` → macOS's native
  `<OpenGL/gl3.h>` — **no GLAD/loader needed** on macOS since Apple exposes core-profile
  entry points directly. Windows/Linux aren't wired up yet (see Platform support below).

## Platform support

Only macOS actually builds and runs right now, but the codebase is structured so adding
a second platform is additive rather than a rewrite:

- **`src/platform/<platform>/`** holds one dedicated implementation per target platform
  of anything that isn't cross-platform by nature (currently just the native menu
  bar / file dialogs). `include/NativeMenu.h` declares the shared function signatures;
  `src/platform/macos/NativeMenu.mm` is the real Cocoa implementation, and
  `src/platform/stub/NativeMenu_stub.cpp` is a harmless no-op fallback. `CMakeLists.txt`
  picks which one to compile based on `if(APPLE)` — engine code (`main.cpp`) calls these
  functions unconditionally, with **no `#ifdef __APPLE__` at the call sites**. Where
  `TriggerOpenDialog()`'s `false` return (stub, or a canceled real dialog) has a
  sensible fallback, main.cpp uses it instead of special-casing the platform (see the
  title screen's OPEN handler).
- **`include/PlatformGL.h`** picks the right OpenGL header the same way, but by
  `#ifdef` inside one header rather than a whole file — it's a one-line-per-platform
  choice, not enough logic to justify a dedicated source file per platform.
- **Adding a real Windows/Linux build** needs two things this doesn't yet provide:
  an OpenGL loader (e.g. GLAD, vendored into `third_party/` and wired into
  `PlatformGL.h` + `CMakeLists.txt` in place of the current `#error`), and GLFW/CMake
  package availability on that platform (not verified — this has only ever been built
  via Homebrew on macOS). Nobody has attempted either yet.
- **Other targets, noted for later, not started:**
  - **Windows** — GLFW + OpenGL both support it natively; realistically the easiest
    non-Apple target once a loader is vendored in. Native menu bar would need a Win32
    implementation under `src/platform/windows/` (or just keep using the stub, since
    Windows apps don't strictly need a classic menu bar).
  - **Linux** — same GLFW/OpenGL story as Windows; native file dialogs would need
    something like GTK or a portal (xdg-desktop-portal) under `src/platform/linux/`,
    or the stub, same tradeoff.
  - **Android** — a much bigger lift: no GLFW (would need `ANativeActivity`/`ndk_helper`
    or similar), touch input instead of mouse/keyboard, OpenGL ES rather than desktop
    GL, and the whole editor tool assumes a mouse cursor + keyboard shortcuts that
    don't exist on touch. Would need real design work, not just a new platform folder.
  - **3DS** — homebrew toolchain (devkitARM/citro3d), no OpenGL at all, dual screens,
    tiny fixed resources — closer to a second renderer + input backend than a "platform
    file." Interesting for the retro angle but a genuinely separate effort from the
    OpenGL/GLFW path the engine is built around today.

## Layout

```
retrobit/
  CMakeLists.txt
  src/
    main.cpp             # everything currently lives here: window, input, physics, rendering
    Shader.cpp
    platform/            # one dedicated implementation per target platform — see below
      macos/
        NativeMenu.mm    # Objective-C++: real File menu / save & open dialogs (Cocoa)
      stub/
        NativeMenu_stub.cpp  # no-op fallback for every other platform
  include/
    Shader.h
    NativeMenu.h         # platform-agnostic declarations — same signatures, either implementation
    PlatformGL.h         # picks the right OpenGL header per platform
    BlockScript.h        # block-coding data model (types/defs/layout) — no engine coupling
  shaders/
    basic.vert
    basic.frag
    ui.vert         # 2D UI: rounded-rect + puzzle-notch SDF (title/spawn menu/block editor)
    ui.frag
  build/            # cmake build dir (gitignored-worthy, not committed)
  level.retrobitl   # wherever you last saved to — not committed, not auto-loaded yet
```

## Build & run

```
cd retrobit/build
cmake --build .
./retrobit
```

(First time: `mkdir build && cd build && cmake ..` first.)

## Controls

**Title screen** (shown at launch): three stacked buttons, click or press the shortcut —
**PLAYGROUND** (`P`, the hardcoded ramp+pillar scene), **HILLS** (`H`, procedural rolling
terrain, no boxes), or **OPEN** (`O`, native open dialog — `Cmd+O` / `File > Open` do the
same thing and land you in play the same way, see Level save/open below). Rendered as an
ortho overlay with hand-rolled button rectangles and a minimal 5x7 bitmap font
(`FONT_5X7`) — no text/image-loading infrastructure exists yet.

**Play mode:**
- `WASD` — move (camera-relative, flattened — no flying)
- `E` — sprint: a flat 1.6x speed multiplier on top of `RUN_SPEED`, no separate
  accel/ramp. Normal walk mode only — flight mode already uses `E` for its own boost.
- Mouse — camera orbit (look)
- `Space` — jump
- `F` — toggle **flight mode**: StarFox All-Range Mode-style — the ship moves
  forward on its own at all times (no key needed), `W`/`S` steer up/down and
  `A`/`D` strafe left/right (camera-relative), gravity is off — but collision
  stays on (not noclip), so you still can't fly through props/terrain. Mouse
  look is disabled while flying (steering is entirely W/S/A/D, matching a
  stick-only control scheme) and resumes the moment you land. Distinct
  from edit mode's freecam, which already flies and keeps its own WASD+forward
  scheme; `F` only does anything in play mode. Title bar shows
  `[FLIGHT MODE - always forward, W/S up/down, A/D strafe]` while active.
  Flight mode also has some further StarFox 64 (All-Range Mode)-inspired
  extras, ported from what the
  [SF64 decompilation](https://github.com/HarbourMasters/Starship)'s gameplay
  is known for rather than from that repo's code directly (it's a PC
  port/build-tooling repo, not a mechanics reference):
  - `E` — **boost** (temporarily faster flight speed), `C` — **brake**
    (temporarily slower). Not `Q` (already the global quit key) or `Left Ctrl`
    (macOS commonly grabs a held Ctrl for Mission Control/Spaces switching,
    which yanks focus from the app mid-flight and looks like a crash).
  - Double-tap `A` or `D` — **barrel roll**: a quick cosmetic 360 camera roll,
    purely visual (doesn't affect movement/collision).
  - Holding `A`/`D` also **banks the camera** into the turn (rolls toward level
    again when released) instead of staying perfectly level — this is a camera
    roll only since RETRObit is first-person with no visible ship model.
  - **Arena boundary**: flying more than 160 units from spawn pushes you back
    inward instead of allowing infinite freeflight, echoing All-Range Mode's
    bounded play area.
  - `U` — **U-turn**: instantly flips `g_yaw` 180 degrees and reverses current
    velocity to match, so you're immediately flying back the way you came
    instead of coasting through a slow turn. `playerPos` itself is never
    touched — but the chase camera used to ease from "behind the old
    direction" to "behind the new one" over about a second, arcing wildly
    across/through the world since the ideal camera offset flipped instantly;
    that swing read as the player getting reset. Fixed by snapping the camera
    straight to its new spot on the u-turn's frame instead of easing.
- `Esc` — toggle mouse capture
- `/` — enter the edit tool (see below)
- `Q` — quit

**Edit mode** (`/` to enter/exit): mouse starts **locked** — same look-around feel as
play mode, but `WASD` flies a freecam instead of moving the player (full pitch, not
flattened; `Space`/`Left Shift` for up/down). Player physics is fully paused while
editing so the world doesn't drift out from under you.

- `Esc` — toggle **lock/unlock** without leaving edit mode. Locked = fly/look.
  Unlocked = cursor is free to click/drag; mouse movement still also turns the
  camera even while unlocked (same raw input drives both the look and the pick ray).
  Selection persists across lock/unlock, so the flow is: look around (locked) →
  unlock → grab a handle → lock to reposition the view → unlock to keep dragging.
- Left-click a part (while unlocked) to select it.
- `R` — switch the gizmo to **Rotate** (toggles back to Move on a second press).
- `T` — switch the gizmo to **Scale** (toggles back to Move on a second press).
- Drag the gizmo (arrows in Move, rings in Rotate, small cubes in Scale) to edit
  the selected part.
- `M` — open the **spawn menu** (works locked or unlocked; automatically unlocks the
  mouse so you can click immediately). Two buttons — **BOX**/`1` and **SPHERE**/`2` —
  spawn a new part 8 units in front of wherever the freecam is looking (floored so it
  doesn't land underground), auto-selected and in Move mode so you can reposition it
  right away. Press `M` again to close without spawning.
- `B` — open the **block-coding editor** (full-screen, not an overlay — the 3D world
  is hidden while it's open). Drag blocks from the palette (left) onto the workspace
  (center) to build a script top-to-bottom; drag within the workspace to reorder,
  drag onto the red zone (bottom) to delete. Blocks with parameters show `-`/`+`
  steppers next to the value (no free-text entry — there's no text-input widget in
  the engine yet). Each block also has a pair of `-`/`+` **nesting buttons** at its
  far right — outdent/indent it to control REPEAT loop nesting (drag-into-the-C-shape
  isn't implemented; nesting is edited by depth instead — see "Block coding" below).
  Click **RUN** to execute the script against the live 3D world (switches back to the
  normal view while it runs); `Esc` or `B` again returns to the editor without
  running. Press `B` again mid-run to stop and come back to the editor, script intact.
  See "Block coding" below for the full design.
- The window title bar doubles as a debug HUD while in edit mode: shows lock
  state, current tool mode, `selected=<part index>`, `drag=<axis>`, and `LMB=` — or,
  while the spawn menu is open, just that.
- `Cmd+N` / `Cmd+S` / `Cmd+O` (would be `Ctrl+N` / `Ctrl+S` / `Ctrl+O` on a non-Apple
  build, see below) — new/save/open. Work globally, not just in edit mode. New
  unconditionally resets to the Playground preset and (re)enters play, even mid-game —
  unlike Open, there's no dialog/cancel to account for. Save/Open use native dialogs;
  Open clears the current selection (part indices may not mean the same thing).

## What's implemented

- **Title screen** (`AppState::TITLE` / `AppState::PLAYING`, gates the top of the main
  loop with an early `continue`): PLAYGROUND/HILLS/HILLS+ pick a `WorldPreset` and
  rebuild the scene via `resetToPlaygroundScene()`/`resetToHillsScene()`/
  `resetToHillsPlusScene()` (keys `P`/`H`/`J` — `J` since `H` was already taken); OPEN
  calls `TriggerOpenDialog()`. Buttons are ortho-projected quads (`makeQuad()`), hit-tested
  with a plain screen-space AABB check against `glfwGetCursorPos` (no 3D raycast
  needed, unlike the editor's picking) — note the ortho projection's top-left-origin
  Y-flip also flips triangle winding, so `GL_CULL_FACE` has to be disabled for this
  one draw pass or the quads vanish. `loadLevelFromFile` itself decides whether to
  call `enterPlayMode()` (via `g_appStateForLoad`/`g_enterPlayModeFn`, checked against
  `AppState::TITLE`) so the native File > Open menu item and the title screen's own
  OPEN button both transition correctly, and loading a level while already playing
  doesn't reset the player/camera or ground preset. Loading always resets to the
  Playground preset (`g_worldPresetForLoad`) since saved `.retrobitl` files don't carry
  a ground preset yet — only `parts`. Text is a minimal 5x7 bitmap font (`FONT_5X7`)
  drawn as small quads — see Controls above.
- **World presets** (`WorldPreset::PLAYGROUND` / `HILLS` / `HILLS_PLUS`, `currentPreset`
  in `main()`): Playground is the original flat checkerboard + ramp/pillar scene.
  Hills is procedural rolling terrain (`makeHillTerrain`) — a sine-height-fielded grid
  built once at startup (not regenerated per preset switch), with no boxes. Both the
  render mesh (with finite-difference normals) and its world-space collision triangles
  come from the same generator call, so hills collide via the same
  `resolveSphereVsTriangles` path as props — merged into `propTriangles` only when
  `currentPreset == HILLS`. The analytic flat ground plane (see below) is Playground-only
  and now infinite; Hills/Hills+ have no flat-plane fallback at all, so their terrain
  triangles are the only thing holding the player up, and walking off the generated
  grid's edge is still a real void-fall there (unlike Playground, which no longer has
  an edge to walk off).
  **Hills+** (`hillPlusMesh`/`hillPlusTriangles`) is the same generator at a bigger
  scale — half-size 130 vs. Hills' 60 (~4x the area), segment count scaled up in step
  (104 vs. 48) so the terrain doesn't get blocky when stretched over more area, and
  frequency halved (0.06 vs. 0.12) so hills stay similarly-sized bumps rather than
  shrinking relative to the bigger map. It's a genuinely separate mesh/triangle set
  built once at startup alongside Hills, not Hills scaled at draw time. Flight mode's
  arena boundary (see below) also uses a bigger radius on Hills+ (340 vs. 160) so the
  larger map actually has room to fly around in.
- **Spawn menu** (`M` in edit mode, `g_spawnMenuOpen`): a small ortho-overlay menu
  (BOX/SPHERE, click or `1`/`2`) for adding new parts without hand-editing code. Reuses
  the same collide-and-slide/gizmo system every other part uses — a spawned part is a
  normal `Part`, just constructed at runtime instead of hardcoded in `main()`. The
  `drawButton`/`drawText` ortho-UI lambdas used to render it (and the title screen's
  buttons) are defined once, before the main loop, specifically so both screens could
  share them without duplicating the quad/font-drawing logic.
  - **Collision is always an oriented box**, regardless of which mesh is spawned —
    `buildBoxTriangles`/`rayHitsPart` both work purely off `Part::size`, so a spawned
    sphere looks round but collides like its bounding box. A true sphere collider
    would need its own resolver; deferred as a known simplification, not a bug.
  - **Shape doesn't survive save/load** — the `.retrobitl` format only stores
    position/size/color/rotation, not which mesh a part uses, so `loadLevelFromFile`
    always points loaded parts at the box mesh (`g_partMeshForLoad`). A saved sphere
    part will reload looking like a box (still collides identically either way, since
    collision was already box-shaped). Extending the format with a shape ID is the
    fix, not attempted here.
- Window/GL context + render loop (`src/main.cpp`), explicit `glViewport` set from
  `glfwGetFramebufferSize` at startup (previously relied on driver defaults)
- Basic forward-shaded pipeline: `Shader` class, `basic.vert`/`basic.frag` with
  simple diffuse + rim light + exponential-squared fog (the "early 3D playground" look)
- **2D UI pipeline** (`ui.vert`/`ui.frag`, separate from the 3D shader above): draws
  rounded rectangles, with an optional puzzle-connector notch, via a signed-distance-
  field (Inigo Quilez's rounded-box SDF) in the fragment shader — the only way to get
  real curves/notches out of a quad-only renderer without adding an image/texture
  pipeline for pre-made sprites. See "Block coding"'s Visual style entry for the full
  rationale and the `drawRoundedRect`/`drawBlockShape` lambdas it's built on.
- Flat checkerboard ground (Playground preset only; analytic plane collision at y=0,
  not real geometry, for perf) — now genuinely **infinite** (unbounded plane
  collision, no `GROUND_HALF_EXTENT` clamp). Used to be a `GRID x GRID` loop of
  individual tile box meshes (`tile.draw()` per tile — 33x33 = 1089 draw calls/frame),
  bounded to that fixed area since drawing tiles out to any real "infinite" distance
  wasn't feasible; replaced with a single huge plane mesh (`groundPlane`, half-extent
  4000) with the checkerboard pattern painted per-fragment in `basic.frag` from
  world-space position (`uUseCheckerboard`/`uColorB`/`uCheckerSize` uniforms) instead
  of baked into per-tile vertex colors — one draw call instead of ~1089, and it reads
  as infinite well past where the old grid's edge used to drop you into the void.
  `uUseCheckerboard` must be reset to `0` right after the ground draw call (`shader`
  is reused for every other 3D/UI draw this frame) or every later draw would start
  painting a checker pattern instead of respecting `uColor` — same footgun class as
  the earlier `shader.use()`-not-implicit bug, now with a per-draw uniform instead of
  program binding. `groundPlaneTriangles` (12 triangles — a box's triangle count
  doesn't grow with its size) gives it real collision geometry too, merged into
  `propTriangles` for the Playground preset: flight mode only ever collided against
  `propTriangles` (parts + hills), never the analytic y=0 plane (that only runs in
  the non-flight branch), so flying low over Playground used to sink straight through
  with no collision response at all — invisible with the old thin tile grid, obvious
  once the ground became one big rendered box (screenshot showed the player sphere
  visibly clipped into the floor).
- Player: a sphere stand-in for Sonic, with run/accel/friction, gravity, jump
- Void/respawn: gravity keeps accelerating you down past the level with no floor once
  you're off the ground bounds or through a gap; crossing `VOID_Y` (-20) resets you to
  `SPAWN_POS` with zero velocity
- Chase camera: mouse-orbit, smoothed follow, **with collision** — raycasts from the
  look target toward the ideal camera position against prop triangles + ground plane,
  pulls the camera in so it doesn't clip through geometry
- General collision: sphere-vs-triangle collide-and-slide (`resolveSphereVsTriangles`),
  built from a `Tri` list generated per-frame from each `Part`'s current transform
  (`buildBoxTriangles`). This is the generalized system meant to eventually support
  curved/loop geometry too — anything that can produce a triangle list can be collided
  against without touching the resolver.
- FPS counter in the window title bar (updated 2x/sec; replaced by the edit-mode debug
  readout while the tool is open)
- **Editable level parts** (`struct Part`): two exist right now — an orange ramp
  (12° tilt) and a red pillar — holding mutable position/size/rotation/color, edited
  live by the tool below. `Part::modelMatrix()` is translate+rotate only (what
  collision/picking use); `Part::renderModelMatrix()` adds a scale by `size` for
  drawing. The render mesh is one shared unit cube — `size` is the single source of
  truth for a part's actual dimensions, for both what you see and what you collide with.
- **Move/Rotate/Scale tool** (Roblox-Studio-style), see Controls above for the full
  interaction flow. Implementation notes:
  - Picking: mouse-ray unprojection (`inverse(proj)`/`inverse(view)`) + ray-vs-oriented-box
    in the part's local space (`rayHitsPart`)
  - Move: drag distance along an axis via closest-point-between-two-lines
    (`closestParamOnLineToRay`) — this had a sign-inversion bug (grab-range check almost
    always failed) that's now fixed and verified by hand-derivation
  - Rotate: ray-plane intersection (`rayPlaneIntersect`) + `atan2` angle tracking in an
    axis-appropriate 2D basis (`ringBasis`), applied as `angleAxis(delta, axis) * startRotation`
    so repeated partial drags compose correctly instead of drifting
  - Scale: handles sit on the part's **local** +axis faces (rotated with the part, so
    resizing the tilted ramp resizes along its actual slope, not world axes); the
    opposite face is anchored and stays fixed while the dragged face moves, matching
    Roblox's face-resize behavior; size is clamped to a minimum to avoid inverting
  - Collision triangles rebuild every frame from live part transforms, so moved,
    rotated, and resized parts all collide correctly immediately
- **Level new/save/open** (`.retrobitl` files): `File > New`/`Save…`/`Open…` in the
  native macOS menu bar, or `Cmd+N`/`Cmd+S`/`Cmd+O`. Save/Open use an
  `NSSavePanel`/`NSOpenPanel` to write/read the `parts` vector (and, as of v2, the
  block script — see "Block coding" below); New has no dialog, it just resets.
  Format is plain text (`RETROBITLEVEL <version>` header, part count, then one line
  per part: position, size, color, rotation quaternion, then — version 2+ only —
  block count and one `<blockType p0 p1 p2>` line per block) — human-readable on
  purpose, since there's no tooling to inspect a binary format yet. `loadLevelFromFile`
  only reads the block section `if (version >= 2)`, so pre-existing version-1 files
  (parts only) still load fine; version 1 also has no block section to reset from, so
  loading one clears whatever script was already in the editor.
  - `src/platform/macos/NativeMenu.mm` / `include/NativeMenu.h` (see the Platform
    support section below for why this lives under `src/platform/`): GLFW has no
    menu-bar API, so this is hand-written Cocoa (`NSMenu`/`NSMenuItem`/
    `NSSavePanel`/`NSOpenPanel`), built as Objective-C++. Exposes
    `SetupNativeFileMenu(onNew, onSaveWithPath, onOpenWithPath)` plus
    `TriggerNew()`/`TriggerSaveDialog()`/`TriggerOpenDialog()` — all three are also
    called directly from `keyCallback` on `Cmd+N`/`Cmd+S`/`Cmd+O` (`Ctrl+N`/`Ctrl+S`/
    `Ctrl+O` on the `#else` non-Apple branch, which doesn't build yet) so the
    shortcuts work without going through the menu. `main.cpp` wires New through
    `g_newSceneFn`/`triggerNewScene()` the same `std::function`-indirection pattern
    used for `g_enterPlayModeFn`, since the native callback is a plain function
    pointer with no way to capture the reset/enterPlayMode lambdas directly.
  - Loading replaces the `parts` vector **in place** (`*g_partsForSave =
    std::move(loaded)`), not by swapping the pointer — everything else that references
    `parts` (collision rebuild, rendering) sees the update automatically next frame.
    Selection (`selectedPart`/`dragAxis`) is cleared on load since old indices may no
    longer point at the same part.
  - The startup scene (ramp + pillar, hardcoded in `main()`) is unaffected by
    save/load — nothing auto-loads a level at launch yet. Opening a file only replaces
    `parts` once you explicitly do it.
  - `g_partsForSave`/`g_partMeshForLoad`/`g_selectedPartForLoad`/`g_dragAxisForLoad` are
    raw pointers into `main()`'s locals, set once after each is constructed — needed
    because the native callbacks are plain `void(const char*)` function pointers with
    no way to capture state by reference.

- **Block coding** (`B` in edit mode — see Controls above for the interaction flow).
  Original plan/rationale: `~/.claude/plans/luminous-bubbling-unicorn.md` (the
  approved implementation plan the v1 milestone was built from). Explicitly modeled
  on **TurboWarp** (a fast Scratch 3.0 implementation) — the user's own scripting
  language, [Retron](https://github.com/RetroGigabyte/Retron), was deliberately set
  aside for this rather than used, at least for now.
  - **Data model** (`include/BlockScript.h`, no engine coupling — no `Shader`, no
    GLFW, no globals): `enum class BlockType` (six blocks — see below), `struct
    BlockDef` (label/color/param ranges, one static `BLOCK_DEFS[]` array), `struct
    BlockInstance` (a `BlockType` + up to 3 float params + an indent `depth` — one
    per block placed in a script), `struct LoopFrame` (a REPEAT's execution state:
    body start/end index + remaining iterations), and pure layout/hit-test functions
    (`hitTestPalette`, `hitTestWorkspace`, `workspaceInsertIndexForY`, `findBodyEnd`)
    so main.cpp's input handling doesn't duplicate geometry/nesting math inline.
  - **Block set** — colors match Scratch's actual per-category palette (not
    arbitrary picks): Events yellow, Looks purple, Motion blue, Control orange —
    including WAIT and REPEAT sharing the exact same orange, since real Scratch's
    Control category does that too:
    | Block | Category color | Params | Effect |
    |---|---|---|---|
    | WHEN START | Events (yellow) | none | Marks the script's start (visual only — execution always starts at index 0) |
    | SPAWN BOX / SPAWN SPHERE | Looks (purple) | none | `parts.push_back(...)`, same as the spawn menu |
    | MOVE LAST PART | Motion (blue) | dx, dy, dz | Offsets `parts.back().position` — operates on the most recent part rather than introducing variables/references |
    | WAIT | Control (orange) | seconds | Holds the current step for real time (visible since Run switches to `AppState::PLAYING`) |
    | REPEAT | Control (orange) | count | Loops the blocks nested under it (see Nesting below) |

    **Still deliberately deferred**: variables, custom blocks, multiple
    scripts/sprites, conditionals, free-text numeric entry (steppers avoid needing
    a text-input widget, which still doesn't exist anywhere in the engine).
  - **Nesting (REPEAT) is depth-based, not a containment tree.** Storage is still a
    **flat** `std::vector<BlockInstance>` — each block just carries an indent `depth`
    (like Python), and a REPEAT's body is "every immediately following block one
    depth deeper, until depth drops back to its own or lower" (`findBodyEnd`). Depth
    is edited with per-block `-`/`+` **indent buttons** (far right of each block),
    not by dragging a block spatially into a C-shape — implementing real
    drop-into-container detection was explicitly scoped out as a meaningfully bigger
    UI problem than indentation buttons, while still giving genuine nested/repeated
    execution. A block's depth is capped at one more than the block above it (can't
    nest inside a loop that doesn't visually exist yet) and at `BLOCK_MAX_DEPTH` (3).
  - **`AppState::BLOCK_EDITOR`** — a third state alongside `TITLE`/`PLAYING`, added
    to `main.cpp` following the exact shape `TITLE` already established: its own
    input polling, its own ortho `drawButton`/`drawText` pass, its own `glfwSwapBuffers`
    + early `continue` that skips the rest of the loop for that frame. Entered/exited
    via `B` in edit mode; since keyCallback can't reach `appState` directly (it's a
    `main()`-local, same problem `g_appStateForLoad` solves for Open), `B` just raises
    `g_toggleBlockEditorRequested`, consumed once per frame near the top of the loop.
    `enterBlockEditorMode()`/`exitBlockEditorMode()` (lambdas, same shape as
    `enterPlayMode`) only touch mouse-lock state — no player/camera/parts reset,
    since opening the editor isn't a level reset.
  - **Interaction**: palette (left) → click-drag onto the workspace (center) inserts
    a new `BlockInstance`, landing at the same depth as whatever's above the drop
    point so dropping mid-run of indented blocks doesn't reset them to top level;
    drag within the workspace reorders; drag onto the trash zone (bottom) deletes.
    Per-param `-`/`+` steppers and the indent buttons are both hit-tested *before*
    treating a click as a block-body drag-start, so tweaking a value or nesting depth
    doesn't accidentally start a reorder. All of this uses the same per-frame
    `glfwGetMouseButton` + `WasDown` edge-detect idiom used everywhere else in the
    engine (title screen, spawn menu, gizmo drag) — no GLFW mouse-button callbacks
    were added.
  - **Visual style**: real rounded rectangles with a puzzle-connector notch, via a
    dedicated SDF shader (`ui.vert`/`ui.frag`, see "2D UI pipeline" above) rather than
    the flat-quad-plus-overlay-tab hack the first pass used. Two lambdas, defined
    once alongside `drawButton`/`drawText`:
    - `drawRoundedRect(x0,y0,x1,y1,color,hovered,radiusPx)` — generic rounded button
      (steppers, indent buttons, RUN, trash zone, and the title screen/spawn menu
      buttons, switched over too for a consistent look across every UI screen).
    - `drawBlockShape(x0,y0,x1,y1,color,hovered,hasTab)` — same, plus the notch, used
      only for palette entries and workspace blocks (`BLOCK_MAX_DEPTH`-sized radius,
      fixed tab offset from the left edge).
    Both switch the active GL program to `uiShader` and set its uniforms (`uColor`,
    `uHalfSizePx`, `uRadiusPx`, `uHasTab`, `uTabOffsetFromLeftPx`) each call — this
    surfaced a real bug: `drawButton`/`drawText` (still flat, used for glyph pixels —
    tiny per-pixel quads would just look like blobs if independently rounded) assumed
    `shader` stayed the bound GL program for an entire screen's draw calls, which
    broke the moment a `drawRoundedRect`/`drawBlockShape` call left `uiShader` bound
    in between. Fixed by having `drawButton` call `shader.use()` itself rather than
    relying on caller ordering — `Shader::setMat4`/`setVec3`/etc. don't call `use()`,
    they just target whichever program is currently bound via `glUniform*`, so this is
    a real footgun for future callers, not just a one-off bug. Also added
    `Shader::setVec2` for `uHalfSizePx`.
    Nesting/params still indent from a fixed left edge while staying right-aligned
    across depths (`workspaceBlockX0(depth)` vs. the depth-independent
    `workspaceBlockX1()`).

    **Blocks stack flush now (no gap)** — `BlockLayout::WORKSPACE_BLOCK_GAP` is gone
    entirely, not just set to 0, since the tab/notch only reads as "plugging into"
    the block above when there's no gap for it to float in. This is what pushed a
    real layout change: block Y-position stopped being a simple `index * (height +
    gap)` formula the moment REPEAT needed a **true C-shape** — a left arm
    (`BlockLayout::ARM_W`, from the REPEAT block down to its closing bar) plus a
    **bottom closing bar** (`CLOSE_BAR_H`) right after the body, not just a thin
    spine down the side. The closing bar needs real reserved vertical space that
    isn't tied to any `BlockInstance`, so per-block Y is now computed once per frame
    by `computeWorkspaceRowY()` (walks the script, adds `CLOSE_BAR_H` after every
    REPEAT's body — including stacking multiple bars where nested loops close at the
    same point) into a `rowY` array that hit-testing (`hitTestWorkspace`,
    `workspaceInsertIndexForY`) and rendering all index into, instead of each
    recomputing position independently and risking disagreement. `workspaceBlockY0`/
    `Y1(index)` were removed rather than left around unused, since they'd compute the
    wrong (pre-C-shape) position now.
  - **Execution**: `RUN` sets `scriptRunning = true`, resets `runningStep`/`waitTimer`/
    `loopStack`, and calls `exitBlockEditorMode()` (switches to `AppState::PLAYING`
    so the 3D world is visible) — deliberately does **not** clear `g_editMode`, so `B`
    still works to return to the editor mid-run. A step block placed right after the
    player-physics section (guarded by `scriptRunning`, not `g_editMode`, since Run
    intentionally leaves edit mode set) advances `runningStep` through `blockScript`
    once per frame. Before executing whatever's at `runningStep`, a `while` loop pops/
    loops any `loopStack` frames whose body has just been reached the end of (a
    `while`, not `if`, since falling out of one loop can immediately land on its
    parent's end too) — `REPEAT` itself just pushes a `LoopFrame` and jumps into its
    body, or skips straight to `findBodyEnd` if the count is 0 or the body is empty.
    `WAIT` counts `waitTimer` down by `dt` before letting the step advance. Spawn
    position is a **fixed offset from `playerPos`** (`playerPos + (0, 2, 3)`), not
    camera-relative — avoids depending on `eyePos`/`camFront`, which aren't computed
    until later in the frame (the chase-camera section comes after script execution
    in the loop).
  - **Save format**: `.retrobitl` is at version 3 now — each block line gained a
    trailing `depth` column. `loadLevelFromFile` only reads it `if (version >= 3)`,
    so version-2 files (blocks, no depth) load with every block at depth 0 — exactly
    what they meant, since v2 predates REPEAT.
  - **Visually confirmed twice**, both times catching real bugs no amount of
    compiling clean would have found:
    1. Flat-rectangle v1 pass — layout logic (palette, workspace stack, steppers,
       indent buttons, trash zone, REPEAT with its param) positioned sensibly.
    2. Rounded-rect pass — **segfaulted** the instant a block was placed. Root
       cause: `rowY` (per-block Y positions, from `computeWorkspaceRowY`) was
       computed once at the top of the frame, but the drag-release handling later
       that same frame could insert/erase into `blockScript`, changing its size.
       The render loop right after iterates `blockScript.size()` and indexes
       `rowY[i]` — with `blockScript` now bigger than the `rowY` it was computed
       from, that's an out-of-bounds `std::vector` read. Fixed by recomputing
       `rowY = computeWorkspaceRowY(blockScript)` again immediately after the
       insert/erase logic, before rendering uses it. The lesson: any per-frame
       array derived from `blockScript` needs recomputing after *every* point in
       the frame that can resize it, not just once at the top.
    That same round also surfaced two real overflow bugs once actually seen:
    `drawText` only takes a **center** point, not a left edge, so a long label
    like "MOVE LAST PART" — anchored at a fixed offset that assumed shorter text —
    overflowed past its own block's **left** edge into whatever sat before it
    (visible as text bleeding between the palette and workspace columns). And the
    per-param value text (`"%.1f"`) sat in a slot only 36px wide, narrower than
    worst-case text like `"-5.0"`, so the `+` button (drawn after it) silently
    painted over the trailing digit — `"1.0"` rendered as `"1."`. Fixed by adding
    `textWidthPx()` so labels can be properly left-anchored (`leftEdge +
    width/2`), and by widening the per-param slot (`PARAM_SLOT_W`/
    `PARAM_PLUS_OFFSET`) and `WORKSPACE_BLOCK_W` (520→620) so text has real room
    regardless of how many digits/minus signs a value has. Block/palette label
    text also switched to white (was dark 0.05, illegible-ish against Scratch's
    actual saturated category colors) — matches real Scratch, which uses white
    text on every block regardless of category.

## Known issues / rough edges

- No hover highlight — gizmo handles only turn yellow once you're already dragging,
  so there's no feedback for what you're about to grab before you commit to a click.
- Gizmo/ring/handle geometry is drawn with the normal lit shader (not a flat/unlit
  one), so visibility varies with sun angle — fine for now, would benefit from an
  unlit shader pass if it becomes hard to read.
- No undo and no drag-cancel (e.g. right-click mid-drag to abort) — a bad drag has to
  be manually corrected.
- Only two parts exist in the Playground preset; the tool and collision system both
  generalize to more, but nothing's been added yet.
- `cameraUnobstructedDistance`'s hardcoded flat-plane-at-y=0 fallback (used for chase-cam
  collision) doesn't know about Hills terrain — it's harmless (can only ever pull the
  camera closer in, never let it clip further out) but not strictly correct once hills
  amplitude/frequency change enough for the plane assumption to matter.
- `Ctrl+S`/`Ctrl+O` on non-Apple builds are stubbed (the key combos are detected in
  `keyCallback`, but the `#else` branch has no dialog implementation, since the whole
  engine is macOS-only right now — `<OpenGL/gl3.h>`, Cocoa). Not a real gap yet, just
  noting the shape is there for whenever a cross-platform build becomes a goal.

## Design decisions / rationale worth knowing

- OpenGL over Metal: chosen for speed of solo development (way more reference material),
  at the cost of using a deprecated-but-functional API on macOS. Metal port is a possible
  future step but not planned near-term.
- Ground is an analytic plane, not real triangle geometry, purely for performance (the
  16x16 tile grid would be ~13k triangles if meshed, brute-force collision against that
  every frame is wasteful for a flat surface). Props use real triangle collision. If the
  ground ever needs to be non-flat (ramps built into the ground, curved terrain), it will
  need to join the same triangle-list system as props.
- Edit mode locks the mouse by default (rather than starting unlocked) so that opening
  the tool feels continuous with play-mode look controls; `Esc` is the dedicated
  lock/unlock toggle so clicking/dragging and freecam repositioning don't fight over
  the cursor.
- Scale resizes from a fixed opposite face (Roblox-style) rather than symmetrically
  from the center, so resizing doesn't require also repositioning to keep an edge in place.

## Next steps (not started)

1. Scratch-style block coding layer — not started. Whatever scripting/gameplay-logic API
   gets built for this should be planned as a clean boundary now rather than retrofitted.
2. Address the rough edges above (hover feedback, unlit gizmo shader, undo/cancel) if
   the tool starts feeling hard to use as more parts get added.
3. Tune/playtest the loop track (see "Loop track" below) — radius, width, and run
   speed haven't been validated against each other yet.

## Loop track (Playground preset)

The actual "Sonic Xtreme" signature move — a curved vertical loop, `makeLoopTrack()`,
Playground-only, centered at `(LOOP_CENTER_X, 0, LOOP_CENTER_Z)` = `(30, 0, 0)` with
radius 7 and half-width 3.5, built once at startup like the ground plane/hills. It's a
thin ribbon (2 rows across the width, swept around a circle in the X-Y plane, 48
segments), not a full tube — resolving collision against it works because
`resolveSphereVsTriangles` derives its push-out normal purely from `(sphere center -
closest point on triangle)`, not triangle winding, so a zero-thickness ribbon acts as
real two-sided collision geometry, same as the ribbon-like hill terrain. Parametrized
so `theta=0` sits at ground level with zero slope (`dy/dtheta = R*sin(0) = 0`) —
tangent to the flat ground, so it needs no separate entry/exit ramp; you just run
straight onto it from the Playground checkerboard. Getting around the loop relies on
having enough speed when you hit the bottom (same principle as a marble staying in a
curved pipe via normal force from the collide-and-slide resolver) — radius 7 against
`RUN_SPEED = 9`/`GRAVITY = -28` hasn't actually been playtested, so the numbers may
need tuning if it doesn't complete the loop in practice. Triangles are merged into
`propTriangles` for the Playground preset (same block as `groundPlaneTriangles`), and
it draws as a flat steel-grey ribbon right after the ground plane.
