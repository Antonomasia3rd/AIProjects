param(
    [string]$Windhawk = 'C:\Program Files\Windhawk',
    [ValidateSet('all','windows-11-lockapp-styler','lockapp-xaml-dumper')][string]$Only = 'all'
)
$ErrorActionPreference = 'Stop'
$project = Split-Path $PSScriptRoot -Parent
$compiler = Join-Path $Windhawk 'Compiler\bin\clang++.exe'
$engine = Get-ChildItem -LiteralPath (Join-Path $Windhawk 'Engine') -Directory |
    Sort-Object { [version]$_.Name } -Descending | Select-Object -First 1
$importLibrary = Join-Path $engine.FullName '64\windhawk.lib'
$buildPath = Join-Path $project 'build'
New-Item -ItemType Directory -Force -Path $buildPath | Out-Null
$records = @()
$recordFile = Join-Path $buildPath 'build-inputs.json'
if ($Only -ne 'all' -and (Test-Path -LiteralPath $recordFile)) {
    $records = @(Get-Content -LiteralPath $recordFile -Raw | ConvertFrom-Json |
        Where-Object source -ne ('builds/' + $Only + '.wh.cpp'))
}
$mods = @(@{Id='windows-11-lockapp-styler'; Version='1.0.3'}, @{Id='lockapp-xaml-dumper'; Version='0.5.0'})
if ($Only -ne 'all') { $mods = @($mods | Where-Object Id -eq $Only) }
foreach ($mod in $mods) {
    $sourceFile = Join-Path $project ('builds/' + $mod.Id + '.wh.cpp')
    $sourceHash = (Get-FileHash -LiteralPath $sourceFile -Algorithm SHA256).Hash
    $libraries = @('-lole32','-loleaut32','-lruntimeobject')
    if ($mod.Id -eq 'windows-11-lockapp-styler') { $libraries += '-lcomctl32' }
    $arguments = @('-std=c++23','-O2','-shared','-target','x86_64-w64-mingw32',
        '-DUNICODE','-D_UNICODE','-DWINVER=0x0A00','-D_WIN32_WINNT=0x0A00',
        '-D_WIN32_IE=0x0A00','-DNTDDI_VERSION=0x0A000008','-D__USE_MINGW_ANSI_STDIO=0','-DWH_MOD',
        ('-DWH_MOD_ID=L"' + $mod.Id + '"'), ('-DWH_MOD_VERSION=L"' + $mod.Version + '"'),
        '-include','windhawk_api.h',$sourceFile,
        $importLibrary) + $libraries + @('-Wl,--export-all-symbols',
        '-o',(Join-Path $buildPath ($mod.Id + '.dll')))
    $log = Join-Path $buildPath ($mod.Id + '.build.log')
    & $compiler @arguments 2>&1 | Tee-Object -FilePath $log
    if ($LASTEXITCODE -ne 0) { throw "$($mod.Id): compiler failed with exit code $LASTEXITCODE" }
    if ((Get-FileHash -LiteralPath $sourceFile -Algorithm SHA256).Hash -ne $sourceHash) {
        throw "$($mod.Id): source changed during compilation"
    }
    $records += @{source=('builds/' + $mod.Id + '.wh.cpp'); sha256=$sourceHash; windhawk=$engine.Name; target='x86_64-w64-mingw32'; compiledUtc=[DateTime]::UtcNow.ToString('o')}
    "$($mod.Id): build passed" | Tee-Object -FilePath $log -Append
}
$records | ConvertTo-Json -Depth 3 | Set-Content -LiteralPath (Join-Path $buildPath 'build-inputs.json') -Encoding utf8
