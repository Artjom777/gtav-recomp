GTA V Xbox 360 recompilation - Linux
=====================================

This package contains no game. It holds a launcher, an Xbox 360 runtime and the tools that turn
your own copy of Grand Theft Auto V for Xbox 360 into a native Linux build on your computer.
No Rockstar Games or Microsoft game code, data, shaders or title-update files are included.

What you need
-------------
- Your own Xbox 360 discs of GTA V: Disc 1 (Install) and Disc 2 (Play), as ISO images or as
  extracted folders.
- Title Update 27 for GTA V from your own console: the TU file (tu00000009_00000000) or a folder
  with disc002/default.xexp and disc002/update.rpf.
- 64-bit Linux with glibc 2.35 and GCC 13's libstdc++ (GLIBCXX_3.4.32) or newer: Ubuntu 24.04,
  Debian 13, Fedora 39, Arch, openSUSE Tumbleweed or newer. (Ubuntu 22.04 works after installing
  libstdc++6 from the ubuntu-toolchain-r/test PPA; Debian 12 is too old.)
- A CPU with AVX2 (Intel Haswell / AMD Zen or newer), a GPU with current Vulkan drivers and the
  Vulkan loader (libvulkan.so.1; on Ubuntu/Debian: libvulkan1 and mesa-vulkan-drivers, or the
  NVIDIA driver). A desktop session (X11 or Wayland): the X11/xcb and Wayland client libraries,
  zlib and zstd that every desktop system has.
- A controller (Xbox-style, through SDL).
- About 25 GB free on this drive during setup (about 17 GB once installed), 8 GB RAM or more.
  Put the package on a Linux filesystem (ext4, btrfs, xfs) or exFAT; it needs executable files.

First run
---------
1. Unpack the archive somewhere with enough space:
     tar xf gtav-recomp-linux-x64.tar.gz
     cd gtav-recomp-linux-x64
2. Start the launcher in a terminal and give it your Disc 1, Disc 2 and the title update (it asks
   for each path if you leave them out; you can drag a file into the terminal):
     ./gtav_launcher --setup "/path/Disc 1.iso" "/path/Disc 2.iso" /path/tu00000009_00000000
3. The launcher checks every file against known-good hashes, copies them into data/, generates C++
   from your game executable, compiles it into gtav_game.so with the bundled clang and removes the
   build files. This takes 15-30 minutes depending on your CPU. Ctrl+C stops it; running setup
   again continues where it stopped.
4. The game starts. The first minutes of play translate shaders in the background; some objects
   can be missing until their shader is ready. They are kept in data/spirv for later runs.

Later runs: ./gtav_launcher starts the game directly. ./gtav_launcher --setup runs setup again,
for example after moving your discs.

Game options go after "--". On a laptop with two GPUs, pick the dedicated one with
  ./gtav_launcher -- --vulkan_device=1
Esc in game opens the settings menu (quality presets, frame rate limit, VSync, FPS counter).
The game log is data/logs/gtav.log, the setup log data/logs/install.log.

Everything made from your files stays in this folder (data/ and gtav_game.so). Do not share it:
it contains Rockstar Games' copyrighted work.

This project is free software under the GNU GPL, version 3 or later (LICENSES/gtav-recomp-GPL-3.0.txt).
Source code: https://github.com/andrewnakas/gtav-recomp. The complete source of every program in
this package that is under the GPL or LGPL (this project, the ReXGlue SDK runtime with the FFmpeg
it links statically, XenosRecomp, plume) is in gtav-recomp-src-<version>.tar.xz on the same
release page.
Bundled tools: clang and lld from LLVM 19.1.5 (official release build, Apache-2.0 with LLVM
exception), DirectX Shader Compiler (NCSA). tools/llvm/lib/libxml2.so.2 is not libxml2: it is a
small stand-in from this project (tools/release/lld_libxml2_stub.c) so lld starts on systems
without libxml2; the ELF linking the launcher does never uses it.
Licenses for the bundled open-source components are in LICENSES/.
Grand Theft Auto V is a trademark of Take-Two Interactive Software. This project is not affiliated
with or endorsed by Rockstar Games, Take-Two Interactive or Microsoft.
