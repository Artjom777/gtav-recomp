#!/bin/sh
# Launch the recompiled GTA V (TU27) for play: sound on, info log.
# Usage: play.sh [logname] [extra runtime flags...]
# Game data stays on the TOSHIBA drive (exFAT is case-insensitive, matching Xbox paths).
# Renders on Vulkan device 1 (the RTX 4050 on this laptop; 0 = Intel iGPU); override with GTAV_VULKAN_DEVICE.
# GTAV_LOG_LEVEL=debug for verbose logs.
E="/run/media/$USER/TOSHIBA EXT/gtav-rex"
BUILD="$HOME/gtav-linux/build/gtav"
name=${1:-play}; [ $# -gt 0 ] && shift
mkdir -p "$HOME/gtav-linux/logs"
# Killed runs leak their 4.8 GB guest arena in /dev/shm; once tmpfs fills, the next run dies of SIGBUS.
for f in /dev/shm/xenia_memory_*; do
  [ -e "$f" ] && ! fuser -s "$f" 2>/dev/null && rm -f "$f"
done
cd "$BUILD" || exit 1
export LD_LIBRARY_PATH="$BUILD${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
exec ./gtav \
  --game_data_root="$E/game/disc2_files" \
  --user_data_root="$E/userdata" \
  --update_data_root="$E/game/update" \
  --gpu_plugin=xenos --headless=true --log_level=${GTAV_LOG_LEVEL:-info} \
  --vulkan_device=${GTAV_VULKAN_DEVICE:-1} \
  --log_max_file_size_mb=1024 --log_max_files=2 \
  --log_file="$HOME/gtav-linux/logs/$name.log" "$@"
