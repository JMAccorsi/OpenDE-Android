param(
    [string]$GameDir = ""
)

$ErrorActionPreference = "Stop"
$RepoRoot = Split-Path -Parent $MyInvocation.MyCommand.Path

if ([string]::IsNullOrWhiteSpace($GameDir)) {
    $candidates = @(
        "G:\SteamLibrary\steamapps\common\Deus Ex",
        "G:\Games\Deus Ex GOTY"
    )
    foreach ($candidate in $candidates) {
        if (Test-Path (Join-Path $candidate "System\Engine.u")) {
            $GameDir = $candidate
            break
        }
    }
}

if ([string]::IsNullOrWhiteSpace($GameDir) -or !(Test-Path (Join-Path $GameDir "System\Engine.u"))) {
    throw "Could not locate Deus Ex game data. Pass -GameDir <path_to_Deus_Ex>."
}

$adb = @(
    "C:\Program Files (x86)\ADB & Fastboot++\adb.exe",
    "C:\Users\Accorsi\AppData\Local\Android\Sdk\platform-tools\adb.exe",
    (Get-Command adb -ErrorAction SilentlyContinue | Select-Object -ExpandProperty Source -ErrorAction SilentlyContinue)
) | Where-Object { $_ -and (Test-Path $_) } | Select-Object -First 1

if (-not $adb) {
    throw "adb.exe not found."
}

$apk = Join-Path $RepoRoot "android\app\build\outputs\apk\debug\app-debug.apk"
if (!(Test-Path $apk)) {
    throw "APK not found at $apk. Run gradlew.bat assembleDebug first."
}

Write-Host "Using ADB: $adb"
Write-Host "Using GameDir: $GameDir"

$devices = & $adb devices | Where-Object { $_ -match "\tdevice$" }
if (-not $devices) {
    throw "No authorized Android device connected over ADB. Enable USB debugging and connect your device."
}

Write-Host "Installing APK..."
& $adb install -r $apk
if ($LASTEXITCODE -ne 0) { throw "adb install failed" }

$remoteRoot = "/sdcard/OpenDE"
& $adb shell "mkdir -p $remoteRoot/System $remoteRoot/Maps $remoteRoot/Textures $remoteRoot/Sounds $remoteRoot/Music"

foreach ($folder in @("System", "Maps", "Textures", "Sounds", "Music")) {
    $localFolder = Join-Path $GameDir $folder
    if (Test-Path $localFolder) {
        Write-Host "Pushing $folder -> $remoteRoot/$folder ..."
        & $adb push "$localFolder/." "$remoteRoot/$folder/"
        if ($LASTEXITCODE -ne 0) { throw "Failed pushing $folder" }
    }
}

Write-Host "Applying DXController overlay..."
& $adb shell "if [ -f $remoteRoot/System/DeusEx.u ] && [ ! -f $remoteRoot/System/DeusEx.u.vanilla ]; then cp $remoteRoot/System/DeusEx.u $remoteRoot/System/DeusEx.u.vanilla; fi"
& $adb push (Join-Path $RepoRoot "dxcontroller-release\DXController.u") "$remoteRoot/System/DXController.u"
& $adb push (Join-Path $RepoRoot "dxcontroller-release\DeusEx.u") "$remoteRoot/System/DeusEx.u"

Write-Host "Done! Grant 'All files access' when OpenDE first launches."
& $adb shell am start -n "com.opende.deusex/.OpenDEActivity"
