# Local installed game launcher. Player data is never copied back over existing saves.
param([switch]$ValidateOnly)
$ErrorActionPreference='Stop'
$root=$PSScriptRoot
$manifest=Get-Content -LiteralPath (Join-Path $root 'installation.json') -Raw | ConvertFrom-Json
foreach($entry in $manifest.files.PSObject.Properties) {
 $path=Join-Path $root $entry.Name
 if((Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant() -ne $entry.Value) {throw "Installation changed: $($entry.Name)"}
}
$data=(Get-Content -LiteralPath (Join-Path $root 'player-data.json') -Raw | ConvertFrom-Json).data_directory
$pak=Get-Item -LiteralPath (Join-Path $data 'controller.mpk')
$input=Get-Item -LiteralPath (Join-Path $data 'input.cfg')
if($pak.Length -ne 32768 -or $input.Length -gt 1048576){throw 'Player-data size differs'}
foreach($item in @($pak,$input,(Get-Item -LiteralPath $data))) {
 if($item.Attributes -band [IO.FileAttributes]::ReparsePoint){throw 'Linked player data is unsupported'}
}
if($ValidateOnly){Write-Output 'Installation hashes and player-data preflight verified; no game launched';exit 0}
$lock=$null
$mutex=[Threading.Mutex]::new($false,'Local\Rush2049-Native-Session')
$ownsMutex=$false
$pushed=$false
try {
 try {$ownsMutex=$mutex.WaitOne(0)} catch [Threading.AbandonedMutexException] {$ownsMutex=$true}
 if(!$ownsMutex){throw 'Another installed Rush session is active'}
 if(Get-Process -ErrorAction SilentlyContinue | Where-Object {$_.ProcessName -match '^Rush'}) {throw 'Another Rush instance is active'}
 $lock=[IO.File]::Open((Join-Path $data 'session.lock'),[IO.FileMode]::OpenOrCreate,[IO.FileAccess]::ReadWrite,[IO.FileShare]::None)
 foreach($name in @('RUSH_WINDOWS_AUDIO','RUSH_DIAGNOSTIC_INPUT','RUSH_NATIVE_INPUT','RUSH_NATIVE_PRESENT_TRACE','RUSH_INPUT_DIAGNOSTIC','RUSH_GFX_TASK_TRACE','RUSH_RUN_SECONDS','RUSH_PCM_CAPTURE','RUSH_AUDIO_RING_TRACE','VK_DRIVER_FILES','VK_ICD_FILENAMES')) {
  Remove-Item -LiteralPath ('Env:\'+$name) -ErrorAction SilentlyContinue
 }
 $env:RUSH_CONTROLLER_PAK=Join-Path $data 'controller.mpk'
 $env:RUSH_INPUT_CONFIG=Join-Path $data 'input.cfg'
 if(Test-Path -LiteralPath (Join-Path $root 'remaster-pack\rt64.json')) {
  $env:RUSH_REMASTER_TEXTURE_PACK=Join-Path $root 'remaster-pack'
  $env:RUSH_REMASTER_CIRCUIT_GLOW='1'
 }

 # Native argument passing preserves paths with spaces. No forced session limit.
 Push-Location -LiteralPath $root
 $pushed=$true
 & (Join-Path $root 'RushWindowsRT64.exe') (Join-Path $root 'game.z64') (Join-Path $data 'profile') 'play'
 $result=$LASTEXITCODE
} finally {if($pushed){Pop-Location};if($lock){$lock.Dispose()};if($ownsMutex){$mutex.ReleaseMutex()};$mutex.Dispose()}
exit $result
