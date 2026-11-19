# Packs build\bin into Solrino-Setup.exe, a single self-contained installer.
# Uses only the C# compiler that ships with Windows, so it needs no extra tools.
param(
    [string]$ProjectRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path,
    [string]$Notes = ''
)

$ErrorActionPreference = 'Stop'
$bin = Join-Path $ProjectRoot 'build\bin'
$output = Join-Path $ProjectRoot 'build\Solrino-Setup.exe'
$work = Join-Path $ProjectRoot 'build\installer-work'
$csc = Join-Path $env:WINDIR 'Microsoft.NET\Framework64\v4.0.30319\csc.exe'

if (-not (Test-Path (Join-Path $bin 'Solrino.exe'))) { throw "Build Solrino first: $bin\Solrino.exe is missing." }
if (-not (Test-Path $csc)) { throw "The C# compiler wasn't found at $csc" }
if (Test-Path $work) { Remove-Item $work -Recurse -Force -Confirm:$false }
New-Item -ItemType Directory -Path $work | Out-Null

# A test copy could leave these behind. They'd change how a friend's copy runs.
foreach ($name in 'modes', 'portable', 'Settings', 'Logs', 'Cache', 'Crashes', 'Misc') {
    if (Test-Path (Join-Path $bin $name)) { throw "$bin\$name exists. Remove it before packaging." }
}

# Only the app itself: leftover test builds in the folder stay out of the setup
$items = Get-ChildItem $bin | Where-Object { $_.PSIsContainer -or $_.Extension -ne '.exe' -or $_.Name -eq 'Solrino.exe' }
$items = $items | Where-Object { $_.Extension -ne '.pdb' }
$zip = Join-Path $work 'app.zip'
Compress-Archive -Path $items.FullName -DestinationPath $zip -CompressionLevel Optimal

if (Test-Path $output) { Remove-Item $output -Force -Confirm:$false }
$arguments = @(
    '/nologo', '/optimize+', '/target:winexe', '/platform:anycpu',
    "/out:$output",
    "/win32icon:$(Join-Path $ProjectRoot 'resources\icon.ico')",
    "/win32manifest:$(Join-Path $PSScriptRoot 'setup.manifest')",
    "/resource:$zip,app.zip",
    "/resource:$(Join-Path $ProjectRoot 'resources\icon.png'),logo.png",
    "/resource:$(Join-Path $PSScriptRoot 'uninstall.ps1'),uninstall.ps1",
    '/r:System.dll', '/r:System.Core.dll', '/r:System.Drawing.dll', '/r:System.Windows.Forms.dll',
    '/r:System.IO.Compression.dll', '/r:Microsoft.CSharp.dll',
    (Join-Path $PSScriptRoot 'SolrinoSetup.cs')
)
$result = & $csc @arguments 2>&1
if ($LASTEXITCODE -ne 0 -or -not (Test-Path $output)) {
    $result | ForEach-Object { "$_" } | Select-Object -First 30
    throw 'Compiling the installer failed.'
}
Remove-Item $work -Recurse -Force -Confirm:$false
"Created $output ($([int]((Get-Item $output).Length / 1MB)) MB)"

# What the app's updater reads. See RELEASING.md.
$versionFile = Get-Content (Join-Path $ProjectRoot 'src\common\SolrinoVersion.hpp') -Raw
if ($versionFile -notmatch '#define SOLRINO_VERSION "([0-9.]+)"') { throw 'SOLRINO_VERSION was not found.' }
$manifest = [ordered]@{ version = $Matches[1]; sha256 = (Get-FileHash $output -Algorithm SHA256).Hash.ToLower(); notes = $Notes }
$manifestPath = Join-Path $ProjectRoot 'build\solrino-update.json'
[IO.File]::WriteAllText($manifestPath, ($manifest | ConvertTo-Json))
"Created $manifestPath for version $($Matches[1])"
