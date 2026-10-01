# D:\Work\Fut\tools\work\validate_offsets.ps1
# Runs ELEVATED via the ClbqAdmin scheduled task.
# Live-validates the freshly dumped offsets against the running game by reading
# GObjects / GWorld straight out of the process. ASCII only.
$ErrorActionPreference = 'Continue'
$OUT = 'D:\Work\Fut\evidence\live_validation.txt'
function W([string]$m) { $m | Out-File $OUT -Append -Encoding UTF8; Write-Host $m }
'' | Out-File $OUT -Encoding UTF8
W "=== live offset validation $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss') ==="

# offsets from the Dumper-7 run of 2026-09-30
$OFF_GOBJECTS     = 0x072BE4B0
$OFF_GNAMES       = 0x072A4F80
$OFF_GWORLD       = 0x07420710
$OFF_PROCESSEVENT = 0x02A7D630
$ELEMS_PER_CHUNK  = 0x10000

Add-Type -Namespace Fut -Name Mem -MemberDefinition @'
[DllImport("kernel32.dll", SetLastError=true)] public static extern IntPtr OpenProcess(uint a, bool i, int p);
[DllImport("kernel32.dll", SetLastError=true)] public static extern bool ReadProcessMemory(IntPtr h, IntPtr b, byte[] buf, int sz, out IntPtr rd);
[DllImport("kernel32.dll")] public static extern bool CloseHandle(IntPtr h);
'@

$proc = Get-Process -Name 'Calabiyau-Win64-Shipping' -ErrorAction SilentlyContinue | Select-Object -First 1
if (-not $proc) { W "ABORT: game not running"; exit 1 }
W "game pid = $($proc.Id)  ws = $([int]($proc.WorkingSet64/1MB)) MB"

$h = [Fut.Mem]::OpenProcess(0x10, $false, $proc.Id)   # PROCESS_VM_READ
if ($h -eq [IntPtr]::Zero) { W "ABORT: OpenProcess failed err=$([Runtime.InteropServices.Marshal]::GetLastWin32Error())"; exit 2 }
W "OpenProcess(VM_READ) OK"

# module base of the main executable
$base = $null
try { $base = $proc.Modules[0].BaseAddress } catch { W "module enum failed: $($_.Exception.Message)" }
if (-not $base) { $base = [IntPtr]0x140000000; W "using assumed image base 0x140000000" }
W ("image base = 0x{0:X}" -f [int64]$base)

function Read-Bytes([int64]$addr, [int]$size) {
    $buf = [byte[]]::new($size)
    $rd = [IntPtr]::Zero
    $ok = [Fut.Mem]::ReadProcessMemory($h, [IntPtr]$addr, $buf, $size, [ref]$rd)
    if (-not $ok) { return $null }
    return $buf
}
function U32([byte[]]$b, [int]$o) { return [BitConverter]::ToUInt32($b, $o) }
function I32([byte[]]$b, [int]$o) { return [BitConverter]::ToInt32($b, $o) }
function U64([byte[]]$b, [int]$o) { return [BitConverter]::ToUInt64($b, $o) }

# ---- GObjects -----------------------------------------------------------
W ""
W "--- GObjects @ base+0x$('{0:X}' -f $OFF_GOBJECTS) ---"
$goAddr = [int64]$base + $OFF_GOBJECTS
$go = Read-Bytes $goAddr 0x20
if (-not $go) { W "  READ FAILED at GObjects slot" }
else {
    $objects   = U64 $go 0x00
    $maxElems  = I32 $go 0x10
    $numElems  = I32 $go 0x14
    $maxChunks = I32 $go 0x18
    $numChunks = I32 $go 0x1C
    W ("  FUObjectItem** Objects = 0x{0:X}" -f $objects)
    W "  MaxElements = $maxElems"
    W "  NumElements = $numElems"
    W "  MaxChunks   = $maxChunks"
    W "  NumChunks   = $numChunks"
    $ok = ($numElems -gt 1000 -and $numElems -lt 2000000 -and $objects -gt 0x10000)
    W "  >>> GObjects VALID: $ok  (expect ~198592 from the dump)"

    # chunk 0 -> first few FUObjectItem.Object pointers
    if ($ok) {
        $chunkTable = Read-Bytes ([int64]$objects) 8
        if ($chunkTable) {
            $chunk0 = U64 $chunkTable 0
            W ("  chunk[0] = 0x{0:X}" -f $chunk0)
            $items = Read-Bytes ([int64]$chunk0) 0x18
            if ($items) {
                W ("  item[0].Object = 0x{0:X}" -f (U64 $items 0))
                $obj = U64 $items 0
                if ($obj -gt 0x10000) {
                    # UObject header: vtable @0x00, Class @0x10, Name @0x18 (UE4.25)
                    $hdr = Read-Bytes ([int64]$obj) 0x28
                    if ($hdr) {
                        W ("  item[0] vtable    = 0x{0:X}" -f (U64 $hdr 0x00))
                        W ("  item[0] Class     = 0x{0:X}" -f (U64 $hdr 0x10))
                        W ("  item[0] Name(idx) = 0x{0:X8}" -f (U32 $hdr 0x18))
                    }
                }
            }
        }
    }
}

# ---- GWorld -------------------------------------------------------------
W ""
W "--- GWorld @ base+0x$('{0:X}' -f $OFF_GWORLD) ---"
$gw = Read-Bytes ([int64]$base + $OFF_GWORLD) 8
if (-not $gw) { W "  READ FAILED" }
else {
    $world = U64 $gw 0
    W ("  UWorld* = 0x{0:X}" -f $world)
    W "  >>> GWorld VALID: $($world -gt 0x10000)"
    if ($world -gt 0x10000) {
        # UWorld: PersistentLevel @0x30, Levels @0x148, OwningGameInstance @0x188
        $w = Read-Bytes ([int64]$world) 0x190
        if ($w) {
            W ("  PersistentLevel   @0x30  = 0x{0:X}" -f (U64 $w 0x30))
            W ("  Levels.Data       @0x148 = 0x{0:X}  Num={1}" -f (U64 $w 0x148), (I32 $w 0x150))
            W ("  OwningGameInstance@0x188 = 0x{0:X}" -f (U64 $w 0x188))
        }
    }
}

# ---- GNames -------------------------------------------------------------
W ""
W "--- GNames @ base+0x$('{0:X}' -f $OFF_GNAMES) ---"
$gn = Read-Bytes ([int64]$base + $OFF_GNAMES) 0x20
if (-not $gn) { W "  READ FAILED" }
else {
    W ("  qword@0x00 = 0x{0:X}" -f (U64 $gn 0x00))
    W ("  qword@0x08 = 0x{0:X}" -f (U64 $gn 0x08))
    W ("  qword@0x10 = 0x{0:X}" -f (U64 $gn 0x10))
    W ("  qword@0x18 = 0x{0:X}" -f (U64 $gn 0x18))
}

# ---- ProcessEvent -------------------------------------------------------
W ""
W "--- ProcessEvent @ base+0x$('{0:X}' -f $OFF_PROCESSEVENT) ---"
$pe = Read-Bytes ([int64]$base + $OFF_PROCESSEVENT) 16
if ($pe) { W ("  first bytes: " + (($pe | ForEach-Object { $_.ToString('X2') }) -join ' ')) } else { W "  READ FAILED" }

[Fut.Mem]::CloseHandle($h) | Out-Null
W ""
W "=== done ==="
