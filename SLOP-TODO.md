# Slop of the Wild — to do

## Launcher / setup
- Setup page: same BOTW title-art background as the main launcher. The art can't ship (Nintendo's), so setup
  should capture it from the player's own game files (the title screen's key art, as captured by hand on the
  first install: splash.jpg, splash2..4.jpg next to the exe) and write splash*.jpg itself.
- Setup page and its messages: any "Savage Wind" names left (most go through tr() and are rebranded at run
  time; check the literals).
- "Not version 1.8.2 / 1.9.0" message wraps mid-number ("1.\n8.2").
- Setup's pre-copy checks: one walk of the game files with a live file counter.

## Release packaging (when the fork exists)
- Zip name / top folder ("Savage Wind"), "Savage Wind Configuration.exe", README and docs, icon.

## Optimization
- Graphics pipeline libraries crash on AMD (vkCreateDescriptorPool -13 in exec.c vk_alloc_set).
- TotK-port techniques: memory mirror, pipeline prebuild / skip, split submits, 1 ms timer.
- Existing installs: offer to switch romfs.bin (14 GB) to romfs.idx.
