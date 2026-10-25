# Architecture

The build translates the supported executable's x86 instructions into C, then
compiles that C alongside a handwritten native runtime. At runtime the executable
supplies the original data image; its machine instructions are not executed by an
x86 emulator. The loader verifies its hash before mapping any of it.

```mermaid
flowchart TD
  Game[Your local game installation] --> Setup[Verified metadata import and listings]
  Setup --> Translator[Static translator]
  Translator --> Generated[Local generated C archive]
  Generated --> Runtime[Guest memory, imports and scheduler]
  Game --> Runtime
  Runtime --> DX[Graphics, sound and input adapters]
  DX --> Host[SDL3 window, GPU interface, audio]
  Runtime <--> Mods[C and Lua mod API]
```

## Guest memory and calls

`runtime/memory.cpp` owns the guest address arena and allocator. Guest pointers
remain 32-bit offsets even in a 64-bit process. `loader.cpp` maps PE sections,
zero-fills data tails and replaces import-table entries with runtime trampolines.
`imports.cpp` decodes calls and dispatches the original calling conventions.

`runtime/x86.h` defines the register file, flags, x87 state and
instruction helpers used by generated functions. Functions retain stable guest
addresses for dispatch, hooks and diagnostics. Those addresses are identifiers,
not host pointers and not evidence of human-recovered intent.

A game that ships part of its code as a DLL can name it in game.toml as an
auxiliary module (`[modules.aux.<key>]`). The loader maps it beside the image
at its preferred base, above the shim trampolines, so `[game] guest_size`
grows the arena to hold it. The translator turns the module into its own
generated library with prefixed tables that register with the runtime at
start-up; `recomp_call` and `recomp_jump` consult that registry after the
image's own table misses, so calls in either direction cross the boundary
without a special case. `LoadLibrary` of the module's name returns its base
and `GetProcAddress` reads its export directory, which is all the game sees.

## Threads and ownership

Original game threads are cooperatively scheduled by `runtime/kernel32.cpp`.
Each has a register file and stack, but **one guest thread holds the execution
baton at a time**. A blocking import yields it. Host workers never mutate guest
memory concurrently with that thread.

The presentation worker consumes completed immutable frames. GPU resources and
texture revisions remain alive until commands that reference them complete.
Audio queues use their own clock and synchronization. Window events publish
requests for the guest thread rather than directly executing guest functions.

## A frame through the host

1. The original game submits DirectDraw/Direct3D operations to `dx/`.
2. `host/d3d_render.cpp` translates draws and texture revisions into GPU commands over `host/gpu/gpu.h`.
3. `host/ui_layer.cpp` extracts UI elements from recorded blits.
4. `host/present_thread.cpp` seals the frame, retains its resources and queues it.
5. `host/compositor.cpp` combines world, UI and overlays for presentation.
6. Completion acknowledgements release resources and update frame-pacing samples.

Classic renders at the selected game resolution and aspect-fits the image.
Enhanced can render the world at drawable resolution with separately scaled UI.
Wide view expands the world only when the drawable is wider than the selected
game canvas. The game's own docs (populous-recomp's `docs/DISPLAY.md`) describe the visible behavior.

## Timing, input and settings

The render limit and animation clock are separate from the simulation clock.
Increasing the presentation limit must not advance game logic or animations faster.
`mods/animation_timing.cpp` redirects only reviewed visual clock reads.

`host/input_gate.cpp` maps window coordinates through the published frame layout,
corrects the original relative cursor, and handles edge scrolling and focus.
`host/controls/` holds the on-screen controls: a layout model and hit test, a
router that owns each finger, a virtual pad that on-screen and physical
controllers both write to, a binding stage that turns the pad into keys and
mouse (or, in `native` mode, into the DirectInput joystick and XInput devices
in `dx/`), the overlay that draws it and the on-device editor. Everything but
the overlay and the SDL glue is SDL-free and unit-tested in `controls_tests`.
See `docs/superpowers/specs/2026-09-17-touch-controls-design.md`.
For native pad mode, `[controls.native] attached_only = true` makes DirectInput
enumeration and XInput availability follow the physical controller connection.
The default is false, preserving the always-present virtual pad. Connection
state is published through the virtual pad's mutex; guest polling never calls
SDL. A smoke script that uses a pad marks its simulated controller attached.
`mods/options_menu.cpp` extends the original Options page and queues changes at
guest-safe boundaries. `mods/game_settings.cpp` persists original graphics choices;
`mods/settings.cpp` atomically persists host/mod settings in the selected profile.

## Discovery, and code a build does not carry

Hardware probes use `[system] desktop = "1024x768"` and `memory_mb = 512`
by default. The desktop is shared by USER32, GDI and D3D9 until a selected
display mode takes over. `[d3d9.adapter]` accepts `vendor_id`, `device_id`,
`description` and `video_memory_mb`. Defaults remain the GeForce 7800 GTX
(0x10de/0x0091) and DirectDraw's 32 MB; D3D texture memory has a 256 MB floor.
Memory is reported consistently by the legacy and 64-bit memory structures;
legacy 32-bit totals saturate at UINT32_MAX. D3D9 enumerates 17 X8R8G8B8
modes from 640x480 to 2560x1600 at 60 Hz.

A port can audit its CRT pair and set `[translate] intrinsics` to a table
containing `setjmp` and `longjmp` guest addresses. These replace both CRT
bodies with runtime intrinsics and remove alternate entries inside them.
Direct setjmp calls save the host jump buffer in their caller's live frame;
indirect setjmp calls retain the runtime's explicit failure path. An empty
table disables substitutions, while omitting it retains existing-port defaults.
An executable's pair is cleared when translating an auxiliary module.

Multimedia timers default to the guest message pump. A game can select
`[system] mm_timers = "thread"` to run them on a runtime-owned cooperative
worker with its own guest stack and TLS. It waits on an event until the next
deadline, so creating or cancelling a timer wakes it. Frame services remain
on the message-pump path. Periodic timers retain their phase and skip missed
intervals after one catch-up callback. Timer deadlines use the actual pinned
clock when one is active. Hosts stop the worker before driving guest shutdown.

Static discovery is a guess about where code is, and it misses some: a
function only ever reached through a pointer nothing resolves, a jump-table
slot no listing owns, a block Ghidra ended early. Each shows up at run time as
a call or jump the address table cannot place.

`RECOMP_DISCOVERY=<file>` writes those addresses in the form
`tools/recomp/translate.py --discovered` reads back as entry points, so a run
tells the next translation what it missed (`runtime/discovery.h`).
`tools/discover.py` repeats that until a pass finds nothing new. Meanwhile
`runtime/interp.cpp` runs what the translation lacks, so a gap costs speed
rather than correctness.

On the desktop, `tools/lazy_static.py` compiles just the discovered functions
into a library that registers itself with the runtime's module table
(`RECOMP_EXTRA_CODE`), which is the difference between a minute and a full
rebuild. iOS runs no code that was not signed into the app, so there the
addresses go into `game.toml` and the app is rebuilt.

## Current boundaries

The original simulation is generated code, not a hand-rewritten gameplay engine.
It intentionally retains low-level register operations. The handwritten runtime,
adapter APIs, build tools and reviewed extension points are the primary places to
contribute. Generated functions have address/symbol comments and per-instruction
provenance; change the translator or a reviewed replacement, then regenerate.

The desktop app builds on macOS, Linux and Windows; the iOS packager runs
on macOS. `src/core/` contains only shared type headers used by the retained
tests; it is not a second game engine. Capture/replay under `mods/native/`
validates prospective native replacements locally.

Platform services (threads, virtual memory, plugins, files, clocks) go through
`platform/os.h`, with POSIX and Win32 implementations; the build is
CMake with presets per platform (`CMakePresets.json`).
