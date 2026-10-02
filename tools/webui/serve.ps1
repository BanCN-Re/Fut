# D:\Work\Fut\tools\webui\serve.ps1
#
# Out-of-process parameter UI for the FUT overlay.
#
# ===========================================================================
# WHY THIS IS A SEPARATE PROCESS
#
# The game-side DLL publishes its settings into a named shared-memory block and
# performs NO network calls whatsoever. This script opens the shared block, serves
# a small control page over HTTP on 127.0.0.1, and writes edits back into the same
# block. All socket traffic lives here.
#
# That split is the whole point: the anti-cheat scans the GAME process, and a
# listening socket inside a module that claims to be a compression proxy is about
# the loudest signal there is. Here it belongs to an ordinary console process that
# the game has no relationship with.
#
# SAFETY CHOICES, AND WHY EACH ONE
#   * 127.0.0.1 prefix only. HttpListener will refuse to bind anything reachable
#     from the LAN or the internet; there is no configuration path to widen it.
#   * A RANDOM high port per run, printed on start. A fixed port is trivially
#     discovered by anything enumerating loopback listeners; a random one is not
#     worth scanning for.
#   * A per-run ACCESS TOKEN. Any request without it is refused, so another
#     program on the machine cannot read your settings or drive your aim by
#     guessing the port.
#   * The page is served from memory; nothing is written to disk.
#
# LATENCY
#   Slider -> HTTP POST -> shared block -> next game frame. Loopback round trip is
#   1-3 ms and the block write is a memcpy, so the total is one frame plus a
#   millisecond or two. That is the same effective latency as the in-game ImGui
#   panel, which also only takes effect at a frame boundary.
#
# ASCII only: PowerShell 5.1 mangles BOM-less CJK source.
$ErrorActionPreference = 'Stop'

# ---------------------------------------------------------------------------
# field table
#
# FIELDS and BLOCK_SIZE come from fields.ps1, which is GENERATED from
# cheat\src\webui_shm.h by gen_fields.ps1. Never hand-edit either one: this reader
# addresses the block by byte offset, so a table that drifts from the C++ struct
# does not fail loudly -- it reads and writes the wrong fields.
#
# Regenerate after changing webui_shm.h:
#     pwsh -File tools\webui\gen_fields.ps1 -Write
# ---------------------------------------------------------------------------
. (Join-Path $PSScriptRoot 'fields.ps1')

# ---------------------------------------------------------------------------
# shared-memory access
#
# The block is  opened with a file mapping. The layout MUST match
# webui_shm.h exactly: a 4-byte seqlock, 4 bytes padding, then the Params struct.
# ---------------------------------------------------------------------------
$MAPPING_NAME = 'L6c1f0a9d84be3275'
# BLOCK_SIZE comes from fields.ps1 (generated); see the dot-source below.

Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;

public static class Shm {
    [DllImport("kernel32.dll", SetLastError=true, CharSet=CharSet.Ansi)]
    public static extern IntPtr OpenFileMappingA(uint access, bool inherit, string name);

    [DllImport("kernel32.dll", SetLastError=true)]
    public static extern IntPtr MapViewOfFile(IntPtr h, uint access, uint hi, uint lo, UIntPtr bytes);

    [DllImport("kernel32.dll", SetLastError=true)]
    public static extern bool UnmapViewOfFile(IntPtr p);

    [DllImport("kernel32.dll", SetLastError=true)]
    public static extern bool CloseHandle(IntPtr h);

    public const uint FILE_MAP_ALL_ACCESS = 0xF001F;
}
'@

$script:mapHandle = [IntPtr]::Zero
$script:viewPtr   = [IntPtr]::Zero

function Open-SharedBlock {
    $h = [Shm]::OpenFileMappingA([Shm]::FILE_MAP_ALL_ACCESS, $false, $MAPPING_NAME)
    if ($h -eq [IntPtr]::Zero) {
        Write-Host "  shared block not found (is the game running with the core loaded?)" -ForegroundColor Yellow
        return $false
    }
    $p = [Shm]::MapViewOfFile($h, [Shm]::FILE_MAP_ALL_ACCESS, 0, 0, [UIntPtr]::Zero)
    if ($p -eq [IntPtr]::Zero) {
        [Shm]::CloseHandle($h) | Out-Null
        Write-Host "  MapViewOfFile failed" -ForegroundColor Red
        return $false
    }
    $script:mapHandle = $h
    $script:viewPtr   = $p
    return $true
}

function Close-SharedBlock {
    if ($script:viewPtr -ne [IntPtr]::Zero) { [Shm]::UnmapViewOfFile($script:viewPtr) | Out-Null; $script:viewPtr = [IntPtr]::Zero }
    if ($script:mapHandle -ne [IntPtr]::Zero) { [Shm]::CloseHandle($script:mapHandle) | Out-Null; $script:mapHandle = [IntPtr]::Zero }
}

# Read the whole block as bytes (with a seqlock retry).
function Read-Block {
    if ($script:viewPtr -eq [IntPtr]::Zero) { return $null }
    for ($try = 0; $try -lt 8; $try++) {
        $seq1 = [Runtime.InteropServices.Marshal]::ReadInt32($script:viewPtr, 0)
        if (($seq1 -band 1) -ne 0) { Start-Sleep -Milliseconds 1; continue }   # write in progress
        $buf = New-Object byte[] $BLOCK_SIZE
        [Runtime.InteropServices.Marshal]::Copy($script:viewPtr, $buf, 0, $BLOCK_SIZE)
        $seq2 = [Runtime.InteropServices.Marshal]::ReadInt32($script:viewPtr, 0)
        if ($seq1 -eq $seq2) { return $buf }                                   # consistent
    }
    return $null
}

# ---------------------------------------------------------------------------
# Params <-> hashtable
#
# The offsets below are generated to match webui_shm.h. They are listed in one
# table so a change to the struct is a one-line change here, and a mismatch is
# visible rather than silent.
# ---------------------------------------------------------------------------
# Field table and block size come from fields.ps1, which is GENERATED from
# webui_shm.h by gen_fields.ps1. Never hand-edit either one: a drifted offset
# reads and writes the wrong field silently.
function Read-Params {
    $buf = Read-Block
    if (-not $buf) { return $null }
    $o = @{}
    foreach ($f in $script:FIELDS) {
        $name = $f[0]; $off = $f[1]; $type = $f[2]
        switch ($type) {
            'u8'  { $o[$name] = [int]$buf[$off] }
            'i32' { $o[$name] = [BitConverter]::ToInt32($buf, $off) }
            'u32' { $o[$name] = [BitConverter]::ToUInt32($buf, $off) }
            'f32' { $o[$name] = [math]::Round([BitConverter]::ToSingle($buf, $off), 3) }
            default {
                # str<N>: a fixed-size char array, NUL-terminated.
                if ($type -match '^str(\d+)$') {
                    $len = [int]$Matches[1]
                    $o[$name] = ([Text.Encoding]::UTF8.GetString($buf, $off, $len)).TrimEnd([char]0)
                }
            }
        }
    }
    return $o
}

function Write-Params($values) {
    if ($script:viewPtr -eq [IntPtr]::Zero) { return $false }

    # seqlock: bump to odd, write, then to even
    $seq = [Runtime.InteropServices.Marshal]::ReadInt32($script:viewPtr, 0)
    [Runtime.InteropServices.Marshal]::WriteInt32($script:viewPtr, 0, $seq + 1)

    foreach ($f in $script:FIELDS) {
        $name = $f[0]; $off = $f[1]; $type = $f[2]
        if (-not $values.ContainsKey($name)) { continue }
        $v = $values[$name]
        switch ($type) {
            'u8'  { [Runtime.InteropServices.Marshal]::WriteByte($script:viewPtr, $off, [byte]$v) }
            'i32' { [Runtime.InteropServices.Marshal]::WriteInt32($script:viewPtr, $off, [int]$v) }
            'f32' { [Runtime.InteropServices.Marshal]::WriteInt32($script:viewPtr, $off, [BitConverter]::ToInt32([single]$v, 0)) }
            'u32' { [Runtime.InteropServices.Marshal]::WriteInt32($script:viewPtr, $off, [int]$v) }
        }
    }

    [Runtime.InteropServices.Marshal]::WriteInt32($script:viewPtr, 0, $seq + 2)
    return $true
}

# ---------------------------------------------------------------------------
# start
# ---------------------------------------------------------------------------
Write-Host ""
Write-Host "FUT parameter UI" -ForegroundColor Cyan
Write-Host "----------------"

if (-not (Open-SharedBlock)) {
    Write-Host "Start the game first, then run this again." -ForegroundColor Yellow
    exit 1
}
Write-Host "  shared block opened" -ForegroundColor Green

$token = -join ((1..32) | ForEach-Object { '{0:x}' -f (Get-Random -Max 16) })
$port  = Get-Random -Minimum 20000 -Maximum 60000
$prefix = "http://127.0.0.1:$port/"

$listener = New-Object System.Net.HttpListener
$listener.Prefixes.Add($prefix)
try {
    $listener.Start()
} catch {
    Write-Host "  cannot bind $prefix : $_" -ForegroundColor Red
    Close-SharedBlock
    exit 1
}

$url = "$prefix`?t=$token"
Write-Host ""
Write-Host "  open this in your browser:" -ForegroundColor Green
Write-Host "  $url" -ForegroundColor White
Write-Host ""
Write-Host "  bound to 127.0.0.1 only. Ctrl+C to stop." -ForegroundColor DarkGray
Write-Host ""

# ---------------------------------------------------------------------------
# page
# ---------------------------------------------------------------------------
$pagePath = Join-Path $PSScriptRoot 'index.html'

function Get-Page {
    if (Test-Path $pagePath) { return (Get-Content $pagePath -Raw -Encoding UTF8) }
    return '<!doctype html><meta charset="utf-8"><h1>index.html missing</h1>'
}

# ---------------------------------------------------------------------------
# loop
# ---------------------------------------------------------------------------
$running = $true
try {
    while ($running) {
        $ctx = $listener.GetContext()
        $req = $ctx.Request
        $res = $ctx.Response

        # token check: refuse anything without it
        if ($req.QueryString['t'] -ne $token) {
            $res.StatusCode = 403
            $res.Close()
            continue
        }

        try {
            switch ($req.Url.AbsolutePath) {
                '/' {
                    $html = Get-Page
                    $bytes = [Text.Encoding]::UTF8.GetBytes($html)
                    $res.ContentType = 'text/html; charset=utf-8'
                    $res.ContentLength64 = $bytes.Length
                    $res.OutputStream.Write($bytes, 0, $bytes.Length)
                }
                '/api/get' {
                    $p = Read-Params
                    $json = if ($p) { $p | ConvertTo-Json -Compress } else { '{}' }
                    $bytes = [Text.Encoding]::UTF8.GetBytes($json)
                    $res.ContentType = 'application/json'
                    $res.ContentLength64 = $bytes.Length
                    $res.OutputStream.Write($bytes, 0, $bytes.Length)
                }
                '/api/set' {
                    $body = (New-Object IO.StreamReader($req.InputStream, [Text.Encoding]::UTF8)).ReadToEnd()
                    $vals = @{}
                    if ($body) {
                        $obj = $body | ConvertFrom-Json
                        foreach ($prop in $obj.PSObject.Properties) {
                            $vals[$prop.Name] = $prop.Value
                        }
                    }
                    # The UI bumps this; the game applies the block exactly once per
                    # change, which is what stops it fighting the ImGui panel.
                    # Resolve the offset from the generated table rather than hardcoding it: the
# struct gained and lost fields while this was being written, and 196 was a stale
# value by the time it mattered.
                    $genOff = ($script:FIELDS | Where-Object { $_[0] -eq 'uiGeneration' })[1]
                    $cur = [Runtime.InteropServices.Marshal]::ReadInt32($script:viewPtr, $genOff)
                    $vals['uiGeneration'] = $cur + 1

                    $ok = Write-Params $vals
                    $json = if ($ok) { '{"ok":true}' } else { '{"ok":false}' }
                    $bytes = [Text.Encoding]::UTF8.GetBytes($json)
                    $res.ContentType = 'application/json'
                    $res.ContentLength64 = $bytes.Length
                    $res.OutputStream.Write($bytes, 0, $bytes.Length)
                }
                default {
                    $res.StatusCode = 404
                }
            }
        } catch {
            try { $res.StatusCode = 500 } catch {}
        } finally {
            try { $res.Close() } catch {}
        }
    }
} finally {
    try { $listener.Stop(); $listener.Close() } catch {}
    Close-SharedBlock
    Write-Host "stopped" -ForegroundColor DarkGray
}
