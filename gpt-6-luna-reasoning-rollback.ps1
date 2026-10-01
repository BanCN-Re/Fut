$ErrorActionPreference = 'Stop'
$path = 'C:\Users\BanCN\.dsh\profiles\desktop\cordis.patch.yml'
$content = Get-Content -LiteralPath $path -Raw
$pattern = '(?m)(^\s+- id: gpt-6-luna\r?\n\s+name: gpt-6-luna\r?\n)(?!\s+reasoningEfforts:)'
$matches = [regex]::Matches($content, $pattern)
if ($matches.Count -ne 1) { throw "Expected exactly one patched Luna model; found $($matches.Count). No change made." }
$content = [regex]::Replace($content, $pattern, '$1', 1)
[System.IO.File]::WriteAllText($path, $content, [System.Text.UTF8Encoding]::new($false))
Write-Output "Restored $path (removed local reasoning override)."
