# Removes Solrino's app files, shortcuts and Installed apps entry.
# Your settings in %APPDATA%\Solrino are kept, so a reinstall picks them up.
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Windows.Forms
$folder = $PSScriptRoot

if (-not (Test-Path (Join-Path $folder 'Solrino.exe'))) {
    [void][System.Windows.Forms.MessageBox]::Show('Solrino was not found in this folder.', 'Solrino')
    exit 1
}

$answer = [System.Windows.Forms.MessageBox]::Show(
    "Remove Solrino from this computer?`n`nYour settings stay in %APPDATA%\Solrino.",
    'Solrino', 'YesNo', 'Question')
if ($answer -ne 'Yes') { exit 0 }

if (Get-Process Solrino -ErrorAction SilentlyContinue | Where-Object { $_.Path -like "$folder\*" }) {
    [void][System.Windows.Forms.MessageBox]::Show('Quit Solrino from its tray icon first, then try again.', 'Solrino')
    exit 1
}

foreach ($place in @([Environment]::GetFolderPath('Desktop'), [Environment]::GetFolderPath('Programs'))) {
    Remove-Item (Join-Path $place 'Solrino.lnk') -Force -ErrorAction SilentlyContinue
}
Remove-Item 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\Solrino' -Recurse -Force -ErrorAction SilentlyContinue

# This script is inside the folder, so a separate process deletes it after exit
Start-Process cmd.exe -WindowStyle Hidden -ArgumentList "/c timeout /t 2 /nobreak >nul & rmdir /s /q `"$folder`""
