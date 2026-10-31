# The next idea list

Saved for later. Nothing here is being worked on yet. Sources: what Savage Wind has open or planned as of 2026-10-31, and our
own ideas. Numbers are their pull requests and issues on [Revivedx/SavageWindRecomp](https://github.com/Revivedx/SavageWindRecomp).

Already taken from them for the current list: draw distance (#88) and the memory fixes on reloads (#237).

## Already here

Checked against the code on 2026-10-31.

- **Mouse sensitivity** (#288, #289): up to 1000%, a separate vertical speed, and invert.
- **Key mapping shown in the HUD** (#304): Button icons has a Keyboard choice and follows the device in use, and all keys can be rebound.
- **Real controller gyro**: works, with its own sensitivity and invert.

Partly here:

- **16:10** (#303): the view is widened for taller screens, but it hasn't been run at 16:10 here.
- **More control customization** (#180): keys, sticks, controller buttons, dead zones, camera speed and motion are all settable. Find what's still missing.
- **Native controller** (#143): any controller through SDL, with Xbox, PlayStation and Nintendo prompts.
- **Benchmark history**: every run is kept in `benchmark/results.txt`, but there's no comparison yet.
- **Shader cache check**: the Game Files page says to run it again after a driver update, but nothing warns before playing.
- **Install folder check**: the program tests whether its folder can be written to, but gives no warning.
- **GPU readings** (roadmap): the F10 overlay reads NVIDIA on Windows and AMD on Linux, not AMD or Intel on Windows.

## The work list, easiest to hardest (2026-10-31)

An estimate before reading the code each needs. Speed items are theories: each gets a benchmark on the three routes, and stays only if it shows a gain.

| # | Item | Kind or state |
|---|---|---|
| 1 | Cache the per-frame `getenv` calls | Done |
| 2 | Read only the game flags achievements use | Not needed: moving the check outside the lock took the stall away |
| 3 | Compile test hooks out of release builds | Not needed: nearly all are read once and kept |
| 4 | Automatic save backups before each game start | Done |
| 5 | FPS and frame-time graph in the F10 overlay | Done |
| 6 | Skip intro logos and unskippable text | Done as Start at Continue, off by default. Unskippable text not done |
| 7 | More cheats: rupees, one-hit kills, moon jump, rune cooldowns | Partly: infinite rupees done; one-hit kills, moon jump and rune cooldowns not |
| 8 | Durability multiplier | Done, as Weapon wear |
| 9 | Settings profiles | Done, three slots |
| 10 | One-click problem report, with no game files or saves | Done, a folder rather than a zip |
| 11 | GPU readings on AMD for Windows | Already there: the F10 overlay reads an AMD GPU on Windows |
| 12 | Recommended preset from a first-run benchmark | Done, on the Benchmark page |
| 13 | Smaller installer, FSR 4 files as an optional download | Not done: the program has no downloader, so the FSR 4 files stay in the installer |
| 14 | Difficulty sliders for damage dealt and taken | Partly: Damage taken done. Damage dealt not, because the same code hurts Link and enemies |
| 15 | Time of day and weather in photo mode | Tried and cut: the game keeps the clock, and holding the d-pad barely moved the sun |
| 16 | Depth of field and filters in photo mode | Not done: the Colour page already covers filters; depth of field would be new shader work |
| 17 | Fast-travel to any map marker | Not started |
| 18 | Teleport bookmarks inside shrines | Not started: needs the current map, which bookmarks do not record |
| 19 | Smarter CPU core placement | Nothing to gain on a 7800X3D (one die); untested on other CPUs |
| 20 | Skip shadow draws that don't matter | Not started: 200+ fps already, and skipping cascades risks visible shadow flicker |
| 21 | Higher optimization on the hottest game code | Dropped: all game code at -O2 gave 243 to 255 fps on the field route and nothing at Hateno, for 70 s more setup |
| 22 | Large memory pages for the game's heap, optional | Not tested: needs a Windows security permission, which is your call |
| 23 | Native versions of hot game routines | Dropped: all game code at -O2 barely moved Hateno, so faster routines look unlikely to help |
| 24 | Less guest memory kept committed | Not started: 5.2 GB in use on a 31 GB PC, no sign it limits anything |
| 25 | Skip descriptor and state changes that haven't changed | Mostly there already: about 90% of descriptor sets are reused. stage_set trimmed, gain within noise |
| 26 | Read ahead for the next area | Not started |
| 27 | Pre-warm pipelines at the title screen | Already there: shader precompilation and warm-up |
| 28 | Cache indirect call targets | Dropped: dispatch is 0.2% of samples |
| 29 | Less waiting for emulated cores | Tried and dropped: spinning before sleep cut 210 fps to 120 |
| 30 | Cheaper far grass | Done: removing my older patches took draw distance on from 149 to 211 fps with no visible change |
| 31 | Draw distance that holds a target frame rate | Done as Hold the frame rate, grass only |
| 32 | Incremental setup, so updates don't rebuild everything | Not started |
| 33 | Better horse steering on keyboard (#290) | Not started: needs testing on a horse with a keyboard |
| 34 | Update checks and in-place updates | Partly: a check button and a link to the page, no in-place update. Not tried against GitHub |
| 35 | Per-action remapping (#180) | Not started |
| 36 | XeSS and DLSS | Not started: needs vendor SDKs |
| 37 | Community language packs (#265) | Done for menus: lang/<code>.txt. The game's own text is not covered |
| 38 | Mod loader for file replacements | Done: mods/<name>/romfs/ replaces files by path |
| 39 | Parallel draw translation | Not started: the render thread is 81% busy, not the only limit, and it is the riskiest item |
| 40 | Randomizer | Not started |

## Chosen list, done on 2026-10-31

| Idea | Where | State |
|---|---|---|
| Cheat menu | #25, #296 | In. Health, stamina and weapons tested; arrows not yet |
| Wii U and Cemu save import | `6cf491e` | In, not tested here |
| Menu pointer, mouse in menus, mouse bow aiming | #234, #183 | In, on by default, not tried with a real mouse yet |
| Teleport bookmarks | roadmap | In. Restarts the game and lands on Continue, from the last save |
| Achievements | #236 | In, the save reading tested |
| FSR 4 | #308 | In as an anti-aliasing mode at native size, Radeon on Windows |
| Confirm button (Xbox and PlayStation layout) | #143, #180 | In. Actions can't be moved one by one beyond the rebinding that already exists |
| Photo mode | roadmap | In, F6. The game still chooses what to draw from its own camera, so far from it the world is cut off |
| How shadows look | #300 | Not changed. The report's stripes are on an Intel GPU and don't show on AMD here. Needs an Intel PC to test |

## Savage Wind: work in progress

| Idea | Where | Note |
|---|---|---|
| DLSS | #111 | |
| Profiler you can turn on while playing | #309 | Useful to us for reports |
| Online | #294 | Large, and a lot could go wrong |
| Control commands | #284-286 | Buttons that run actions |
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

- Effects on and off, one at a time, including the three grass layers
- GPU readings on AMD and Intel
- Mods
- Accessibility and assists
- amiibo rewards

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
