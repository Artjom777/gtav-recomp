GTA V Xbox 360 recompilation - macOS
=====================================

This package contains no game. It holds an app with a setup window, an Xbox 360 runtime and the
tools that turn your own copy of Grand Theft Auto V for Xbox 360 into a native Apple silicon build
on your Mac. No Rockstar Games or Microsoft game code, data, shaders or title-update files are
included.

What you need
-------------
- Your own Xbox 360 discs of GTA V: Disc 1 (Install) and Disc 2 (Play), as ISO images or as
  extracted folders.
- Title Update 27 for GTA V from your own console: the TU file (tu00000009_00000000) or a folder
  with disc002/default.xexp and disc002/update.rpf.
- A Mac with Apple silicon (M1 or newer) and macOS 15 (Sequoia) or newer. 8 GB RAM works; 16 GB is
  more comfortable. Nothing else to install: the compiler that builds the game is inside the app.
- A controller (Xbox or PlayStation, through SDL), or the keyboard.
- About 25 GB free during setup (about 17 GB once installed) on a normal, case-insensitive volume
  (the macOS default). The internal SSD loads much faster than a USB drive.

First run
---------
1. Unzip the download and move GTAV.app to Applications (or anywhere you like).
2. The app is not notarized by Apple, so macOS blocks it the first time. Either:
     - Open it once, then System Settings > Privacy & Security > "GTAV was blocked..." > Open Anyway
     - or, in Terminal:  xattr -dr com.apple.quarantine /Applications/GTAV.app
3. Open GTAV.app. Pick Disc 1, Disc 2 and the title update with the "Choose..." buttons, and the
   game folder (default: ~/Library/Application Support/GTAV Recomp). Press Install.
4. Setup checks every file against known-good hashes, copies them into the game folder, generates
   C++ from your game executable, compiles it into gtav_game.dylib with the bundled clang and
   removes the build files. This takes about 15-40 minutes. Cancel (or closing the window) stops
   it; installing again continues where it stopped.
5. Press Play. The first minutes of play translate shaders in the background; some objects can be
   missing until their shader is ready. They are kept in the game folder (data/spirv) for later.

Later runs: opening GTAV.app starts the game directly. Hold Option while opening it to get the
setup window again (for example after moving your discs, or to repair the install).
Esc in game opens the settings menu (quality presets, frame rate limit, VSync, FPS counter).
Keyboard: Space = A, Backspace = B, Return = Start, WASD = left stick, arrows = right stick.
Logs: <game folder>/data/logs (gtav.log for the game, install.log for setup).

iPhone (experimental)
---------------------
The Mac builds the iPhone app from your installed game (iOS cannot compile code or shaders); the
phone then installs the game's files itself from your discs, like the Mac's first-run setup.
Needs an iPhone 12 or newer (A14+, 4 GB RAM; tested on an iPhone 13 mini) on iOS 17 or newer, and
a sideloading tool that signs apps with your own Apple ID (Sideloadly or AltStore; with a free
Apple ID the app has to be re-signed every 7 days).
1. Install and play the game on the Mac for a while first: the game saves every shader it meets,
   and only those can be translated for the phone (drive around the map, enter interiors).
2. Hold Option while opening GTAV.app and press "Build for iPhone" (about 15-25 minutes). The game
   folder then has iphone/GTAV.ipa (the game code and shaders for the phone; not the game files).
3. Install iphone/GTAV.ipa on the phone with Sideloadly or AltStore.
4. Put your Disc 1 and Disc 2 ISO images and the TU27 file (tu00000009_00000000) where the phone
   can read them: in GTA V's folder (Finder > your iPhone > Files > GTA V, or the Files app), or
   on a USB drive plugged into the phone.
5. Open GTA V. Its setup screen finds the files in its folder (or pick them with Choose), copies
   and checks about 16 GB (keep the app open; an interrupted install continues where it stopped),
   offers to delete the disc images from its folder, and starts the game. Touch controls appear
   on screen; a Bluetooth controller works too.
Free space on the phone: about 17 GB, plus the disc images while installing if you copy them
there (about 16 GB; not needed with a USB drive).
Shaders the phone has no translation for are skipped (missing objects) and saved on the phone in
GTA V's folder, shader_cache. To fix them: copy that folder to <game folder>/iphone/shader_cache
on the Mac, press "Build for iPhone" again (only the shaders are redone, a few minutes) and
install the new GTAV.ipa over the old one (the game files on the phone stay).

Everything made from your files stays in the game folder (data/ and gtav_game.dylib). Do not share
it: it contains Rockstar Games' copyrighted work. To uninstall, delete GTAV.app and the game folder.

This project is free software under the GNU GPL, version 3 or later (LICENSES/gtav-recomp-GPL-3.0.txt).
Source code: https://github.com/andrewnakas/gtav-recomp. The complete source of every program in
this package that is under the GPL or LGPL (this project, the ReXGlue SDK runtime with the FFmpeg
it links statically, XenosRecomp, plume) is in gtav-recomp-src-<version>.tar.xz on the same
release page.
Bundled tools: clang and lld from LLVM 19.1.7 (official release build, Apache-2.0 with LLVM
exception), DirectX Shader Compiler (NCSA), MoltenVK and the Vulkan loader (Apache-2.0), simde
(MIT). tools/game_dll/libSystem.tbd is a list of five C library function names (this project) that
lets the bundled linker build gtav_game.dylib without Apple's SDK.
Licenses for the bundled open-source components are in LICENSES/.
Grand Theft Auto V is a trademark of Take-Two Interactive Software. This project is not affiliated
with or endorsed by Rockstar Games, Take-Two Interactive, Microsoft or Apple.
