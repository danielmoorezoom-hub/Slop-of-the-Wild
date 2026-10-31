# Slop of the Wild

**A fork of [Savage Wind Recomp](https://github.com/Revivedx/SavageWindRecomp)**, the static recompilation of
*The Legend of Zelda: Breath of the Wild* (Switch version 1.9.0) into a native Windows program. It aims for the look,
smoothness and polish of the [TotK PC recomp](https://github.com/valentderah/totk-pc-recomp).

> [!IMPORTANT]
> **Everything in this fork was made with Claude.** Every line of code, every test, every measurement and this page were
> written by Claude (Anthropic's AI model, working through Claude Code). A human ([devall96](https://github.com/devall96))
> directed it: set the goals, played the game, judged how it looked and felt, reported what was wrong, and made the
> calls. No human wrote or reviewed the code line by line. Savage Wind itself is also made almost entirely with AI (see
> [its README](docs/SAVAGE-WIND-README.md)). If you don't trust code written by AI, don't run this. As with anything on
> the internet, you run it at your own risk.

> [!CAUTION]
> **No game files are included, and none ever will be, anywhere on this repository**: not in the code, the releases, the
> issues or the wiki. Please don't post game files, dumps, keys, an installed copy, or anything extracted from the game
> (screenshots of bugs are fine).

> [!NOTE]
> **Bring your own game.** You need your own copy, version 1.9.0 with the update merged, dumped from your own Switch. The
> DLC is optional, dumped the same way.

## Download and install

1. Download **`SlopOfTheWild-0.1-Setup.exe`** from [the releases](../../releases). It's a single file: the program and
   the tools it needs, with no game files and no pictures from the game.
2. Run it. It installs to `C:\Slop of the Wild` by default, and you can choose another folder. Don't pick Program Files:
   saves, settings and game files are kept next to the program.
3. Start the game and choose the folder with your dump. If you have the DLC, keep it in that same folder. Setup takes
   about five minutes, because it builds the game code on your PC.

**Needs:** Windows 10 or newer, 64-bit; a Vulkan 1.3 graphics driver; about 16 GB of free disk space. Tested on one kind
of PC only: AMD Radeon RX 9070 XT (driver 26.10.1), 2560 x 1440, 12 CPU threads.

**Coming from 0.0?** Install to a new folder, or over the old one. The game code has changed, so setup has to run again,
and your copy of the game has to be on hand for it. **Setup removes any DLC already installed if the folder you choose
doesn't contain it**, so choose a folder that has your DLC too. To carry your saves over, copy the `save` folder.

## What's new in 0.1

0.1 was rebuilt from 0.0 with Savage Wind's later work brought in, then a round of its own work on stutter, frame rate and
options.

### New in this fork

| Change | Why |
| --- | --- |
| **First visits no longer hitch on file loading** | A thread reading a file for the first time kept its emulated CPU core while the disk worked. That could stop another thread holding a lock the main thread needed, which made a 25 to 60 ms hitch. Reading and unpacking files now give the core up, as a thread waiting on a read does on the console. On one test route with the file cache dropped, hitches over 15 ms went from 8 to 12 down to 4. A later first-visit run on another route, with the draw distance sliders at their top, had none. |
| **Faster shader waits on first load** | Waiting for a shader now yields at first instead of sleeping. The first-load stall went from 665 ms to about 120 ms with the driver cache warm. |
| **A little more frame rate** | The render thread wakes the recorder thread far less often, and indirect calls no longer save registers on their usual path. Hateno went from 208 to 215 fps in one run each. |
| **Controller camera speed** (Controls, off by default, beta) | The game's top camera speed is 85 degrees a second, over four seconds for a full turn. This goes past it with separate left/right and up/down sliders, up to 600%, measured at exactly 2x and 4x. |
| **Extended draw distance** (its own Draw Distance page, off by default, beta) | Overall distance moves everything at once, with sliders under it for Draw distance (to 150%), Grass distance (to 250%) and Object distance (to 140%; past that the game drops parts of Rito Village). |
| **Benchmark** (Video, and the start screen) | Three set walks with your settings, on a copy of your save. The results stay in `benchmark/results.txt`. |
| **Cheats** (their own page) | Infinite health, stamina and arrows, unbreakable weapons. Off by default. |
| **Teleport** (its own page, in game) | Four bookmarks and the main places. The game restarts and Continue lands you there, from your last save. |
| **Photo mode** (F6, or both sticks pressed in) | The world stops and the HUD goes. Sticks fly the camera, L and R go up and down, ZR is faster, B leaves, F7 saves a picture in `screenshots/`. |
| **Saves** (Settings, start screen) | Import a Wii U or Cemu save, back up your saves. |
| **Achievements** (their own page) | 63, read from your saves, with a notice as they unlock. |
| **Mouse pointer** (Controls, on by default) | Point and click in menus, inventory and map, and aim the bow with the right button. Esc pauses and goes back, F2 frees the cursor. |
| **AMD FSR 4** (Video > Anti-aliasing) | On a Radeon on Windows. Anti-aliasing at full size, with no upscaling. Elsewhere it acts as TAA. |
| **Confirm button** (Controller) | Confirm on the bottom button, as on Xbox and PlayStation pads. The prompts follow. |
| **Start at Continue** (Video) | Goes past the title screen by itself, into your latest save. Any button stops it. |
| **Profiles** | Three saved sets of all your settings. |
| **Save backups** (on by default) | A copy before each game start when your saves have changed, the last 5 in `save-backups/`. |
| **Mods** | `mods/<name>/romfs/<game path>` replaces that game file. Your copy of the game isn't changed. |
| **Language packs** | `lang/<code>.txt` with lines of English, a tab, then the translation, for the menus. |
| **Home button menu** | The settings overlay opens from a controller's Home button, or Back + Start. Esc frees the mouse. The game's own pause menu has the options too. |
| **Straight into the game** | Once set up, starting the game goes to the title with a splash, not a launcher and not a black screen. `Slop of the Wild Configuration.exe` opens the settings. |
| **A single installer** | One exe, in place of 0.0's drop-in zip that needed Savage Wind's release first. |

### Tried and removed

| Change | Why |
| --- | --- |
| **No FSR 3, FSR 4 or frame generation** | The game is limited by the CPU, so upscaling only cost frames here: 209 fps off, 159 with FSR 3, and it looked blurrier. TAA stays, using our own shaders. |
| **Game code stays at `-O1`** | `-O2` gave about 6% more frames for about 30 seconds more setup. It was tested and left out for now. |

### From Savage Wind, brought in for 0.1

The DLC (The Master Trials and The Champions' Ballad), colour grading with LUTs, HDR10 output, TAA, a recorder thread for
the driver's work, native 1:1 mouse look, game data served from the romfs folder with no second 15 GB copy, gyro fixes,
button icons, NPC speech bubbles, a window size list, and many rendering and bug fixes from Savage Wind's contributors. See
[its README](docs/SAVAGE-WIND-README.md) for what they did. Nearly everything here is their work.

### Still here from 0.0

- **The dark "crust" around Link and edges is fixed.** Savage Wind forced 16x anisotropic filtering on every sampler,
  including render targets with one mip level. That smeared the game's FXAA and left a dark speckled outline: 2.86% of
  the frame's edge pixels, down to 0.17%.
- **Clarity**, a port of Cemu's BOTW graphic pack (less haze and bloom, deeper blacks, richer color), with presets.
- **Render scale** 100 / 125 / 150 / 200%.
- **"AMD fast path"**, a toggle that took the test PC from about 72 to 120 fps at the time.
- **Frame rate choices** up to 240 and a default that follows the display.
- **A new look** for the settings, and art from your own title screen behind the launcher. It's captured from your copy
  the first time you play, and never shipped.

## How the beta options perform

Uncapped on the test PC, on a two-minute walk through Hateno Village, SMAA at 100% render scale. One run each, split
between two identical PCs, so treat gaps of a couple of percent as noise.

| Object distance | fps | 1% low | 0.1% low | Draws per frame |
| --- | --- | --- | --- | --- |
| 100% | 210 | 156 | 126 | 3,289 |
| 125% | 207 | 153 | 115 | 3,452 |
| 150% | 202 | 141 | 105 | 3,595 |
| 175% | 191 | 143 | 107 | 3,700 |

With Draw distance at 1000% and Grass density at 1500% (Object distance at 100%), the same walk went from 220 to 183 fps.
Graphics card time barely changes, because the cost is on the CPU. Results on other PCs will differ.

## Known issues

**The seven new options are marked beta for a reason. They may be broken.**

- **Grass shows its edges.** The game chooses which grass gets full detail from the camera's view. At stock distances that's
  well hidden, but Extended draw distance pulls those edges out to where you can see them moving as the camera turns.
- **Flowers near Link, far tree billboards and texture detail don't change** with these options. Those values haven't been
  found, so raising "everything" isn't done.
- **Object distance can change how the game plays.** Enemies are active from farther away, so they can notice you sooner.
  175% is only lightly tested, with no crash in short runs. Beyond it the game ran short of memory for objects: at 200%
  a tower went missing and enemies floated, and at 400% it crashed, which is why the slider stops there.
- **Extended draw distance only applies to game version 1.9.0.**
- **Link's model glitches very briefly at random**, like a tiny flicker. Not investigated yet.
- **Distant detail shimmers.** Flowers and far detail can look jagged, most with SMAA. TAA (Medium and High presets) and a higher render scale smooth it, and more draw distance may make it worse.
- **Shadows stay crunchy at a distance.** Raising Video > Shadow resolution helps up close, for under 1 ms a frame at the
  highest setting. How far the sharp shadows reach hasn't changed.
- **Setup can delete DLC**, as above, if the folder you choose doesn't have it.
- **The first play after setup can be rougher**, while Windows and the driver cache things.
- **The icon is still Savage Wind's.**

## What we're hoping to do

**These are hopes, not promises.** There's no schedule, and any of it may change, turn out to be impossible, or never happen.

- Raise the level of detail on everything: flowers, trees, actors, objects and texture detail, and fix the grass edges
- A friendlier installer and launcher: new art, a choice to add the DLC or not, plain explanations of which files go where,
  simple enough for anyone
- Benchmarks built into the menus, using the routes these tests were run on
- High, Medium, Low and Potato presets on the graphics page
- Fix or upgrade textures that don't scale with the resolution, like the menu UI
- Fix Link's model glitch
- Fix jaggies and shimmer on distant detail
- Port Cemu's shadow draw distance and reflection packs

---

## Who did what

**From devall96, the human:** the project, its goal and name; playing every build and judging how it looked and felt;
spotting problems the numbers missed, like the crust, the moving grass edges and the file-loading stutter on first visits;
deciding what to keep, cut or default; and lending the PCs, the controller and the game dump.

**From Claude:** all the code, scripts and this page; the test rig, including scripted routes that steer from Link's real
position; finding causes from frame dumps, profiles and logs; and the measurements above.

---

## Build it yourself

On Windows, with [MSYS2](https://www.msys2.org) at `C:\msys64`, in the **UCRT64** shell ([more detail](docs/BUILD-windows.md)):

```sh
pacman -Syuu    # twice, as MSYS2 asks
pacman -S --needed git make mingw-w64-ucrt-x86_64-clang mingw-w64-ucrt-x86_64-lld mingw-w64-ucrt-x86_64-sdl3 \
  mingw-w64-ucrt-x86_64-ffmpeg mingw-w64-ucrt-x86_64-vulkan-headers mingw-w64-ucrt-x86_64-vulkan-loader \
  mingw-w64-ucrt-x86_64-shaderc mingw-w64-ucrt-x86_64-spirv-tools mingw-w64-ucrt-x86_64-pkgconf \
  mingw-w64-ucrt-x86_64-vulkan-memory-allocator mingw-w64-ucrt-x86_64-nasm mingw-w64-ucrt-x86_64-nsis
git -c core.autocrlf=false clone https://github.com/devall96/Slop-of-the-Wild slop && cd slop
make deps-windows             # FFmpeg, LGPL build
tools/fetch_zig.sh            # the compiler that setup bundles
make gen-helper GEN_PY=python # the game code generator (needs Python 3.12)
make -j dist GEN_PY=python    # build/dist/Slop of the Wild
make installer                # build/dist/SlopOfTheWild-<version>-Setup.exe
```

Each change has its own commit saying what and why, and the code comments carry the reasoning and measurements.

---

## Credits

- The title's lettering is [Cinzel Decorative](https://github.com/google/fonts/tree/main/ofl/cinzeldecorative) by Natanael Gama (OFL). The launcher art and icon were made with Gemini.
- **[Savage Wind Recomp](https://github.com/Revivedx/SavageWindRecomp)** by Revivedx and contributors, and
  **[LAS Recomp](https://github.com/peiikai/LASRecomp)** by Peiikai, which it builds on: nearly everything here is theirs.
  MIT License (see [LICENSE](LICENSE)).
- **Clarity** from the [Cemu graphic packs](https://github.com/cemu-project/cemu_graphic_packs) (CC0): Jamie, Kiri and
  Crementif; presets by Serfrost. The same packs are where the draw distance values' meanings came from (Crementif).
- **[The Legend of Zelda decompilation project](https://github.com/zeldaret/botw)**, whose function names helped find them.
- **[TotK PC recomp](https://github.com/valentderah/totk-pc-recomp)** by valentderah: the bar this aims for.
- *The Legend of Zelda: Breath of the Wild* is Nintendo's. This project is not affiliated with or endorsed by Nintendo.
