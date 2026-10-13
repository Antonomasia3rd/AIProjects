#requires -Version 7.0
param([switch]$Run)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'ReviewValidation.ps1')

# Both legacy recipes retain their default compile-and-run behavior. This path
# explicitly builds only, so each resulting fixture is scanned before execution.
foreach ($recipe in @('TestManagedConfiguration.cmd', 'TestPackagedStartup.cmd')) {
    $built = Invoke-ReviewProcess -File $env:ComSpec -Parameters @('/d','/c',
        (Join-Path $PSScriptRoot $recipe), '/build-only') -Label ($recipe + '-configuration-build') -Seconds 60
    Write-Output $built.Output.Trim()
    if ($built.ExitCode -ne 0) { throw "Configuration fixture compilation failed: $recipe" }
}

if (-not $Run) {
    Write-Output 'Compilation only. Pass -Run for custom-scanned, bounded offscreen/fake checks.'
    return
}
$fixtures = @(
    @{ Name='ManagedIniTransactionTests'; Relative='build/ManagedIniTransactionTests.exe' },
    @{ Name='ConfigurationTrayUiTests'; Relative='build/ConfigurationTrayUiTests.exe' },
    @{ Name='ManagedIniDocumentTests'; Relative='build/review-validation/ManagedIniDocumentTests.exe' },
    @{ Name='PackagedStartupTests'; Relative='build/PackagedStartupTests.exe' }
)
foreach ($fixture in $fixtures) {
    $path = Join-Path $ReviewRoot $fixture.Relative
    Assert-ReviewScan -Path $path -Label ($fixture.Name + '-configuration')
    $tested = Invoke-ReviewProcess -File $path -Parameters @() -Label ($fixture.Name + '-configuration-tests') -Seconds 60
    Write-Output $tested.Output.Trim()
    if ($tested.ExitCode -ne 0) { throw "Configuration fixture checks failed: $($fixture.Name)" }
}
# These fixtures create no visible windows/NotifyIcons, real StartupTask lookup
# or mutations, packages, services, utility jobs, device or account requests.
