# Unattended performance run on Windows (port of bench.sh): hidden native window, muted, uncapped.
# Boots into the world, plays tools/native/input_run_around.txt (unless -Still), then measures
# -Secs seconds: per-thread CPU (top 10) and the [native] timing lines.
# usage: bench.ps1 NAME [-Secs 30] [-Still] [extra game flags...]
param([Parameter(Mandatory = $true)] [string]$Name, [int]$Secs = 30, [switch]$Still, [switch]$Profile,
      [string]$Build = "E:\gtav-native\build",
      [Parameter(ValueFromRemainingArguments = $true)] [string[]]$Extra)
$root = "E:\gtav-native"
$L = "$root\logs\boot\$Name.log"
$go = "$root\logs\boot\${Name}_input.txt"
if (Get-Process gtav_fast -ErrorAction SilentlyContinue) { "game already running: not starting"; exit 1 }
Remove-Item -Force -ErrorAction SilentlyContinue $L, $go
$flags = @("--gtav_native_stats=true", "--gtav_native_hidden=true", "--audio_mute=true", "--input_script=$go")
if ($Extra) { $flags += $Extra }
& "$root\play.ps1" -Log $Name -Fps 0 -Mode on -Counter -Build $Build -Extra $flags | Out-Null
Start-Sleep 2
$p = Get-Process gtav_fast -ErrorAction SilentlyContinue | Select-Object -First 1
if (-not $p) { "game did not start"; exit 3 }
$t0 = Get-Date
while ($true) {
  Start-Sleep 5
  if ($p.HasExited) { "game exited before reaching the world"; exit 3 }
  if ((Test-Path $L) -and (Select-String -Path $L -Pattern '\[native\] frame [0-9]+: [0-9]{3,} draws' -Quiet)) { break }
  if (((Get-Date) - $t0).TotalSeconds -gt 420) { "no in-game frame after 420 s (boot hang?)"; Stop-Process -Id $p.Id -Force; exit 2 }
}
"in game after $([int]((Get-Date) - $t0).TotalSeconds) s"
Start-Sleep 10
if (-not $Still) { Copy-Item "$root\tools\native\input_run_around.txt" $go }
Start-Sleep 5
$p.Refresh()
$cpu0 = @{}; foreach ($t in $p.Threads) { try { $cpu0[$t.Id] = $t.TotalProcessorTime.TotalMilliseconds } catch {} }
$line0 = (Get-Content $L | Measure-Object -Line).Lines
# -Profile: sample the 5 busiest threads (tools/native/sample.py) -> logs\boot\NAME_profile.txt
if ($Profile) { python -I "$root\tools\native\sample.py" $Secs 5 400 2>&1 | Out-File -Encoding utf8 "$root\logs\boot\${Name}_profile.txt" }
else { Start-Sleep $Secs }
$p.Refresh()
$rows = foreach ($t in $p.Threads) {
  try { if ($cpu0.ContainsKey($t.Id)) { [pscustomobject]@{ Id = $t.Id; Pct = ($t.TotalProcessorTime.TotalMilliseconds - $cpu0[$t.Id]) / 10 / $Secs } } } catch {}
}
Stop-Process -Id $p.Id -Force
$p.WaitForExit(60000) | Out-Null
"== CPU per thread over $Secs s (% of one core, top 10; match ids with [tNNN] in the log)"
$rows | Sort-Object Pct -Descending | Select-Object -First 10 | ForEach-Object { "{0,6:N1}%  t{1}" -f $_.Pct, $_.Id }
"== timing lines in the window"
Get-Content $L | Select-Object -Skip $line0 |
  Select-String -Pattern '\[native\] (fps=|record ms|presented|  avg acquire|  textures per|command processor waited|fence copies|write watch|capture constants)|audio: ' |
  Select-Object -Last 16 | ForEach-Object { $_.Line -replace '^\[[^\]]*\] \[[^\]]*\] \[[^\]]*\] ', '' }
