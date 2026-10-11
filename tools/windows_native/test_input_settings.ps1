$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'input_settings.ps1')
$root=Join-Path ([IO.Path]::GetTempPath()) ('rush-settings-'+[guid]::NewGuid().ToString())
[IO.Directory]::CreateDirectory($root) | Out-Null
try {
 $original="rush-input 1`nkeyboard-port 0`n"
 [IO.File]::WriteAllText((Join-Path $root 'input-default.cfg'),$original)
 foreach($label in @('first','second','explicit','invalid')){[IO.Directory]::CreateDirectory((Join-Path $root ($label+'\profile'))) | Out-Null}
 $first=Initialize-RushInputSettings -NativeRoot $root -RunRoot (Join-Path $root 'first')
 if([IO.File]::ReadAllText($first) -ne $original){throw 'Initial defaults not seeded'}
 $changed="rush-input 1`nkeyboard-port 1`n"
 [IO.File]::WriteAllText($first,$changed)
 $second=Initialize-RushInputSettings -NativeRoot $root -RunRoot (Join-Path $root 'second')
 if($second -ne $first -or [IO.File]::ReadAllText($second) -ne $changed){throw 'Saved settings overwritten'}
 if([IO.File]::ReadAllText((Join-Path $root 'second\profile\input-before.cfg')) -ne $changed){throw 'Receipt snapshot wrong'}
 $custom=Join-Path $root 'custom.cfg';[IO.File]::WriteAllText($custom,$original)
 $explicit=Initialize-RushInputSettings -NativeRoot $root -RunRoot (Join-Path $root 'explicit') -InputConfig $custom
 if($explicit -ne $custom -or [IO.File]::ReadAllText($first) -ne $changed){throw 'Explicit settings interfered with defaults'}
 $rejected=$false
 try {Initialize-RushInputSettings -NativeRoot $root -RunRoot (Join-Path $root 'invalid') -InputConfig $root | Out-Null} catch {$rejected=$true}
 if(!$rejected){throw 'Directory settings accepted'}
 Write-Output 'PASS first launch, saved remap preservation, next-launch snapshot, explicit isolation, invalid-path rejection'
}finally{
 if([IO.Path]::GetFullPath($root).StartsWith([IO.Path]::GetFullPath([IO.Path]::GetTempPath())) -and (Split-Path $root -Leaf).StartsWith('rush-settings-')){Remove-Item -LiteralPath $root -Recurse -Force}
}
