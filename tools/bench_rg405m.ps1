param(
    [string]$Device = 'RG405M01566670',
    [string]$Map = '01_NYC_UNATCOIsland',
    [int]$Warmup = 300,
    [int]$Frames = 1200,
    # Each entry: "label|cmd1;cmd2" -- commands sent before the bench (e.g. "noop_off|opt noop 0")
    [string[]]$Variants = @('base|'),
    [int]$Repeats = 2,
    [switch]$NoInstall,
    [string]$Apk = 'C:\Users\Accorsi\.gemini\antigravity\scratch\OpenDE-Android\android\app\build\outputs\apk\debug\app-debug.apk'
)

$ErrorActionPreference = 'Stop'
$pkg = 'com.opende.deusex'
$ext = "/storage/emulated/0/OpenDE"

function Adb { & adb -s $Device @args }
function SendCmd([string]$text) {
    $tmp = [IO.Path]::GetTempFileName()
    [IO.File]::WriteAllText($tmp, $text + "`n")
    Adb push $tmp "$ext/debug_cmd.txt" | Out-Null
    Remove-Item $tmp
}
function ResultCount {
    $c = Adb shell "cat $ext/bench_results.txt 2>/dev/null | wc -l"
    return [int]($c | Out-String).Trim()
}
function Temps {
    $t = Adb shell 'for z in /sys/class/thermal/thermal_zone*/; do echo -n "$(cat $z/type 2>/dev/null)=$(cat $z/temp 2>/dev/null) "; done'
    return ($t | Out-String).Trim()
}

if (-not $NoInstall) { Adb install -r $Apk | Out-Null }
Adb shell "appops set $pkg MANAGE_EXTERNAL_STORAGE allow; rm -f $ext/bench_results.txt $ext/debug_cmd.txt /sdcard/Android/data/$pkg/files/debug_cmd.txt; input keyevent KEYCODE_WAKEUP" | Out-Null
Adb shell "am force-stop $pkg"
Start-Sleep 1
Adb shell "am start -n $pkg/.OpenDEActivity" | Out-Null
Start-Sleep 8
SendCmd "exec open $Map"
Start-Sleep 14

$results = @()
for ($r = 1; $r -le $Repeats; $r++) {
    foreach ($v in $Variants) {
        $parts = $v.Split('|', 2)
        $label = $parts[0]
        $cmds = if ($parts.Length -gt 1 -and $parts[1]) { $parts[1].Split(';') } else { @() }
        $before = ResultCount
        $payload = (@($cmds) + "bench $Warmup $Frames ${label}_r$r") -join "`n"
        SendCmd $payload
        $deadline = (Get-Date).AddSeconds(240)
        while ((ResultCount) -le $before) {
            if ((Get-Date) -gt $deadline) { throw "bench timeout ($label r$r)" }
            Start-Sleep 3
        }
        $line = (Adb shell "tail -n 1 $ext/bench_results.txt" | Out-String).Trim()
        Write-Host $line
        $results += $line
    }
}
Write-Host "temps: $(Temps)"
