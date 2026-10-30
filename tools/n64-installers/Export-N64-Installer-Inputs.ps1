# Export local built-game candidates for private installer preparation.
# Does not run games, install software, upload files, or alter existing builds.
[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$BuildRoot,
    [Parameter(Mandatory=$true)][string]$OutputZip,
    [string]$Catalog = (Join-Path $PSScriptRoot 'catalog.json')
)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.IO.Compression.FileSystem
$root = [IO.Path]::GetFullPath((Resolve-Path -LiteralPath $BuildRoot).Path)
$output = [IO.Path]::GetFullPath($OutputZip)
if (Test-Path -LiteralPath $output) { throw 'Choose a NEW output ZIP. Existing files are preserved.' }
if ([IO.Path]::GetExtension($output) -ne '.zip') { throw 'OutputZip must end in .zip' }
if ($output.StartsWith($root.TrimEnd('\')+'\',[StringComparison]::OrdinalIgnoreCase)) { throw 'Keep the export ZIP outside the built-game root.' }
$catalogue = Get-Content -Raw -LiteralPath $Catalog | ConvertFrom-Json
if ($catalogue.schema -ne 1 -or $catalogue.games.Count -ne 61) { throw 'Use the matching reviewed 61-title catalog.' }
$normal = { param([string]$name) ($name.ToLowerInvariant() -replace '[^a-z0-9]','') }
$excludedExtensions = @('.z64','.v64','.n64','.rom','.mpk','.sav','.srm','.sra','.eep','.fla','.state','.pdb','.obj','.o','.log','.bak','.key','.pem')
$skipNames = '^(saves?|userdata|playerdata|screenshots?|captures?|recordings?|roms?|\.git|\.codex|node_modules|__pycache__)$'
$report = [ordered]@{schema=1;purpose='private-n64-installer-inputs';games=@();unmatched=@();errors=@();gameplay_verified=$false;publication_authorized=$false}
$entries = New-Object 'Collections.Generic.List[object]'
# Restrict candidate discovery to immediate game folders; helpers are not games.
foreach ($folder in Get-ChildItem -LiteralPath $root -Directory) {
    if ($folder.Attributes -band [IO.FileAttributes]::ReparsePoint) { continue }
    $key = & $normal $folder.Name
    $candidates = @($catalogue.games | Where-Object { $id=& $normal $_.game_id; $title=& $normal $_.title; $key -eq $id -or $key -eq $title })
    if ($candidates.Count -ne 1) { $report.unmatched += $folder.Name; continue }
    $game = $candidates[0]
    try {
        $files = @()
        $pending = New-Object 'Collections.Generic.Stack[string]'
        $pending.Push($folder.FullName)
        while ($pending.Count -gt 0) {
            foreach ($item in Get-ChildItem -LiteralPath $pending.Pop()) {
                if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) { continue }
                if ($item.PSIsContainer) {
                    if ($item.Name -notmatch $skipNames) { $pending.Push($item.FullName) }
                    continue
                }
                if ($excludedExtensions -contains $item.Extension.ToLowerInvariant()) { continue }
                if ($item.Name -match '^\.env($|\.)|^(settings|input|display|options|rom-location|player-data-location)\.(json|cfg|txt)$') { continue }
                if ($item.Extension.ToLowerInvariant() -notin @('.exe','.dll','.json','.cfg','.toml','.ini','.dat','.data','.txt','.md','.pak','.rt64','.png','.jpg','.dds','.hlsl','.spv','.py','.cmd','.ps1','.bat') -and $item.Name -notmatch '^(LICENSE|COPYING|NOTICE|README)') { continue }
                $files += $item
            }
        }
        $executables = @($files | Where-Object { $_.Extension -eq '.exe' -and $_.Name -notmatch '(setup|uninstall|crashpad|updateagent|compiler|cmake|ninja|probe|check)' })
        if ($executables.Count -eq 0) { throw 'No candidate native game executable found.' }
        $records=@()
        foreach ($file in $files) {
            $relative=$file.FullName.Substring($folder.FullName.Length+1).Replace('\','/')
            if ($relative -match '(^|/)(\.\.?)(/|$)' -or $relative.Contains(':')) { throw 'Unsafe relative path.' }
            # Refuse ROM images masquerading as runtime files.
            $stream=[IO.File]::OpenRead($file.FullName)
            try { $magic=New-Object byte[] 4; $n=$stream.Read($magic,0,4) } finally { $stream.Dispose() }
            $hex=([BitConverter]::ToString($magic)).Replace('-','')
            if ($n -eq 4 -and $hex -in @('80371240','37804012','40123780')) { continue }
            $hash=(Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
            $path='games/'+$game.game_id+'/'+$relative
            $records += [ordered]@{path=$relative;bytes=$file.Length;sha256=$hash}
            $entries.Add(@{source=$file.FullName;path=$path;sha256=$hash})
        }
        $report.games += [ordered]@{game_id=$game.game_id;title=$game.title;identity_verified=$false;candidate_executables=@($executables | ForEach-Object {$_.FullName.Substring($folder.FullName.Length+1).Replace('\','/')});files=$records;required_next_step='Review exact game identity, launch arguments, ROM references, save/config paths and licenses before packaging.'}
    } catch { $report.errors += @{game_id=$game.game_id;message=$_.Exception.Message} }
}
if ($report.games.Count -eq 0) { throw 'No matching built-game folders found. Use a folder whose immediate child folders are the exact catalog titles or game IDs.' }
$staging=$output+'.partial-'+[Guid]::NewGuid().ToString('N')
$zip=$null
try {
    $parent=Split-Path -Parent $output
    if (-not (Test-Path -LiteralPath $parent)) { New-Item -ItemType Directory -Path $parent | Out-Null }
    $zip=[IO.Compression.ZipFile]::Open($staging,[IO.Compression.ZipArchiveMode]::Create)
    foreach ($entry in $entries) {
        if ((Get-FileHash -LiteralPath $entry.source -Algorithm SHA256).Hash.ToLowerInvariant() -ne $entry.sha256) { throw 'A build changed during export. Close its build and retry.' }
        [IO.Compression.ZipFileExtensions]::CreateEntryFromFile($zip,$entry.source,$entry.path,[IO.Compression.CompressionLevel]::Optimal) | Out-Null
    }
    $item=$zip.CreateEntry('installer-inputs.json')
    $writer=New-Object IO.StreamWriter($item.Open(),(New-Object Text.UTF8Encoding($false)))
    try { $writer.Write(($report | ConvertTo-Json -Depth 30)) } finally { $writer.Dispose() }
    $zip.Dispose();$zip=$null
    $checked=[IO.Compression.ZipFile]::OpenRead($staging)
    try {
        foreach ($entry in $entries) {
            $stream=$checked.GetEntry($entry.path).Open();$algorithm=[Security.Cryptography.SHA256]::Create()
            try { $actual=([BitConverter]::ToString($algorithm.ComputeHash($stream))).Replace('-','').ToLowerInvariant() } finally { $stream.Dispose();$algorithm.Dispose() }
            if ($actual -ne $entry.sha256) { throw 'Exported bytes changed during packaging. Retry from stopped builds.' }
        }
    } finally { $checked.Dispose() }
    Move-Item -LiteralPath $staging -Destination $output
    Write-Output ('Created private candidate export: '+$output)
    Write-Output ('Games: '+$report.games.Count+'; unmatched folders: '+$report.unmatched.Count+'; errors: '+$report.errors.Count)
    Write-Output 'Review installer-inputs.json before sharing. No game was executed or uploaded. This is NOT a finished installer.'
} finally { if ($zip) {$zip.Dispose()}; if (Test-Path -LiteralPath $staging) {Remove-Item -LiteralPath $staging} }
