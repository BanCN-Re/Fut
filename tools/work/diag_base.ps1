# D:\Work\Fut\tools\work\diag_base.ps1
# Runs ELEVATED. Finds the real module base via PSAPI and probes reads.
# ASCII only.
$ErrorActionPreference = 'Continue'
$OUT = 'D:\Work\Fut\evidence\diag_base.txt'
function W([string]$m) { $m | Out-File $OUT -Append -Encoding UTF8; Write-Host $m }
'' | Out-File $OUT -Encoding UTF8
W "=== diag_base $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss') ==="

Add-Type -Namespace Fut2 -Name Mem -MemberDefinition @'
[DllImport("kernel32.dll", SetLastError=true)] public static extern IntPtr OpenProcess(uint a, bool i, int p);
[DllImport("kernel32.dll", SetLastError=true)] public static extern bool ReadProcessMemory(IntPtr h, IntPtr b, byte[] buf, int sz, out IntPtr rd);
[DllImport("kernel32.dll")] public static extern bool CloseHandle(IntPtr h);
[DllImport("kernel32.dll", SetLastError=true)] public static extern bool IsWow64Process(IntPtr h, out bool wow);
[DllImport("psapi.dll", SetLastError=true)] public static extern bool EnumProcessModulesEx(IntPtr h, IntPtr[] mods, int cb, out int needed, uint filter);
[DllImport("psapi.dll", CharSet=CharSet.Unicode)] public static extern int GetModuleBaseNameW(IntPtr h, IntPtr mod, System.Text.StringBuilder name, int n);
[DllImport("psapi.dll", CharSet=CharSet.Unicode)] public static extern int GetModuleFileNameExW(IntPtr h, IntPtr mod, System.Text.StringBuilder name, int n);
[DllImport("psapi.dll", SetLastError=true)] public static extern bool GetModuleInformation(IntPtr h, IntPtr mod, out MODULEINFO mi, int cb);
[StructLayout(LayoutKind.Sequential)] public struct MODULEINFO { public IntPtr lpBaseOfDll; public uint SizeOfImage; public IntPtr EntryPoint; }
'@

$procs = Get-Process -Name 'Calabiyau-Win64-Shipping' -ErrorAction SilentlyContinue
if (-not $procs) { W "ABORT: game not running"; exit 1 }
foreach ($p in $procs) { W "found pid=$($p.Id) ws=$([int]($p.WorkingSet64/1MB))MB start=$($p.StartTime)" }

$proc = $procs | Select-Object -First 1
$h = [Fut2.Mem]::OpenProcess(0x0410, $false, $proc.Id)   # QUERY_INFORMATION|VM_READ
W "OpenProcess(0x410) handle=$h err=$([Runtime.InteropServices.Marshal]::GetLastWin32Error())"
if ($h -eq [IntPtr]::Zero) {
    $h = [Fut2.Mem]::OpenProcess(0x10, $false, $proc.Id)
    W "retry OpenProcess(0x10) handle=$h err=$([Runtime.InteropServices.Marshal]::GetLastWin32Error())"
}
if ($h -eq [IntPtr]::Zero) { W "ABORT"; exit 2 }

$wow = $false
[Fut2.Mem]::IsWow64Process($h, [ref]$wow) | Out-Null
W "IsWow64 (32-bit under WOW64) = $wow"

# --- enumerate modules ---------------------------------------------------
$mods = [IntPtr[]]::new(2048)
$needed = 0
$ok = [Fut2.Mem]::EnumProcessModulesEx($h, $mods, $mods.Length * 8, [ref]$needed, 0x03)  # LIST_MODULES_ALL
W "EnumProcessModulesEx ok=$ok needed=$needed err=$([Runtime.InteropServices.Marshal]::GetLastWin32Error())"
$count = [int]($needed / 8)
W "module count = $count"

$sb = [System.Text.StringBuilder]::new(512)
$rows = @()
for ($i = 0; $i -lt [Math]::Min($count, 400); $i++) {
    $m = $mods[$i]
    if ($m -eq [IntPtr]::Zero) { continue }
    $sb.Clear() | Out-Null
    [Fut2.Mem]::GetModuleBaseNameW($h, $m, $sb, $sb.Capacity) | Out-Null
    $nm = $sb.ToString()
    $mi = New-Object Fut2.Mem+MODULEINFO
    [Fut2.Mem]::GetModuleInformation($h, $m, [ref]$mi, [Runtime.InteropServices.Marshal]::SizeOf($mi)) | Out-Null
    $rows += [pscustomobject]@{ Name = $nm; Base = [int64]$m; Size = [int64]$mi.SizeOfImage }
}
W ""
W "--- modules of interest ---"
$rows | Where-Object { $_.Name -match 'Shipping|Calabiyau|ACE|d3d11|zlibwapi' } | Sort-Object Base | ForEach-Object {
    W ("  {0,-42} base=0x{1:X12} size=0x{2:X}" -f $_.Name, $_.Base, $_.Size)
}
W ""
W ("total modules enumerated: {0}" -f $rows.Count)

# --- pick the main exe ---------------------------------------------------
$main = $rows | Where-Object { $_.Name -eq 'Calabiyau-Win64-Shipping.exe' } | Select-Object -First 1
if (-not $main) { $main = $rows | Where-Object { $_.Name -match 'Shipping\.exe' } | Select-Object -First 1 }
if ($main) { W ("MAIN EXE base = 0x{0:X}  size=0x{1:X}" -f $main.Base, $main.Size) }
else { W "MAIN EXE not found in module list" }

# --- probe reads at the real base ---------------------------------------
function Probe([int64]$addr, [int]$size, [string]$label) {
    $buf = [byte[]]::new($size)
    $rd = [IntPtr]::Zero
    $ok = [Fut2.Mem]::ReadProcessMemory($h, [IntPtr]$addr, $buf, $size, [ref]$rd)
    $e = [Runtime.InteropServices.Marshal]::GetLastWin32Error()
    if ($ok) { W ("  {0,-28} @0x{1:X12} OK  rd={2}  bytes={3}" -f $label, $addr, $rd, (($buf | Select-Object -First 16 | ForEach-Object { $_.ToString('X2') }) -join ' ')) }
    else     { W ("  {0,-28} @0x{1:X12} FAIL err={2}" -f $label, $addr, $e) }
    return $ok
}

W ""
W "--- probe reads ---"
Probe 0x140000000 0x40 "hardcoded 0x140000000"
if ($main) { Probe $main.Base 0x40 "module base" }

# with the real base, try the dumped offsets
if ($main) {
    $b = $main.Base
    Probe ($b + 0x072BE4B0) 0x20 "GObjects"
    Probe ($b + 0x07420710) 0x08 "GWorld"
    Probe ($b + 0x072A4F80) 0x20 "GNames"
    Probe ($b + 0x02A7D630) 0x10 "ProcessEvent"
}

[Fut2.Mem]::CloseHandle($h) | Out-Null
W ""
W "=== done ==="
