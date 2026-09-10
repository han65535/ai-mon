# Open and cancel the real installer wizard on a private, never-visible desktop.
param([string]$Package = '', [ValidateSet('en','ko')][string]$Language = 'en')
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent
if (-not $Package) {
    $report = Get-Content -LiteralPath (Join-Path $projectRoot "out\Installer\installer-check.$Language.json") -Raw -Encoding UTF8 | ConvertFrom-Json
    $Package = Join-Path $projectRoot ('out\Installer\' + $report.package)
}
Add-Type -TypeDefinition @'
using System;
using System.ComponentModel;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading;
public static class AiMonInstallerUiTest {
    delegate bool EnumProc(IntPtr window, IntPtr parameter);
    [StructLayout(LayoutKind.Sequential, CharSet=CharSet.Unicode)]
    struct Startup {
        public uint size; public string reserved, desktop, title;
        public uint x,y,width,height,xChars,yChars,fill,flags;
        public ushort show,reservedSize; public IntPtr reservedData,stdin,stdout,stderr;
    }
    [StructLayout(LayoutKind.Sequential)] struct ProcessInfo { public IntPtr process,thread; public uint pid,tid; }
    [DllImport("user32.dll",CharSet=CharSet.Unicode,SetLastError=true)] static extern IntPtr CreateDesktop(string name,IntPtr device,IntPtr mode,uint flags,uint access,IntPtr security);
    [DllImport("user32.dll")] static extern bool CloseDesktop(IntPtr desktop);
    [DllImport("user32.dll")] static extern bool EnumDesktopWindows(IntPtr desktop,EnumProc callback,IntPtr parameter);
    [DllImport("user32.dll")] static extern bool EnumChildWindows(IntPtr parent,EnumProc callback,IntPtr parameter);
    [DllImport("user32.dll")] static extern bool IsWindowVisible(IntPtr window);
    [DllImport("user32.dll",CharSet=CharSet.Unicode)] static extern int GetWindowText(IntPtr window,StringBuilder text,int capacity);
    [DllImport("user32.dll")] static extern bool PostMessage(IntPtr window,uint message,IntPtr w,IntPtr l);
    [DllImport("kernel32.dll",CharSet=CharSet.Unicode,SetLastError=true)] static extern bool CreateProcess(string app,StringBuilder command,IntPtr processSecurity,IntPtr threadSecurity,bool inherit,uint flags,IntPtr environment,string directory,ref Startup startup,out ProcessInfo process);
    [DllImport("kernel32.dll")] static extern uint WaitForSingleObject(IntPtr handle,uint milliseconds);
    [DllImport("kernel32.dll")] static extern bool GetExitCodeProcess(IntPtr handle,out uint code);
    [DllImport("kernel32.dll")] static extern bool CloseHandle(IntPtr handle);
    [DllImport("kernel32.dll")] static extern bool TerminateProcess(IntPtr process,uint code);
    static string Text(IntPtr window) { var text=new StringBuilder(2048); GetWindowText(window,text,text.Capacity); return text.ToString(); }
    public static string Run(string package,string log,string welcome,string cancel,string close) {
        string name="AiMonSetupTest"+Guid.NewGuid().ToString("N");
        IntPtr desktop=CreateDesktop(name,IntPtr.Zero,IntPtr.Zero,0,0x01FF,IntPtr.Zero);
        if(desktop==IntPtr.Zero) throw new Win32Exception(Marshal.GetLastWin32Error());
        ProcessInfo process=new ProcessInfo();
        try {
            string executable=Environment.GetFolderPath(Environment.SpecialFolder.System)+"\\msiexec.exe";
            var startup=new Startup(); startup.size=(uint)Marshal.SizeOf(startup); startup.desktop=name;
            var command=new StringBuilder("\""+executable+"\" /i \""+package+"\" /qf /norestart /L*v \""+log+"\"");
            if(!CreateProcess(executable,command,IntPtr.Zero,IntPtr.Zero,false,0x08000000,IntPtr.Zero,null,ref startup,out process))
                throw new Win32Exception(Marshal.GetLastWin32Error());
            bool sawWelcome=false, clickedCancel=false, sawExit=false;
            DateTime deadline=DateTime.UtcNow.AddSeconds(30);
            while(DateTime.UtcNow<deadline && WaitForSingleObject(process.process,0)==258) {
                EnumDesktopWindows(desktop,(window,unused)=>{
                    if(!IsWindowVisible(window)) return true;
                    EnumChildWindows(window,(child,ignored)=>{
                        if(!IsWindowVisible(child)) return true;
                        string text=Text(child);
                        if(text.Contains(welcome)) sawWelcome=true;
                        if(sawWelcome && !clickedCancel && text==cancel) { PostMessage(child,0xF5,IntPtr.Zero,IntPtr.Zero); clickedCancel=true; }
                        if(clickedCancel && text==close) { sawExit=true; PostMessage(child,0xF5,IntPtr.Zero,IntPtr.Zero); }
                        return true;
                    },IntPtr.Zero);
                    return true;
                },IntPtr.Zero);
                Thread.Sleep(100);
            }
            if(WaitForSingleObject(process.process,1000)==258) {
                TerminateProcess(process.process,1);
                throw new Exception("Installer wizard timed out on private desktop; inspect "+log);
            }
            uint exit; GetExitCodeProcess(process.process,out exit);
            if(!sawWelcome || !clickedCancel || !sawExit || exit!=1602)
                throw new Exception("Wizard check failed: welcome="+sawWelcome+", cancel="+clickedCancel+", exitDialog="+sawExit+", code="+exit);
            return "PASS: Localized welcome dialog, cancel action, exit dialog; no desktop shown";
        } finally {
            if(process.thread!=IntPtr.Zero)CloseHandle(process.thread);
            if(process.process!=IntPtr.Zero)CloseHandle(process.process);
            CloseDesktop(desktop);
        }
    }
}
'@
$log = Join-Path $projectRoot "out\Installer\wizard-test.$Language.log"
$ui = Get-Content -LiteralPath (Join-Path $projectRoot "installer\ui.$Language.json") -Raw -Encoding UTF8 | ConvertFrom-Json
[AiMonInstallerUiTest]::Run($Package,$log,$ui.welcomeTitle,$ui.cancel,$ui.close)
