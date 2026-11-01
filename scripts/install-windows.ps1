$ErrorActionPreference = 'Stop'

$repoRoot = Split-Path -Parent $PSScriptRoot
$packageDir = if (Test-Path -LiteralPath (Join-Path $PSScriptRoot 'WeatherScope.exe')) {
    $PSScriptRoot
} else {
    Join-Path $repoRoot 'dist\WeatherScope'
}
$appExe = Join-Path $packageDir 'WeatherScope.exe'

if (-not (Test-Path -LiteralPath $appExe)) {
    throw 'The desktop app is not built yet. Run scripts\build-desktop.ps1 first.'
}
if ([string]::IsNullOrWhiteSpace($env:LOCALAPPDATA) -or [string]::IsNullOrWhiteSpace($env:APPDATA)) {
    throw 'Windows user folders could not be found. Sign in to your Windows account and try again.'
}

$installDir = Join-Path $env:LOCALAPPDATA 'Programs\WeatherScope'
$startMenuDir = Join-Path $env:APPDATA 'Microsoft\Windows\Start Menu\Programs\WeatherScope'
$desktopDir = [Environment]::GetFolderPath([Environment+SpecialFolder]::DesktopDirectory)

New-Item -ItemType Directory -Force -Path $installDir, $startMenuDir | Out-Null
Copy-Item -Path (Join-Path $packageDir '*') -Destination $installDir -Recurse -Force

$installedExe = Join-Path $installDir 'WeatherScope.exe'
$shell = New-Object -ComObject WScript.Shell
foreach ($shortcutPath in @(
    (Join-Path $startMenuDir 'WeatherScope.lnk'),
    (Join-Path $desktopDir 'WeatherScope.lnk')
)) {
    $shortcut = $shell.CreateShortcut($shortcutPath)
    $shortcut.TargetPath = $installedExe
    $shortcut.WorkingDirectory = $installDir
    $shortcut.Description = 'Open the WeatherScope weather app'
    $shortcut.IconLocation = "$installedExe,0"
    $shortcut.Save()
}

Write-Host 'WeatherScope is installed. Open it from the Start menu or Desktop shortcut.'

