$out = 'D:\Work\Fut\evidence\elev_probe.txt'
"elev probe $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')" | Out-File $out -Encoding UTF8
$id = [Security.Principal.WindowsIdentity]::GetCurrent()
"user: $($id.Name)" | Out-File $out -Append -Encoding UTF8
"isAdmin: $((New-Object Security.Principal.WindowsPrincipal($id)).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator))" | Out-File $out -Append -Encoding UTF8
"whoami /groups:" | Out-File $out -Append -Encoding UTF8
whoami /groups 2>&1 | Out-File $out -Append -Encoding UTF8
Add-Type -Namespace T -Name P -MemberDefinition '[DllImport("kernel32.dll", SetLastError=true)] public static extern IntPtr OpenProcess(uint a, bool i, int p);'
$h = [T.P]::OpenProcess(0x20, $false, 34728)
"OpenProcess(VM_WRITE) on 34728 handle=$h lasterr=$([Runtime.InteropServices.Marshal]::GetLastWin32Error())" | Out-File $out -Append -Encoding UTF8
