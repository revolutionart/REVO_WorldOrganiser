#Requires -Version 5.1
<#
.SYNOPSIS
    Packages the REVO_WorldOrganiser plugin for distribution.

.DESCRIPTION
    Collects only the files needed to run the plugin with pre-compiled binaries:
      - *.uplugin
      - Source/
      - Binaries/  (live-coding patch files excluded)
      - Resources/

    Excluded: Intermediate/, docs (*.md), .gitignore, .git/, .github/,
              .vscode/, *.code-workspace, Releases/, live-coding patches.

.PARAMETER OutputDir
    Destination folder for the resulting zip. Defaults to .\Releases next to this script.
#>
param(
    [string]$OutputDir = (Join-Path $PSScriptRoot 'Releases')
)

$ErrorActionPreference = 'Stop'

# ---------------------------------------------------------------------------
# Resolve plugin identity from the .uplugin file
# ---------------------------------------------------------------------------
$UpluginFile = Get-ChildItem -Path $PSScriptRoot -Filter '*.uplugin' -File |
Select-Object -First 1

if (-not $UpluginFile) {
    Write-Error "No .uplugin file found in '$PSScriptRoot'."
    exit 1
}

$PluginMeta = Get-Content $UpluginFile.FullName -Raw | ConvertFrom-Json
$PluginName = $UpluginFile.BaseName          # e.g. REVOWorldOrganiser (used for zip filename)
$FolderName = 'REVO_WorldOrganiser'          # folder name inside the zip
$Version = $PluginMeta.VersionName        # e.g. 1.0.1
$ZipFileName = "${PluginName}_v${Version}.zip"
$ZipPath = Join-Path $OutputDir $ZipFileName

# ---------------------------------------------------------------------------
# Define what to include / exclude
# ---------------------------------------------------------------------------
$IncludedDirs = @('Source', 'Binaries', 'Resources')

# Files whose names match any of these patterns are skipped
$ExcludedNamePatterns = @(
    '*.patch_*'        # Live-coding patches  (*.patch_0.dll / .exe / .pdb / .lib / .exp)
    '*.code-workspace' # VS Code workspace files
)

# Top-level directory names to skip entirely
$ExcludedDirNames = @(
    'Intermediate'
    'Releases'
    '.git'
    '.github'
    '.vscode'
)

# ---------------------------------------------------------------------------
# Collect files
# ---------------------------------------------------------------------------
$AllFiles = [System.Collections.Generic.List[System.IO.FileInfo]]::new()

# Root-level: only the .uplugin
$AllFiles.Add($UpluginFile)

# Subdirectories listed in $IncludedDirs
foreach ($DirName in $IncludedDirs) {
    $DirPath = Join-Path $PSScriptRoot $DirName
    if (-not (Test-Path $DirPath -PathType Container)) { continue }

    Get-ChildItem -Path $DirPath -File -Recurse | Where-Object {
        $f = $_
        $excluded = $false
        foreach ($Pattern in $ExcludedNamePatterns) {
            if ($f.Name -like $Pattern) { $excluded = $true; break }
        }
        -not $excluded
    } | ForEach-Object { $AllFiles.Add($_) }
}

if ($AllFiles.Count -eq 0) {
    Write-Error 'No files found to package.'
    exit 1
}

# ---------------------------------------------------------------------------
# Create output directory and zip
# ---------------------------------------------------------------------------
if (-not (Test-Path $OutputDir -PathType Container)) {
    New-Item -ItemType Directory -Path $OutputDir | Out-Null
}

if (Test-Path $ZipPath) {
    Remove-Item $ZipPath -Force
}

Write-Host ''
Write-Host "Plugin : $PluginName  v$Version"
Write-Host "Output : $ZipPath"
Write-Host "Files  : $($AllFiles.Count)"
Write-Host ''

Add-Type -AssemblyName System.IO.Compression.FileSystem

$Zip = [System.IO.Compression.ZipFile]::Open($ZipPath, 'Create')
try {
    foreach ($File in $AllFiles) {
        # Build a relative path from the plugin root, then prefix with the plugin folder name
        # so the zip extracts as  REVO_WorldOrganiser/<subpath>
        $Relative = $File.FullName.Substring($PSScriptRoot.Length).TrimStart('\', '/')
        $EntryName = ($FolderName + '/' + $Relative) -replace '\\', '/'

        [System.IO.Compression.ZipFileExtensions]::CreateEntryFromFile(
            $Zip, $File.FullName, $EntryName,
            [System.IO.Compression.CompressionLevel]::Optimal
        ) | Out-Null

        Write-Verbose "  + $EntryName"
    }
}
finally {
    $Zip.Dispose()
}

Write-Host "Done. Package ready:"
Write-Host "  $ZipPath"
Write-Host ''
