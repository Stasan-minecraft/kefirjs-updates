function Child([string]$root, [string]$rel) {
    $p = $root
    foreach ($part in $rel.Split('/')) {
        $p = [IO.Path]::Combine($p,$part)
        $item = Get-Item -LiteralPath $p -Force -ErrorAction SilentlyContinue
        if ($item -and ($item.Attributes -band [IO.FileAttributes]::ReparsePoint)) {
            throw "A linked path is present: $p"
        }
    }
    return $p
}
function Folder([string]$p) { [void][IO.Directory]::CreateDirectory($p) }
function HashOf([string]$p) {
    if ([IO.Directory]::Exists($p)) { throw "Expected a file at $p" }
    if (![IO.File]::Exists($p)) { return '' }
    $f = [IO.File]::OpenRead($p)
    $sha = [Security.Cryptography.SHA256]::Create()
    try { return [BitConverter]::ToString($sha.ComputeHash($f)).Replace('-','').ToLowerInvariant() }
    finally { $sha.Dispose(); $f.Dispose() }
}
function CopyChecked([string]$src, [string]$dest, [string]$expected) {
    Folder ([IO.Path]::GetDirectoryName($dest))
    $f = [IO.File]::OpenRead($src)
    try {
        $o = [IO.File]::Open($dest,'CreateNew','Write','None')
        try { $f.CopyTo($o); $o.Flush($true) } finally { $o.Dispose() }
    } finally { $f.Dispose() }
    if ((HashOf $dest) -ne $expected) { throw "Copy verification failed: $src" }
}
function WriteReceipt([string]$file, [object]$data) {
    $bytes = [Text.Encoding]::UTF8.GetBytes((ConvertTo-Json -InputObject $data -Depth 5 -Compress))
    $f = [IO.File]::Open($file,'CreateNew','Write','None')
    try { $f.Write($bytes,0,$bytes.Length); $f.Flush($true) } finally { $f.Dispose() }
}
if (!('KefirizDistributionFile' -as [type])) {
    Add-Type -TypeDefinition @"
using System.Runtime.InteropServices;
public static class KefirizDistributionFile {
    [DllImport("kernel32.dll", EntryPoint="MoveFileExW", CharSet=CharSet.Unicode, SetLastError=true)]
    public static extern bool Move(string from, string to, int flags);
}
"@
}
function Commit([string]$src, [string]$dest, [string]$before, [string]$after) {
    Folder ([IO.Path]::GetDirectoryName($dest))
    if ((HashOf $dest) -ne $before) { throw "The destination changed during the operation: $dest" }
    if (![KefirizDistributionFile]::Move($src,$dest,9)) {
        throw (New-Object ComponentModel.Win32Exception ([Runtime.InteropServices.Marshal]::GetLastWin32Error()))
    }
    if ((HashOf $dest) -ne $after) { throw "Final verification failed: $dest" }
}
