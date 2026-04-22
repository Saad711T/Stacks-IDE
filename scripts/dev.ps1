# Windows development helper
$ErrorActionPreference = "Stop"

Set-Location $PSScriptRoot\..

Write-Host "Building stacksd…" -ForegroundColor Cyan
cmake -S core -B core\build
cmake --build core\build --config Release

$bin = "core\build\Release\stacksd.exe"
Write-Host "Starting daemon" -ForegroundColor Cyan
Start-Process -FilePath $bin -ArgumentList "$PWD\workspace" -WindowStyle Hidden

Write-Host "Serving web UI on http://localhost:8000" -ForegroundColor Cyan
Push-Location web
python -m http.server 8000
Pop-Location
