param([Parameter(Mandatory=$true)][string]$NativeRoot,
      [int]$Seconds=600,[string]$Stage='play',[string]$Pak='',[string]$InputConfig='')
$ErrorActionPreference='Stop'
if($Seconds -lt 10 -or $Seconds -gt 1800) {throw 'Session bound must be 10..1800 seconds'}
$nativeRoot=[IO.Path]::GetFullPath($NativeRoot)
$source=Join-Path $nativeRoot 'source'
$exe=Join-Path $nativeRoot 'build\Release\RushWindowsRT64.exe'
if(!(Test-Path -LiteralPath $exe)) {throw 'Native executable missing'}
if(Get-Process -ErrorAction SilentlyContinue | Where-Object {$_.ProcessName -match '^Rush'}) {throw 'Another Rush instance is active'}
$runRoot=Join-Path $nativeRoot ('runs\'+[Guid]::NewGuid().ToString())
New-Item -ItemType Directory -Path (Join-Path $runRoot 'profile') | Out-Null
foreach($name in @('RUSH_WINDOWS_AUDIO','RUSH_CONTROLLER_PAK','RUSH_DIAGNOSTIC_INPUT','VK_DRIVER_FILES','VK_ICD_FILENAMES','RUSH_PCM_CAPTURE','RUSH_AUDIO_RING_TRACE')) {
 Remove-Item -LiteralPath ('Env:\'+$name) -ErrorAction SilentlyContinue
}
. (Join-Path $nativeRoot 'input_settings.ps1')
$env:RUSH_INPUT_CONFIG=Initialize-RushInputSettings -NativeRoot $nativeRoot -RunRoot $runRoot -InputConfig $InputConfig
$inputHashBefore=(Get-FileHash -LiteralPath $env:RUSH_INPUT_CONFIG).Hash
$env:RUSH_INPUT_DIAGNOSTIC='raw'
$env:RUSH_RUN_SECONDS="$Seconds"
$env:RUSH_NATIVE_INPUT=Join-Path $runRoot 'input.txt'
$env:RUSH_NATIVE_PRESENT_TRACE=Join-Path $runRoot 'present.csv'
Set-Content -LiteralPath $env:RUSH_NATIVE_INPUT -Value '0 0 0 0 0'
if($Pak) {
 $testPak=Join-Path $runRoot 'synthetic-test.mpk'
 Copy-Item -LiteralPath $Pak -Destination $testPak
 $env:RUSH_CONTROLLER_PAK=$testPak
}
$graphicsEnvironment=@{}
foreach($key in @('VK_DRIVER_FILES','VK_ICD_FILENAMES','SDL_VIDEODRIVER','SDL_AUDIODRIVER')) {
 $graphicsEnvironment[$key]=[Environment]::GetEnvironmentVariable($key)
}
$graphicsEnvironment | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $runRoot 'graphics-environment.json')
$started=Get-Date
$hash=(Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash.ToLowerInvariant()
$game=Start-Process -FilePath $exe -ArgumentList @((Join-Path $source 'test-rom.z64'),(Join-Path $runRoot 'profile'),$Stage) -WorkingDirectory $source -RedirectStandardOutput (Join-Path $runRoot 'stdout.log') -RedirectStandardError (Join-Path $runRoot 'stderr.log') -WindowStyle Hidden -PassThru
$retainedHandle=$game.Handle
Write-Output "RUN_DIR=$runRoot PID=$($game.Id)"
$timeout=$false
try {
 if(!$game.WaitForExit(($Seconds+45)*1000)) {$timeout=$true;$game.Kill();$game.WaitForExit()}
} finally {
 if(!$game.HasExited) {$game.Kill();$game.WaitForExit()}
}
$game.Refresh()
@{stage=$Stage;pid=$game.Id;exit_code=$game.ExitCode;outer_timeout=$timeout;
duration_seconds=((Get-Date)-$started).TotalSeconds;exe_sha256=$hash;
run_directory=$runRoot;input_config=$env:RUSH_INPUT_CONFIG;input_before=$inputHashBefore;
input_after=(Get-FileHash -LiteralPath $env:RUSH_INPUT_CONFIG).Hash;utc=[DateTime]::UtcNow.ToString('o');
pak_before=if($Pak){(Get-FileHash -LiteralPath $Pak).Hash}else{$null};
pak_after=if($Pak){(Get-FileHash -LiteralPath $testPak).Hash}else{$null}} |
 ConvertTo-Json | Set-Content -LiteralPath (Join-Path $runRoot 'receipt.json')
Get-Content -LiteralPath (Join-Path $runRoot 'receipt.json')
if($timeout){exit 124}
exit $game.ExitCode
