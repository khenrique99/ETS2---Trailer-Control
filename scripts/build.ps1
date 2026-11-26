<#
.SYNOPSIS
    Compila plugin + app, roda os testes e gera o zip da release em dist\ (so os 3 arquivos da pasta plugins).
.DESCRIPTION
    Precisa do Visual Studio 2022 (ou Build Tools) com C++ e do SCS SDK em .\sdk.
    Uso:  powershell -ExecutionPolicy Bypass -File scripts\build.ps1 [-SkipTests] [-GameTag ETS2-1.62] [-TestPackage]
    -TestPackage tambem gera build\...-teste.zip (plugin + jogo falso), fora da release.
    GameTag vai no nome dos arquivos: a versao do jogo para a qual a release foi feita.
#>
param([switch]$SkipTests, [string]$GameTag = 'ETS2-1.62', [switch]$TestPackage)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root

# Localiza o CMake que vem com o Visual Studio
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$cmake = (Get-Command cmake -ErrorAction SilentlyContinue).Source
if (-not $cmake -and (Test-Path $vswhere)) {
    $vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    $cand = Join-Path $vs 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
    if (Test-Path $cand) { $cmake = $cand }
}
if (-not $cmake) { throw 'CMake nao encontrado. Instale o Visual Studio 2022 com "Desenvolvimento para desktop com C++".' }
$ctest = Join-Path (Split-Path $cmake) 'ctest.exe'

if (-not (Test-Path "$root\sdk\include\scssdk_telemetry.h")) {
    throw 'SCS SDK nao encontrado em .\sdk (veja o README).'
}

$vsEdition = & $vswhere -latest -products * -property installationPath
$generator = if ($vsEdition -match 'Microsoft Visual Studio\\18') { 'Visual Studio 18 2026' } else { 'Visual Studio 17 2022' }
& $cmake -S $root -B "$root\build" -G $generator -A x64 | Out-Host
if ($LASTEXITCODE) { throw 'Falha ao configurar' }
& $cmake --build "$root\build" --config Release | Out-Host
if ($LASTEXITCODE) { throw 'Falha ao compilar' }

if (-not $SkipTests) {
    & $ctest --test-dir "$root\build" -C Release --output-on-failure | Out-Host
    if ($LASTEXITCODE) { throw 'Testes falharam' }
}

# Versao a partir do CMakeLists (project(... VERSION x.y.z ...))
$version = ([regex]::Match((Get-Content "$root\CMakeLists.txt" -Raw), 'VERSION\s+(\d+\.\d+\.\d+)')).Groups[1].Value
$base = "TrailerControl-v$version-$GameTag"
$rel = "$root\build\Release"
$dist = Join-Path $root 'dist'
if (Test-Path $dist) { Remove-Item -Recurse -Force $dist }
New-Item -ItemType Directory -Force $dist | Out-Null

# O que vai para a release: so o zip com os 3 arquivos da pasta plugins do jogo
# (o app fica junto da DLL: o atalho no jogo abre ele de la).
$pluginFiles = @("$rel\trailer_control.dll", "$root\config\trailer_control.ini", "$rel\TrailerControl.exe")
Compress-Archive -Path $pluginFiles -DestinationPath (Join-Path $dist "$base-plugins.zip")

# Opcional, para desenvolvimento: plugin + jogo falso + guias (fora da release)
if ($TestPackage) {
    $stage = Join-Path $root "build\stage\$base-teste"
    if (Test-Path $stage) { Remove-Item -Recurse -Force $stage }
    New-Item -ItemType Directory -Force "$stage\plugin", "$stage\teste-sem-jogo" | Out-Null
    Copy-Item $pluginFiles "$stage\plugin\"
    Copy-Item "$rel\fake_game.exe", "$rel\core_ets2mp.dll" "$stage\teste-sem-jogo\"
    Copy-Item "$root\docs\TESTE-NO-JOGO.md" "$stage\LEIA-ME.md"
    Copy-Item "$root\docs\TESTING-IN-GAME.md" "$stage\README-EN.md"
    Compress-Archive -Path "$stage\*" -DestinationPath (Join-Path $root "build\$base-teste.zip") -Force
    Write-Host "Pacote de teste: build\$base-teste.zip"
}

Write-Host "Release em $dist :"
Get-ChildItem $dist -File | ForEach-Object { Write-Host ("  {0,-48} {1,10:N0} bytes" -f $_.Name, $_.Length) }
