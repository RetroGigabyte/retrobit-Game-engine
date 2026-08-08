# RETRObit Engine

**AI Disclaimer:** This project was developed with the assistance of Claude AI. I believe AI-written code should be open source to benefit everyone and maintain transparency.

A custom native C++ 3D game engine, currently proving itself out with a test scene
that goes for late-Sonic-Xtreme-era vibes — bright saturated colors, simple fog,
curved/ramped playgrounds, fast movement. The engine itself is general-purpose;
that scene is just the current demo, not a Sonic game in its own right.

A Roblox-Studio-style in-engine editor (move / rotate / scale, level save & load)
is built in. Scratch-style block coding is planned as a future layer on top.

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

**Title screen:** click **NEW** or **OPEN**, or press `N` / `O`.

**Play mode:**
- `WASD` — move · Mouse — look · `Space` — jump
- `Esc` — toggle mouse capture · `/` — open the editor · `Q` — quit
- `Cmd+S` / `Cmd+O` — save / open a level (`.retrobitl` file), from anywhere

**Editor** (`/` to open): mouse starts locked for a `WASD` freecam (`Space`/`Left Shift`
for up/down); `Esc` unlocks the cursor to click and drag parts. `R` switches the gizmo
to Rotate, `T` to Scale (press again to go back to Move).

## Status

Early and actively evolving. See `CLAUDE.md` for the detailed, up-to-date breakdown of
what's implemented, known rough edges, and design decisions.
