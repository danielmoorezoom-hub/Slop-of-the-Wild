# The list

The current tasks and the ideas together, easiest to hardest, as of 2026-11-01. The order is an estimate, mostly made before reading the code each one needs. Numbers are pull requests and issues on [Revivedx/SavageWindRecomp](https://github.com/Revivedx/SavageWindRecomp). The sections after this one are the older lists, kept for their notes.

### Very easy: minutes to an hour

1. Port Savage Wind's AMD fix (#341, about 13 lines), then turn the HUD copies back on, which should stop text letters jumping
2. Correct the white grass fix's match: use the game code's hash, already written in the `sw-test` copy, so a translator change can't turn it off
3. Version and source on the log's first line (2b1db43)
4. Warn at start if the install folder can't be written to
5. Warn when a driver update has made the shader cache stale
6. Collect the new feature requests from upstream's issues
7. Retake the missing colour shot: Alpha 4 at noon with no look
8. Tune the white grass sheen's strength, if it looks too pale in play
9. Clarity: lift the shadows and pull back noon saturation

### Easy: an hour or two

10. Test Cheats > Weather: the arrows, No rain, No thunderstorms, and snow in a cold area
11. Play-test the ported fixes: moving platforms, shield surf, the Kohga freeze
12. Alpha 4.1's other fixes: menu pointer crash, controller map, quick selector, Game Over mouse
13. Check the six items from 0aa6f04 one by one: wide pause menu, map stretching, start screen freeze, Alt-Tab controller, unbound arrow keys, overlay coordinates
14. Map panning off its lower edge (#319)
15. Linux overlay reads the GPU that renders (#315), only once there is a Linux build
16. Benchmark history: show the change since the last run
17. Finish the update flow: try running the downloaded installer
18. Reddit post

### Medium: an afternoon or so

19. Test Alpha 3, then find and port what brings back the time and weather tile
20. Garbled Game Over title: retest after item 1, then look into it if it's still there
21. OBS Game Capture black frames, and frames on the game's own queue (#293)
22. Cheaper write watch (f1f2d28), which may help the 35 to 47 ms slow lists
23. Monitor and fullscreen mode choices (#245)
24. Hitch log for players: the overlay names what caused the last long frame
25. Frame recorder, then use it on the shadow flashes and Link's glitch, trying GPU overlap off first
26. Grass shimmer and artifacts in sunlight, now that the weather can be held
27. Share with Savage Wind: the white grass bug report and fix, tested on their `test` branch. Not the weather offset as code, since it came from the decompilation and their rules forbid copying from one
28. Offer them more, one at a time as they respond: incremental setup, save backups
29. Better horse steering on keyboard (#290, their open #343 may do it)
30. Unskippable text
31. Item spawner, from what `promosave.py` learned. First find which item sets off cutscene Demo200_0
32. Draw distance by area: less Object distance in dense places

### Harder: a day or more

33. In-game console and control commands (#284-286, 24 files conflict), then Link's position in the log (752d72f)
34. Geometry copy for 4-6 GB cards (f1f2d28)
35. Teleport bookmarks inside shrines
36. Fast travel to any map marker
37. Per-action remapping (#180, their draft #329)
38. Depth of field in photo mode
39. Effects on and off, one at a time, including the three grass layers
40. Accessibility and assists: text size, colour-blind filters, toggles for held buttons
41. Profiler you can turn on while playing (#309)
42. NaN-aware min/max in shaders (#153), which may fix some rendering glitches
43. Less guest memory kept committed
44. Skip shadow draws that don't matter (risks shadow flicker)
45. Read ahead for the next area
46. 16:10 screens (#303), older Vulkan (#250), sparse heap (#197), how shadows look on Intel (#300): each needs hardware we don't have
47. Our benchmark, mods and bookmarks offered to Savage Wind: the benchmark needs our test route code to go with it

### Hard: days to weeks, and may not work

48. Linux build (Ubuntu, Arch)
49. Port Cemu's shadow draw distance and reflection packs
50. Fix or upgrade textures that don't scale, like the menu UI
51. Raise detail on flowers, far trees and textures, and fix the grass edges
52. Friendlier setup and launcher explanations for anyone
53. amiibo rewards without figures
54. XeSS and DLSS (vendor SDKs; their #111 for DLSS)
55. Steam Deck gyro through DSU (#281), which needs the Linux build first
56. Smarter CPU core placement, untested on other CPUs: needs other PCs
57. Parallel draw translation, the riskiest item
58. Randomizer
59. Online (their #294)
60. macOS

**Needs you, not ranked:** your checks of the real mouse, save import and Confirm button on a controller; merging and releases.

# Older lists, kept for their notes

Sources: what Savage Wind has open or planned as of 2026-10-31, and our own ideas. Already taken from them: draw distance (#88) and the memory fixes on reloads (#237).

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
| 7 | More cheats: rupees, one-hit kills, moon jump, rune cooldowns | Partly: infinite rupees and one-hit kills done. Moon jump and rune cooldowns dropped for now: the jump height value is shared by many parameters with nothing to tell them apart, and no cooldown names turned up in the game's data |
| 8 | Durability multiplier | Done, as Weapon wear |
| 9 | Settings profiles | Done, three slots |
| 10 | One-click problem report, with no game files or saves | Done, a folder rather than a zip |
| 11 | GPU readings on AMD for Windows | Already there: the F10 overlay reads an AMD GPU on Windows |
| 12 | Recommended preset from a first-run benchmark | Done, on the Benchmark page |
| 13 | Smaller installer, FSR 4 files as an optional download | Partly: debug data stripped from the program. The FSR 4 files stay in the installer, as a separate download they would only trade size for a failure at first use |
| 14 | Difficulty sliders for damage dealt and taken | Done |
| 15 | Time of day and weather in photo mode | Tried and cut: the game keeps the clock, and holding the d-pad barely moved the sun |
| 16 | Depth of field and filters in photo mode | Filters done: black and white, faded, vivid, golden, cold (F8 in photo mode). Depth of field not done: new shader work |
| 17 | Fast-travel to any map marker | Not started |
| 18 | Teleport bookmarks inside shrines | Not started: needs the current map, which bookmarks do not record |
| 19 | Smarter CPU core placement | Nothing to gain on a 7800X3D (one die); untested on other CPUs |
| 20 | Skip shadow draws that don't matter | Not started: 200+ fps already, and skipping cascades risks visible shadow flicker |
| 21 | Higher optimization on the hottest game code | Dropped: all game code at -O2 gave 243 to 255 fps on the field route and nothing at Hateno, for 70 s more setup |
| 22 | Large memory pages for the game's heap, optional | Dropped: the game's memory is reserved with a write watch and committed a page at a time on first use, which large pages can't do. From the API's rules, not tried |
| 23 | Native versions of hot game routines | Dropped: all game code at -O2 barely moved Hateno, so faster routines look unlikely to help |
| 24 | Less guest memory kept committed | Not started: 5.2 GB in use on a 31 GB PC, no sign it limits anything |
| 25 | Skip descriptor and state changes that haven't changed | Mostly there already: about 90% of descriptor sets are reused. stage_set trimmed, gain within noise |
| 26 | Read ahead for the next area | Not started |
| 27 | Pre-warm pipelines at the title screen | Already there: shader precompilation and warm-up |
| 28 | Cache indirect call targets | Dropped: dispatch is 0.2% of samples |
| 29 | Less waiting for emulated cores | Tried and dropped: spinning before sleep cut 210 fps to 120 |
| 30 | Cheaper far grass | Done: removing my older patches took draw distance on from 149 to 211 fps with no visible change |
| 31 | Draw distance that holds a target frame rate | Done as Hold the frame rate, grass only |
| 32 | Incremental setup, so updates don't rebuild everything | Done: compiled pieces kept in cache/gamecode |
| 33 | Better horse steering on keyboard (#290) | Not started: needs testing on a horse with a keyboard |
| 34 | Update checks and in-place updates | Done up to the last step: checks GitHub, downloads the Setup.exe, compares it with the digest GitHub lists. Tried against the 0.1 release. Running the downloaded installer not tried |
| 35 | Per-action remapping (#180) | Not started |
| 36 | XeSS and DLSS | Not started: needs vendor SDKs |
| 37 | Community language packs (#265) | Done for menus: lang/<code>.txt. The game's own text goes through mods: the game's message files under mods/<name>/romfs. Not tried with a real translation |
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

## Beta: not thoroughly tested

Tagged (beta) in the menus, with a warning that they may break things: the white grass fix (Video > Wet grass fix; matched by the game code's hash), Cheats and every row on it including Weather, Profiles, Teleport, Start at Continue, Photo mode, AMD FSR 4, the mouse pointer and bow aiming, the Confirm button, and download and install. The distance sliders and the controller camera speed were tagged already.

Not tagged because they have no row of their own: the Alpha 4 gameplay fixes (moving platforms, shield surf, the Kohga freeze), not tried in play.

## Known bug: garbled Game Over title

Seen in play on 2026-11-01: the screen with Continue and Quit after dying read "ME HORWE" in red instead of the game's title, with a red smear to its left. Not looked into. One thing to check first: Alpha 4's fix for letters jumping above 60 fps (#263) is off on AMD's Windows driver here, and this may be the same text problem.

## Known bug: no time and weather tile

Alpha 4 draws the time and forecast tile by the minimap. Ours doesn't, with or without the DLC, started the same way as Alpha 4, and with the mouse pointer, HUD letters, Clarity, wide view and GPU overlap off. Cause not found: the next step is testing older builds.

## Parity

What Savage Wind 0.4.0-alpha.4 (2026-11-01, commit 02bbb0f) has that we don't. Found by looking for each change's switches and code in ours; nothing here has been ported or tried yet.

### Gameplay

| Change | Upstream | Note |
|---|---|---|
| Moving platforms at high frame rates: Link hopped on Vah Medoh's floor | #209, 6a3725d | |
| Shield surf takeoff above 100 fps | #305, 6a3725d | |
| Master Kohga's entrance freezing: that event held at 30 fps | #196, 232bb49 | |
| Horse names: "Talon" showed as "n" | #271, 3512f19 | Already fixed here another way. Theirs also guards the buffer's end |
| Speech and item text letters jumping above 60 fps | #263, 6a3725d | Our switch exists, off by default. Alpha 4 copies the HUD's reads on every GPU |

### Crashes and setup

| Change | Upstream | Note |
|---|---|---|
| Setup failing on some Windows 11 builds: the generator started without Control Flow Guard | #113, 93a1f18 | |
| Setup checks for the cutscene videos: without them the game crashed at the first tower | #292, 93a1f18 | |
| Map panning off its lower edge | #319, bb9bab9 | Some pan limit code exists in ours: check before porting |

### Graphics and speed

| Change | Upstream | Note |
|---|---|---|
| OBS Game Capture black frames: frames presented on the game's own queue | #293, 3fae40c | |
| Geometry copy for 4-6 GB cards, in chunks, within the driver's memory budget | f1f2d28 | |
| Cheaper write watch: 0.54 to 0.33 ms a list | f1f2d28 | Our log shows slow lists of about 35 ms |
| Video memory blocks made ahead of need, off by default | f1f2d28 | |

### Added features

| Change | Upstream | Note |
|---|---|---|
| In-game console (F12), `report`, `pos` | #285, ed9279a | |
| Control commands over a local socket, tools/swctl.py, tools/swbench.py | #284, #286, ed9279a | Could drive our tests: goto, time, shots |
| Video > Monitor and Video > Fullscreen mode | #245, 0aa6f04 | |
| Steam Deck gyro through DSU | #281, ed9279a | #281 stayed on our list when #235 and #272 came off |
| Link's position in the log every 30 s | 752d72f | |
| Version and source on the log's first line | 2b1db43 | |
| Crash details after a device loss, NVIDIA only | 3fae40c | |
| Linux overlay reads the GPU that renders | #315, 0fc5e4d | |

### In a commit we don't have, not checked one by one

All from 0aa6f04. Check each in ours before porting.

| Change | Upstream |
|---|---|
| Pause menu centred on wide screens | #238 |
| World map not stretched with Fill wide screens | #282, #253 |
| First open of Settings or Game Files no longer freezes the start screen | #279, #276 |
| Controller used in another program no longer acts in the game after Alt-Tab | #301 |
| Unbound arrow keys and Enter work in menus | #275 |
| Link's coordinates in the full overlay | #274 |

### Left out on purpose

Listed so the gap is on record. Not to port unless asked.

| Change | Upstream | Why |
|---|---|---|
| Tilt puzzles with keys, mouse or sticks | #235, #272, ed9279a | Taken off our list |
| FSR 3 frame generation | 9c02087 | Removed from ours |
| Draw distance Original / Medium / High | efd37bd | We have sliders instead |
