param([ValidateSet('en','ko')][string]$Language='en')
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent
$testRoot = Join-Path $projectRoot ('out\quota-ui-' + $Language + '-' + [Guid]::NewGuid().ToString('N'))
$logs = Join-Path $testRoot 'Logs'
$empty = Join-Path $testRoot 'Empty'
New-Item -ItemType Directory -Force -Path $logs,$empty | Out-Null
$utf8 = New-Object Text.UTF8Encoding($false)
$now = [DateTimeOffset]::UtcNow.ToUnixTimeSeconds()
$settings = @{version=1;interval=10;show_start=$true;language=$Language;providers=@(@{enabled=$true;path=$empty},@{enabled=$true;path=$logs})} | ConvertTo-Json -Depth 5
[IO.File]::WriteAllText((Join-Path $testRoot 'settings.json'),$settings,$utf8)
$claude = @{observed=$now;plan='';short=@{remaining=7450;minutes=300;resets=$now+7200};week=@{remaining=3900;minutes=10080;resets=$now+3*86400}} | ConvertTo-Json -Depth 4
[IO.File]::WriteAllText((Join-Path $testRoot 'claude-quota.json'),$claude,$utf8)
$lines = @()
foreach ($minutesAgo in @(180,120,60,0)) {
    $lines += @{type='event_msg';timestamp=([DateTimeOffset]::FromUnixTimeSeconds($now-$minutesAgo*60).UtcDateTime.ToString('o'));payload=@{type='token_count';info=$null;rate_limits=@{limit_id='codex';primary=@{used_percent=(40-$minutesAgo/6);window_minutes=300;resets_at=$now+7200};secondary=@{used_percent=(55-$minutesAgo/20);window_minutes=10080;resets_at=$now+3*86400}}}} | ConvertTo-Json -Depth 7 -Compress
}
[IO.File]::WriteAllText((Join-Path $logs 'quota.jsonl'),($lines -join "`n")+"`n",$utf8)
# Account RPC fixture is authoritative even when conflicting session telemetry exists.
$codex = @{source='codex-account';limit_id='codex';observed=$now;plan='prolite';scope='fixture-account';short=@{remaining=6000;minutes=300;resets=$now+7200};week=@{remaining=4500;minutes=10080;resets=$now+3*86400}} | ConvertTo-Json -Depth 5
[IO.File]::WriteAllText((Join-Path $testRoot 'codex-quota.json'),$codex,$utf8)
Add-Type -ReferencedAssemblies System.Drawing -TypeDefinition @'
using System;
using System.Text;
using System.Drawing;
using System.Drawing.Imaging;
using System.Runtime.InteropServices;
using System.Threading;
public static class QuotaUiTest {
    delegate bool EnumProc(IntPtr window,IntPtr unused);
    [StructLayout(LayoutKind.Sequential,CharSet=CharSet.Unicode)] struct Startup {
        public uint size; public string reserved,desktop,title;
        public uint x,y,width,height,xChars,yChars,fill,flags;
        public ushort show,reservedSize; public IntPtr reservedData,stdin,stdout,stderr;
    }
    [StructLayout(LayoutKind.Sequential)] struct ProcessInfo {public IntPtr process,thread;public uint pid,tid;}
    [StructLayout(LayoutKind.Sequential)] struct Rect {public int left,top,right,bottom;}
    [StructLayout(LayoutKind.Sequential)] struct MonitorInfo {public uint size;public Rect monitor,work;public uint flags;}
    [DllImport("user32.dll",CharSet=CharSet.Unicode)] static extern IntPtr CreateDesktop(string name,IntPtr device,IntPtr mode,uint flags,uint access,IntPtr security);
    [DllImport("user32.dll")] static extern bool CloseDesktop(IntPtr desktop);
    [DllImport("user32.dll")] static extern bool EnumDesktopWindows(IntPtr desktop,EnumProc callback,IntPtr unused);
    [DllImport("user32.dll")] static extern bool EnumChildWindows(IntPtr window,EnumProc callback,IntPtr unused);
    [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr window,out uint pid);
    [DllImport("user32.dll",CharSet=CharSet.Unicode)] static extern int GetClassName(IntPtr window,StringBuilder name,int capacity);
    [DllImport("user32.dll",CharSet=CharSet.Unicode)] static extern int GetWindowText(IntPtr window,StringBuilder name,int capacity);
    [DllImport("user32.dll")] static extern bool GetWindowRect(IntPtr window,out Rect rect);
    [DllImport("user32.dll")] static extern bool PrintWindow(IntPtr window,IntPtr dc,uint flags);
    [DllImport("user32.dll")] static extern bool RedrawWindow(IntPtr window,IntPtr rect,IntPtr region,uint flags);
    [DllImport("user32.dll")] static extern bool IsWindowVisible(IntPtr window);
    [DllImport("user32.dll")] static extern bool ShowWindow(IntPtr window,int command);
    [DllImport("user32.dll",EntryPoint="GetWindowLongPtrW")] static extern IntPtr GetWindowLongPtr(IntPtr window,int index);
    [DllImport("user32.dll")] static extern bool SetWindowPos(IntPtr window,IntPtr after,int x,int y,int width,int height,uint flags);
    [DllImport("user32.dll")] static extern IntPtr MonitorFromWindow(IntPtr window,uint flags);
    [DllImport("user32.dll",CharSet=CharSet.Unicode)] static extern bool GetMonitorInfo(IntPtr monitor,ref MonitorInfo info);
    [DllImport("user32.dll")] static extern IntPtr GetDlgItem(IntPtr window,int id);
    [DllImport("user32.dll")] static extern uint GetDpiForWindow(IntPtr window);
    [DllImport("user32.dll")] static extern bool GetLayeredWindowAttributes(IntPtr window,out uint key,out byte alpha,out uint flags);
    [DllImport("user32.dll")] static extern IntPtr SendMessage(IntPtr window,uint message,IntPtr w,IntPtr l);
    [DllImport("user32.dll")] static extern bool PostMessage(IntPtr window,uint message,IntPtr w,IntPtr l);
    [DllImport("user32.dll")] static extern uint GetGuiResources(IntPtr process,uint flag);
    [DllImport("kernel32.dll",CharSet=CharSet.Unicode)] static extern bool CreateProcess(string app,StringBuilder command,IntPtr a,IntPtr b,bool inherit,uint flags,IntPtr environment,string directory,ref Startup startup,out ProcessInfo process);
    [DllImport("kernel32.dll")] static extern uint WaitForSingleObject(IntPtr process,uint timeout);
    [DllImport("kernel32.dll")] static extern bool GetExitCodeProcess(IntPtr process,out uint code);
    [DllImport("kernel32.dll")] static extern bool TerminateProcess(IntPtr process,uint code);
    [DllImport("kernel32.dll")] static extern bool CloseHandle(IntPtr handle);
    static string Class(IntPtr window) {var b=new StringBuilder(256);GetClassName(window,b,b.Capacity);return b.ToString();}
    static IntPtr Find(IntPtr desktop,uint owner,string cls) {
        IntPtr found=IntPtr.Zero;EnumDesktopWindows(desktop,(h,p)=>{uint pid;GetWindowThreadProcessId(h,out pid);if(pid==owner && Class(h)==cls)found=h;return true;},IntPtr.Zero);return found;
    }
    static void Capture(IntPtr window,string path) {
        Thread.Sleep(250);
        RedrawWindow(window,IntPtr.Zero,IntPtr.Zero,0x185);
        Rect r;GetWindowRect(window,out r);
        using(var bitmap=new Bitmap(r.right-r.left,r.bottom-r.top)) {
            using(var g=Graphics.FromImage(bitmap)) {IntPtr dc=g.GetHdc();bool ok=PrintWindow(window,dc,2);g.ReleaseHdc(dc);if(!ok)throw new Exception("Capture failed");}
            bitmap.Save(path,ImageFormat.Png);
        }
    }
    public static string Run(string exe,string data,string screenshot,string reportPrefix) {
        string name="AiMonQuota"+Guid.NewGuid().ToString("N");
        IntPtr desktop=CreateDesktop(name,IntPtr.Zero,IntPtr.Zero,0,0x1ff,IntPtr.Zero);
        if(desktop==IntPtr.Zero) throw new Exception("Private desktop creation failed");
        ProcessInfo process=new ProcessInfo();
        try {
            var startup=new Startup();startup.size=(uint)Marshal.SizeOf(startup);
            startup.desktop=name;
            var cmd=new StringBuilder("\""+exe+"\" --no-claude-probe --no-codex-probe --no-startup-registration --data-dir \""+data+"\"");
            if(!CreateProcess(exe,cmd,IntPtr.Zero,IntPtr.Zero,false,0x08000000,IntPtr.Zero,null,ref startup,out process)) throw new Exception("App launch failed");
            IntPtr main=IntPtr.Zero,mini=IntPtr.Zero; bool ready=false; DateTime deadline=DateTime.UtcNow.AddSeconds(15);
            while(DateTime.UtcNow<deadline && !ready) {
                EnumDesktopWindows(desktop,(window,unused)=>{
                    uint pid;GetWindowThreadProcessId(window,out pid);
                    if(pid==process.pid && Class(window).StartsWith("AI.Mon.Window")) main=window;
                    if(pid==process.pid && Class(window)=="AI.Mon.MiniWindow.1") mini=window;
                    return true;
                },IntPtr.Zero);
                if(main!=IntPtr.Zero) EnumChildWindows(main,(window,unused)=>{
                    var b=new StringBuilder(1024);GetWindowText(window,b,b.Capacity);
                    if(b.ToString().StartsWith(reportPrefix)) ready=true;
                    return true;
                },IntPtr.Zero);
                Thread.Sleep(100);
            }
            if(!ready) throw new Exception("Quota data did not reach the UI");
            int charts=0;EnumChildWindows(main,(window,unused)=>{if(Class(window)=="AI.Mon.QuotaChart.1")charts++;return true;},IntPtr.Zero);
            if(charts!=4) throw new Exception("Expected four quota charts");
            if(mini==IntPtr.Zero || IsWindowVisible(mini)) throw new Exception("Mini window should initially be hidden");
            SendMessage(main,0x111,new IntPtr(105),IntPtr.Zero);
            if(!IsWindowVisible(mini)) throw new Exception("Mini command did not show the window");
            long style=GetWindowLongPtr(mini,-20).ToInt64();
            if((style&0x88)!=0x88) throw new Exception("Mini must be a topmost tool window");
            ShowWindow(main,6);
            if(!IsWindowVisible(mini)) throw new Exception("Minimizing details hid the mini window");
            ShowWindow(main,9);
            SendMessage(mini,0x111,new IntPtr(1001),IntPtr.Zero);
            if((GetWindowLongPtr(mini,-20).ToInt64()&8)!=0) throw new Exception("Always-on-top toggle failed");
            Rect initial;GetWindowRect(mini,out initial);
            uint dpi=GetDpiForWindow(mini);
            if((GetWindowLongPtr(mini,-16).ToInt64()&0x00C00000)!=0 || initial.right-initial.left>260*dpi/96 || initial.bottom-initial.top>56*dpi/96)
                throw new Exception("Mini is not clock-sized and borderless");
            SetWindowPos(mini,IntPtr.Zero,initial.left-20,initial.top-20,0,0,0x15);
            SendMessage(mini,0x232,IntPtr.Zero,IntPtr.Zero);
            Rect saved;GetWindowRect(mini,out saved);
            uint before=GetGuiResources(process.process,0);
            using(var bitmap=new Bitmap(saved.right-saved.left,saved.bottom-saved.top)) {
                using(var graphics=Graphics.FromImage(bitmap)) {
                    IntPtr dc=graphics.GetHdc();
                    for(int i=0;i<100;i++) {
                        SendMessage(main,0x8002,IntPtr.Zero,IntPtr.Zero);
                        if(!PrintWindow(mini,dc,2))throw new Exception("Mini paint failed");
                    }
                    graphics.ReleaseHdc(dc);
                }
                bitmap.Save(screenshot.Replace("quota-ui-","mini-ui-"),ImageFormat.Png);
            }
            uint after=GetGuiResources(process.process,0);
            if(after>before+2) throw new Exception("GDI handles grew during repeated tray/chart refreshes");
            SendMessage(main,0x111,new IntPtr(102),IntPtr.Zero);
            IntPtr settings=Find(desktop,process.pid,"AI.Mon.Settings.1");
            if(settings==IntPtr.Zero)throw new Exception("Settings did not open");
            IntPtr slider=GetDlgItem(settings,234);
            if(slider==IntPtr.Zero || GetDlgItem(settings,233)==IntPtr.Zero)throw new Exception("Missing transparency/startup controls");
            SendMessage(slider,0x405,new IntPtr(1),new IntPtr(35));
            SendMessage(settings,0x114,IntPtr.Zero,slider);
            uint key,flags;byte alpha;
            if(!GetLayeredWindowAttributes(mini,out key,out alpha,out flags) || alpha!=166)throw new Exception("Transparency preview failed");
            Capture(settings,screenshot.Replace("quota-ui-","settings-ui-"));
            SendMessage(settings,0x111,new IntPtr(2),IntPtr.Zero);
            GetLayeredWindowAttributes(mini,out key,out alpha,out flags);
            if(alpha!=255)throw new Exception("Cancel did not restore opacity");
            SendMessage(main,0x111,new IntPtr(102),IntPtr.Zero);
            settings=Find(desktop,process.pid,"AI.Mon.Settings.1");slider=GetDlgItem(settings,234);
            SendMessage(slider,0x405,new IntPtr(1),new IntPtr(35));
            SendMessage(settings,0x114,IntPtr.Zero,slider);
            SendMessage(settings,0x111,new IntPtr(1),IntPtr.Zero);
            if(Find(desktop,process.pid,"AI.Mon.Settings.1")!=IntPtr.Zero)throw new Exception("Settings did not save");
            GetLayeredWindowAttributes(mini,out key,out alpha,out flags);
            if(alpha!=166)throw new Exception("Saved opacity was lost");
            PostMessage(main,0x111,new IntPtr(103),IntPtr.Zero);
            IntPtr about=IntPtr.Zero;deadline=DateTime.UtcNow.AddSeconds(5);
            while(about==IntPtr.Zero && DateTime.UtcNow<deadline){about=Find(desktop,process.pid,"#32770");Thread.Sleep(100);}
            if(about==IntPtr.Zero)throw new Exception("About dialog did not open");
            bool author=false;EnumChildWindows(about,(h,p)=>{var b=new StringBuilder(4096);GetWindowText(h,b,b.Capacity);if(b.ToString().Contains("hanSU") && b.ToString().Contains("han65535"))author=true;return true;},IntPtr.Zero);
            if(!author)throw new Exception("Author information missing");
            SendMessage(about,0x111,new IntPtr(1),IntPtr.Zero);
            Rect rect;GetWindowRect(main,out rect);
            using(var bitmap=new Bitmap(rect.right-rect.left,rect.bottom-rect.top)) {
                using(var graphics=Graphics.FromImage(bitmap)) {
                    IntPtr dc=graphics.GetHdc();bool ok=PrintWindow(main,dc,2);graphics.ReleaseHdc(dc);
                    if(!ok)throw new Exception("Screenshot capture failed");
                }
                bitmap.Save(screenshot,ImageFormat.Png);
            }
            SendMessage(main,0x111,new IntPtr(104),IntPtr.Zero);
            if(WaitForSingleObject(process.process,10000)!=0) throw new Exception("App shutdown timed out");
            uint exit;GetExitCodeProcess(process.process,out exit);if(exit!=0)throw new Exception("App exited with failure");
            CloseHandle(process.thread);CloseHandle(process.process);process=new ProcessInfo();
            cmd=new StringBuilder("\""+exe+"\" --startup --no-claude-probe --no-codex-probe --no-startup-registration --data-dir \""+data+"\"");
            if(!CreateProcess(exe,cmd,IntPtr.Zero,IntPtr.Zero,false,0x08000000,IntPtr.Zero,null,ref startup,out process))throw new Exception("Restart failed");
            main=IntPtr.Zero;mini=IntPtr.Zero;deadline=DateTime.UtcNow.AddSeconds(10);
            while(DateTime.UtcNow<deadline && (mini==IntPtr.Zero || !IsWindowVisible(mini))) {
                EnumDesktopWindows(desktop,(window,unused)=>{
                    uint pid;GetWindowThreadProcessId(window,out pid);
                    if(pid==process.pid && Class(window).StartsWith("AI.Mon.Window"))main=window;
                    if(pid==process.pid && Class(window)=="AI.Mon.MiniWindow.1")mini=window;
                    return true;
                },IntPtr.Zero);
                Thread.Sleep(100);
            }
            if(mini==IntPtr.Zero || !IsWindowVisible(mini))throw new Exception("Mini visibility did not survive restart");
            if(IsWindowVisible(main))throw new Exception("Automatic startup should not open the detail window");
            GetLayeredWindowAttributes(mini,out key,out alpha,out flags);
            if(alpha!=166)throw new Exception("Opacity did not survive restart");
            if((GetWindowLongPtr(mini,-20).ToInt64()&8)!=0)throw new Exception("Topmost preference did not survive restart");
            Rect restored;GetWindowRect(mini,out restored);
            if(restored.left!=saved.left || restored.top!=saved.top)throw new Exception("Mini position did not survive restart");
            SetWindowPos(mini,IntPtr.Zero,-100000,-100000,0,0,0x15);
            SendMessage(mini,0x232,IntPtr.Zero,IntPtr.Zero);
            Rect recovered;GetWindowRect(mini,out recovered);
            var monitor=new MonitorInfo();monitor.size=(uint)Marshal.SizeOf(monitor);
            if(!GetMonitorInfo(MonitorFromWindow(mini,2),ref monitor) || recovered.left<monitor.work.left ||
               recovered.top<monitor.work.top || recovered.right>monitor.work.right || recovered.bottom>monitor.work.bottom)
                throw new Exception("Off-screen mini window was not recovered into the work area");
            SendMessage(mini,0x111,new IntPtr(1002),IntPtr.Zero);
            SendMessage(mini,0x10,IntPtr.Zero,IntPtr.Zero);
            if(IsWindowVisible(mini) || WaitForSingleObject(process.process,0)==0)throw new Exception("Closing mini must only hide it");
            SendMessage(main,0x111,new IntPtr(105),IntPtr.Zero);
            if(!IsWindowVisible(mini))throw new Exception("Mini cannot be reopened");
            SendMessage(main,0x111,new IntPtr(104),IntPtr.Zero);
            if(WaitForSingleObject(process.process,10000)!=0)throw new Exception("Restart shutdown timed out");
            GetExitCodeProcess(process.process,out exit);if(exit!=0)throw new Exception("Restart exited with failure");
            return "PASS: four charts, mini show/hide, independent minimize, topmost toggle, position/restart persistence, 100 paints with stable GDI handles; "+screenshot;
        } finally {
            if(process.process!=IntPtr.Zero && WaitForSingleObject(process.process,0)!=0) TerminateProcess(process.process,1);
            if(process.thread!=IntPtr.Zero)CloseHandle(process.thread);
            if(process.process!=IntPtr.Zero)CloseHandle(process.process);
            CloseDesktop(desktop);
        }
    }
}
'@
$locale = Get-Content -LiteralPath (Join-Path $projectRoot "locales\$Language.json") -Raw -Encoding UTF8 | ConvertFrom-Json
$prefix = $locale.strings.'quota.updated'.Split('{')[0]
$screenshot = Join-Path $projectRoot "out\Release\quota-ui-$Language.png"
[QuotaUiTest]::Run((Join-Path $projectRoot 'out\Release\ai-mon.exe'),$testRoot,$screenshot,$prefix)
