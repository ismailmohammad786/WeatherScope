$ErrorActionPreference = 'Stop'

$repoRoot = Split-Path -Parent $PSScriptRoot
$backendDir = Join-Path $repoRoot 'backend'
$buildDir = Join-Path $backendDir 'build\desktop'
$packageDir = Join-Path $repoRoot 'dist\WeatherScope'
$cmake = 'C:\msys64\ucrt64\bin\cmake.exe'

if (-not (Test-Path -LiteralPath $cmake)) {
    throw "CMake was not found at $cmake. Install or repair CMake in MSYS2 UCRT64, then try again."
}
$env:PATH = "C:\msys64\ucrt64\bin;$env:PATH"

$configureArgs = @('-S', $backendDir, '-B', $buildDir, '-G', 'Ninja')
$dependencyRoot = Join-Path $backendDir 'build\_deps'
$dependencyFolders = @{
    'ASIO' = 'asio-src'
    'CROW' = 'crow-src'
    'NLOHMANN_JSON' = 'nlohmann_json-src'
}
foreach ($name in $dependencyFolders.Keys) {
    $sourceDir = Join-Path $dependencyRoot $dependencyFolders[$name]
    if (Test-Path -LiteralPath $sourceDir) {
        $configureArgs += "-DFETCHCONTENT_SOURCE_DIR_${name}=$($sourceDir.Replace('\','/'))"
    }
}

Write-Host 'Configuring WeatherScope for Windows…'
& $cmake @configureArgs
if ($LASTEXITCODE -ne 0) { throw 'WeatherScope could not be configured.' }

Write-Host 'Building the desktop app and weather service…'
& $cmake --build $buildDir --config Release
if ($LASTEXITCODE -ne 0) { throw 'WeatherScope could not be built.' }

New-Item -ItemType Directory -Force -Path $packageDir | Out-Null
Copy-Item -LiteralPath (Join-Path $buildDir 'WeatherScope.exe') -Destination $packageDir -Force
Copy-Item -LiteralPath (Join-Path $buildDir 'weather_backend.exe') -Destination $packageDir -Force
Copy-Item -LiteralPath (Join-Path $repoRoot 'README.md') -Destination $packageDir -Force
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'uninstall-windows.ps1') -Destination $packageDir -Force
$packageWebDir = Join-Path $packageDir 'web'
New-Item -ItemType Directory -Force -Path $packageWebDir | Out-Null
Copy-Item -Path (Join-Path $buildDir 'web\*') -Destination $packageWebDir -Recurse -Force
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'install-windows.ps1') -Destination $packageDir -Force
$archive = Join-Path $repoRoot 'dist\WeatherScope-Windows.zip'
Compress-Archive -Path (Join-Path $packageDir '*') -DestinationPath $archive -Force

Write-Host "Desktop package ready: $packageDir"
Write-Host "Portable app archive ready: $archive"
Write-Host 'Run install-windows.ps1 to add WeatherScope to your Start menu and Desktop.'

