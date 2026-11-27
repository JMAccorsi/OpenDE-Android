param(
    [Parameter(Mandatory = $true, Position = 0)]
    [ValidateSet("debug", "release", "shot")]
    [string]$Action,

    [string]$Serial = "MC94528AYF122800013",
    [string]$Version = "",
    [string]$Out = "",
    [string]$Crop = "",
    [switch]$Install,
    [switch]$Launch,
    [switch]$Full
)

$ErrorActionPreference = "Stop"
$RepoRoot = Split-Path -Parent $PSScriptRoot
$AndroidDir = Join-Path $RepoRoot "android"
$ExpectedSha256 = "f4386ba9642d015cc2d2bfaa2ebe76d682b9c068444e5008b0ad426b0118104f"

if ($Action -eq "debug") {
    Push-Location $AndroidDir
    try {
        .\gradlew.bat assembleDebug -q --console=plain
        if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
        $apk = Join-Path $AndroidDir "app\build\outputs\apk\debug\app-debug.apk"
        Write-Output "OK debug APK: $apk"
        if ($Install) {
            adb -s $Serial install -r $apk | Out-Null
            if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
            Write-Output "OK installed on $Serial"
        }
        if ($Launch) {
            adb -s $Serial shell am start -n com.opende.deusex/com.opende.deusex.OpenDEActivity | Out-Null
            if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
            Write-Output "OK launched com.opende.deusex/.OpenDEActivity"
        }
    } finally {
        Pop-Location
    }
    exit 0
}

if ($Action -eq "release") {
    Push-Location $AndroidDir
    try {
        .\gradlew.bat assembleRelease -q --console=plain
        if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

        $bt = Get-ChildItem "$env:LOCALAPPDATA\Android\Sdk\build-tools" | Sort-Object Name -Descending | Select-Object -First 1
        $zipalign = Join-Path $bt.FullName "zipalign.exe"
        $apksigner = Join-Path $bt.FullName "apksigner.bat"
        $inApk = Join-Path $AndroidDir "app\build\outputs\apk\release\app-release-unsigned.apk"
        $alignedApk = Join-Path $AndroidDir "app\build\outputs\apk\release\app-release-aligned.apk"
        $apkName = if ($Version) { "openDE-$Version-arm64-v8a.apk" } else { "openDE-release-arm64-v8a.apk" }
        $outApk = if ($Out) { $Out } else { Join-Path $AndroidDir "app\build\outputs\apk\release\$apkName" }

        & $zipalign -f -p 4 $inApk $alignedApk
        if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

        & $apksigner sign --ks "$env:USERPROFILE\.android\debug.keystore" --ks-pass pass:android --ks-key-alias androiddebugkey --out $outApk $alignedApk
        if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

        $certs = & $apksigner verify --print-certs $outApk
        if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
        if (($certs -join "`n") -notmatch $ExpectedSha256) {
            Write-Error "FATAL: APK signature SHA-256 mismatch! Expected $ExpectedSha256"
            exit 1
        }
        Write-Output "OK signed release APK (OTA SHA-256 verified): $outApk"

        if ($Install) {
            adb -s $Serial install -r $outApk | Out-Null
            if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
            Write-Output "OK installed on $Serial"
        }
        if ($Launch) {
            adb -s $Serial shell am start -n com.opende.deusex/com.opende.deusex.OpenDEActivity | Out-Null
            if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
            Write-Output "OK launched com.opende.deusex/.OpenDEActivity"
        }
    } finally {
        Pop-Location
    }
    exit 0
}

if ($Action -eq "shot") {
    if (-not $Out) {
        Write-Error "Specify -Out <path.png>"
        exit 1
    }
    $rawPath = if ($Full -and -not $Crop) { $Out } else { "$Out.raw.png" }
    cmd /c "adb -s $Serial exec-out screencap -p > `"$rawPath`""
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

    if ($Full -and -not $Crop) {
        Write-Output "OK full screenshot: $Out"
        exit 0
    }

    python -c "
from PIL import Image
im = Image.open(r'$rawPath')
crop = '$Crop'
if crop:
    x, y, w, h = [int(v) for v in crop.split(',')]
    im = im.crop((x, y, x + w, y + h))
elif not $($Full.IsPresent.ToString()):
    im = im.resize((max(1, im.width // 2), max(1, im.height // 2)), Image.Resampling.BILINEAR)
im.save(r'$Out', optimize=True)
"
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    Remove-Item -Force $rawPath -ErrorAction SilentlyContinue
    Write-Output "OK screenshot: $Out"
    exit 0
}
