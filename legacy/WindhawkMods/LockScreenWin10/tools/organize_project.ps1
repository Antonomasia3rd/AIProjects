# One-time, lossless migration of the original flat working folder.
$ErrorActionPreference = 'Stop'
$project = [IO.Path]::GetFullPath((Split-Path $PSScriptRoot -Parent))
$moves = [ordered]@{}
foreach ($name in @('EVIDENCE.md','VALIDATION.md','CRASH-AND-SPOTLIGHT-FIX.md','WINDOWS10-HOVER-MATCH.md')) { $moves[$name] = "markdowns/$name" }
foreach ($name in @('windows-11-lockapp-styler.wh.cpp','lockapp-xaml-dumper.wh.cpp')) { $moves[$name] = "builds/$name" }
foreach ($stem in @('windows-10-lockscreen','lockapp-xaml-dumper.capture')) {
    foreach ($suffix in @('.advanced-settings.json','.preset.json')) { $moves[$stem+$suffix] = 'preferences/'+$stem+$suffix }
}
foreach ($name in @('windows-10-lockscreen.wh.preferences','lockapp-xaml-dumper.capture.preferences')) {
    foreach ($suffix in @('.yaml','.txt')) { $moves[$name+$suffix] = 'preferences/'+$name+$suffix }
}
foreach ($name in @('comparison.html','comparison.audit.json','dump-explorer.html')) { $moves[$name] = "reports/$name" }
foreach ($n in 1..3) { $name="Windows10-LockScreen-Candidate$n.zip"; $moves[$name]="releases/archive/$name" }
$moves['sources/LockApp-XamlDump-20260911-155106-pid9088.22h2.jsonl']='dumps/windows-10-22h2-stock-initial.jsonl'
$moves['sources/LockApp-XamlDump-20260911-155251-pid9980.25h2.jsonl']='dumps/windows-11-25h2-stock-initial.jsonl'
$moves['sources/LockApp-XamlDump-20260913-110739-pid7464.jsonl']='dumps/windows-10-22h2-feedback-submitted.jsonl'
$moves['LockApp-XamlDump-20260913-152627-pid6728.jsonl']='dumps/windows-10-22h2-spotlight-expanded.jsonl'
$moves['sources/LockApp-XamlDump-20260911-155106-pid9088.22h2.png']='screenshots/windows-10-22h2-stock-initial.png'
$moves['sources/LockApp-XamlDump-20260911-155251-pid9980.25h2.png']='screenshots/windows-11-25h2-stock-initial.png'
$moves['sources/LockApp-XamlDump-20260911-155106-pid9088.otherperson.png']='screenshots/other-person-preset.png'
$moves['sources/windows-11-lockapp-styler.wh.preferences.mine.png']='screenshots/original-user-preset.png'
$records=@()
# Resolve and check every target before moving anything.
foreach ($entry in $moves.GetEnumerator()) {
    $from=[IO.Path]::GetFullPath((Join-Path $project $entry.Key))
    $to=[IO.Path]::GetFullPath((Join-Path $project $entry.Value))
    foreach ($candidate in @($from,$to)) {
        if (-not $candidate.StartsWith($project+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)) { throw 'Move outside project rejected' }
    }
    if (-not (Test-Path -LiteralPath $from -PathType Leaf)) {
        if (Test-Path -LiteralPath $to -PathType Leaf) { continue }
        throw "Missing migration source: $from"
    }
    if (Test-Path -LiteralPath $to) { throw "Destination already exists: $to" }
    $records+=@{from=$entry.Key;to=$entry.Value;sha256=(Get-FileHash -LiteralPath $from -Algorithm SHA256).Hash}
}
foreach ($entry in $records) {
    $to=Join-Path $project $entry.to
    New-Item -ItemType Directory -Force -Path (Split-Path $to -Parent) | Out-Null
    Move-Item -LiteralPath (Join-Path $project $entry.from) -Destination $to
    if ((Get-FileHash -LiteralPath $to -Algorithm SHA256).Hash -ne $entry.sha256) { throw "Hash mismatch: $to" }
}
if ($records.Count) { $records | ConvertTo-Json -Depth 3 | Set-Content -LiteralPath (Join-Path $project 'analysis/project-file-map.json') -Encoding utf8 }
"Moved and hash-verified $($records.Count) files; no files deleted."
