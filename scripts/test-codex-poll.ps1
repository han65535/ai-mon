$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent
$data = Join-Path $projectRoot ('out\codex-idle-' + [Guid]::NewGuid().ToString('N'))
$empty = Join-Path $data 'empty'
$bin = Join-Path $data 'bin'
New-Item -ItemType Directory -Path $empty,$bin | Out-Null
Copy-Item -LiteralPath (Join-Path $projectRoot 'out\Test\ai-mon-tests.exe') -Destination (Join-Path $bin 'codex.exe')
$settings = @{version=1;interval=300;show_start=$false;language='en';providers=@(@{enabled=$false;path=$empty},@{enabled=$true;path=$empty})} | ConvertTo-Json -Depth 5
[IO.File]::WriteAllText((Join-Path $data 'settings.json'),$settings,(New-Object Text.UTF8Encoding($false)))
Add-Type -TypeDefinition @'
using System;
using System.Text;
using System.Runtime.InteropServices;
public static class CodexIdleTest {
    [StructLayout(LayoutKind.Sequential,CharSet=CharSet.Unicode)] struct Startup {
        public uint size; public string reserved,desktop,title;
        public uint x,y,width,height,xChars,yChars,fill,flags;
        public ushort show,reservedSize; public IntPtr reservedData,stdin,stdout,stderr;
    }
    [StructLayout(LayoutKind.Sequential)] struct ProcessInfo {public IntPtr process,thread;public uint pid,tid;}
    delegate bool EnumProc(IntPtr window,IntPtr unused);
    [DllImport("user32.dll",CharSet=CharSet.Unicode)] static extern IntPtr CreateDesktop(string name,IntPtr device,IntPtr mode,uint flags,uint access,IntPtr security);
    [DllImport("user32.dll")] static extern bool CloseDesktop(IntPtr desktop);
    [DllImport("user32.dll")] static extern bool EnumDesktopWindows(IntPtr desktop,EnumProc callback,IntPtr unused);
    [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr window,out uint pid);
    [DllImport("user32.dll")] static extern bool PostMessage(IntPtr window,uint message,IntPtr w,IntPtr l);
    [DllImport("kernel32.dll",CharSet=CharSet.Unicode)] static extern bool CreateProcess(string app,StringBuilder command,IntPtr a,IntPtr b,bool inherit,uint flags,IntPtr environment,string directory,ref Startup startup,out ProcessInfo process);
    [DllImport("kernel32.dll")] static extern uint WaitForSingleObject(IntPtr process,uint timeout);
    [DllImport("kernel32.dll")] static extern bool TerminateProcess(IntPtr process,uint code);
    [DllImport("kernel32.dll")] static extern bool CloseHandle(IntPtr handle);
    static IntPtr desktop; static ProcessInfo process;
    public static void Start(string exe,string data) {
        string name="AiMonIdle"+Guid.NewGuid().ToString("N");
        desktop=CreateDesktop(name,IntPtr.Zero,IntPtr.Zero,0,0x1ff,IntPtr.Zero);
        if(desktop==IntPtr.Zero)throw new Exception("CreateDesktop failed");
        var startup=new Startup();startup.size=(uint)Marshal.SizeOf(startup);startup.desktop=name;
        var cmd=new StringBuilder("\""+exe+"\" --no-claude-probe --no-startup-registration --data-dir \""+data+"\"");
        if(!CreateProcess(exe,cmd,IntPtr.Zero,IntPtr.Zero,false,0x08000000,IntPtr.Zero,data,ref startup,out process))
            throw new Exception("CreateProcess failed");
    }
    public static void Stop() {
        if(process.process!=IntPtr.Zero) {
            EnumDesktopWindows(desktop,(h,p)=>{uint pid;GetWindowThreadProcessId(h,out pid);if(pid==process.pid)PostMessage(h,0x111,new IntPtr(104),IntPtr.Zero);return true;},IntPtr.Zero);
            if(WaitForSingleObject(process.process,5000)!=0)TerminateProcess(process.process,1);
            CloseHandle(process.process);CloseHandle(process.thread);
        }
        if(desktop!=IntPtr.Zero)CloseDesktop(desktop);
    }
}
'@
$saved = @{}
foreach ($key in @('PATH','USERPROFILE','LOCALAPPDATA','APPDATA','AI_MON_TEST_CODEX')) { $saved[$key] = [Environment]::GetEnvironmentVariable($key) }
try {
    [Environment]::SetEnvironmentVariable('PATH',$bin)
    foreach ($key in @('USERPROFILE','LOCALAPPDATA','APPDATA')) { [Environment]::SetEnvironmentVariable($key,$data) }
    [Environment]::SetEnvironmentVariable('AI_MON_TEST_CODEX','success')
    [CodexIdleTest]::Start((Join-Path $projectRoot 'out\Release\ai-mon.exe'),$data)
    $deadline = [DateTime]::UtcNow.AddSeconds(155)
    $first = 0
    $second = 0
    while ([DateTime]::UtcNow -lt $deadline) {
        Start-Sleep -Seconds 1
        $statePath = Join-Path $data 'state.json'
        if (-not (Test-Path -LiteralPath $statePath)) { continue }
        $state = Get-Content -LiteralPath $statePath -Raw -Encoding UTF8 | ConvertFrom-Json
        $quota = $state.quotas[1].latest
        if (-not $quota.observed) { continue }
        if ($quota.week.remaining -ne 7800 -or $null -ne $quota.short) { throw 'Main account quota selection failed' }
        if (-not $first) { $first = $quota.observed; Write-Output 'Startup account lookup reached the worker snapshot.' }
        elseif ($quota.observed -gt $first) { $second = $quota.observed; break }
    }
    if (-not $second -or $second - $first -lt 120 -or $second - $first -gt 140) { throw 'Idle quota polling failed its two-minute schedule' }
    if (@(Get-ChildItem -LiteralPath $empty -Force).Count) { throw 'Fixture session directory must remain empty' }
    Write-Output "Codex idle poll PASS: two account lookups $($second-$first)s apart, empty session logs, 300s token scan interval."
} finally {
    [CodexIdleTest]::Stop()
    foreach ($key in $saved.Keys) { [Environment]::SetEnvironmentVariable($key,$saved[$key]) }
}
