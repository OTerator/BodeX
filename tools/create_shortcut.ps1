# Create (or refresh) a BodeX shortcut on the Desktop pointing at the built exe.
# Run:  powershell -ExecutionPolicy Bypass -File tools/create_shortcut.ps1
$ErrorActionPreference = "Stop"

$repo    = Split-Path -Parent $PSScriptRoot           # repo root (parent of tools/)
$exe     = Join-Path $repo "build\BodeX.exe"
$workdir = Join-Path $repo "build"
$icon    = Join-Path $repo "resources\BodeX.ico"      # icon straight from the .ico

if (-not (Test-Path $exe)) {
    Write-Warning "build\BodeX.exe not found. Build it first: mingw32-make"
}

$desktop = [Environment]::GetFolderPath('Desktop')
$lnkPath = Join-Path $desktop "BodeX.lnk"

$ws  = New-Object -ComObject WScript.Shell
$lnk = $ws.CreateShortcut($lnkPath)
$lnk.TargetPath       = $exe
$lnk.WorkingDirectory = $workdir
# Point the icon at the .ico file (not the exe) so a rebuilt exe doesn't leave a
# stale cached icon on the shortcut.
if (Test-Path $icon) { $lnk.IconLocation = "$icon,0" } else { $lnk.IconLocation = "$exe,0" }
$lnk.Description       = "BodeX - grading tracker"
$lnk.WindowStyle       = 1
$lnk.Save()

# Nudge the shell to drop stale icon caches so the new icon shows immediately.
Add-Type @"
using System;
using System.Runtime.InteropServices;
public class BodexShell {
  [DllImport("shell32.dll")] public static extern void SHChangeNotify(int e, int f, IntPtr a, IntPtr b);
}
"@
[BodexShell]::SHChangeNotify(0x08000000, 0, [IntPtr]::Zero, [IntPtr]::Zero)  # SHCNE_ASSOCCHANGED
try { & ie4uinit.exe -show } catch {}

Write-Output "Shortcut created: $lnkPath"
Write-Output "  Target: $exe"
Write-Output "  Icon  : $($lnk.IconLocation)"
