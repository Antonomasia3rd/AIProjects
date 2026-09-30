[CmdletBinding()]
param([string]$RepositoryRoot = (Split-Path -Parent (Split-Path -Parent (Split-Path -Parent $PSScriptRoot))))
$ErrorActionPreference = 'Stop'
$sourcePath = Join-Path $RepositoryRoot 'dependencies/ChromeProfileCounter/chrome_profile_counter_app.ps1'
$tokens = $null; $errors = $null
$ast = [System.Management.Automation.Language.Parser]::ParseFile($sourcePath, [ref]$tokens, [ref]$errors)
if ($errors.Count) { throw ($errors | Out-String) }

# Load only named function definitions. Never evaluate product initialization,
# the interactive loop, real Chrome process discovery, or explorer launch.
$wanted = @('Get-StateText', 'Get-CounterMatch', 'Set-CounterInText', 'Get-DiskProfiles',
    'Get-RegisteredProfiles', 'New-LocalStateBackupPath', 'Write-LocalStateAtomically')
foreach ($name in $wanted) {
    $definition = $ast.Find({ param($node)
        $node -is [System.Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -eq $name
    }, $false)
    if ($null -eq $definition) { throw "Missing fixture function: $name" }
    . ([scriptblock]::Create($definition.Extent.Text))
}
function Test-ChromeRunning { return $false }
function Require([bool]$condition, [string]$message) {
    if (-not $condition) { throw $message }
}
function Require-Throws([scriptblock]$action, [string]$message) {
    $threw = $false
    try { & $action | Out-Null } catch { $threw = $true }
    Require $threw $message
}

$fixture = '{"profile":{"profiles_created":4,"info_cache":{"Profile 3":{}}},"other":"keep this"}'
$updated = Set-CounterInText $fixture 8
Require ($updated -ceq $fixture.Replace('"profiles_created":4', '"profiles_created":8')) 'Only the exact counter may change.'
Require ((Get-RegisteredProfiles $fixture).ContainsKey(3)) 'Registered profile was lost.'
Require-Throws { Set-CounterInText '{"profile":{"profiles_created":4},"other":{"profiles_created":9}}' 8 } 'Ambiguous counters must be rejected.'
Require-Throws { Get-RegisteredProfiles '{invalid' } 'Unreadable registration must not become an empty list.'
Require-Throws { Set-CounterInText $fixture 0 } 'Zero counter must be rejected.'
Require-Throws { Get-CounterMatch '{"profile":{"profiles_created":2147483648}}' } 'Overflowing counter must be rejected.'

$root = Join-Path ([IO.Path]::GetTempPath()) ('AIProjects-ChromeFixture-' + [Guid]::NewGuid().ToString('N'))
$UserData = $root
$LocalState = Join-Path $root 'Local State'
$BackupDir = Join-Path $root 'Backups'
try {
    [IO.Directory]::CreateDirectory((Join-Path $root 'Profile 2')) | Out-Null
    [IO.Directory]::CreateDirectory((Join-Path $root 'Profile 7')) | Out-Null
    $disk = Get-DiskProfiles
    Require ($disk.ContainsKey(2) -and $disk.ContainsKey(7) -and $disk.Count -eq 2) 'Disk profile matches must not share stale regex capture state.'
    # Windows named-mutex and replace behavior is tested only on Windows.
    if ([Environment]::OSVersion.Platform -eq [PlatformID]::Win32NT) {
        [IO.File]::WriteAllText($LocalState, $fixture)
        $backup = Write-LocalStateAtomically $updated $fixture
        Require ([IO.File]::ReadAllText($backup) -ceq $fixture) 'Backup must be the exact replaced file.'
        Require ([IO.File]::ReadAllText($LocalState) -ceq $updated) 'Atomic change must publish the updated text.'
        Require-Throws { Write-LocalStateAtomically $fixture $fixture } 'A stale edit must be rejected.'
        Require ([IO.File]::ReadAllText($LocalState) -ceq $updated) 'A stale edit must leave the file unchanged.'
        Require (@(Get-ChildItem -LiteralPath $root -Filter '*.tmp' -Force).Count -eq 0) 'Temporary state file leaked.'
    }
} finally {
    if ([IO.Directory]::Exists($root)) { [IO.Directory]::Delete($root, $true) }
}
Write-Output 'ChromeProfileCounter synthetic tests passed.'
