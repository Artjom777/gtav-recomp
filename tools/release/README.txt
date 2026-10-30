GTA V Xbox 360 recompilation - Windows
=======================================

This package contains no game. It holds a launcher, an Xbox 360 runtime and the tools that turn
your own copy of Grand Theft Auto V for Xbox 360 into a native Windows build on your computer.
No Rockstar Games or Microsoft game code, data, shaders or title-update files are included.

What you need
-------------
- Your own Xbox 360 discs of GTA V: Disc 1 (Install) and Disc 2 (Play), as ISO images or as
  extracted folders.
- Title Update 27 for GTA V from your own console: the TU file (tu00000009_00000000) or a folder
  with disc002\default.xexp and disc002\update.rpf.
- Windows 10/11 x64, a CPU with AVX2 (Intel Haswell / AMD Zen or newer), a GPU with current Vulkan drivers.
- The Microsoft Visual C++ 2015-2022 Redistributable (x64):
  https://aka.ms/vs/17/release/vc_redist.x64.exe
- An Xbox-compatible (XInput) controller.
- About 25 GB free on this drive during setup (about 17 GB once installed), 8 GB RAM or more.

First run
---------
1. Unzip this folder somewhere with enough space and run gtav_launcher.exe.
2. Pick Disc 1, Disc 2 and the title update, then press Install.
3. The launcher checks every file against known-good hashes, copies them into data\, generates
   C++ from your game executable, compiles it into gtav_game.dll and removes the build files.
   This takes 15-30 minutes depending on your CPU.
4. The game starts. The first minutes of play translate shaders in the background; some objects
   can be missing until their shader is ready. They are kept in data\spirv for later runs.

Later runs start the game directly. Hold Shift while starting the launcher (or pass --setup) to
open setup again, for example after moving your discs.

Everything made from your files stays in this folder (data\ and gtav_game.dll). Do not share it:
it contains Rockstar Games' copyrighted work.

This project is free software under the GNU GPL, version 3 or later (LICENSES\gtav-recomp-GPL-3.0.txt).
Source code: https://github.com/andrewnakas/gtav-recomp (the launcher, the runtime changes as
patches on top of ReXGlue SDK v0.10.0 and XenosRecomp, and the release tools).
Licenses for the bundled open-source components are in LICENSES\.
Grand Theft Auto V is a trademark of Take-Two Interactive Software. This project is not affiliated
with or endorsed by Rockstar Games, Take-Two Interactive or Microsoft.
