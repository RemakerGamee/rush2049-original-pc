function Initialize-RushInputSettings {
 param([Parameter(Mandatory=$true)][string]$NativeRoot,
       [Parameter(Mandatory=$true)][string]$RunRoot,
       [string]$InputConfig='')
 $selected=if($InputConfig){[IO.Path]::GetFullPath($InputConfig)}else{Join-Path $NativeRoot 'settings\input.cfg'}
 if(Test-Path -LiteralPath $selected){
  $item=Get-Item -LiteralPath $selected
  if($item.PSIsContainer -or ($item.Attributes -band [IO.FileAttributes]::ReparsePoint)){throw 'Input settings must be a regular file'}
 }else{
  [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($selected)) | Out-Null
  $defaults=Join-Path $NativeRoot 'input-default.cfg'
  $bytes=if(Test-Path -LiteralPath $defaults){[IO.File]::ReadAllBytes($defaults)}else{[Text.Encoding]::UTF8.GetBytes("rush-input 1`nkeyboard-port 0`ndeadzone 0.15`n")}
  $stream=[IO.File]::Open($selected,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::None)
  try{$stream.Write($bytes,0,$bytes.Length);$stream.Flush($true)}finally{$stream.Dispose()}
 }
 Copy-Item -LiteralPath $selected -Destination (Join-Path $RunRoot 'profile\input-before.cfg')
 return $selected
}
