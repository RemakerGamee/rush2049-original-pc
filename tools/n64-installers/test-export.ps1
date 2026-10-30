$ErrorActionPreference='Stop'
Add-Type -AssemblyName System.IO.Compression.FileSystem
$root=Join-Path ([IO.Path]::GetTempPath()) ('n64-export-test-'+[Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $root | Out-Null
try {
    $builds=Join-Path $root 'Built Games';$game=Join-Path $builds 'super-mario-64'
    New-Item -ItemType Directory -Path (Join-Path $game 'saves') -Force | Out-Null
    [IO.File]::WriteAllBytes((Join-Path $game 'Game.exe'),[byte[]](0x4d,0x5a,0,0))
    [IO.File]::WriteAllText((Join-Path $game 'LICENSE.txt'),'preserved notice')
    [IO.File]::WriteAllText((Join-Path $game 'settings.json'),'private settings must stay local')
    [IO.File]::WriteAllText((Join-Path $game '.env'),'private secret must stay local')
    [IO.File]::WriteAllText((Join-Path $game 'saves/save.data'),'private save must stay local')
    [IO.File]::WriteAllBytes((Join-Path $game 'renamed.data'),[byte[]](0x80,0x37,0x12,0x40,0,0))
    $before=(Get-FileHash -LiteralPath (Join-Path $game 'Game.exe')).Hash
    $zip=Join-Path $root 'Inputs.zip'
    & (Join-Path $PSScriptRoot 'Export-N64-Installer-Inputs.ps1') -BuildRoot $builds -OutputZip $zip
    $archive=[IO.Compression.ZipFile]::OpenRead($zip)
    try {
        $names=@($archive.Entries | ForEach-Object {$_.FullName})
        if ($names.Count -ne 3 -or 'games/super-mario-64/Game.exe' -notin $names -or 'games/super-mario-64/LICENSE.txt' -notin $names) {throw 'Unexpected export contents'}
        $reader=New-Object IO.StreamReader($archive.GetEntry('installer-inputs.json').Open())
        try {$report=$reader.ReadToEnd() | ConvertFrom-Json} finally {$reader.Dispose()}
        if ($report.games.Count -ne 1 -or $report.games[0].identity_verified -ne $false -or $report.publication_authorized -ne $false) {throw 'Incorrect candidate status'}
    } finally {$archive.Dispose()}
    if ((Get-FileHash -LiteralPath (Join-Path $game 'Game.exe')).Hash -ne $before) {throw 'Original executable changed'}
    $refused=$false
    try {& (Join-Path $PSScriptRoot 'Export-N64-Installer-Inputs.ps1') -BuildRoot $builds -OutputZip $zip} catch {$refused=$true}
    if (-not $refused) {throw 'Existing export was overwritten'}
    Write-Output 'PASS: isolated game export, ROM magic and private-data exclusions, exact hashes, source preservation and overwrite refusal.'
} finally {Remove-Item -LiteralPath $root -Recurse -Force}
