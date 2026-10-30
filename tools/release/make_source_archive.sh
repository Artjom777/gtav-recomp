#!/bin/sh
# Complete corresponding source for a binary release (GPL-3.0 for this project and binutils'
# disassembler in the SDK; LGPL-2.1+ for the FFmpeg that is linked statically into the runtime).
# Packs, at the exact commits the binaries were built from: this repo, ReXGlue SDK + every
# submodule (FFmpeg included), XenosRecomp + submodules, plume. Our changes to the three
# upstreams are the *.patch files in this repo. Sources come from `git archive`, so only committed
# files go in: no build output, no game data.
#
# usage: make_source_archive.sh OUT.tar.xz TAG SDK_DIR XENOSRECOMP_DIR PLUME_DIR
#   TAG  the release tag of this repo (e.g. v0.1.0-windows)
set -eu
out=$1 tag=$2 sdk=$3 xr=$4 plume=$5
repo=$(cd "$(dirname "$0")/../.." && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
top=gtav-recomp-src
tarball=$work/src.tar

add() {  # add <git dir> <commit> <prefix>
  git -C "$1" archive --format=tar --prefix="$top/$3/" "$2" > "$work/part.tar"
  if [ -f "$tarball" ]; then tar --force-local -Af "$tarball" "$work/part.tar"; else mv "$work/part.tar" "$tarball"; fi
  echo "  $3 @ $(git -C "$1" rev-parse --short "$2^{commit}")"
}
add_tree() {  # add a repo and, recursively, its submodules at their checked-out commits
  add "$1" HEAD "$2"
  git -C "$1" submodule status --recursive | while read -r sha path rest; do
    sha=${sha#[-+ U]}
    add "$1/$path" "$sha" "$2/$path"
  done
}

echo "packing:"
add "$repo" "$tag" gtav-recomp
add_tree "$sdk" rexglue-sdk
add_tree "$xr" XenosRecomp
add_tree "$plume" plume

cat > "$work/SOURCE.txt" <<EOF
Corresponding source for the gtav-recomp $tag binaries.

gtav-recomp/   this project at $tag (GPL-3.0-or-later)
rexglue-sdk/   ReXGlue SDK $(git -C "$sdk" describe --tags --always) ($(git -C "$sdk" rev-parse --short HEAD)) with every submodule
               at the commit the release was built from. Apply gtav-recomp/rexglue-sdk-gtav-linux.patch
               (all platforms) before building: git apply ../gtav-recomp/rexglue-sdk-gtav-linux.patch
XenosRecomp/   $(git -C "$xr" rev-parse --short HEAD) + submodules; apply gtav-recomp/xenosrecomp-gtav.patch
plume/         $(git -C "$plume" rev-parse --short HEAD); apply gtav-recomp/plume-gtav.patch

FFmpeg (rexglue-sdk/thirdparty/FFmpeg, LGPL-2.1-or-later; FFmpeg's GPL parts are not enabled
by the SDK's build) is linked statically into rexruntimerd.dll. To use a modified FFmpeg: change
it in rexglue-sdk/thirdparty/FFmpeg, rebuild the SDK (gtav-recomp/build_sdk.cmd on Windows), and
put the new rexruntimerd.dll (and the other SDK DLLs it was built with) in the release folder.
The rest of the program needs no change: gtav_fast.exe links to rexruntimerd.dll dynamically.

Building everything: gtav-recomp/docs/RELEASE_CLEANROOM.md (release layout and tools) and
gtav-recomp/build_sdk.cmd, build_xr.cmd, build_release_exe.cmd, build_launcher.cmd (Windows).
LLVM (clang, lld) and the DirectX Shader Compiler are bundled unmodified from their official
releases (LLVM 19.1.5: github.com/llvm/llvm-project; dxc: XenosRecomp/thirdparty/dxc-bin).

No game data is in this archive or in the release.
EOF
tar --force-local -C "$work" --transform "s,^,$top/," -rf "$tarball" SOURCE.txt
xz -T0 -6 -c "$tarball" > "$out"
echo "$(du -h "$out" | cut -f1) -> $out"
