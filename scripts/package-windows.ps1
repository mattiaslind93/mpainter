param([Parameter(Mandatory=$true)][string]$Runtime, [Parameter(Mandatory=$true)][string]$Qt, [string]$Output="dist")
$ErrorActionPreference = "Stop"
$Root = Split-Path $PSScriptRoot -Parent
$Stage = Join-Path $Output "mPainter-0.9.0-alpha-windows-x64"
New-Item -ItemType Directory -Force $Stage | Out-Null
Copy-Item "$Runtime/mPainter.exe" $Stage
Copy-Item "$Runtime/5.2" $Stage -Recurse
Remove-Item "$Stage/5.2/datafiles/assets" -Recurse -Force -ErrorAction SilentlyContinue
Remove-Item "$Stage/5.2/scripts/addons_core/cycles/lib" -Recurse -Force -ErrorAction SilentlyContinue
Remove-Item "$Stage/5.2/scripts/startup/bl_app_templates_system" -Recurse -Force -ErrorAction SilentlyContinue
Copy-Item "$Runtime/*.dll" $Stage -ErrorAction SilentlyContinue
if (Test-Path "$Runtime/lib") { Copy-Item "$Runtime/lib" $Stage -Recurse }
python "$Root/scripts/sanitize-runtime.py" "$Stage/5.2"
if ($LASTEXITCODE -ne 0) { throw "Runtime normalization failed" }
Copy-Item "$Root/assets/branding" $Stage -Recurse
New-Item -ItemType Directory -Force "$Stage/assets" | Out-Null
Copy-Item "$Root/assets/meshes" "$Stage/assets" -Recurse
Copy-Item "$Root/LICENSE", "$Root/NOTICE.md", "$Root/README.md" $Stage
& "$Qt/bin/windeployqt.exe" --release --no-translations --compiler-runtime "$Stage/mPainter.exe"
if ($LASTEXITCODE -ne 0) { throw "Qt deployment failed" }
New-Item -ItemType Directory -Force "$Stage/licenses" | Out-Null
Get-ChildItem $Runtime -File | Where-Object { $_.Name -match "license|copyright" } | Copy-Item -Destination "$Stage/licenses"
if (Test-Path "$Qt/licenses") { Copy-Item "$Qt/licenses" "$Stage/licenses/Qt" -Recurse }
if (Test-Path "$Runtime/license") { Copy-Item "$Runtime/license" "$Stage/licenses/Blender" -Recurse }
if (Test-Path "$Runtime/licenses") { Copy-Item "$Runtime/licenses" "$Stage/licenses/Blender" -Recurse }
if (Test-Path "$Runtime/dependencies.md") { Copy-Item "$Runtime/dependencies.md" "$Stage/licenses" }
if ($env:VCPKG_INSTALLATION_ROOT) {
  Get-ChildItem "$env:VCPKG_INSTALLATION_ROOT/installed/x64-windows/share" -Filter copyright -Recurse | ForEach-Object {
    $LicenseDir = Join-Path "$Stage/licenses/vcpkg" $_.Directory.Name
    New-Item -ItemType Directory -Force $LicenseDir | Out-Null
    Copy-Item $_.FullName $LicenseDir
  }
}
Compress-Archive -Path "$Stage/*" -DestinationPath "$Output/mPainter-0.9.0-alpha-windows-x64.zip" -Force
