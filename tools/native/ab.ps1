# A/B a runtime setting in one unattended run (port of ab.sh; scenes differ between runs, so
# separate runs compare badly): standing still at the spawn point, the setting flips between A and
# B every -Period seconds (logs/menu_set test hook) and the fps lines are averaged per value.
# usage: ab.ps1 NAME SETTING A B [-Rounds 4] [-Period 10] [-Move] [extra game flags...]
# -Move: run around (tools/native/input_run_around.txt, looped) instead of standing still.
param([Parameter(Mandatory = $true)] [string]$Name, [Parameter(Mandatory = $true)] [string]$Setting,
      [Parameter(Mandatory = $true)] [string]$A, [Parameter(Mandatory = $true)] [string]$B,
      [int]$Rounds = 4, [int]$Period = 10, [switch]$Move,
      [Parameter(ValueFromRemainingArguments = $true)] [string[]]$Extra)
$root = "E:\gtav-native"
$L = "$root\logs\boot\$Name.log"
$set = "$root\build\logs\menu_set"
if (Get-Process gtav_fast -ErrorAction SilentlyContinue) { "game already running: not starting"; exit 1 }
$go = "$root\logs\boot\${Name}_input.txt"
Remove-Item -Force -ErrorAction SilentlyContinue $L, $set, $go
# menu_set saves Esc-menu settings to gtav_settings.ini, which overrides the command line on the
# next play: keep the owner's file and put it back afterwards (a lag_ms=2 A/B once leaked into play).
$ini = "$root\build\gtav_settings.ini"
$iniSaved = if (Test-Path $ini) { Get-Content -Raw $ini } else { $null }
$flags = @("--gtav_native_stats=true", "--gtav_native_hidden=true", "--audio_mute=true", "--input_script=$go")
if ($Extra) { $flags += $Extra }
& "$root\play.ps1" -Log $Name -Fps 0 -Mode on -Counter -Extra $flags | Out-Null
Start-Sleep 2
$p = Get-Process gtav_fast -ErrorAction SilentlyContinue | Select-Object -First 1
if (-not $p) { "game did not start"; exit 3 }
$t0 = Get-Date
while ($true) {
  Start-Sleep 5
  if ($p.HasExited) { "game exited before reaching the world"; exit 3 }
  if ((Test-Path $L) -and (Select-String -Path $L -Pattern '\[native\] frame [0-9]+: [0-9]{3,} draws' -Quiet)) { break }
  if (((Get-Date) - $t0).TotalSeconds -gt 420) { "no in-game frame after 420 s"; Stop-Process -Id $p.Id -Force; exit 2 }
}
if ($Move) { Copy-Item "$root\tools\native\input_run_around.txt" $go }
Start-Sleep 15
$marks = @()
for ($i = 0; $i -lt $Rounds; $i++) {
  foreach ($v in @($A, $B)) {
    Set-Content -Path $set -Value "$Setting=$v" -Encoding ascii
    $marks += [pscustomobject]@{ T = Get-Date; V = $v }
    Start-Sleep $Period
  }
}
$marks += [pscustomobject]@{ T = Get-Date; V = "end" }
Stop-Process -Id $p.Id -Force
$p.WaitForExit(60000) | Out-Null
if ($null -ne $iniSaved) { Set-Content -Path $ini -Value $iniSaved -NoNewline -Encoding ascii } else { Remove-Item -Force -ErrorAction SilentlyContinue $ini }
# Average fps and draws per value, skipping 2 s after each flip.
$acc = @{}
foreach ($m in Select-String -Path $L -Pattern '^\[(\S+ \S+)\].*\[native\] fps=([0-9.]+).*drawidx=([0-9]+)') {
  $t = [datetime]::ParseExact($m.Matches[0].Groups[1].Value, 'yyyy-MM-dd HH:mm:ss.fff', $null)
  for ($k = $marks.Count - 2; $k -ge 0; $k--) {
    if ($t -ge $marks[$k].T.AddSeconds(2) -and $t -lt $marks[$k + 1].T) {
      $v = $marks[$k].V
      if (-not $acc.ContainsKey($v)) { $acc[$v] = @(0.0, 0.0, 0) }
      $acc[$v] = @(($acc[$v][0] + [double]$m.Matches[0].Groups[2].Value), ($acc[$v][1] + [double]$m.Matches[0].Groups[3].Value), ($acc[$v][2] + 1))
      break
    }
  }
}
foreach ($v in $acc.Keys) { "{0}={1}: {2:N1} fps (draws {3:N0}, {4} samples)" -f $Setting, $v, ($acc[$v][0] / $acc[$v][2]), ($acc[$v][1] / $acc[$v][2]), $acc[$v][2] }
# One-frame glitches per value (with --gtav_native_flicker_catch=true).
$fl = @{}
foreach ($m in Select-String -Path $L -Pattern '^\[(\S+ \S+)\].*\[native\] flicker #') {
  $t = [datetime]::ParseExact($m.Matches[0].Groups[1].Value, 'yyyy-MM-dd HH:mm:ss.fff', $null)
  for ($k = $marks.Count - 2; $k -ge 0; $k--) {
    if ($t -ge $marks[$k].T -and $t -lt $marks[$k + 1].T) { $fl[$marks[$k].V] = 1 + [int]$fl[$marks[$k].V]; break }
  }
}
foreach ($v in $fl.Keys) { "{0}={1}: {2} flicker events" -f $Setting, $v, $fl[$v] }
