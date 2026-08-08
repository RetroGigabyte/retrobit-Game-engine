# Goals

A running list of where RETRObit Engine is headed. Not a roadmap with dates — just
what's aimed for, roughly in order of how realistic/near-term it is. See `CLAUDE.md`
for what's actually implemented today.

## Near-term

- Engine/demo separation — pull the renderer, collision system, and editor tool apart
  from the current hardcoded playground scene so the engine is usable for games other
  than the Sonic-Xtreme-style test level it grew up around.
- Curved terrain / loop geometry — the actual Sonic-Xtreme signature move, not yet
  implemented (see `CLAUDE.md`'s Next steps).
- Scratch-style block coding layer on top of the engine.

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

## Why list consoles the project can't realistically ship on yet

Because it's useful to know what "supporting more platforms" would actually require
before deciding to chase it — some of the above are a weekend of loader/CMake work,
others are a different renderer entirely, and others need access nobody currently has.
Keeping that distinction visible here is the point of this file.
