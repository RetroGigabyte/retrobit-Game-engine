# RETRObit Engine

> **AI Disclaimer:** This project was developed with the assistance of Claude AI. I believe AI-written code should be open source to benefit everyone and maintain transparency.

A custom native C++ 3D game engine, currently proving itself out with a test scene
that goes for late-Sonic-Xtreme-era vibes — bright saturated colors, simple fog,
curved/ramped playgrounds, fast movement, an actual loop-the-loop. The engine itself
is general-purpose; that scene is just the current demo, not a Sonic game in its own
right.

A Roblox-Studio-style in-engine editor (move / rotate / scale, spawn menu, level save
& load) is built in, along with a TurboWarp-style visual block-coding editor
(currently paused) and a StarFox 64-inspired flight mode.

## Stack

- C++17, built with CMake
- [GLFW](https://www.glfw.org/) for windowing/input
- [GLM](https://github.com/g-truc/glm) for math
- OpenGL 4.1 core profile (via macOS's native `<OpenGL/gl3.h>` — no loader needed)
- Native macOS menu bar / file dialogs via a small Objective-C++ module

Currently macOS-only.

## Build & run

```
brew install glfw glm
cd retrobit
mkdir -p build && cd build
cmake ..
cmake --build .
./retrobit
```

## Controls

**Title screen:** click a button or press its shortcut — **PLAYGROUND** (`P`, the
flat checkerboard scene with a ramp/pillar and a loop), **HILLS** (`H`, procedural
rolling terrain), **HILLS+** (`J`, a bigger version of Hills), or **OPEN** (`O`).

**Play mode:**
- `WASD` — move · Mouse — look · `Space` — jump · `E` — sprint
- `F` — toggle **flight mode**: constant-forward StarFox All-Range Mode-style flight.
  `W`/`S` steer up/down, `A`/`D` strafe/turn, `E` boosts, `C` brakes, double-tap `A`/`D`
  barrel-rolls, `U` does an animated climbing U-turn, and flying past the arena
  boundary wraps you around to the opposite side instead of stopping you.
- `Esc` — toggle mouse capture · `/` — open the editor · `Q` — quit
- `Cmd+S` / `Cmd+O` — save / open a level (`.retrobitl` file), from anywhere

**Editor** (`/` to open): mouse starts locked for a `WASD` freecam (`Space`/`Left Shift`
for up/down); `Esc` unlocks the cursor to click and drag parts. `R` switches the gizmo
to Rotate, `T` to Scale (press again to go back to Move). `M` opens a spawn menu
(Box/Sphere). `B` opens the block-coding editor.

## Status

Early and actively evolving. See `CLAUDE.md` for the detailed, up-to-date breakdown of
what's implemented, known rough edges, and design decisions.
