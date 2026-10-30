# Slop of the Wild — to do

## Done (0.0)
- Start screen art from the player's own game (captured from the title screen on the first play).
- "Savage Wind" -> "Slop of the Wild" in menus (tr), window titles, dialogs, release packaging.
- Version numbers no longer split when text wraps ("1.\n8.2").
- Setup: one fewer walk of the game files, a live file count, "clearing the earlier copy" step; lands on the start
  screen when done; no 14 GB romfs.bin (romfs.idx).

## Left
- A Slop of the Wild icon (the exe still has Savage Wind's sword-in-stone).
- A full `make dist` release (LGPL FFmpeg, frozen generator, slimmed zig) from this tree; 0.0 ships as a drop-in
  update over Savage Wind 0.3.1-alpha.3.

## Optimization
- Graphics pipeline libraries crash on AMD: GPU page fault reading 0x13f800000 every run, with or without the guest
  heap import or feedback loops. TotK uses the same feature on the same GPU: our bug. Next: Vulkan validation layers.
- From the TotK port: a VRAM mirror of buffer-bound guest memory on discrete GPUs (we copy ~1.7 GB/s), GPU work
  submitted in parts (every 64 draws), dropping only full-screen draws while their pipeline builds, VMA blocks
  allocated ahead on a thread, shipped shader seeds.
