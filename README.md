# Slop of the Wild

**A fork of [Savage Wind Recomp](https://github.com/Revivedx/SavageWindRecomp)** (the static recompilation of
*The Legend of Zelda: Breath of the Wild*, Switch version 1.9.0, into a native Windows program), aimed at the look,
smoothness and polish of the [TotK PC recomp](https://github.com/valentderah/totk-pc-recomp).

> [!IMPORTANT]
> **Everything in this fork was made with Claude.** Every line of code, every test, every measurement and this page
> were written by Claude (Anthropic's AI model, working through Claude Code), in one long session on 2026-10-29/30.
> A human ([devall96](https://github.com/devall96)) directed it: set the goals, played the game, judged how it looked
> and felt, reported what was wrong, and made the calls. No human wrote or reviewed the code line by line. Savage Wind
> itself is also made almost entirely with AI (see [its README](docs/SAVAGE-WIND-README.md)). If you don't trust code
> written by AI, don't run this. As with anything on the internet, you run it at your own risk.

> [!NOTE]
> **No game files are included, and none ever will be.** Like Savage Wind, Slop of the Wild needs your own copy of
> the game, version 1.9.0 with the update merged, dumped from your own Switch. Even the launcher's background art is
> not shipped: the game saves it from your own copy's title screen the first time you play.

## Download and install

1. Download **Savage Wind 0.3.1-alpha.3** for Windows from
   [Savage Wind's releases](https://github.com/Revivedx/SavageWindRecomp/releases/tag/v0.3.1-alpha.3) and extract it
   to a folder you can write to (not Program Files).
2. Download **`SlopOfTheWild-0.0-windows.zip`** from [this fork's releases](../../releases) and extract it *into
   that same folder*, replacing the files it asks about (`savagewind.exe` and the README).
3. Start `savagewind.exe`. Set up your game files from your own dump (version 1.9.0 with the update merged), then
   press Play.

Slop of the Wild 0.0 ships as a drop-in update rather than a full package. Savage Wind's release already has the
parts that don't change (the game code builder, the bundled compiler, the DLLs, the fonts), so they aren't uploaded
again. Already have Savage Wind 0.3.1-alpha.3 set up? Just do step 2: your game files, saves and settings are kept,
and the 14 GB `romfs.bin` is replaced by a small index the first time the start screen opens.

Version **0.0**, built on Savage Wind **0.3.1-alpha.3**. Tested on one PC: Windows, AMD Radeon RX 9070 XT
(driver 26.10.1), 2560 x 1440 at 280 Hz, 12 CPU threads.

---

## What changed, and why

### Picture quality

| Change | Why |
| --- | --- |
| **Fixed the dark "crust" outlining Link and edges** (`vulkan/textures.c`, `exec.c`) | Savage Wind forced 16x anisotropic filtering on *every* sampler, including ones reading render targets that have a single mip level. BOTW's FXAA pass samples the scene with a sampler whose LOD range is 0..14, so the override smeared its taps and left a dark speckled outline on edges. Found by dumping the FXAA pass's input and output and re-running the shader on the CPU. Anisotropy now applies only to textures with more than one mip level. Measured: speckled edge pixels went from 2.86 % to 0.17 % of the frame. |
| **Clarity** (`vulkan/clarity.c`, new) | A port of Cemu's well-known BOTW "Clarity" graphic pack (CC0; shader by Jamie, Kiri and Crementif, presets by Serfrost). It replaces BOTW's tone-mapping pass (found by its shader constants) with Clarity's: less haze and bloom, deeper blacks, richer color. Presets: Serfrost, Serfrost (dim display), Serfrost (bright display). |
| **Render scale** 100 / 125 / 150 / 200 % (`nvn/output_scale.c`) | Supersampling: the cleanest anti-aliasing there is, and the TotK port's main quality lever. 150 % at 1440p is 4K internally, and it held 120 fps on the test PC. |
| **Removed the upscaler, upscaler mode and CAS sharpening options** | FSR 1 only helps when rendering *below* the window size, and with render scale nobody needs that. Its "Performance" mode and CAS above 50 % made the picture visibly worse. "FSR 3" and "DLSS" were placeholders that did nothing. |
| **Better defaults**: fullscreen at the display's native size, 16x anisotropic, SMAA, Clarity on, asynchronous shaders on, the frame rate matched to the display | A fresh install should look its best without a trip to the settings. |

### Performance

| Change | Why |
| --- | --- |
| **"AMD fast path" setting, on by default** (`vulkan/device.c`) | Savage Wind turns Vulkan *attachment feedback loops* off on AMD and Intel on Windows, after a black-screen bug in 0.3.0. On the test PC, turning them back on took the game from about 72 to 120 fps with no visible problem. It's now a toggle (Video > AMD fast path), so anyone who hits trouble can switch it off. |
| **No more 14 GB second copy of the game data** (`romfs_build.c`, `services/fs.c`, `nvn/shader_archive.c`) | Setup used to copy the game files (as hard links, so that cost nothing) and then write *all of them again* into a 14 GB `romfs.bin` image. Now it writes `romfs.idx`, a 2.6 MB index of the image's header and tables, and the game reads each file's bytes straight from the `romfs` folder. Verified byte for byte against the old image (a full read plus 20,000 random reads). Existing installs switch over automatically at the start screen, after the same check, which frees 14 GB. The TotK port does the same thing (`romfs-index.bin`). |
| **Frame rate choices 144 / 165 / 240**, and a default that follows the display | The test monitor is 280 Hz. With no frame rate chosen yet, the game picks the highest choice the display can show. |
| **VSync changes settle for 0.4 s before they apply** | Stepping through VSync values in the menu rebuilt the swapchain several times a second, and that hard-crashed the AMD driver during play testing. |

Measured afterwards: with no frame rate limit at 150 % render scale, the open world ran at about 115 to 150 fps with
the GPU about 70 % busy, so the game's own CPU work is now the limit. The stutters that remain are first-time shader
and pipeline compiles.

### Launcher and menus

| Change | Why |
| --- | --- |
| **New look** for the launcher and the in-game settings (`ui/option_rows.mm`, `menu.mm`, `launcher.mm`) | The old one "looked and felt rushed". The new one is a Sheikah-slate style: deep blue glass, a cyan accent, animated on/off switches, value bars with notches, eased highlights, and amber for any value you changed from its default. |
| **The settings menu opens again with F1 / Esc** (also the controller's Guide button, or Back + Start) | Savage Wind had turned its overlay menu off, so nothing came up when you pressed Esc or F1. It's back, restyled. |
| **The game pauses while the menu is open** | During testing the game kept running behind the menu and Link died while settings were being changed. Opening the menu now presses the game's own pause (Plus), and closing it unpauses. This is skipped on the title screen and during cutscenes. |
| **Real BOTW art behind the launcher** (`ui/splash.mm`, `vulkan/device.c`) | The title screen's own key art (plateau, Epona, cliff, Guardian) cycles behind the launcher with a slow crossfade and pan. It can't be shipped, since it's Nintendo's, so the game captures it from your own title screen the first time you play and saves it as `splash.jpg`, `splash2.jpg` and so on next to the program. |
| **The launcher shows every time** (`main.c`) | A two-step start: Quiver (or the exe) opens the launcher, then Play. `--play` still starts the game straight away. |
| **Setup lands on the launcher when it finishes**, with a live file counter and a "clearing the earlier copy" step | Setup used to go straight into the game, and it looked frozen while it checked about 41,000 files. |
| **Game Files page no longer freezes** | It added up the size of all 41,000 files before drawing anything. That now happens on a background thread. |
| **Text keeps numbers together** (`third_party/imgui/imgui_draw.cpp`) | Wrapped text used to break version numbers in two ("1." on one line, "8.2" on the next). |
| **Renamed**: title screen "Slop of the Wild 0.0 / a fork of Savage Wind", window titles, menus, error dialogs | The title screen's version text box only holds 15 characters, so the game's line is left blank and the overlay draws ours in its place. The program and settings files keep their names (`savagewind.exe`, `savagewind.ini`) so existing shortcuts and settings keep working. |

### Not done / known issues

- **Graphics pipeline libraries still crash on AMD** (a GPU page fault at the same address every run). The TotK port uses the same Vulkan feature on the same GPU without trouble, so this is a bug in Savage Wind's code, not the driver. Disabling the guest-memory import and disabling feedback loops each ruled out one cause. Finding the rest needs the Vulkan validation layers. Pipeline libraries stay off on AMD, as in Savage Wind.
- The program icon is still Savage Wind's.
- A full fresh-install-to-play test was started on the final build and then postponed. Earlier builds passed the same test: setup in about 6 to 7 minutes, then booting and playing.
- Ideas taken from the TotK port but not built yet: a video-memory mirror of game memory on discrete GPUs, GPU work submitted in parts, smarter skipping of draws whose pipeline is still compiling, and shipped shader seeds. See [SLOP-TODO.md](SLOP-TODO.md).

---

## Who did what

**From devall96 (the human):**
- The project and its goal: make Breath of the Wild on PC as good as the TotK PC recomp, by fixing Savage Wind rather than starting over.
- The name, "Slop of the Wild", and the title-screen wording.
- Spotting the dark crust around Link and the washed-out color, and insisting it was real when the first measurements missed it ("it's very obvious even in your crops").
- The order of work: filtering and broken toggles, then a clarity filter, then the launcher rewrite, then optimization.
- Asking for Cemu's BOTW clarity filter specifically, and for options that make the picture worse to be removed.
- The launcher brief: smooth and unique, default to fullscreen at native resolution, useless options gone, toggles for every fix, real BOTW art in the background, no slow start or hang.
- Asking that the AMD fast path be a real setting, and that the launcher open every time.
- Reporting that Esc and F1 did nothing, the hard crash while changing options, and that setup was slow and looked stuck.
- The frame-rate default (highest native, with options), asking for a test from a fresh install, and pointing Claude at the TotK install to learn from.
- Lending the PC: running the test and build agents, the PS5 controller, the game dump, and play testing.

**From Claude:**
- All the code, the scripts and this page.
- The test rig: a job agent on the PC that runs the game with scripted controller input, screenshots and logs; a build agent that builds from a patch with MSYS2; and frame dumps, shader dumps and CPU emulation of single shader passes.
- Root-causing the crust to the anisotropy override, and measuring the fix.
- Porting Clarity, finding which shader to replace, and adding render scale.
- The romfs index design, its byte-for-byte check, and the automatic switch for existing installs.
- The launcher and menu design, art capture from the title screen, the pause-under-menu fix, VSync debouncing, and the frame-rate default logic.
- The benchmarks, the GPL investigation, and reading the TotK port's headers for ideas.

**Together:**
- The crust hunt: devall96's eye said it was there, Claude's frame dumps showed why.
- The look of the picture: Claude proposed and measured, devall96 judged ("wooow that looked amazing").
- The launcher: devall96's brief and feedback through several rounds, Claude's design.
- Setup speed: devall96 reported it, Claude found the 14 GB rewrite and replaced it.
- What to keep, cut or default, decided option by option.

---

## Build it yourself

Slop of the Wild is a set of changes on top of Savage Wind's source at the tag `v0.3.1-alpha.3`. To build it on
Windows (from [Savage Wind's build notes](docs/BUILD-windows.md); this is what the build agent ran):

1. Install [MSYS2](https://www.msys2.org) (`C:\msys64`), open the **UCRT64** shell, then update twice and install the tools:
   ```sh
   pacman -Syuu    # twice, as MSYS2 asks
   pacman -S --needed git make mingw-w64-ucrt-x86_64-clang mingw-w64-ucrt-x86_64-lld mingw-w64-ucrt-x86_64-sdl3 \
     mingw-w64-ucrt-x86_64-ffmpeg mingw-w64-ucrt-x86_64-vulkan-headers mingw-w64-ucrt-x86_64-vulkan-loader \
     mingw-w64-ucrt-x86_64-shaderc mingw-w64-ucrt-x86_64-spirv-tools mingw-w64-ucrt-x86_64-pkgconf \
     mingw-w64-ucrt-x86_64-vulkan-memory-allocator mingw-w64-ucrt-x86_64-nasm
   ```
2. Get this fork, or Savage Wind at `v0.3.1-alpha.3` with this fork's changes applied:
   ```sh
   git -c core.autocrlf=false clone <this fork's URL> slop && cd slop
   # or: git clone https://github.com/Revivedx/SavageWindRecomp slop && cd slop &&
   #     git checkout v0.3.1-alpha.3 && git apply slop.patch   (git diff v0.3.1-alpha.3..HEAD from this fork)
   ```
3. Build:
   ```sh
   make -j RELEASE=1 build/savagewind.exe
   ```
4. Put `build/savagewind.exe` in a Savage Wind 0.3.1-alpha.3 release folder, in place of its exe (the release has the
   DLLs, fonts, the code generator and the bundled compiler that setup needs). Start it, set up your game files from
   your own dump, and play.

**To recreate the work rather than just the build:** each change above is its own commit with a message that says
what and why, and the code comments carry the reasoning and measurements. The order matches the sections above. The
test numbers came from scripted runs: the game was started with `SAVAGEWIND_*` environment variables (scripted input
`SAVAGEWIND_HOST_INPUT`, screenshots `SAVAGEWIND_SCREENSHOT_EVERY` and `SAVAGEWIND_WINDOW_SHOT_AT`, the profiler
`SAVAGEWIND_VK_PROF=2`, `SAVAGEWIND_DATA_ROOT` for a clean test install), and the logs were compared.

---

## Credits

- **[Savage Wind Recomp](https://github.com/Revivedx/SavageWindRecomp)** by Revivedx and contributors, and **[LAS Recomp](https://github.com/peiikai/LASRecomp)** by Peiikai, which it builds on: nearly everything here is theirs. MIT License (see [LICENSE](LICENSE)).
- **Clarity** from the [Cemu graphic packs](https://github.com/cemu-project/cemu_graphic_packs) (CC0): Jamie, Kiri and Crementif; presets by Serfrost.
- **[TotK PC recomp](https://github.com/valentderah/totk-pc-recomp)** by valentderah: the bar this aims for, and the source of several ideas.
- *The Legend of Zelda: Breath of the Wild* is Nintendo's. This project is not affiliated with or endorsed by Nintendo.
