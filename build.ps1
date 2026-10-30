# Builds the SKSE plugin and deploys DLL + ESP into the local Skyrim SE/AE install.
# Usage: .\build.ps1            (after git pull)
# Needs: VS 2022 (C++ workload), vcpkg in $env:VCPKG_ROOT (default ..\deps\vcpkg), submodules checked out.
# no $ErrorActionPreference='Stop': PowerShell 5.1 would abort on CMake's stderr messages; exit codes are checked instead
Set-Location $PSScriptRoot

if (-not $env:VCPKG_ROOT) { $env:VCPKG_ROOT = Resolve-Path "$PSScriptRoot\..\deps\vcpkg" }
if (-not $env:SKYRIM_DATA_DIR) {
	$game = (Get-ItemProperty 'HKLM:\SOFTWARE\WOW6432Node\Bethesda Softworks\Skyrim Special Edition').'installed path'
	$env:SKYRIM_DATA_DIR = Join-Path $game 'Data'
}

# MSVC + Ninja + CMake from the VS developer shell
if (-not (Get-Command cl.exe -ErrorAction SilentlyContinue)) {
	$vs = & "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" -latest -property installationPath
	Import-Module "$vs\Common7\Tools\Microsoft.VisualStudio.DevShell.dll"
	Enter-VsDevShell -VsInstallPath $vs -SkipAutomaticLocation -DevCmdArguments '-arch=x64 -host_arch=x64' | Out-Null
}

# CMake/Ninja: use installed ones, else vcpkg's portable copies (VS without the CMake component)
foreach ($tool in 'cmake', 'ninja') {
	if (-not (Get-Command $tool -ErrorAction SilentlyContinue)) {
		$exe = & "$env:VCPKG_ROOT\vcpkg.exe" fetch $tool 2>$null | Select-Object -Last 1
		$env:PATH = "$(Split-Path $exe);$env:PATH"
	}
}

git submodule update --init --recursive
cmake --preset release
if ($LASTEXITCODE) { exit $LASTEXITCODE }
cmake --build --preset release
if ($LASTEXITCODE) { exit $LASTEXITCODE }

# activate the ESP once (the shout needs it, the fan control works without it)
$plugins = "$env:LOCALAPPDATA\Skyrim Special Edition\Plugins.txt"
$active = @(Get-Content $plugins | Where-Object { $_ -notmatch '^\*?ImmersiveWindsVR\.esp$' }) + '*ImmersiveWindsVR.esp'
Set-Content -Path $plugins -Value $active

Write-Host "Deployed to $env:SKYRIM_DATA_DIR. Start the game with skse64_loader.exe."
