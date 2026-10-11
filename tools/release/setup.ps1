param([Parameter(Mandatory=$true)][string]$Rom,
 [Parameter(Mandatory=$true)][string]$InstallDirectory,
 [string]$DataDirectory=(Join-Path $env:LOCALAPPDATA 'Rush2049\PlayerData'),
 [string]$DependencyDonor='', [string]$WorkspaceDirectory='', [string]$Generator='Visual Studio 18 2026')
$ErrorActionPreference='Stop'
foreach($tool in @('python','git','cmake')) {if(!(Get-Command $tool -ErrorAction SilentlyContinue)){throw "Required build tool missing: $tool"}}
if(Test-Path -LiteralPath $InstallDirectory) {throw 'Choose a new installation directory'}
if(!$WorkspaceDirectory) {
 $WorkspaceDirectory=Join-Path (Split-Path -Parent ([IO.Path]::GetFullPath($InstallDirectory))) ('.rush-build-'+[Guid]::NewGuid().ToString('N').Substring(0,8))
}
$work=[IO.Path]::GetFullPath($WorkspaceDirectory)
if(Test-Path -LiteralPath $work){throw 'Build workspace must be new'}
function Invoke-Checked([string]$Executable,[string[]]$Arguments) {
 & $Executable @Arguments
 if($LASTEXITCODE -ne 0) {throw "Setup stopped: $Executable returned $LASTEXITCODE. Private workspace: $work"}
}
# Validate ROM identity before downloading dependencies or creating a workspace.
Invoke-Checked 'python' @((Join-Path $PSScriptRoot 'prepare.py'),'--validate-rom',$Rom)
if(!$DependencyDonor) {
 $DependencyDonor=Join-Path $work 'dependencies'
 Invoke-Checked 'python' @((Join-Path $PSScriptRoot 'dependencies.py'),$DependencyDonor)
}
$build=Join-Path $work 'build-work'
Invoke-Checked 'python' @((Join-Path $PSScriptRoot 'prepare.py'),$build,'--rom',$Rom,'--dependency-donor',$DependencyDonor)
$native=Join-Path $build 'native'
Invoke-Checked 'cmake' @('-S',(Join-Path $native 'source'),'-B',(Join-Path $native 'build'),'-G',$Generator,'-A','x64')
Invoke-Checked 'cmake' @('--build',(Join-Path $native 'build'),'--config','Release','--target','RushWindowsRT64','--parallel','2')
if(!(Test-Path -LiteralPath $DataDirectory)) {
 Invoke-Checked 'python' @((Join-Path $PSScriptRoot 'install.py'),'data',$DataDirectory)
}
Invoke-Checked 'python' @((Join-Path $PSScriptRoot 'install.py'),'install',$native,$InstallDirectory,'--data-dir',$DataDirectory)
Write-Output "Ready: $InstallDirectory\play.ps1. Private build retained at $work. Saves: $DataDirectory"
