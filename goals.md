# Goals

A running list of where RETRObit Engine is headed. Not a roadmap with dates — just
what's aimed for, roughly in order of how realistic/near-term it is. See `CLAUDE.md`
for what's actually implemented today.

## Near-term

- U-turn (`U`, flight mode) is now a smooth animated 180 turn (`g_yaw` eased over 0.7s
  via smoothstep) instead of an instant snap — the instant version force-reversed
  velocity and hard-cut the camera to avoid a visible swirl, which read as too
  abrupt/mechanical. The animated version needs neither hack: existing per-frame
  steering physics (`playerVel` blending toward `camFront * FLIGHT_SPEED`) naturally
  arcs the ship around as `g_yaw` rotates, and the camera's normal easing tracks it
  since `playerPos` never jumps. Guarded against retriggering mid-turn. Also had to
  make the U-turn count as "steering" for the fast velocity blend rate — otherwise
  playerVel couldn't keep pace with the rotating camFront and the path came out as a
  sharp-cornered L instead of a smooth U (user: "that is more of an L-turn"). User
  clarified further: "the plane goes up and rotates to go back" — added a pitch-up
  hump (peaks mid-turn, lands back level) and a full 360 roll (reusing the barrel
  roll's camFront-axis up-vector rotation) on top of the yaw animation, so it's an
  actual climbing flip maneuver instead of a flat spin.
- Fixed a real bug: barrel roll (double-tap A/D) could retrigger mid-roll, since
  tapping A/D repeatedly to steer (encouraged by the forceful turn tuning) often
  satisfies the double-tap window on nearly every tap. Each retrigger reset the roll
  before it finished, flipping the camera upside-down over and over — reported by the
  user as the screen "keeps going up and down." Fixed by guarding the trigger on
  `barrelRollTimer <= 0`.
- Sprint (`E`, normal walk mode) — flat 1.6x speed multiplier on `RUN_SPEED`. Reuses
  `E` from flight mode's boost since the two modes are mutually exclusive.
- Flight mode's arena boundary is now a wraparound (Asteroids-style) instead of a
  push-back — flying past the edge teleports you to the opposite side with velocity
  untouched, reading as an infinite loop of space rather than bouncing off a solid
  wall (which is how the old push-back + cancel-outward-velocity version read). The
  chase camera also snaps instantly on wrap, same fix as U-turn, for the same reason
  (`playerPos` jumps clear across the arena in one frame).

- **Infinite Playground ground** — the checkerboard used to be a bounded `GRID x GRID`
  loop of individual tile meshes (1089 draw calls/frame, and walking past its edge
  dropped you into the void). Replaced with a single huge plane (`groundPlane`) and
  the checker pattern painted per-fragment in `basic.frag` from world position
  (`uUseCheckerboard`/`uColorB`/`uCheckerSize`), so it's one draw call and reads as
  genuinely infinite. Ground collision is now an unbounded plane check too (no more
  `GROUND_HALF_EXTENT`). Hills/Hills+ are unaffected — they still have real terrain
  edges and a real void-fall past them. Also gave the ground real collision geometry
  (`groundPlaneTriangles`, merged into `propTriangles`) since flight mode never
  collided against the analytic plane and was clipping straight through — confirmed
  by the user's screenshot showing the player sphere visibly sunk into the floor.
- **Hills+ world preset** — a bigger version of Hills (title screen `J`, or click
  HILLS+): half-size 130 vs. Hills' 60 (~4x area), segments scaled up to 104 (was 48)
  so it doesn't go blocky, frequency halved so hills stay similarly-sized bumps.
  Genuinely separate mesh/triangles built once at startup, not Hills scaled at draw
  time. Flight mode's arena boundary is also widened on Hills+ (340 vs. 160) so the
  bigger map has room to actually fly around in. Compiles clean (incl. strict
  `-Wall -Wextra`) but not yet visually confirmed by the user.

- Engine/demo separation — pull the renderer, collision system, and editor tool apart
  from the current hardcoded playground scene so the engine is usable for games other
  than the Sonic-Xtreme-style test level it grew up around.
- **Loop track — implemented, Playground preset** (see `CLAUDE.md`'s "Loop track"):
  the actual Sonic-Xtreme signature move. A thin curved ribbon (`makeLoopTrack()`,
  radius 7, centered 30 units along +X from spawn), tangent to the ground so no ramp
  is needed, collision merged into `propTriangles`. Compiles clean but not yet
  playtested — whether `RUN_SPEED = 9` is actually enough to complete a radius-7 loop
  against `GRAVITY = -28` is unverified; may need radius/speed tuning.
- Flight mode (`F`, play mode) is now StarFox All-Range Mode-style: constant
  automatic forward movement (held at a fixed speed regardless of steering —
  vertical/strafe are additive on top rather than sharing a normalized budget with
  forward, which used to make "up" feel wildly fast and forward speed wobble),
  `W`/`S` steer up/down, `A`/`D` strafe left/right with a stronger, faster-blending
  turn force (now 34 speed / 140 accel, up from 8/40 originally — turning kept
  feeling weak/mushy through two rounds of tuning), faster vertical (14 speed, up
  from 6, also now blending at the same fast 140 accel as turning rather than the
  slower 40 forward uses), a `U` u-turn (instant 180 yaw
  flip + velocity reversal, with the chase camera hard-snapped to its new spot
  instead of easing — the easing swing across the world used to look like the
  player's position getting reset, which it never actually did), mouse look
  disabled while flying (steering is stick/keys-only), plus
  boost/brake (`E`/`C` — not `Left Ctrl`, which macOS grabs for Mission
  Control/Spaces and looked like a crash), barrel roll (double-tap `A`/`D`), turn
  banking, and a 160-unit arena boundary that pushes you back instead of infinite
  freeflight. See `CLAUDE.md`'s Play mode controls. Compiles clean (including
  strict `-Wall -Wextra`) but not yet visually confirmed by the user — camera roll
  math (rotating the `lookAt` up vector around `camFront`) is untested in practice.
- **Block coding — in progress** (see `CLAUDE.md`'s "Block coding"): a real, if
  minimal, TurboWarp-style visual editor (`B` in edit mode) — drag/reorder/delete
  blocks, `-`/`+` param steppers, a REPEAT block with genuine nested/looped
  execution (depth-based, not a drag-into-C-shape containment model — see CLAUDE.md
  for why), real rounded/notched block shapes via a dedicated SDF shader
  (`ui.vert`/`ui.frag`, not just flat rectangles), blocks that stack flush (no gap,
  so the notch actually reads as plugging into the block above), a REPEAT rendered
  as a true C-shape (left arm + bottom closing bar, not just a side spine) with real
  reserved layout space for it, Scratch-accurate per-category colors, right-aligned
  params across indent depths, Run against the live 3D world, all saved/loaded as
  part of `.retrobitl` (now version 3). Explicitly *not* built on Retron (the user's
  own scripting language) — set aside for now, may return to it later. The
  flat-rectangle layout was confirmed working from a real screenshot; everything
  since (rounded shapes, flush stacking, the true C-shape) hasn't been seen yet
  (compiles clean, including a strict `-Wall -Wextra` pass, but is otherwise
  unverified). Next up, roughly in order: actually see this version run and tune
  whatever looks off; variables; more block categories (conditionals, more part
  manipulation); real spatial drag-into-loop nesting, if the indent-button
  approach turns out to feel wrong in practice.

## Platform support

Only macOS builds today. Roughly ordered by how close each one is to what the engine
already runs on — GLFW + desktop OpenGL, no official console SDKs (not paying for those,
so anything console-side means homebrew toolchains):

- **Windows** — GLFW and OpenGL both support it natively; mainly needs a loader
  (e.g. GLAD) vendored in. The most realistic next platform.
- **Linux** — same story as Windows, same missing loader. Native file dialogs would
  need a GTK/portal implementation (or can keep using the no-op stub).
- **3DS** — homebrew toolchain (devkitARM/citro3d), no OpenGL, dual screens. A real
  second renderer/input backend, not a drop-in port — appealing for the retro angle,
  but a genuinely separate effort.
- **PS Vita** — homebrew toolchain (VitaSDK), similar story to 3DS: different graphics
  API, different input model, no official support.
- **Wii** — homebrew toolchain (devkitPPC/libogc via the Homebrew Channel), one of the
  most mature homebrew scenes out there. Fixed-function GX graphics, not OpenGL, and
  standard-def output — another real renderer/input backend, but a well-documented one.
- **Wii U** — homebrew toolchain (devkitPPC + the Aroma/Tiramisu environment), less
  mature than Wii's scene but active. Dual-screen-ish (GamePad + TV) like 3DS, GX2
  graphics API rather than OpenGL.
- **Xbox 360 / PS3 / PS4** — homebrew route, not official devkits (not paying for those).
  Xbox 360 and PS3 both have mature jailbreak/homebrew scenes, and with their storefronts
  effectively shut down there's no official ecosystem left to conflict with anyway — if
  anything that makes homebrew the *only* realistic path for those two now. PS4 homebrew
  exists too (firmware-version-dependent, less mature than 360/PS3). None of this has
  been investigated yet — toolchain, graphics API, and packaging are all unknowns per
  console — but it's the intended approach, not official SDKs.
- **Nintendo Switch** — homebrew route (Atmosphère CFW + devkitA64/libnx), not an
  official Nintendo devkit. Mature, active homebrew scene. Graphics would go through
  deko3d/nouveau rather than desktop OpenGL, so same "real renderer backend" caveat as
  the other homebrew consoles above.
- **ESP32** — via [ESP-Nix](https://github.com/RetroGigabyte/ESP-Nix), a real declarative
  OS/shell for ESP32 (WROOM32E, with ESP32-S3 builds underway) built separately from
  RETRObit: Unix-like commands, its own scripting language (Retron — variables,
  functions, recursion), a WiFi file server + browser terminal, OTA updates, and even a
  runtime loader for compiled `.o` code (`runmod`). It's already headed toward
  retro/Sonic-adjacent territory on its own — it references Sonic decompilation cores
  (RSDKv3/v4/v5: Mania, Sonic CD, Sonic 1&2) and `retro-go`, and is GPL-2.0 specifically
  to match those cores' licensing. There's a planned CRT/composite-video-output project
  that Retron's `DRAW` command is already waiting on, which is the natural hook-up point
  for anything RETRObit-shaped, once it exists. The graphics API itself is expected to
  stay pretty simple (matches the hardware — see the ceilings below), but it's still
  work in progress, not settled yet. Blocked on setting up a video for it first —
  nothing further to report until that happens. Hardware ceilings to plan
  around whenever it does: no OpenGL, ~300KB usable RAM, no MMU/paging.

## Why list consoles the project can't realistically ship on yet

Because it's useful to know what "supporting more platforms" would actually require
before deciding to chase it — some of the above are a weekend of loader/CMake work,
others are a different renderer entirely, and others need access nobody currently has.
Keeping that distinction visible here is the point of this file.
