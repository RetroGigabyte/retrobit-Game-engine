# RETRObit Engine

A custom native C++ 3D game engine — general-purpose, not a Sonic game. The current
test scene (checkerboard playground, sphere player, run/jump physics) is built to
exercise and validate late-Sonic-Xtreme-era vibes (bright saturated colors, simple fog,
curved/ramped playgrounds, fast movement) as a proving ground for the engine's
rendering, collision, and editor systems — but that's demo content, not the engine
itself. The engine should stay capable of building other kinds of games. Scratch-style
block coding is a planned future layer on top, not started yet.

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
- Graphics: OpenGL 4.1 core profile, via macOS's native `<OpenGL/gl3.h>` — **no GLAD/loader
  needed** on macOS since Apple exposes core-profile entry points directly (this would need
  a loader like GLAD if ever ported to Windows/Linux).

## Layout

```
retrobit/
  CMakeLists.txt
  src/
    main.cpp       # everything currently lives here: window, input, physics, rendering
    Shader.cpp
    NativeMenu.mm  # Objective-C++, macOS-only: File menu / save dialog (see below)
  include/
    Shader.h
    NativeMenu.h
  shaders/
    basic.vert
    basic.frag
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

**Title screen** (shown at launch): click **NEW** or **OPEN**, or press `N`/`O`.
NEW resets to the hardcoded ramp+pillar scene; OPEN shows the native open dialog
(`Cmd+O` or `File > Open` do the same thing and land you in play the same way — see
Level save/open below). Rendered as an ortho overlay with hand-rolled button
rectangles and a minimal 5x7 bitmap font (`FONT_5X7`) — no text/image-loading
infrastructure exists yet.

**Play mode:**
- `WASD` — move (camera-relative, flattened — no flying)
- Mouse — camera orbit (look)
- `Space` — jump
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
- The window title bar doubles as a debug HUD while in edit mode: shows lock
  state, current tool mode, `selected=<part index>`, `drag=<axis>`, and `LMB=`.
- `Cmd+S` / `Cmd+O` (would be `Ctrl+S` / `Ctrl+O` on a non-Apple build, see below) —
  save/open the level via native dialogs. Work globally, not just in edit mode. Opening
  a level clears the current selection (part indices may not mean the same thing).

## What's implemented

- **Title screen** (`AppState::TITLE` / `AppState::PLAYING`, gates the top of the main
  loop with an early `continue`): NEW rebuilds the default scene via
  `resetToDefaultScene()`; OPEN calls `TriggerOpenDialog()`. Buttons are ortho-projected
  quads (`makeQuad()`), hit-tested with a plain screen-space AABB check against
  `glfwGetCursorPos` (no 3D raycast needed, unlike the editor's picking) — note the
  ortho projection's top-left-origin Y-flip also flips triangle winding, so
  `GL_CULL_FACE` has to be disabled for this one draw pass or the quads vanish.
  `loadLevelFromFile` itself decides whether to call `enterPlayMode()` (via
  `g_appStateForLoad`/`g_enterPlayModeFn`, checked against `AppState::TITLE`) so the
  native File > Open menu item and the title screen's own OPEN button both transition
  correctly, and loading a level while already playing doesn't reset the player/camera.
  Text ("NEW"/"OPEN") is a minimal 5x7 bitmap font (`FONT_5X7`) drawn as small quads —
  see Controls above.
- Window/GL context + render loop (`src/main.cpp`), explicit `glViewport` set from
  `glfwGetFramebufferSize` at startup (previously relied on driver defaults)
- Basic forward-shaded pipeline: `Shader` class, `basic.vert`/`basic.frag` with
  simple diffuse + rim light + exponential-squared fog (the "early 3D playground" look)
- Flat checkerboard ground (analytic plane collision at y=0, not real geometry, for perf) —
  **bounded** to the actual tile area (`GROUND_HALF_EXTENT`), not infinite, so walking
  off the edge really does drop you
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
- **Level save/open** (`.retrobitl` files): `File > Save`/`Open…` in the native macOS
  menu bar, or `Cmd+S`/`Cmd+O`, use an `NSSavePanel`/`NSOpenPanel` to write/read the
  `parts` vector. Format is plain text (`RETROBITLEVEL 1` header, part count, then one
  line per part: position, size, color, rotation quaternion) — human-readable on
  purpose, since there's no tooling to inspect a binary format yet.
  - `src/NativeMenu.mm` / `include/NativeMenu.h`: GLFW has no menu-bar API, so this is
    hand-written Cocoa (`NSMenu`/`NSMenuItem`/`NSSavePanel`/`NSOpenPanel`), built as
    Objective-C++ and gated behind `if(APPLE)` in `CMakeLists.txt` (`project(... OBJCXX)`,
    `file(GLOB ... src/*.mm)`). Exposes `SetupNativeFileMenu(onSaveWithPath,
    onOpenWithPath)` plus `TriggerSaveDialog()`/`TriggerOpenDialog()` — the latter two
    are also called directly from `keyCallback` on `Cmd+S`/`Cmd+O` (`Ctrl+S`/`Ctrl+O` on
    the `#else` non-Apple branch, which doesn't build yet — see below) so the shortcuts
    work without going through the menu.
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

## Known issues / rough edges

- No hover highlight — gizmo handles only turn yellow once you're already dragging,
  so there's no feedback for what you're about to grab before you commit to a click.
- Gizmo/ring/handle geometry is drawn with the normal lit shader (not a flat/unlit
  one), so visibility varies with sun angle — fine for now, would benefit from an
  unlit shader pass if it becomes hard to read.
- No undo and no drag-cancel (e.g. right-click mid-drag to abort) — a bad drag has to
  be manually corrected.
- Only two parts exist in the level; the tool and collision system both generalize to
  more, but nothing's been added yet.
- Nothing auto-loads a level at startup — you always get the hardcoded ramp+pillar
  scene until you manually `Cmd+O` a saved file.
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

1. Curved terrain / loop geometry — the actual "Sonic Xtreme" signature move. Will need
   either a spline-based mesh generator or hand-authored curved meshes, fed into the same
   `buildBoxTriangles`-style triangle list the collision resolver already consumes.
2. Scratch-style block coding layer — not started. Whatever scripting/gameplay-logic API
   gets built for this should be planned as a clean boundary now rather than retrofitted.
3. Address the rough edges above (hover feedback, unlit gizmo shader, undo/cancel) if
   the tool starts feeling hard to use as more parts get added.
