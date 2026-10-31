# The next idea list

Saved for later. Nothing here is being worked on yet. Sources: what Savage Wind has open or planned as of 2026-10-31, and our
own ideas. Numbers are their pull requests and issues on [Revivedx/SavageWindRecomp](https://github.com/Revivedx/SavageWindRecomp).

Already taken from them for the current list: draw distance (#88) and the memory fixes on reloads (#237).

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
| Mouse sensitivity | #288, #289 |
| Key mapping | #304 |
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
