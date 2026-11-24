<#
.SYNOPSIS
  Promove a branch dev para a main SEM levar os arquivos do Claude.

.DESCRIPTION
  CLAUDE.md e a pasta .claude/ existem somente na dev. Este script faz o merge
  dev -> main, remove esses arquivos do resultado e cria o commit de merge.
  Opcionalmente cria a tag da versao.

.EXAMPLE
  ./scripts/promote-to-main.ps1 -Version 0.2.0
#>
param(
    [string]$Version,
    [switch]$Push
)

$ErrorActionPreference = 'Stop'
$ClaudePaths = @('CLAUDE.md', '.claude')

function Invoke-Git {
    git @args
    if ($LASTEXITCODE -ne 0) { throw "git $($args -join ' ') falhou" }
}

if (git status --porcelain) { throw 'Working tree com alteracoes. Commit ou stash antes.' }

Invoke-Git fetch origin
Invoke-Git checkout main
Invoke-Git pull --ff-only origin main

git merge --no-ff --no-commit origin/dev
if ($LASTEXITCODE -ne 0) {
    Write-Warning 'Conflitos no merge. Resolva, remova os arquivos do Claude e faca o commit manualmente.'
    exit 1
}

foreach ($p in $ClaudePaths) {
    if (Test-Path $p) { Invoke-Git rm -r -q --cached --ignore-unmatch -- $p; Remove-Item -Recurse -Force $p }
}

$msg = if ($Version) { "chore(release): v$Version" } else { 'chore: merge dev into main' }
Invoke-Git commit -m $msg

if ($Version) { Invoke-Git tag -a "v$Version" -m "v$Version" }

if ($Push) {
    Invoke-Git push origin main
    if ($Version) { Invoke-Git push origin "v$Version" }
}

Invoke-Git checkout dev
Write-Host 'main atualizada (sem arquivos do Claude). Voltou para a dev.'
