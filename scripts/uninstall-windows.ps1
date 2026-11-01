$ErrorActionPreference = 'Stop'

if ([string]::IsNullOrWhiteSpace($env:LOCALAPPDATA) -or [string]::IsNullOrWhiteSpace($env:APPDATA)) {
    throw 'Windows user folders could not be found. Sign in to your Windows account and try again.'
}

$expectedRoot = [System.IO.Path]::GetFullPath((Join-Path $env:LOCALAPPDATA 'Programs'))
$installDir = [System.IO.Path]::GetFullPath((Join-Path $expectedRoot 'WeatherScope'))
$startMenuDir = [System.IO.Path]::GetFullPath((Join-Path $env:APPDATA 'Microsoft\Windows\Start Menu\Programs\WeatherScope'))
$desktopLink = Join-Path ([Environment]::GetFolderPath([Environment+SpecialFolder]::DesktopDirectory)) 'WeatherScope.lnk'

if (-not $installDir.StartsWith($expectedRoot + [System.IO.Path]::DirectorySeparatorChar, [System.StringComparison]::OrdinalIgnoreCase)) {
    throw 'The WeatherScope install folder is outside the expected user Programs folder.'
}

$confirmation = Read-Host 'Remove WeatherScope and its Start menu/Desktop shortcuts? Type Y to continue'
if ($confirmation -notin @('Y', 'y')) {
    Write-Host 'Uninstall cancelled.'
    exit 0
}

if (Test-Path -LiteralPath $installDir) { Remove-Item -LiteralPath $installDir -Recurse -Force }
if (Test-Path -LiteralPath $startMenuDir) { Remove-Item -LiteralPath $startMenuDir -Recurse -Force }
if (Test-Path -LiteralPath $desktopLink) { Remove-Item -LiteralPath $desktopLink -Force }

Write-Host 'WeatherScope was removed. Browser profile data under LocalAppData\WeatherScope was kept.'

