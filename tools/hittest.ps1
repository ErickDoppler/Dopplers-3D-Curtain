# Regression test for clicks: opens a picture and checks, for every visible Edit and Button of the main window, that the window under its centre is the control itself
# (a window stacked on top of the controls swallows every click). Run after building: powershell -File tools/hittest.ps1
Get-Process Dopplers3DCurtain -ErrorAction SilentlyContinue | Stop-Process -Force; Start-Sleep 2
$g = 'E:\PROJECTS\DESKTOP\Dopplers-3D-Curtain'
Start-Process "$g\dist\Dopplers3DCurtain.exe" -ArgumentList '"C:\workenv\ComfyUI-Easy-Install\ComfyUI-Easy-Install\ComfyUI\input\Helmet.png"' -WorkingDirectory "$g\dist"; Start-Sleep 7
Add-Type -TypeDefinition @'
using System; using System.Text; using System.Collections.Generic; using System.Runtime.InteropServices;
public class H {
  [StructLayout(LayoutKind.Sequential)] public struct PT { public int x, y; }
  [StructLayout(LayoutKind.Sequential)] public struct RC { public int l, t, r, b; }
  [DllImport("user32.dll")] public static extern IntPtr WindowFromPoint(PT p);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RC r);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetClassNameW(IntPtr h, StringBuilder s, int n);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr FindWindowW(string c, string t);
  [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
  [DllImport("user32.dll")] public static extern bool IsWindowEnabled(IntPtr h);
  [DllImport("user32.dll")] public static extern IntPtr GetParent(IntPtr h);
  public delegate bool CB(IntPtr h, IntPtr l);
  [DllImport("user32.dll")] public static extern bool EnumChildWindows(IntPtr p, CB c, IntPtr l);
  public static string Cls(IntPtr h) { var s = new StringBuilder(80); GetClassNameW(h, s, 80); return s.ToString(); }
  public static List<IntPtr> Kids(IntPtr p) { var l = new List<IntPtr>(); EnumChildWindows(p, (h, x) => { l.Add(h); return true; }, IntPtr.Zero); return l; }
}
'@
$main = [H]::FindWindowW('D3CMain', [NullString]::Value)
"main enabled: $([H]::IsWindowEnabled($main))"
foreach ($k in [H]::Kids($main)) {
  if ([H]::GetParent($k) -ne $main) { continue }
  $c = [H]::Cls($k); if ($c -ne 'Edit' -and $c -ne 'Button') { continue }
  $r = New-Object H+RC; [void][H]::GetWindowRect($k, [ref]$r); if (-not [H]::IsWindowVisible($k)) { continue }
  $p = New-Object H+PT; $p.x = [int](($r.l + $r.r) / 2); $p.y = [int](($r.t + $r.b) / 2); $top = [H]::WindowFromPoint($p)
  "{0,-8} at ({1},{2}) size {3}x{4} enabled={5}  -> window under its centre: {6} {7}" -f $c, $p.x, $p.y, ($r.r - $r.l), ($r.b - $r.t), [H]::IsWindowEnabled($k), [H]::Cls($top), $(if ($top -eq $k) { '(itself, OK)' } else { '<<< NOT the control' })
}
