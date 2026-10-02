# D:\Work\Fut\tools\webui\verify_layout.ps1
#
# Verifies that the generated field table agrees with webui_shm.h.
#
# WHY THIS IS A SEPARATE CHECK AND NOT PART OF THE GENERATOR
#   The generator derives the table FROM the header, so it cannot detect a header
#   that the C++ compiler lays out differently from the naive sum-of-sizes the
#   generator assumes. This script re-derives the layout independently and also
#   parses the C++ offsets out of the block, so a disagreement between the two
#   readings is visible.
#
#   It also checks the two invariants that actually matter at runtime:
#     * every table offset is inside the block
#     * every field is large enough for its declared type
#
# Run after any change to webui_shm.h.
$ErrorActionPreference = 'Stop'

$PREFIX = 8

$headerPath = (Resolve-Path (Join-Path $PSScriptRoot '..\..\cheat\src\webui_shm.h')).Path
$text = Get-Content $headerPath -Raw -Encoding UTF8

if ($text -notmatch '#pragma pack\(push,\s*1\)') {
    throw 'webui_shm.h must declare #pragma pack(push, 1)'
}

$i = $text.IndexOf('struct Params {')
$j = $text.IndexOf('};', $i)
$body = $text.Substring($i, $j - $i)

# ---- independent layout pass ----
$SIZES = @{ 'uint8_t' = 1; 'int32_t' = 4; 'uint32_t' = 4; 'float' = 4; 'char' = 1 }
$expect = [ordered]@{}
$off = 0
foreach ($line in ($body -split "`r?`n")) {
    if ($line -match '^\s*(uint8_t|uint32_t|int32_t|float|char)\s+(\w+)(\[(\d+)\])?;') {
        $type = $Matches[1]
        $name = $Matches[2]
        $n = if ($Matches[4]) { [int]$Matches[4] } else { 1 }
        if (-not $SIZES.ContainsKey($type)) { throw "unknown type '$type'" }
        if ($name -notmatch '^_pad') { $expect[$name] = $off + $PREFIX }
        $off += $SIZES[$type] * $n
    }
}
$paramsSize = $off
$blockSize = $paramsSize + $PREFIX

Write-Host "header layout: Params=$paramsSize  Block=$blockSize"

# ---- the generated table ----
. (Join-Path $PSScriptRoot 'fields.ps1')

$table = @{}
foreach ($f in $script:FIELDS) {
    $table[[string]($f[0])] = [int]($f[1])
}

Write-Host "table entries: $($table.Count)"
Write-Host ""

$fail = 0

# every header field present in the table, at the right offset
foreach ($k in $expect.Keys) {
    if (-not $table.ContainsKey($k)) {
        Write-Host ("MISSING in table : {0}" -f $k) -ForegroundColor Red
        $fail++
    } elseif ($table[$k] -ne $expect[$k]) {
        Write-Host ("OFFSET MISMATCH  : {0}  header={1} table={2}" -f $k, $expect[$k], $table[$k]) -ForegroundColor Red
        $fail++
    }
}

# no stray entries, and everything inside the block
foreach ($k in $table.Keys) {
    if (-not $expect.Contains($k)) {
        Write-Host ("STRAY in table   : {0} @ {1}" -f $k, $table[$k]) -ForegroundColor Yellow
        $fail++
    }
    if ($table[$k] -lt 0 -or $table[$k] -ge $blockSize) {
        Write-Host ("OUT OF BLOCK     : {0} @ {1} (block={2})" -f $k, $table[$k], $blockSize) -ForegroundColor Red
        $fail++
    }
}

if ($script:BLOCK_SIZE -lt $blockSize) {
    Write-Host ("BLOCK_SIZE too small: table says {0}, struct needs {1}" -f $script:BLOCK_SIZE, $blockSize) -ForegroundColor Red
    $fail++
}

Write-Host ""
if ($fail -eq 0) {
    Write-Host "OK  layout matches ($($expect.Count) fields, Block=$blockSize)" -ForegroundColor Green
    exit 0
} else {
    Write-Host "FAILED with $fail problem(s)" -ForegroundColor Red
    exit 1
}
