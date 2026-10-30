#!/bin/sh
# Launch the recompiled GTA V (TU27) on macOS (arm64). Same flags as play.sh.
# Usage: play_mac.sh [logname] [extra runtime flags...]
# Game data: GTAV_DATA (default ~/gtav-ssd if present, else ~/gtav-data; must be a case-insensitive
# volume, Xbox paths ignore case). game/ and userdata/ are read; the internal SSD loads far faster than USB.
# GTAV_NATIVE=off (default here until the native renderer is ported) | on | shadow;
# GTAV_FPS=0 (uncapped) | 30 | N; GTAV_LOG_LEVEL=debug for verbose logs. Keyboard plays as a pad
# (--mnk_mode: Space = A, Backspace = B, Return = Start, WASD = left stick, arrows = right stick).
R="$(cd "$(dirname "$0")" && pwd)"
if [ -z "$GTAV_DATA" ] && [ -d "$HOME/gtav-ssd/game" ]; then GTAV_DATA="$HOME/gtav-ssd"; fi
E="${GTAV_DATA:-$HOME/gtav-data}"
BUILD="${GTAV_BUILD:-$R/build/gtav}"
name=${1:-play}; [ $# -gt 0 ] && shift
mkdir -p "$R/logs"
cd "$BUILD" || exit 1
mode=${GTAV_NATIVE:-off}
set -- --gtav_native=$mode --gtav_fps_limit=${GTAV_FPS:-0} --audio_device_frames=256 --mnk_mode=true "$@"
[ "$mode" = on ] && set -- --gpu_skip_rendering=true --gpu_skip_rendering_lag_ms=${GTAV_CP_LAG:-6} --vsync=false --gtav_native_vsync=false "$@"
exec ./gtav \
  --game_data_root="$E/game/disc2_files" \
  --user_data_root="$E/userdata" \
  --update_data_root="$E/game/update" \
  --gpu_plugin=xenos --headless=true --log_level=${GTAV_LOG_LEVEL:-info} \
  --log_max_file_size_mb=1024 --log_max_files=2 \
  --log_file="$R/logs/$name.log" "$@"
