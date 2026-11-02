# Manually collect a native WER minidump for the EXISTING ActiveX lifecycle test.
# No CI workflow, CMake project, test executable, or test mode is added.
# Invoke from an elevated PowerShell on a native Windows x64 host, after build.
# Example: pwsh -File tests/atl_host/diagnose_activex_heap.ps1 -BuildDir build-msvc-x64
#          pwsh -File tests/atl_host/diagnose_activex_heap.ps1 -BuildDir build-llvm-x86_64
param(
    [Parameter(Mandatory = $true)]
    [string]$BuildDir,
    [string]$DumpDir = ''
)

$ErrorActionPreference = 'Stop'
$identity = [Security.Principal.WindowsIdentity]::GetCurrent()
$principal = [Security.Principal.WindowsPrincipal]::new($identity)
if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw 'WER LocalDumps configuration requires an elevated Windows PowerShell session.'
}

$build = (Resolve-Path -LiteralPath $BuildDir).Path
$binary = Join-Path $build 'openatl_activex.exe'
if (-not (Test-Path -LiteralPath $binary -PathType Leaf)) {
    throw "Existing native lifecycle executable not found: $binary"
}
if (-not (Get-Command ctest -ErrorAction SilentlyContinue)) {
    throw 'ctest must be available in PATH.'
}
if (-not $DumpDir) {
    $DumpDir = Join-Path $build 'activex-crash-dumps'
}
$dumpFolder = [IO.Path]::GetFullPath($DumpDir)
$null = New-Item -ItemType Directory -Path $dumpFolder -Force

# WER LocalDumps is configured under HKLM, NOT HKCU. Do not destroy any
# existing user or machine crash-report settings for this executable.
$werKey = 'HKLM:\SOFTWARE\Microsoft\Windows\Windows Error Reporting\LocalDumps\openatl_activex.exe'
if (Test-Path -LiteralPath $werKey) {
    throw "Existing WER settings detected; leave them untouched: $werKey"
}

$created = $false
$testExit = 1
try {
    $null = New-Item -Path $werKey -Force
    $created = $true
    $null = New-ItemProperty -LiteralPath $werKey -Name DumpFolder -PropertyType ExpandString -Value $dumpFolder -Force
    $null = New-ItemProperty -LiteralPath $werKey -Name DumpCount -PropertyType DWord -Value 2 -Force
    $null = New-ItemProperty -LiteralPath $werKey -Name DumpType -PropertyType DWord -Value 1 -Force

    Write-Host "WTL_MANUAL_WER binary=$binary dump_folder=$dumpFolder"
    & ctest --test-dir $build -R '^openatl\.activex_lifecycle$' --output-on-failure
    $testExit = $LASTEXITCODE
    Write-Host "WTL_MANUAL_WER ctest_exit=$testExit"
}
finally {
    if ($created) {
        Remove-Item -LiteralPath $werKey -Recurse -Force
    }
    $dumps = @(Get-ChildItem -LiteralPath $dumpFolder -Filter '*.dmp' -File -ErrorAction SilentlyContinue)
    if ($dumps.Count -eq 0) {
        Write-Host 'WTL_MANUAL_WER no_dump_found=1 (a native crash or dump collection may not have occurred)'
    } else {
        foreach ($dump in $dumps) {
            Write-Host "WTL_MANUAL_WER dump=$($dump.FullName) bytes=$($dump.Length)"
        }
        Write-Warning 'Crash dumps may contain process memory. Do not publish a raw dump in a public issue or artifact.'
    }
}
exit $testExit
