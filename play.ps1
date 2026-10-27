# Play the recompiled GTA V (TU27) with the native Vulkan renderer.
#   play.ps1 [-Log name] [-Fps 0|60|30] [-Mode on|shadow|off] [-Counter] [extra runtime flags...]
# -Fps 0 = uncapped, 30 = the game's own limiter. Controller via XInput (works while the window is unfocused).
# Runs gtav_fast.exe: the IFEO page-heap entry is keyed to gtav.exe.
param([string]$Log = "play", [int]$Fps = 0, [string]$Mode = "on", [switch]$Counter,
      [Parameter(ValueFromRemainingArguments = $true)] [string[]]$Extra)
$build = "E:\gtav-native\build"
$logs = "E:\gtav-native\logs\boot"
New-Item -ItemType Directory -Force $logs | Out-Null
$a = @("--game_data_root=E:\gtav-rex\game\disc2_files", "--user_data_root=E:/gtav-rex/userdata",
  "--update_data_root=E:/gtav-rex/game/update", "--gpu_plugin=xenos", "--headless=true", "--log_level=info",
  "--log_max_file_size_mb=1024", "--log_max_files=2", "--log_file=$logs/$Log.log", "--input_backend=xinput",
  "--gtav_native=$Mode", "--gtav_native_spirv_dir=$build\spirv", "--gtav_fps_limit=$Fps")
if ($Mode -eq "on") { $a += @("--gpu_skip_rendering=true", "--vsync=false", "--gtav_native_vsync=false") }
if ($Counter) { $a += "--gtav_native_fps" }
if ($Extra) { $a += $Extra }
# Read the exe and DLLs once so they are in the file cache: the first launch after a build paged
# the 134 MB exe in from the busy E: disk during boot (XEX patch 4-8 s instead of 0.3 s), and both
# of those slow boots hung before VdSetGraphicsInterruptCallback.
$buf = New-Object byte[] (8MB)
foreach ($f in @("$build\gtav_fast.exe") + (Get-ChildItem "$build\*.dll").FullName) {
  $s = [IO.File]::OpenRead($f)
  try { while ($s.Read($buf, 0, $buf.Length) -gt 0) {} } finally { $s.Close() }
}
$p = Start-Process -FilePath "$build\gtav_fast.exe" -ArgumentList $a -WorkingDirectory $build -PassThru
"launched pid $($p.Id), log $logs\$Log.log"
