# Slop of the Wild: to do

Current as of **0.3** (2026-11-01). The whole list of tasks and ideas, ranked easiest to hardest, is at the top of [docs/next-ideas.md](docs/next-ideas.md). This page is the short version.

## Known issues

- Weather > Snow and Heavy snow (beta) look like night at noon in cold areas, though the snow does fall. Compare with snow the game picks itself, then fix or accept it
- Blocky steps at the edges of the System page's background at 21:9. Check whether Savage Wind does it too
- Snow may look like dark rain in warm areas
- Grass shimmers and shows artifacts in sunlight
- The white grass fix may still look a little pale, and has only been tested standing still in three places
- Object distance changes how the game plays: enemies can notice you sooner
- Distant detail shimmers, most with SMAA. TAA and a higher render scale help
- Shadows stay crunchy at a distance
- Uninstalling keeps saves, settings and game files
- The first play after setup can be rougher while Windows and the driver cache things

## Not tested yet

- A real mouse with the new pointer: Game Over, the quick selector and panning the map
- Arrow keys and Enter in menus in the running game
- Alt-Tab with a real controller, and the start screen freeze fix on a slow drive. Both only passed automated tests
- Moving the game to a second monitor
- The wide pause menu and map beyond one 2560x1080 window
- Save import, and the Confirm button on a controller
- Moving platforms, shield surf and the Kohga freeze in real play
- No rain and No thunderstorms against real weather
- Updating from 0.2.1 with the built-in updater
- Any hardware but an AMD Radeon RX 9070 XT with a Ryzen 7 7800X3D
- Frame rate on 0.3

## Next, easiest first

- Tune the white grass sheen; Clarity's shadows and noon saturation
- Play-test the ported fixes
- Video > Haze from Savage Wind (#317), and frame rates above 240 fps (#323)
- OBS Game Capture black frames (#293), and the cheaper write watch
- A hitch log for players
- Grass shimmer in sunlight
- Teleport in real time, with no restart and no dependence on saves
- On hold: adjustable dialogue text speed (#338)

## Done

- **0.3:** Fullscreen mode and Monitor (beta); Savage Wind's newer mouse pointer; wide pause menu and map; start screen freeze, Alt-Tab controller and map panning fixes; unbound arrow keys and Enter in menus; coordinates in the overlay; a frame recorder for tests; the benchmark's shadow flashes and Link's pose glitch
- **0.2.1:** the first-run popup removed; the AMD fix for jumping text letters; a sturdier white grass fix; a notice for out-of-date shaders
- **0.2:** the updater, and what's listed in its [release notes](https://github.com/devall96/Slop-of-the-Wild/releases/tag/0.2)
- **0.1:** a single installer and a new front page
- **0.0:** the fork: Slop of the Wild's name and menus, render scale, Clarity, the start screen
