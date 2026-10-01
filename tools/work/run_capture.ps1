Set-Location 'D:\Work\Fut\tools'
$p = (Get-Process 'Calabiyau-Win64-Shipping' -ErrorAction Stop | Select-Object -First 1).Id
"target pid = $p"
& .\live_capture.exe $p
'cap exit=' + $LASTEXITCODE
