# The next idea list

Saved for later. Nothing here is being worked on yet. Sources: what Savage Wind has open or planned as of 2026-10-31, and our
own ideas. Numbers are their pull requests and issues on [Revivedx/SavageWindRecomp](https://github.com/Revivedx/SavageWindRecomp).

Already taken from them for the current list: draw distance (#88) and the memory fixes on reloads (#237).

## Already here

Checked against the code on 2026-10-31.

- **Mouse sensitivity** (#288, #289): up to 1000%, a separate vertical speed, and invert.
- **Key mapping shown in the HUD** (#304): Button icons has a Keyboard choice and follows the device in use, and all keys can be rebound.
- **Real controller gyro**: works, with its own sensitivity and invert. Only #235 and #272 are left.

Partly here:

- **16:10** (#303): the view is widened for taller screens, but it hasn't been run at 16:10 here.
- **More control customization** (#180): keys, sticks, controller buttons, dead zones, camera speed and motion are all settable. Find what's still missing.
- **Native controller** (#143): any controller through SDL, with Xbox, PlayStation and Nintendo prompts.
- **Benchmark history**: every run is kept in `benchmark/results.txt`, but there's no comparison yet.
- **Shader cache check**: the Game Files page says to run it again after a driver update, but nothing warns before playing.
- **Install folder check**: the program tests whether its folder can be written to, but gives no warning.
- **GPU readings** (roadmap): the F10 overlay reads NVIDIA on Windows and AMD on Linux, not AMD or Intel on Windows.

## Chosen next, easiest first

An estimate from the size of each pull request and what is already here, before reading their code.

| # | Idea | Where | Why it sits here |
|---|---|---|---|
| 1 | Wii U and Cemu save import | `6cf491e` | Finished on their side. Three files conflict with ours (Makefile, README, menus). Needs a real Wii U or Cemu save to test |
| 2 | Virtual gyro, dual-stick gyro | #235, #272 | Real controller gyro already works here. About 900 lines each and they touch the same code, so one after the other. #281 (DSU) is a Linux Steam Deck fallback: skip it on Windows |
| 3 | Menu pointer, mouse in menus | #234, #183 | One piece of work: #234 is the fix for #183. 1,600 lines over 21 files, in menus we have changed |
| 4 | Teleport bookmarks | roadmap | No code to take. Our warp only works while a save loads, so either restart to warp or find the game's own warp call |
| 5 | Achievements | #236 | The biggest pull request: 3,900 lines over 39 files, and many triggers to test |
| 6 | FSR 4 | #308 | 1,600 lines, but we removed FSR and they build on it. Needs AMD's SDK. May help shimmer, worth comparing with TAA |
| 7 | Native controller support, more customization | #143, #180 | No code to take and a broad request: settle what is missing first, since rebinding already exists |
| 8 | Free camera and photo mode | roadmap | No code to take. Needs control of the game's camera while the world is paused, and the HUD hidden |
| 9 | How shadows look | #300 | Open-ended, the cause isn't known. The first try at shadow distance changed nothing visible |

## Savage Wind: work in progress

| Idea | Where | Note |
|---|---|---|
| FSR 4 | #308 | We removed FSR earlier: only worth it if it stops the artefacts that made us drop it |
| DLSS | #111 | |
| Menu pointer | #234 | A mouse cursor in the game's own menus |
| Profiler you can turn on while playing | #309 | Useful to us for reports |
| Online | #294 | Large, and a lot could go wrong |
| Unbreakable weapons | #296 | Belongs with accessibility and assists |
| Achievements | #236 | |
| Control commands | #284-286 | Buttons that run actions |
| Gyro: virtual, DSU, dual stick | #235, #281, #272 | |
| NaN-aware min/max in shaders | #153 | May fix some rendering glitches: read before the next graphics bug hunt |
| Sparse heap | #197 | Memory |
| Hebrew | #273 | |

## Savage Wind: open issues worth having

| Idea | Where |
|---|---|
| How shadows look | #300 |
| 16:10 screens | #303 |
| Riding with the keyboard | #290 |
| Native controller support, more control customization | #143, #180 |
| Older Vulkan | #250 |
| Mouse in menus | #183 |
| Community language packs | #265 |

## Savage Wind: their roadmap

- Free camera and photo mode
- Effects on and off, one at a time, including the three grass layers
- GPU readings on AMD and Intel
- Mods
- Accessibility and assists
- Teleport bookmarks
- amiibo rewards
- Import Wii U and Cemu saves (`6cf491e`, already done on their side)
- Vertices and indices from a copy of the game's memory in video memory (`4af9757`, a speed-up, needs a timing test)

## Our own

- **Benchmark history:** keep every result and show the change since the last run, with what settings differed.
- **Recommended preset:** run the benchmark once at first start and offer a preset from the result.
- **Settings profiles:** several named sets of settings, one click in the launcher.
- **Save backups:** a copy of the save folder before each game start, the last ten kept.
- **Report a problem:** one button that gathers the log and settings into a zip (no game files, no saves) ready to attach to an issue.
- **Draw distance by area:** drop Object distance by itself in dense places (Rito Village, Hyrule Castle), so the slider can go further elsewhere.
- **Hitch log for players:** the overlay names what caused the last long frame (shader, file read, memory).
- **Shader cache check:** tell the player when a driver update has made the cache stale, before the game stutters.
- **Portable mode check:** warn at start if the install folder can't be written to.
