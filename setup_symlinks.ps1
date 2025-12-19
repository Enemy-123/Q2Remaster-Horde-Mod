# Setup symlinks from deploy folder to game directory
# Run as Administrator OR with Developer Mode enabled

$repoDir = $PSScriptRoot
$gameDir = "C:\Program Files (x86)\Steam\steamapps\common\Quake 2\rerelease\baseq2"

# Folders to symlink
$folders = @("ents", "bots", "config")

# Check if game directory exists
if (-not (Test-Path $gameDir)) {
    Write-Host "ERROR: Game directory not found: $gameDir" -ForegroundColor Red
    exit 1
}

Write-Host "Creating symlinks from deploy to game directory..." -ForegroundColor Cyan
Write-Host "Source: $repoDir\deploy" -ForegroundColor Gray
Write-Host "Target: $gameDir" -ForegroundColor Gray
Write-Host ""

foreach ($folder in $folders) {
    $source = Join-Path $repoDir "deploy\$folder"
    $target = Join-Path $gameDir $folder

    # Check if source exists
    if (-not (Test-Path $source)) {
        Write-Host "SKIP: Source not found: $source" -ForegroundColor Yellow
        continue
    }

    # Remove existing target (file, folder, or symlink)
    if (Test-Path $target) {
        $item = Get-Item $target -Force
        if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) {
            Write-Host "Removing existing symlink: $target" -ForegroundColor Gray
            $item.Delete()
        } else {
            Write-Host "Removing existing folder: $target" -ForegroundColor Yellow
            Remove-Item $target -Recurse -Force
        }
    }

    # Create symlink
    try {
        New-Item -ItemType SymbolicLink -Path $target -Target $source -ErrorAction Stop | Out-Null
        Write-Host "OK: $folder -> $source" -ForegroundColor Green
    } catch {
        Write-Host "FAILED: $folder - $_" -ForegroundColor Red
        Write-Host "  Try running as Administrator or enable Developer Mode" -ForegroundColor Yellow
    }
}

# Also symlink horde_config.json
$configSource = Join-Path $repoDir "deploy\horde_config.json"
$configTarget = Join-Path $gameDir "horde_config.json"

if (Test-Path $configSource) {
    if (Test-Path $configTarget) {
        Remove-Item $configTarget -Force
    }
    try {
        New-Item -ItemType SymbolicLink -Path $configTarget -Target $configSource -ErrorAction Stop | Out-Null
        Write-Host "OK: horde_config.json -> $configSource" -ForegroundColor Green
    } catch {
        Write-Host "FAILED: horde_config.json - $_" -ForegroundColor Red
    }
}

Write-Host ""
Write-Host "Done!" -ForegroundColor Cyan
