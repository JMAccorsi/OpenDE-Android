param(
    [string]$Device = "MC94528AYF122800013",
    [string]$Cmd = "state",
    [switch]$ResolveCrash
)

$ExtDir = "/sdcard/Android/data/com.opende.deusex/files"

if ($ResolveCrash) {
    $libMain = Get-ChildItem "$PSScriptRoot\app\build\intermediates\cmake" -Filter "libmain.so" -Recurse |
        Where-Object { $_.FullName -match "obj\\arm64-v8a" } |
        Select-Object -First 1
    $addr2line = Get-ChildItem "$env:LOCALAPPDATA\Android\Sdk\ndk" -Filter "llvm-addr2line.exe" -Recurse |
        Select-Object -First 1
    if (-not $libMain -or -not $addr2line) {
        Write-Error "libmain.so (unstripped) ou llvm-addr2line.exe nao encontrado."
        exit 1
    }
    $lines = adb -s $Device logcat -d -s DEBUG | Select-String "libmain.so"
    foreach ($m in $lines) {
        if ($m.Line -match "pc ([0-9a-fA-F]+)\s+.*libmain\.so") {
            $pc = "0x" + $Matches[1]
            $sym = & $addr2line.FullName -e $libMain.FullName -f -C -i $pc
            Write-Host "$pc -> $($sym -join ' | ')"
        }
    }
    exit 0
}

$tmp = [System.IO.Path]::GetTempFileName()
[System.IO.File]::WriteAllText($tmp, "$Cmd`n", [System.Text.UTF8Encoding]::new($false))
adb -s $Device push $tmp "$ExtDir/debug_cmd.txt" | Out-Null
Remove-Item $tmp -Force
Start-Sleep -Milliseconds 350
adb -s $Device shell cat "$ExtDir/debug_state.json"
