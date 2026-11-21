# Test driver: talks to a running Dopplers3DCurtain.exe through window messages (no screen capture of the desktop).
# usage: powershell -File drv.ps1 -Do "cmd 1101; wait 2; snap; status"
param([string]$Do, [string]$SnapDir = "$env:TEMP\d3c_snaps")
$code = @'
using System; using System.Text; using System.Runtime.InteropServices; using System.Collections.Generic;
public class D {
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr FindWindowW(string c, string t);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr FindWindowExW(IntPtr p, IntPtr a, string c, string t);
  [DllImport("user32.dll")] public static extern bool PostMessageW(IntPtr h, uint m, IntPtr w, IntPtr l);
  [DllImport("user32.dll")] public static extern IntPtr SendMessageW(IntPtr h, uint m, IntPtr w, IntPtr l);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr SendMessageW(IntPtr h, uint m, IntPtr w, string l);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetWindowTextW(IntPtr h, StringBuilder s, int n);
  [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
  [DllImport("user32.dll")] public static extern IntPtr GetParent(IntPtr h);
  public delegate bool CB(IntPtr h, IntPtr l);
  [DllImport("user32.dll")] public static extern bool EnumChildWindows(IntPtr p, CB c, IntPtr l);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetClassNameW(IntPtr h, StringBuilder s, int n);
  public static List<IntPtr> Kids(IntPtr p, string cls) { var l = new List<IntPtr>(); EnumChildWindows(p, (h, x) => { var s = new StringBuilder(80); GetClassNameW(h, s, 80); if (s.ToString() == cls) l.Add(h); return true; }, IntPtr.Zero); return l; }
  public static string Text(IntPtr h) { var s = new StringBuilder(600); GetWindowTextW(h, s, 600); return s.ToString(); }
}
'@
Add-Type $code
$main = [D]::FindWindowW("D3CMain", [NullString]::Value)
if ($main -eq [IntPtr]::Zero) { "no window"; exit 1 }
$view = [D]::FindWindowExW($main, [IntPtr]::Zero, "D3CView", [NullString]::Value)
$panel = [D]::FindWindowExW($main, [IntPtr]::Zero, "D3CPanel", [NullString]::Value)
$status = [D]::FindWindowExW($main, [IntPtr]::Zero, "msctls_statusbar32", [NullString]::Value)
function MkLp($x, $y) { [IntPtr](($y -shl 16) -bor ($x -band 0xFFFF)) }
foreach ($step in ($Do -split ';')) {
  $a = $step.Trim() -split '\s+'
  switch ($a[0]) {
    'wait'   { Start-Sleep -Milliseconds ([int]([double]$a[1] * 1000)) }
    'cmd'    { [void][D]::PostMessageW($main, 0x111, [IntPtr][int]$a[1], [IntPtr]::Zero) }
    'snap'   { [void][D]::PostMessageW($main, 0x8000 + 99, [IntPtr]::Zero, [IntPtr]::Zero); Start-Sleep -Milliseconds 700 }
    'shot'   { Add-Type -AssemblyName System.Drawing; Add-Type -TypeDefinition 'using System;using System.Runtime.InteropServices;public class PW{[DllImport("user32.dll")]public static extern bool GetWindowRect(IntPtr h,out R r);[DllImport("user32.dll")]public static extern bool PrintWindow(IntPtr h,IntPtr dc,uint f);public struct R{public int l,t,r,b;}}' -ErrorAction SilentlyContinue
               $r = New-Object PW+R; [void][PW]::GetWindowRect($main, [ref]$r); $bmp = New-Object System.Drawing.Bitmap ($r.r-$r.l), ($r.b-$r.t); $gg = [System.Drawing.Graphics]::FromImage($bmp); $dc = $gg.GetHdc(); [void][PW]::PrintWindow($main, $dc, 2); $gg.ReleaseHdc($dc)
               New-Item -ItemType Directory -Force $SnapDir | Out-Null; $bmp.Save("$SnapDir\$($a[1]).png"); "shot $SnapDir\$($a[1]).png" }
    'status' { "status: " + [D]::Text($status) }
    'imgedit'{ $e = [D]::Kids($main, 'Edit') | Where-Object { [D]::GetParent($_) -eq $main }; $i = [int]$a[1]; [void][D]::SendMessageW($e[$i], 0xC, [IntPtr]::Zero, $a[2]) }
    'edits'  { $e = [D]::Kids($panel, "Edit"); $o = @(); foreach ($h in $e) { if ([D]::IsWindowVisible($h)) { $o += [D]::Text($h) } }; "edits: " + ($o -join ' | ') }
    'settext'{ $e = [D]::Kids($panel, "Edit"); $i = [int]$a[1]; [void][D]::SendMessageW($e[$i], 0xC, [IntPtr]::Zero, $a[2]); [void][D]::PostMessageW($main, 0x111, [IntPtr](0x200 -shl 16 -bor (1400 + $i * 3 + 2)), $e[$i]) }
    'down'   { [void][D]::PostMessageW($view, 0x201, [IntPtr]1, (MkLp ([int]$a[1]) ([int]$a[2]))) }
    'move'   { [void][D]::PostMessageW($view, 0x200, [IntPtr]1, (MkLp ([int]$a[1]) ([int]$a[2]))) }
    'up'     { [void][D]::PostMessageW($view, 0x202, [IntPtr]0, (MkLp ([int]$a[1]) ([int]$a[2]))) }
    'rdown'  { [void][D]::PostMessageW($view, 0x204, [IntPtr]2, (MkLp ([int]$a[1]) ([int]$a[2]))) }
    'rmove'  { [void][D]::PostMessageW($view, 0x200, [IntPtr]2, (MkLp ([int]$a[1]) ([int]$a[2]))) }
    'rup'    { [void][D]::PostMessageW($view, 0x205, [IntPtr]0, (MkLp ([int]$a[1]) ([int]$a[2]))) }
    'wheel'  { [void][D]::PostMessageW($view, 0x20A, [IntPtr]([int]$a[1] -shl 16), [IntPtr]::Zero) }
    default  { }
  }
}
