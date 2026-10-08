# Shared optional PowerShell compatibility launch support. Dot-sourcing defines
# functions only; the product supplies its executable and argument values.
function ConvertTo-AipNativeArgument([string]$Value) {
    # Windows PowerShell's native splatting loses empty strings and can corrupt
    # a quoted path's trailing backslash. Follow the Windows argv convention.
    return '"' + [regex]::Replace($Value, '(\\*)("|$)', {
        param($match)
        $slashes = $match.Groups[1].Value
        if ($match.Groups[2].Value -eq '"') { return ($slashes * 2) + '\"' }
        return $slashes * 2
    }) + '"'
}

function Invoke-AipNativeExecutable {
    param(
        [string]$ExecutablePath,
        [string[]]$ArgumentList = @(),
        [scriptblock]$StartProcess = { param($info) [Diagnostics.Process]::Start($info) }
    )
    $startInfo = New-Object Diagnostics.ProcessStartInfo
    $startInfo.FileName = [IO.Path]::GetFullPath($ExecutablePath)
    $startInfo.UseShellExecute = $false
    # Set-Location does not necessarily update Environment.CurrentDirectory.
    # Relative INI/CLI paths must use the caller's current filesystem location.
    $location = Get-Location
    if ($location.Provider.Name -ne 'FileSystem') { throw 'Launch from a filesystem directory so relative paths are unambiguous.' }
    $startInfo.WorkingDirectory = $location.ProviderPath
    $startInfo.Arguments = (@($ArgumentList | ForEach-Object { ConvertTo-AipNativeArgument $_ }) -join ' ')
    $process = & $StartProcess $startInfo
    if ($null -eq $process) { throw "Could not launch $ExecutablePath." }
    try {
        $process.WaitForExit()
        return $process.ExitCode
    } finally { $process.Dispose() }
}
