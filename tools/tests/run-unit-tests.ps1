# Build and run the standalone unit checks against the patched plugin sources.
# Usage (from the project root): pwsh tools/tests/run-unit-tests.ps1
# Output binaries go to work/tests-build (ignored). Requires VS Build Tools (MSVC).
$ErrorActionPreference = 'Stop'
$root = Resolve-Path "$PSScriptRoot/../.."
$src = Join-Path $root 'upstream/obs-profanity-filter/src'
$out = Join-Path $root 'work/tests-build'
New-Item -ItemType Directory -Force $out | Out-Null

$vswhere = "${env:ProgramFiles(x86)}/Microsoft Visual Studio/Installer/vswhere.exe"
$vs = & $vswhere -products '*' -latest -property installationPath
$vcvars = Join-Path $vs 'VC/Auxiliary/Build/vcvars64.bat'
$env:PATH = "$(Split-Path $vswhere);$env:PATH"  # vcvars64.bat calls vswhere itself

$tests = @(
    @{ Name = 'pinyin-lexicon-test'; Sources = @("$PSScriptRoot/pinyin-lexicon-test.cpp", "$src/utils.cpp") }
    @{ Name = 'byte-token-test'; Sources = @("$PSScriptRoot/byte-token-test.cpp", "$src/utils.cpp") }
)
$failed = 0
foreach ($t in $tests) {
    $exe = Join-Path $out "$($t.Name).exe"
    # /UNDEBUG keeps assert() active in the optimized build.
    $cmd = "`"$vcvars`" >nul && cl /nologo /std:c++17 /EHsc /utf-8 /O2 /UNDEBUG /I `"$src`" " +
           (($t.Sources | ForEach-Object { "`"$_`"" }) -join ' ') + " /Fo`"$out\\`" /Fe`"$exe`""
    cmd /c $cmd | Out-Null
    if ($LASTEXITCODE -ne 0) { Write-Host "BUILD FAIL $($t.Name)"; $failed++; continue }
    & $exe
    if ($LASTEXITCODE -ne 0) { Write-Host "FAIL $($t.Name) (exit $LASTEXITCODE)"; $failed++ }
    else { Write-Host "PASS $($t.Name)" }
}
exit $failed
