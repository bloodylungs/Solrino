# Packs build\bin into Solrino-Setup.exe, a single self-contained installer.
# Uses only the C# compiler that ships with Windows, so it needs no extra tools.
param(
    [string]$ProjectRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path,
    [string]$Notes = '',
    # The release notes, one "- New ..." / "- Fixed ..." line per change. The
    # update card shows them in sections.
    [string]$NotesFile = ''
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
$version = $Matches[1]
$manifest = [ordered]@{ version = $version; sha256 = (Get-FileHash $output -Algorithm SHA256).Hash.ToLower(); notes = $Notes }

# Each change for the card's sections: its kind, a short title from the first
# sentence (without the "New"/"Fixed" word, which the section already says), and
# the next sentence as detail. Older copies ignore this and show the notes.
if ($NotesFile) {
    $changes = @()
    foreach ($line in Get-Content $NotesFile -Encoding UTF8) {
        if ($line -notmatch '^\s*[-*]\s+(.+)$') { continue }
        $text = $Matches[1].Trim()
        $kind = 'changed'
        if ($text -match '^(New|Added|Add)\b') { $kind = 'new' }
        elseif ($text -match '^(Fixed|Fix)\b') { $kind = 'fixed' }
        elseif ($text -match '^(Removed|Remove)\b') { $kind = 'removed' }
        $sentences = [regex]::Split($text, '(?<=[.!?])\s+')
        $title = $sentences[0] -replace '^(New|Added|Add|Fixed|Fix|Removed|Remove)\s+', '' -replace '[.!]$', ''
        if ($title.Length -gt 0) { $title = $title.Substring(0, 1).ToUpper() + $title.Substring(1) }
        $detail = if ($sentences.Count -gt 1) { $sentences[1] } else { '' }
        $changes += [ordered]@{ kind = $kind; title = $title; detail = $detail }
    }
    $manifest.changes = $changes
}

$manifestPath = Join-Path $ProjectRoot 'build\solrino-update.json'
[IO.File]::WriteAllText($manifestPath, ($manifest | ConvertTo-Json -Depth 4))
"Created $manifestPath for version $version"
