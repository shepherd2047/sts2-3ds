# One-shot setup of this project on a Windows PC.
#
# Run in PowerShell (not as admin):
#   powershell -ExecutionPolicy Bypass -File setup_windows.ps1
# The repo is private: download this one file from GitHub in the browser
# (open it on github.com -> "Download raw file"), then run it from Downloads.
#
# Safe to run again: every step skips what is already done.

# 'Continue': Windows PowerShell 5 turns any stderr output of git/gh into a
# terminating error under 'Stop'. Failures are checked explicitly instead.
$ErrorActionPreference = 'Continue'
$Root = 'C:\dev'
$Repo = Join-Path $Root 'sts2-3ds'

function Step($msg) { Write-Host "`n== $msg" -ForegroundColor Cyan }
function RefreshPath {
  $env:Path = [Environment]::GetEnvironmentVariable('Path', 'Machine') + ';' +
              [Environment]::GetEnvironmentVariable('Path', 'User') + ';' +
              (Join-Path $env:USERPROFILE '.dotnet\tools')
}
function Have($cmd) {
  if (-not (Get-Command $cmd -ErrorAction SilentlyContinue)) { return $false }
  # The Microsoft Store "python" stub exists on fresh Windows but is not Python.
  if ($cmd -eq 'python') { python -c "import sys" 2>$null; return $LASTEXITCODE -eq 0 }
  return $true
}

Step 'Tools (winget): Git, GitHub CLI, Python, .NET SDK'
$pkgs = @(
  @{ id = 'Git.Git';                cmd = 'git'    },
  @{ id = 'GitHub.cli';             cmd = 'gh'     },
  @{ id = 'Python.Python.3.12';     cmd = 'python' },
  @{ id = 'Microsoft.DotNet.SDK.8'; cmd = 'dotnet' }
)
foreach ($p in $pkgs) {
  if (Have $p.cmd) { Write-Host "  $($p.cmd) already installed" ; continue }
  winget install --id $p.id -e --silent --accept-package-agreements --accept-source-agreements
}
RefreshPath

Step 'GitHub login'
gh auth status *> $null
if ($LASTEXITCODE -ne 0) { gh auth login --hostname github.com --git-protocol https --web }
gh auth setup-git
if (-not (git config --global user.name)) {
  git config --global user.name 'shepherd2047'
  git config --global user.email '165551441+shepherd2047@users.noreply.github.com'
}

Step "Clone to $Repo"
New-Item -ItemType Directory -Force -Path $Root | Out-Null
if (Test-Path (Join-Path $Repo '.git')) { git -C $Repo pull }
else { gh repo clone shepherd2047/sts2-3ds $Repo }

Step 'Python packages'
python -m pip install --user --upgrade pillow numpy

Step 'ILSpy command line (decompiler)'
if (-not (Have 'ilspycmd')) { dotnet tool install -g ilspycmd }
RefreshPath

Step 'Game files (Steam: Slay the Spire 2 must be installed)'
Push-Location $Repo
python tools/gamepaths.py
if ($LASTEXITCODE -ne 0) {
  Write-Host '  Game not found. Install Slay the Spire 2 in Steam, or set STS2_DIR, then run this script again.' -ForegroundColor Yellow
  Pop-Location; exit 1
}
if (-not (Test-Path (Join-Path $Root 'sts2-decompiled'))) { python tools/decompile.py }
python tools/build_assets.py
Pop-Location

Step 'Manual steps left'
Write-Host @"
  1. devkitPro (3DS compiler): https://github.com/devkitPro/installer/releases
     run the installer, tick "3DS Development". Then build with the
     "MSys2" shortcut it adds:  cd /c/dev/sts2-3ds && make
  2. Azahar (3DS emulator): https://github.com/azahar-emu/azahar/releases
  3. Open C:\dev\sts2-3ds in Claude Code; it reads CLAUDE.md for the rest
     (desktop preview build on Windows is not set up yet).
"@
