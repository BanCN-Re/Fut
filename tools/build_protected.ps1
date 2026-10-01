# D:\Work\Fut\tools\build_protected.ps1
# Full protected-build pipeline.
#
#   1. build zw_core.dll  (with ZW_THEMIDA=1 so the VM markers link)
#   2. build the thin zlibwapi carrier
#   3. run Themida64 on zw_core.dll  (virtualises the VM_START regions)
#
# WHY THE ORDER MATTERS
#   Themida must run AFTER the linker: it rewrites the finished PE. The markers
#   in the source only tell Themida WHICH regions to virtualise; without the
#   Themida pass they are no-op imports and the build behaves identically to a
#   debug build.
#
# THEMIDA LICENCE
#   D:\obf\Themida ships with TMLicenseA1.dat + License.dat already in place.
#
# WHAT THE SCRIPT DOES NOT DO
#   It does not deploy. Deploy with tools\work\deploy_core.ps1 after inspecting
#   the protected DLL (themida output overwrites build\core\zw_core.dll).
# ASCII only (PowerShell 5.1 mangles BOM-less CJK).
$ErrorActionPreference = 'Continue'

$root    = 'D:\Work\Fut\cheat'
$msbuild = 'C:\Program Files\Microsoft Visual Studio\18\Insiders\MSBuild\Current\Bin\amd64\MSBuild.exe'
$themida = 'D:\obf\Themida\Themida64.exe'
$coreOut = Join-Path $root 'build\core\zw_core.dll'
$ldrOut  = Join-Path $root 'build\loader\zlibwapi.dll'
$log     = 'D:\Work\Fut\evidence\build_protected.log'

function W([string]$m) { $l = "$(Get-Date -Format 'HH:mm:ss') $m"; $l | Out-File $log -Append -Encoding ASCII; Write-Host $l }
'' | Out-File $log -Encoding ASCII
W '=== build_protected ==='

# ---- 1. core ----
W 'building zw_core.dll (ZW_THEMIDA=1)...'
& $msbuild (Join-Path $root 'src\cheat.vcxproj') /p:Configuration=Release /p:Platform=x64 `
    /p:ZW_THEMIDA=1 /m /nologo /v:minimal 2>&1 | Select-String 'error' | ForEach-Object { W "  ERR: $_" }
if ($LASTEXITCODE -ne 0) { W 'ABORT: core build failed'; exit 1 }
W ("core built: {0} B" -f (Get-Item $coreOut).Length)

# ---- 2. carrier ----
W 'building zlibwapi.dll carrier...'
& $msbuild (Join-Path $root 'loader\loader.vcxproj') /p:Configuration=Release /p:Platform=x64 `
    /m /nologo /v:minimal 2>&1 | Select-String 'error' | ForEach-Object { W "  ERR: $_" }
if ($LASTEXITCODE -ne 0) { W 'ABORT: carrier build failed'; exit 1 }
W ("carrier built: {0} B" -f (Get-Item $ldrOut).Length)

# ---- 3. plaintext audit (must be clean BEFORE paying the Themida cost) ----
W 'plaintext audit...'
$s = [Text.Encoding]::ASCII.GetString([IO.File]::ReadAllBytes($coreOut))
$bad = @()
foreach ($k in 'CyCharacterBase','CyCharacterParent','FMinimalViewInfo','CameraCache',
               'anticheatexpert','INSERT toggles','blackhole','fake responder','fut-cheat') {
    if ($s -match [regex]::Escape($k)) { $bad += $k }
}
if ($bad.Count) { W ("ABORT: plaintext leaked: " + ($bad -join ', ')); exit 2 }
W 'plaintext audit: CLEAN'

# ---- 4. Themida pass ----
# Themida64 accepts a project file; the GUI writes one. We generate it so the
# protection step is reproducible.
$proj = Join-Path $root 'build\core\zw_core.themidaproj'
@'
<?xml version="1.0" encoding="UTF-8"?>
<Themida_Project>
  <Options>
    <InputFile>D:\Work\Fut\cheat\build\core\zw_core.dll</InputFile>
    <OutputFile>D:\Work\Fut\cheat\build\core\zw_core.dll</OutputFile>
    <Protection>VM</Protection>
    <VMSections>All</VMSections>
    <Compression>1</Compression>
    <AntiDebug>1</AntiDebug>
  </Options>
</Themida_Project>
'@ | Out-File $proj -Encoding ASCII
W "themida project written: $proj"
W 'NOTE: run Themida64.exe once with this project to produce the protected DLL.'
W '      (Themida is GUI-licensed; the command line needs a Winlicence config.)'
W '      After protecting, re-hash and deploy with tools\work\deploy_core.ps1.'

W '=== build_protected done (unprotected but audit-clean artifacts are ready) ==='
W ("carrier: " + $ldrOut)
W ("core   : " + $coreOut + "  (run Themida on this before shipping)")
