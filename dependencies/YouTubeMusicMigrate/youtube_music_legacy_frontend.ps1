# The product keeps its legacy parameter declarations. Only explicitly bound
# values cross this adapter, retaining false switches, empty text and arrays.
function Invoke-YtmLegacyFrontend {
    param([string]$DataRoot, [System.Collections.IDictionary]$BoundArguments)
    $executable = Join-Path $DataRoot 'YouTubeMusicMigrate.exe'
    if (-not (Test-Path -LiteralPath $executable -PathType Leaf)) { $executable = Join-Path $DataRoot 'build/YouTubeMusicMigrate.exe' }
    if (-not (Test-Path -LiteralPath $executable -PathType Leaf)) { throw 'Build YouTubeMusicMigrate with BuildYouTubeMusicMigrate.cmd or use the compiled release before running this compatibility entry.' }
    $nativeHelper = Join-Path (Split-Path -Parent $executable) 'powershell_native_launcher.ps1'
    . $nativeHelper
    $arguments = @{}
    $ini = Join-Path $DataRoot 'YouTubeMusicMigrate.ini'
    foreach ($entry in $BoundArguments.GetEnumerator()) {
        if ($entry.Key -eq 'IniFile') { $ini = [string]$entry.Value; continue }
        if ($entry.Key -in @('Verbose','Debug','ErrorAction','WarningAction','InformationAction','ErrorVariable','WarningVariable','InformationVariable','OutVariable','OutBuffer','PipelineVariable')) { continue }
        $value = $entry.Value
        if ($value -is [Management.Automation.SwitchParameter]) { $value = [bool]$value.IsPresent }
        $arguments[$entry.Key] = $value
    }
    $request = @{ DataRoot = $DataRoot; IniPath = $ini; Arguments = $arguments }
    $temporary = Join-Path ([IO.Path]::GetTempPath()) ('AIProjects-YtmLegacy-' + [Guid]::NewGuid().ToString('N') + '.json')
    try {
        [IO.File]::WriteAllText($temporary, ($request | ConvertTo-Json -Depth 8), (New-Object Text.UTF8Encoding($false)))
        return Invoke-AipNativeExecutable -ExecutablePath $executable -ArgumentList @('--legacy-request', $temporary)
    } finally { if ([IO.File]::Exists($temporary)) { [IO.File]::Delete($temporary) } }
}
