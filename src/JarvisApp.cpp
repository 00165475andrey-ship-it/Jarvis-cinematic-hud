#define _WIN32_WINNT 0x0601
#define UNICODE
#define _UNICODE
#include <windows.h>
#include <commctrl.h>
#include <string>
#include <algorithm>
#include <iphlpapi.h>
#include <netioapi.h>
#include <tlhelp32.h>
#include <windowsx.h>
#include <powrprof.h>
#include <psapi.h>
#include <vector>
#include <shellapi.h>
#include <fstream>
#include <ctime>

#pragma comment(lib, "Comctl32.lib")
#pragma comment(lib, "PowrProf.lib")
#pragma comment(lib, "Psapi.lib")
static constexpr wchar_t APP_NAME[]=L"J.A.R.V.I.S. Desktop";
enum { ID_DASH=100, ID_APPEAR=101, ID_BEHAVIOR=102, ID_ABOUT=103, ID_APPLY=110, ID_OPACITY=120, ID_WIDTH=121, ID_HEIGHT=122, ID_POSITION=123, ID_ANIM=124, ID_MOUSE=125, ID_REACTOR=126, ID_DECOR=127, ID_AUTOSTART=128, ID_HUD_TOGGLE=130, ID_MODULE_BASE=140 };
enum Page { DASHBOARD, APPEARANCE, BEHAVIOR, ABOUT };
static constexpr int MODULE_COUNT=6;
static const wchar_t* moduleNames[MODULE_COUNT]={L"Режимы системы",L"Диспетчер процессов",L"Сетевой контроль",L"Диагностика Windows",L"Автоматические сценарии",L"Журнал системы"};
static bool moduleEnabled[MODULE_COUNT]={true,true,false,false,false,false};
struct Config { int opacity=90,width=600,height=380,position=0; bool animations=true,mouse=true,reactor=true,decorations=true,autostart=false; };
static Config cfg; static HWND mainWnd,nav[4],controls[48],applyButton; static Page page=DASHBOARD; static int moduleSettings=-1; static int preferredMode=0,processRefresh=1; static bool modeDefault=false,showSystemProcesses=false,confirmProcessAction=true; static HWND moduleWnd=nullptr,modeCombo=nullptr,processList=nullptr; static GUID previousPowerScheme{}; static bool havePreviousPowerScheme=false;
static HFONT titleFont,normalFont,smallFont; static HWND aboutControls[4]{}; static COLORREF bg=RGB(9,15,25),panel=RGB(16,27,42),cyan=RGB(0,205,245),muted=RGB(135,160,180);
static std::wstring iniPath; static std::wstring logPath, scenarioPath; static int cpuValue=0,ramValue=0; static ULONGLONG oldIdle=0,oldKernel=0,oldUser=0; static bool cpuReady=false;
static std::wstring IniPath(){wchar_t p[MAX_PATH]{};GetModuleFileNameW(nullptr,p,MAX_PATH);std::wstring s(p);auto n=s.find_last_of(L"\\/");return (n==s.npos?L"":s.substr(0,n+1))+L"jarvis.ini";}
static void Load(){for(int i=0;i<MODULE_COUNT;i++){wchar_t key[32];swprintf_s(key,L"Module%dEnabled",i);moduleEnabled[i]=GetPrivateProfileIntW(L"Modules",key,i<2?1:0,iniPath.c_str())!=0;}int enabled=0;for(bool v:moduleEnabled)if(v)enabled++;if(enabled>2){int kept=0;for(int i=0;i<MODULE_COUNT;i++)if(moduleEnabled[i]&&kept++>=2)moduleEnabled[i]=false;}cfg.opacity=GetPrivateProfileIntW(L"Appearance",L"Opacity",90,iniPath.c_str());cfg.width=GetPrivateProfileIntW(L"Appearance",L"Width",600,iniPath.c_str());cfg.height=GetPrivateProfileIntW(L"Appearance",L"Height",380,iniPath.c_str());cfg.position=GetPrivateProfileIntW(L"Appearance",L"Position",0,iniPath.c_str());cfg.animations=GetPrivateProfileIntW(L"Behavior",L"Animations",1,iniPath.c_str())!=0;cfg.mouse=GetPrivateProfileIntW(L"Behavior",L"MouseEffects",1,iniPath.c_str())!=0;cfg.reactor=GetPrivateProfileIntW(L"Behavior",L"Reactor",1,iniPath.c_str())!=0;cfg.decorations=GetPrivateProfileIntW(L"Behavior",L"Decorations",1,iniPath.c_str())!=0;cfg.autostart=GetPrivateProfileIntW(L"Behavior",L"Autostart",0,iniPath.c_str())!=0;preferredMode=GetPrivateProfileIntW(L"SystemMode",L"Preferred",0,iniPath.c_str());processRefresh=GetPrivateProfileIntW(L"ProcessManager",L"RefreshSeconds",1,iniPath.c_str());showSystemProcesses=GetPrivateProfileIntW(L"ProcessManager",L"ShowSystem",0,iniPath.c_str())!=0;confirmProcessAction=GetPrivateProfileIntW(L"ProcessManager",L"ConfirmActions",1,iniPath.c_str())!=0;modeDefault=GetPrivateProfileIntW(L"SystemMode",L"UseAsDefault",0,iniPath.c_str())!=0;preferredMode=std::clamp(preferredMode,0,2);processRefresh=(processRefresh==5?5:processRefresh==2?2:1);}
static void Save(){wchar_t b[32];for(int i=0;i<MODULE_COUNT;i++){wchar_t key[32];swprintf_s(key,L"Module%dEnabled",i);swprintf_s(b,L"%d",moduleEnabled[i]?1:0);WritePrivateProfileStringW(L"Modules",key,b,iniPath.c_str());}auto put=[&](const wchar_t*s,const wchar_t*k,int v){swprintf_s(b,L"%d",v);WritePrivateProfileStringW(s,k,b,iniPath.c_str());};put(L"Appearance",L"Opacity",cfg.opacity);put(L"Appearance",L"Width",cfg.width);put(L"Appearance",L"Height",cfg.height);put(L"Appearance",L"Position",cfg.position);put(L"Behavior",L"Animations",cfg.animations);put(L"Behavior",L"MouseEffects",cfg.mouse);put(L"Behavior",L"Reactor",cfg.reactor);put(L"Behavior",L"Decorations",cfg.decorations);put(L"Behavior",L"Autostart",cfg.autostart);put(L"SystemMode",L"Preferred",preferredMode);put(L"SystemMode",L"UseAsDefault",modeDefault);put(L"ProcessManager",L"RefreshSeconds",processRefresh);put(L"ProcessManager",L"ShowSystem",showSystemProcesses);put(L"ProcessManager",L"ConfirmActions",confirmProcessAction);}
static HWND Make(const wchar_t*cls,const wchar_t*txt,DWORD style,int x,int y,int w,int h,int id){HWND c=CreateWindowExW(0,cls,txt,WS_CHILD|WS_VISIBLE|style,x,y,w,h,mainWnd,(HMENU)(INT_PTR)id,GetModuleHandleW(nullptr),nullptr);SendMessageW(c,WM_SETFONT,(WPARAM)normalFont,TRUE);return c;}
static void Add(int i,HWND h){if(i>=0&&i<48)controls[i]=h;}
static void HideControls(){for(HWND h:controls)if(h)ShowWindow(h,SW_HIDE);for(HWND h:aboutControls)if(h)ShowWindow(h,SW_HIDE);}
static HWND TextAt(const wchar_t*t,int x,int y,int w,int h){return Make(L"STATIC",t,SS_LEFT,x,y,w,h,0);}
static void SetText(int i,const wchar_t*t){if(controls[i])SetWindowTextW(controls[i],t);}
static void LogEvent(const std::wstring& event){std::wofstream f(logPath,std::ios::app);if(f){SYSTEMTIME t{};GetLocalTime(&t);f<<L"["<<t.wYear<<L"-"<<t.wMonth<<L"-"<<t.wDay<<L" "<<t.wHour<<L":"<<t.wMinute<<L":"<<t.wSecond<<L"] "<<event<<L"\\n";}}
static void ShowModulePanel(int idx);
static void RunModule(int idx){
 const wchar_t* names[]={L"Режимы системы",L"Диспетчер процессов",L"Сетевой контроль",L"Диагностика Windows",L"Автоматические сценарии",L"Журнал системы"};
 LogEvent(std::wstring(L"Открыт модуль: ")+names[idx]);
 switch(idx){
 case 0: case 1: ShowModulePanel(idx); break;
 case 2: ShellExecuteW(mainWnd,L"open",L"ncpa.cpl",nullptr,nullptr,SW_SHOWNORMAL); break;
 case 3: ShellExecuteW(mainWnd,L"open",L"perfmon.exe",L"/rel",nullptr,SW_SHOWNORMAL); break;
 case 4: ShellExecuteW(mainWnd,L"open",L"notepad.exe",L"jarvis-scenarios.txt",nullptr,SW_SHOWNORMAL); break;
 case 5: ShellExecuteW(mainWnd,L"open",L"notepad.exe",logPath.c_str(),nullptr,SW_SHOWNORMAL); break;
 }
}
static void ShowPage(Page p){
 page=p;HideControls();for(int i=0;i<4;i++)InvalidateRect(nav[i],nullptr,TRUE);
 if(applyButton)ShowWindow(applyButton,(p==APPEARANCE||p==BEHAVIOR)?SW_SHOW:SW_HIDE);
 if(p==BEHAVIOR&&moduleSettings>=0){if(applyButton)ShowWindow(applyButton,SW_SHOW);for(int i=34;i<=41;i++)if(controls[i])ShowWindow(controls[i],SW_SHOW);if(moduleSettings==0){SetWindowTextW(controls[34],L"← Назад к модулям");SetWindowTextW(controls[35],L"НАСТРОЙКИ: РЕЖИМЫ СИСТЕМЫ");SetWindowTextW(controls[36],L"Предпочтительный режим");SendMessageW(controls[37],CB_RESETCONTENT,0,0);const wchar_t* m[]={L"Сбалансированный",L"Высокая производительность",L"Энергосбережение"};for(auto v:m)SendMessageW(controls[37],CB_ADDSTRING,0,(LPARAM)v);SendMessageW(controls[37],CB_SETCURSEL,preferredMode,0);SetWindowTextW(controls[38],L"Использовать как выбранный режим по умолчанию");SendMessageW(controls[39],BM_SETCHECK,modeDefault?BST_CHECKED:BST_UNCHECKED,0);ShowWindow(controls[36],SW_SHOW);ShowWindow(controls[37],SW_SHOW);ShowWindow(controls[38],SW_SHOW);ShowWindow(controls[39],SW_SHOW);ShowWindow(controls[40],SW_HIDE);ShowWindow(controls[41],SW_HIDE);}else{SetWindowTextW(controls[34],L"← Назад к модулям");SetWindowTextW(controls[35],L"НАСТРОЙКИ: ДИСПЕТЧЕР ПРОЦЕССОВ");SetWindowTextW(controls[36],L"Интервал обновления списка");SendMessageW(controls[37],CB_RESETCONTENT,0,0);const wchar_t* r[]={L"1 секунда",L"2 секунды",L"5 секунд"};for(auto v:r)SendMessageW(controls[37],CB_ADDSTRING,0,(LPARAM)v);SendMessageW(controls[37],CB_SETCURSEL,processRefresh==1?0:processRefresh==2?1:2,0);SetWindowTextW(controls[38],L"Показывать системные процессы");SendMessageW(controls[39],BM_SETCHECK,showSystemProcesses?BST_CHECKED:BST_UNCHECKED,0);SetWindowTextW(controls[40],L"Подтверждать действия с процессами");SendMessageW(controls[41],BM_SETCHECK,confirmProcessAction?BST_CHECKED:BST_UNCHECKED,0);for(int i=36;i<=41;i++)ShowWindow(controls[i],SW_SHOW);}return;}
 if(p==ABOUT){const wchar_t* lines[]={L"J.A.R.V.I.S. Desktop  |  версия 1.0",L"Самостоятельное настольное приложение для Windows 10/11.",L"Настройки сохраняются рядом с EXE в файле jarvis.ini.",L"Windhawk и подключение к интернету для запуска не нужны."};int ys[]={145,205,245,285};for(int i=0;i<4;i++){if(!aboutControls[i])aboutControls[i]=TextAt(lines[i],286,ys[i],650,35);else ShowWindow(aboutControls[i],SW_SHOW);}return;}
 int a=p==DASHBOARD?0:p==APPEARANCE?9:18,b=p==DASHBOARD?8:p==APPEARANCE?17:29;for(int i=a;i<=b;i++)if(controls[i])ShowWindow(controls[i],SW_SHOW);
 if(p==DASHBOARD){int slot=0;for(int i=0;i<MODULE_COUNT;i++)if(moduleEnabled[i]&&slot<2){SetWindowTextW(controls[30+slot],moduleNames[i]);SetWindowTextW(controls[32+slot],L"Открыть инструменты и действия модуля");ShowWindow(controls[30+slot],SW_SHOW);ShowWindow(controls[32+slot],SW_SHOW);SetWindowLongPtrW(controls[30+slot],GWLP_USERDATA,i);slot++;}for(int j=slot;j<2;j++){SetWindowTextW(controls[30+j],L"Модуль не выбран");SetWindowTextW(controls[32+j],L"Выберите модуль в разделе «Поведение»");ShowWindow(controls[30+j],SW_SHOW);ShowWindow(controls[32+j],SW_SHOW);SetWindowLongPtrW(controls[30+j],GWLP_USERDATA,-1);}}
 if(p==APPEARANCE){SendMessageW(controls[11],TBM_SETRANGE,TRUE,MAKELONG(40,100));SendMessageW(controls[11],TBM_SETPOS,TRUE,cfg.opacity);SendMessageW(controls[13],TBM_SETRANGE,TRUE,MAKELONG(450,1100));SendMessageW(controls[13],TBM_SETPOS,TRUE,cfg.width);SendMessageW(controls[15],TBM_SETRANGE,TRUE,MAKELONG(280,700));SendMessageW(controls[15],TBM_SETPOS,TRUE,cfg.height);SendMessageW(controls[17],CB_RESETCONTENT,0,0);const wchar_t*opts[]={L"По центру",L"Сверху слева",L"Сверху справа",L"Снизу слева",L"Снизу справа"};for(auto s:opts)SendMessageW(controls[17],CB_ADDSTRING,0,(LPARAM)s);SendMessageW(controls[17],CB_SETCURSEL,cfg.position,0);}
 if(p==BEHAVIOR){if(controls[43])ShowWindow(controls[43],SW_SHOW);if(controls[44])ShowWindow(controls[44],SW_SHOW);int ids[]={19,20,21,22,23};bool vals[]={cfg.animations,cfg.mouse,cfg.reactor,cfg.decorations,cfg.autostart};for(int i=0;i<5;i++)SendMessageW(controls[ids[i]],BM_SETCHECK,vals[i]?BST_CHECKED:BST_UNCHECKED,0);for(int i=0;i<MODULE_COUNT;i++)SendMessageW(controls[24+i],BM_SETCHECK,moduleEnabled[i]?BST_CHECKED:BST_UNCHECKED,0);}
}
static void UpdateHud();
static void UpdateMetrics(){FILETIME i{},k{},u{};if(GetSystemTimes(&i,&k,&u)){ULARGE_INTEGER a{},b{},c{};a.LowPart=i.dwLowDateTime;a.HighPart=i.dwHighDateTime;b.LowPart=k.dwLowDateTime;b.HighPart=k.dwHighDateTime;c.LowPart=u.dwLowDateTime;c.HighPart=u.dwHighDateTime;if(cpuReady){ULONGLONG total=(b.QuadPart-oldKernel)+(c.QuadPart-oldUser),idle=a.QuadPart-oldIdle;if(total)cpuValue=(int)std::clamp<ULONGLONG>(100-idle*100/total,0,100);}oldIdle=a.QuadPart;oldKernel=b.QuadPart;oldUser=c.QuadPart;cpuReady=true;}MEMORYSTATUSEX m{};m.dwLength=sizeof(m);if(GlobalMemoryStatusEx(&m))ramValue=(int)m.dwMemoryLoad;wchar_t s[64];swprintf_s(s,L"%d%%",cpuValue);SetText(4,s);swprintf_s(s,L"%d%%",ramValue);SetText(5,s);ULARGE_INTEGER f{},t{},tf{};if(GetDiskFreeSpaceExW(L"C:\\",&f,&t,&tf))swprintf_s(s,L"%.1f GB",f.QuadPart/1073741824.0);else wcscpy_s(s,L"N/A");SetText(6,s);UpdateHud();}

static constexpr int HUD_COUNT=9;
static const wchar_t* hudNames[HUD_COUNT]={L"CPU",L"RAM",L"Диск C: свободно",L"Сеть ↓",L"Сеть ↑",L"Время работы",L"Батарея",L"Процессы",L"Часы"};
static const wchar_t* hudKeys[HUD_COUNT]={L"CPU",L"RAM",L"Disk",L"Download",L"Upload",L"Uptime",L"Battery",L"Processes",L"Clock"};
struct HudItem { HWND hwnd=nullptr; int x=-1,y=-1; std::wstring value=L"--"; };
static HudItem hud[HUD_COUNT]; static bool hudVisible=true; static bool hudEditMode=false;
static ULONGLONG netIn=0,netOut=0; static bool netReady=false; static double netDown=0,netUp=0;
static std::wstring RateText(double n){const wchar_t*u[]={L"B/s",L"KB/s",L"MB/s",L"GB/s"};int j=0;while(n>=1024&&j<3){n/=1024;j++;}wchar_t b[64];swprintf_s(b,L"%.1f %s",n,u[j]);return b;}
static void SaveHudPositions(){for(int i=0;i<HUD_COUNT;i++)if(hud[i].hwnd){RECT r{};GetWindowRect(hud[i].hwnd,&r);hud[i].x=r.left;hud[i].y=r.top;wchar_t b[32];swprintf_s(b,L"%d",hud[i].x);WritePrivateProfileStringW(L"HUD", (std::wstring(hudKeys[i])+L"X").c_str(),b,iniPath.c_str());swprintf_s(b,L"%d",hud[i].y);WritePrivateProfileStringW(L"HUD",(std::wstring(hudKeys[i])+L"Y").c_str(),b,iniPath.c_str());}}
static void ApplyHudOpacity(){for(auto&w:hud)if(w.hwnd)SetLayeredWindowAttributes(w.hwnd,RGB(0,0,0),(BYTE)std::clamp(cfg.opacity,10,100),LWA_ALPHA|LWA_COLORKEY);}
static LRESULT CALLBACK HudProc(HWND h,UINT m,WPARAM wp,LPARAM lp){int id=(int)GetWindowLongPtrW(h,GWLP_USERDATA);static bool drag=false;static POINT mouse0{},win0{};switch(m){
case WM_NCCREATE:{auto*c=(CREATESTRUCTW*)lp;SetWindowLongPtrW(h,GWLP_USERDATA,(LONG_PTR)c->lpCreateParams);return TRUE;}
case WM_ERASEBKGND:return 1;
case WM_PAINT:{PAINTSTRUCT ps;HDC dc=BeginPaint(h,&ps);RECT r{};GetClientRect(h,&r);HBRUSH b=CreateSolidBrush(RGB(0,0,0));FillRect(dc,&r,b);DeleteObject(b);SetBkMode(dc,TRANSPARENT);SetTextColor(dc,RGB(75,225,255));SelectObject(dc,normalFont);std::wstring t=std::wstring(hudNames[id])+L"  "+hud[id].value;DrawTextW(dc,t.c_str(),-1,&r,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX);EndPaint(h,&ps);return 0;}
case WM_LBUTTONDOWN:{drag=true;SetCapture(h);GetCursorPos(&mouse0);RECT r{};GetWindowRect(h,&r);win0={r.left,r.top};return 0;}
case WM_MOUSEMOVE:if(drag&&(wp&MK_LBUTTON)){POINT p{};GetCursorPos(&p);SetWindowPos(h,HWND_TOPMOST,win0.x+p.x-mouse0.x,win0.y+p.y-mouse0.y,0,0,SWP_NOSIZE|SWP_NOACTIVATE);return 0;}break;
case WM_LBUTTONUP:if(drag){drag=false;ReleaseCapture();SaveHudPositions();return 0;}break;
}return DefWindowProcW(h,m,wp,lp);}
static void CreateHud(){WNDCLASSEXW wc{sizeof(wc)};wc.lpfnWndProc=HudProc;wc.hInstance=GetModuleHandleW(nullptr);wc.hCursor=LoadCursorW(nullptr,IDC_SIZEALL);wc.lpszClassName=L"JarvisFloatingMetric";RegisterClassExW(&wc);RECT work{};SystemParametersInfoW(SPI_GETWORKAREA,0,&work,0);for(int i=0;i<HUD_COUNT;i++){std::wstring xkey=std::wstring(hudKeys[i])+L"X",ykey=std::wstring(hudKeys[i])+L"Y";hud[i].x=GetPrivateProfileIntW(L"HUD",xkey.c_str(),work.right-250,iniPath.c_str());hud[i].y=GetPrivateProfileIntW(L"HUD",ykey.c_str(),work.top+60+i*44,iniPath.c_str());hud[i].hwnd=CreateWindowExW(WS_EX_LAYERED|WS_EX_TOPMOST|WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE,L"JarvisFloatingMetric",L"",WS_POPUP,hud[i].x,hud[i].y,230,38,nullptr,nullptr,GetModuleHandleW(nullptr),(void*)(INT_PTR)i);SetLayeredWindowAttributes(hud[i].hwnd,RGB(0,0,0),(BYTE)cfg.opacity,LWA_ALPHA|LWA_COLORKEY);SetWindowLongPtrW(hud[i].hwnd,GWL_EXSTYLE,GetWindowLongPtrW(hud[i].hwnd,GWL_EXSTYLE)|WS_EX_TRANSPARENT);SetWindowPos(hud[i].hwnd,HWND_TOPMOST,hud[i].x,hud[i].y,230,38,SWP_NOACTIVATE|SWP_SHOWWINDOW);}}
static void ToggleHud(){hudVisible=!hudVisible;for(auto&w:hud)if(w.hwnd){if(hudVisible)SetWindowPos(w.hwnd,HWND_TOPMOST,w.x,w.y,230,38,SWP_NOACTIVATE|SWP_SHOWWINDOW);else ShowWindow(w.hwnd,SW_HIDE);}}
static void UpdateHud(){wchar_t b[80];swprintf_s(b,L"%d%%",cpuValue);hud[0].value=b;swprintf_s(b,L"%d%%",ramValue);hud[1].value=b;ULARGE_INTEGER f{},t{},tf{};if(GetDiskFreeSpaceExW(L"C:\\",&f,&t,&tf)){swprintf_s(b,L"%.1f GB",f.QuadPart/1073741824.0);hud[2].value=b;}else hud[2].value=L"N/A";
DWORD tableSize=0;GetIfTable(nullptr,&tableSize,FALSE);std::vector<BYTE> tableBuf(tableSize);auto*tab=(MIB_IFTABLE*)tableBuf.data();if(tableSize&&GetIfTable(tab,&tableSize,FALSE)==NO_ERROR){ULONGLONG in=0,out=0;for(DWORD i=0;i<tab->dwNumEntries;i++){auto&r=tab->table[i];if(r.dwType!=MIB_IF_TYPE_LOOPBACK&&r.dwOperStatus==MIB_IF_OPER_STATUS_OPERATIONAL){in+=r.dwInOctets;out+=r.dwOutOctets;}}if(netReady){netDown=(double)(in-netIn);netUp=(double)(out-netOut);}netIn=in;netOut=out;netReady=true;}hud[3].value=RateText(netDown);hud[4].value=RateText(netUp);
ULONGLONG sec=GetTickCount64()/1000;swprintf_s(b,L"%llu д %02llu:%02llu",sec/86400,(sec/3600)%24,(sec/60)%60);hud[5].value=b;SYSTEM_POWER_STATUS ps{};if(GetSystemPowerStatus(&ps)&&ps.BatteryLifePercent!=255){swprintf_s(b,L"%u%%",ps.BatteryLifePercent);hud[6].value=b;}else hud[6].value=L"Нет батареи";
int count=0;HANDLE snap=CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0);if(snap!=INVALID_HANDLE_VALUE){PROCESSENTRY32W pe{};pe.dwSize=sizeof(pe);if(Process32FirstW(snap,&pe))do{count++;}while(Process32NextW(snap,&pe));CloseHandle(snap);}swprintf_s(b,L"%d",count);hud[7].value=b;SYSTEMTIME st{};GetLocalTime(&st);swprintf_s(b,L"%02u:%02u:%02u",st.wHour,st.wMinute,st.wSecond);hud[8].value=b;for(auto&w:hud)if(w.hwnd)InvalidateRect(w.hwnd,nullptr,TRUE);}


static bool ApplyPowerMode(int mode){
 const wchar_t* ids[]={L"{381b4222-f694-41f0-9685-ff5bb260df2e}",L"{8c5e7fda-e8bf-4a96-9a85-a6e23a8c635c}",L"{a1841308-3541-4fab-bc81-f71556f20b4a}"};
 GUID target{}; if(FAILED(CLSIDFromString(ids[std::clamp(mode,0,2)],&target)))return false;
 return PowerSetActiveScheme(nullptr,&target)==ERROR_SUCCESS;
}
static bool IsProtectedProcess(const wchar_t* n,DWORD pid){
 if(pid<=4)return true;
 const wchar_t* blocked[]={L"csrss.exe",L"wininit.exe",L"winlogon.exe",L"services.exe",L"lsass.exe",L"smss.exe",L"system",L"registry"};
 for(auto x:blocked)if(_wcsicmp(n,x)==0)return true;return false;
}
static void RefreshProcessList(){
 if(!processList)return;SendMessageW(processList,LB_RESETCONTENT,0,0);
 HANDLE snap=CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0);if(snap==INVALID_HANDLE_VALUE)return;
 PROCESSENTRY32W pe{};pe.dwSize=sizeof(pe);
 if(Process32FirstW(snap,&pe))do{
  if(!showSystemProcesses&&(pe.th32ProcessID<=4||_wcsicmp(pe.szExeFile,L"System")==0))continue;
  SIZE_T mem=0;HANDLE p=OpenProcess(PROCESS_QUERY_INFORMATION|PROCESS_VM_READ,FALSE,pe.th32ProcessID);
  if(p){PROCESS_MEMORY_COUNTERS pm{};pm.cb=sizeof(pm);if(GetProcessMemoryInfo(p,&pm,sizeof(pm)))mem=pm.WorkingSetSize;CloseHandle(p);}
  wchar_t line[512];swprintf_s(line,L"%-30s PID %-7lu %8.1f MB",pe.szExeFile,pe.th32ProcessID,(double)mem/1048576.0);
  int at=(int)SendMessageW(processList,LB_ADDSTRING,0,(LPARAM)line);SendMessageW(processList,LB_SETITEMDATA,at,pe.th32ProcessID);
 }while(Process32NextW(snap,&pe));CloseHandle(snap);
}
static void ShowModulePanel(int idx){
 if(moduleWnd){DestroyWindow(moduleWnd);moduleWnd=nullptr;modeCombo=processList=nullptr;}
 moduleWnd=CreateWindowExW(WS_EX_TOPMOST|WS_EX_TOOLWINDOW,L"JarvisModulePanel",idx==0?L"J.A.R.V.I.S. — Системные режимы":L"J.A.R.V.I.S. — Диспетчер процессов",WS_POPUP|WS_CAPTION|WS_SYSMENU|WS_VISIBLE,0,0,idx==0?520:720,idx==0?300:550,mainWnd,nullptr,GetModuleHandleW(nullptr),nullptr);
 if(!moduleWnd)return;RECT w{};SystemParametersInfoW(SPI_GETWORKAREA,0,&w,0);SetWindowPos(moduleWnd,HWND_TOPMOST,w.right-(idx==0?540:740),w.top+80,idx==0?520:720,idx==0?300:550,SWP_SHOWWINDOW);
 if(idx==0){
  CreateWindowW(L"STATIC",L"Выберите режим питания Windows:",WS_CHILD|WS_VISIBLE,24,25,450,28,moduleWnd,nullptr,nullptr,nullptr);
  modeCombo=CreateWindowW(L"COMBOBOX",L"",WS_CHILD|WS_VISIBLE|CBS_DROPDOWNLIST,24,65,450,180,moduleWnd,(HMENU)501,GetModuleHandleW(nullptr),nullptr);
  const wchar_t* names[]={L"Сбалансированный",L"Высокая производительность",L"Энергосбережение"};for(auto n:names)SendMessageW(modeCombo,CB_ADDSTRING,0,(LPARAM)n);SendMessageW(modeCombo,CB_SETCURSEL,preferredMode,0);
  CreateWindowW(L"BUTTON",L"Применить режим",WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON,24,125,200,42,moduleWnd,(HMENU)502,GetModuleHandleW(nullptr),nullptr);
  CreateWindowW(L"BUTTON",L"Вернуть предыдущий режим",WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON,240,125,230,42,moduleWnd,(HMENU)503,GetModuleHandleW(nullptr),nullptr);
  CreateWindowW(L"STATIC",L"Меняется активная схема электропитания Windows. Доступность зависит от конфигурации системы.",WS_CHILD|WS_VISIBLE,24,190,450,52,moduleWnd,nullptr,nullptr,nullptr);
 }else{
  processList=CreateWindowExW(WS_EX_CLIENTEDGE,L"LISTBOX",L"",WS_CHILD|WS_VISIBLE|LBS_NOTIFY|WS_VSCROLL|LBS_NOINTEGRALHEIGHT,18,18,665,440,moduleWnd,(HMENU)511,GetModuleHandleW(nullptr),nullptr);
  CreateWindowW(L"BUTTON",L"Обновить",WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON,18,472,140,38,moduleWnd,(HMENU)512,GetModuleHandleW(nullptr),nullptr);
  CreateWindowW(L"BUTTON",L"Завершить выбранный процесс",WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON,175,472,260,38,moduleWnd,(HMENU)513,GetModuleHandleW(nullptr),nullptr);
  RefreshProcessList();SetTimer(moduleWnd,1,processRefresh*1000,nullptr);
 }
}
static LRESULT CALLBACK ModulePanelProc(HWND h,UINT m,WPARAM wp,LPARAM lp){
 if(m==WM_COMMAND){switch(LOWORD(wp)){
 case 502:{int mode=(int)SendMessageW(modeCombo,CB_GETCURSEL,0,0);if(mode<0)return 0;if(!havePreviousPowerScheme){GUID* active=nullptr;if(PowerGetActiveScheme(nullptr,&active)==ERROR_SUCCESS&&active){previousPowerScheme=*active;havePreviousPowerScheme=true;LocalFree(active);}}if(ApplyPowerMode(mode)){preferredMode=mode;Save();MessageBoxW(h,L"Режим электропитания Windows изменён.",APP_NAME,MB_OK|MB_ICONINFORMATION);}else MessageBoxW(h,L"Windows не смогла применить эту схему питания. Возможно, она отсутствует.",APP_NAME,MB_OK|MB_ICONWARNING);return 0;}
 case 503:if(havePreviousPowerScheme&&PowerSetActiveScheme(nullptr,&previousPowerScheme)==ERROR_SUCCESS)MessageBoxW(h,L"Предыдущая схема питания восстановлена.",APP_NAME,MB_OK|MB_ICONINFORMATION);else MessageBoxW(h,L"Предыдущая схема не сохранена или восстановить её не удалось.",APP_NAME,MB_OK|MB_ICONWARNING);return 0;
 case 512:RefreshProcessList();return 0;
 case 513:{int sel=(int)SendMessageW(processList,LB_GETCURSEL,0,0);if(sel==LB_ERR)return 0;DWORD pid=(DWORD)SendMessageW(processList,LB_GETITEMDATA,sel,0);wchar_t line[512]{};SendMessageW(processList,LB_GETTEXT,sel,(LPARAM)line);wchar_t* name=line;while(*name==L' ')name++;if(IsProtectedProcess(name,pid)){MessageBoxW(h,L"Этот системный процесс защищён от завершения.",APP_NAME,MB_OK|MB_ICONWARNING);return 0;}if(confirmProcessAction&&MessageBoxW(h,L"Завершить выбранный процесс? Несохранённые данные могут быть потеряны.",APP_NAME,MB_YESNO|MB_ICONWARNING)!=IDYES)return 0;HANDLE p=OpenProcess(PROCESS_TERMINATE,FALSE,pid);if(!p||!TerminateProcess(p,1)){if(p)CloseHandle(p);MessageBoxW(h,L"Не удалось завершить процесс. Возможно, нужны права администратора.",APP_NAME,MB_OK|MB_ICONWARNING);}else{CloseHandle(p);LogEvent(L"Завершён процесс PID "+std::to_wstring(pid));RefreshProcessList();}return 0;}
 }}
 if(m==WM_TIMER){RefreshProcessList();return 0;}
 if(m==WM_DESTROY){KillTimer(h,1);if(moduleWnd==h){moduleWnd=nullptr;modeCombo=processList=nullptr;}return 0;}
 return DefWindowProcW(h,m,wp,lp);
}

static void Startup(bool on){HKEY k{};if(RegOpenKeyExW(HKEY_CURRENT_USER,L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",0,KEY_SET_VALUE,&k)!=ERROR_SUCCESS)return;if(on){wchar_t p[MAX_PATH]{};GetModuleFileNameW(nullptr,p,MAX_PATH);RegSetValueExW(k,L"JARVIS Desktop",0,REG_SZ,(BYTE*)p,(DWORD)((wcslen(p)+1)*sizeof(wchar_t)));}else RegDeleteValueW(k,L"JARVIS Desktop");RegCloseKey(k);}
static void Card(HDC dc,int x,int y,int w,int h){HBRUSH b=CreateSolidBrush(panel);RECT r{x,y,x+w,y+h};FillRect(dc,&r,b);DeleteObject(b);HPEN p=CreatePen(PS_SOLID,1,RGB(31,65,86));HGDIOBJ old=SelectObject(dc,p);SelectObject(dc,GetStockObject(NULL_BRUSH));RoundRect(dc,x,y,x+w,y+h,16,16);SelectObject(dc,old);DeleteObject(p);}
static LRESULT CALLBACK Proc(HWND h,UINT m,WPARAM wp,LPARAM lp){
 switch(m){
 case WM_CREATE:{
 mainWnd=h;
 titleFont=CreateFontW(28,0,0,0,FW_SEMIBOLD,0,0,0,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Segoe UI");normalFont=CreateFontW(16,0,0,0,FW_NORMAL,0,0,0,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Segoe UI");smallFont=CreateFontW(12,0,0,0,FW_NORMAL,0,0,0,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Segoe UI");
 const wchar_t*names[]={L"Обзор",L"Внешний вид",L"Поведение",L"О программе"};for(int i=0;i<4;i++)nav[i]=CreateWindowExW(0,L"BUTTON",names[i],WS_CHILD|WS_VISIBLE|BS_OWNERDRAW,24,150+i*58,210,44,h,(HMENU)(INT_PTR)(ID_DASH+i),GetModuleHandleW(nullptr),nullptr);
 Add(0,Make(L"STATIC",L"ЗАГРУЗКА СИСТЕМЫ",SS_LEFT,280,112,500,36,0));Add(1,Make(L"STATIC",L"CPU",SS_LEFT,286,190,130,25,0));Add(2,Make(L"STATIC",L"RAM",SS_LEFT,530,190,130,25,0));Add(3,Make(L"STATIC",L"ДИСК C: СВОБОДНО",SS_LEFT,774,190,230,25,0));Add(4,Make(L"STATIC",L"0%",SS_LEFT,286,225,200,58,0));Add(5,Make(L"STATIC",L"0%",SS_LEFT,530,225,200,58,0));Add(6,Make(L"STATIC",L"-- GB",SS_LEFT,774,225,220,58,0));Add(7,Make(L"STATIC",L"ARC REACTOR  /  SYSTEM CORE",SS_LEFT,286,360,650,28,0));Add(8,Make(L"STATIC",L"Система готова к работе. Показатели обновляются автоматически.",SS_LEFT,286,405,650,30,0));
 Add(9,Make(L"STATIC",L"ВНЕШНИЙ ВИД",SS_LEFT,280,112,500,36,0));Add(10,Make(L"STATIC",L"Прозрачность панели",SS_LEFT,286,180,230,24,0));Add(11,Make(TRACKBAR_CLASSW,L"",TBS_AUTOTICKS|TBS_HORZ,520,174,350,34,ID_OPACITY));Add(12,Make(L"STATIC",L"Ширина окна",SS_LEFT,286,245,230,24,0));Add(13,Make(TRACKBAR_CLASSW,L"",TBS_AUTOTICKS|TBS_HORZ,520,239,350,34,ID_WIDTH));Add(14,Make(L"STATIC",L"Высота окна",SS_LEFT,286,310,230,24,0));Add(15,Make(TRACKBAR_CLASSW,L"",TBS_AUTOTICKS|TBS_HORZ,520,304,350,34,ID_HEIGHT));Add(16,Make(L"STATIC",L"Расположение",SS_LEFT,286,375,230,24,0));Add(17,Make(L"COMBOBOX",L"",CBS_DROPDOWNLIST|WS_VSCROLL,520,369,350,220,ID_POSITION));
 Add(18,Make(L"STATIC",L"ПОВЕДЕНИЕ",SS_LEFT,280,112,500,36,0));Add(19,Make(L"BUTTON",L"Анимация интерфейса",BS_AUTOCHECKBOX,290,185,330,34,ID_ANIM));Add(20,Make(L"BUTTON",L"Реакция на курсор",BS_AUTOCHECKBOX,290,235,330,34,ID_MOUSE));Add(21,Make(L"BUTTON",L"Отображать ARC Reactor",BS_AUTOCHECKBOX,290,285,330,34,ID_REACTOR));Add(22,Make(L"BUTTON",L"Дополнительная графика",BS_AUTOCHECKBOX,290,335,330,34,ID_DECOR));Add(23,Make(L"BUTTON",L"Запускать вместе с Windows",BS_AUTOCHECKBOX,290,385,360,34,ID_AUTOSTART));
 Add(24,Make(L"BUTTON",L"Режимы системы",BS_AUTOCHECKBOX,650,175,210,30,ID_MODULE_BASE));HWND sm=Make(L"BUTTON",L"Настроить",BS_PUSHBUTTON,870,175,105,28,171);Add(43,sm);
 Add(25,Make(L"BUTTON",L"Диспетчер процессов",BS_AUTOCHECKBOX,650,210,210,30,ID_MODULE_BASE+1));HWND pm=Make(L"BUTTON",L"Настроить",BS_PUSHBUTTON,870,210,105,28,172);Add(44,pm);
 Add(26,Make(L"BUTTON",L"Сетевой контроль",BS_AUTOCHECKBOX,650,245,330,30,ID_MODULE_BASE+2));
 Add(27,Make(L"BUTTON",L"Диагностика Windows",BS_AUTOCHECKBOX,650,280,330,30,ID_MODULE_BASE+3));
 Add(28,Make(L"BUTTON",L"Автоматические сценарии",BS_AUTOCHECKBOX,650,315,330,30,ID_MODULE_BASE+4));
 Add(29,Make(L"BUTTON",L"Журнал системы",BS_AUTOCHECKBOX,650,350,330,30,ID_MODULE_BASE+5));
 Add(30,Make(L"BUTTON",L"Модуль 1",BS_PUSHBUTTON,300,500,300,42,ID_MODULE_BASE+20));Add(31,Make(L"BUTTON",L"Модуль 2",BS_PUSHBUTTON,650,500,300,42,ID_MODULE_BASE+21));Add(32,Make(L"STATIC",L"",SS_LEFT,300,548,300,32,0));Add(33,Make(L"STATIC",L"",SS_LEFT,650,548,300,32,0));
 Add(34,Make(L"BUTTON",L"← Назад к модулям",BS_PUSHBUTTON,290,165,190,36,170));Add(35,Make(L"STATIC",L"НАСТРОЙКИ МОДУЛЯ",SS_LEFT,290,220,650,34,0));Add(36,Make(L"STATIC",L"Параметр",SS_LEFT,310,285,260,30,0));Add(37,Make(L"COMBOBOX",L"",CBS_DROPDOWNLIST|WS_VSCROLL,590,278,360,150,0));Add(38,Make(L"BUTTON",L"Дополнительная настройка",BS_AUTOCHECKBOX,310,350,560,34,0));Add(39,Make(L"BUTTON",L"Дополнительная настройка",BS_AUTOCHECKBOX,310,405,560,34,0));Add(40,Make(L"BUTTON",L"Дополнительная настройка",BS_AUTOCHECKBOX,310,460,560,34,0));Add(41,Make(L"BUTTON",L"Дополнительная настройка",BS_AUTOCHECKBOX,310,515,560,34,0));
 CreateHud();UpdateHud();RegisterHotKey(h,1,MOD_CONTROL|MOD_ALT,'J');HWND hudBtn=CreateWindowExW(0,L"BUTTON",L"ПОКАЗАТЬ / СКРЫТЬ ВИДЖЕТЫ",WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON,790,620,230,42,h,(HMENU)ID_HUD_TOGGLE,GetModuleHandleW(nullptr),nullptr);SendMessageW(hudBtn,WM_SETFONT,(WPARAM)normalFont,TRUE);applyButton=CreateWindowExW(0,L"BUTTON",L"СОХРАНИТЬ НАСТРОЙКИ",WS_CHILD|BS_PUSHBUTTON,790,560,230,46,h,(HMENU)ID_APPLY,GetModuleHandleW(nullptr),nullptr);SendMessageW(applyButton,WM_SETFONT,(WPARAM)normalFont,TRUE);ShowPage(DASHBOARD);SetTimer(h,1,1000,nullptr);UpdateMetrics();return 0;}
 case WM_DRAWITEM:{auto*d=(DRAWITEMSTRUCT*)lp;int idx=(int)d->CtlID-ID_DASH;if(idx>=0&&idx<4){HBRUSH b=CreateSolidBrush((int)page==idx?RGB(0,70,91):panel);FillRect(d->hDC,&d->rcItem,b);DeleteObject(b);SetBkMode(d->hDC,TRANSPARENT);SetTextColor(d->hDC,(int)page==idx?RGB(100,230,255):RGB(190,205,218));const wchar_t*t[]={L"Обзор",L"Внешний вид",L"Поведение",L"О программе"};DrawTextW(d->hDC,t[idx],-1,&d->rcItem,DT_CENTER|DT_VCENTER|DT_SINGLELINE);return TRUE;}break;}
 case WM_COMMAND:{int id=LOWORD(wp);if(id>=ID_DASH&&id<=ID_ABOUT){moduleSettings=-1;ShowPage((Page)(id-ID_DASH));InvalidateRect(h,nullptr,TRUE);return 0;}if(id==ID_HUD_TOGGLE){ToggleHud();return 0;}if(id==170){moduleSettings=-1;ShowPage(BEHAVIOR);return 0;}if(id==171||id==172){moduleSettings=id==171?0:1;ShowPage(BEHAVIOR);return 0;}if(id==ID_MODULE_BASE+20||id==ID_MODULE_BASE+21){int idx=(int)GetWindowLongPtrW(controls[id-(ID_MODULE_BASE+20)+30],GWLP_USERDATA);if(idx>=0&&idx<MODULE_COUNT)RunModule(idx);else MessageBoxW(h,L"Сначала выберите модуль в разделе «Поведение».",APP_NAME,MB_OK|MB_ICONINFORMATION);return 0;}if(id>=ID_MODULE_BASE&&id<ID_MODULE_BASE+MODULE_COUNT){int idx=id-ID_MODULE_BASE;bool requested=SendMessageW(controls[24+idx],BM_GETCHECK,0,0)==BST_CHECKED;int active=0;for(int i=0;i<MODULE_COUNT;i++)if(i!=idx&&SendMessageW(controls[24+i],BM_GETCHECK,0,0)==BST_CHECKED)active++;if(requested&&active>=2){SendMessageW(controls[24+idx],BM_SETCHECK,BST_UNCHECKED,0);MessageBoxW(h,L"На главной странице одновременно могут быть включены только два модуля. Сначала отключите один из выбранных.",APP_NAME,MB_OK|MB_ICONINFORMATION);}return 0;}if(id==ID_APPLY){if(page==BEHAVIOR&&moduleSettings>=0){if(moduleSettings==0){preferredMode=(int)SendMessageW(controls[37],CB_GETCURSEL,0,0);if(preferredMode<0)preferredMode=0;modeDefault=SendMessageW(controls[39],BM_GETCHECK,0,0)==BST_CHECKED;}else{int n=(int)SendMessageW(controls[37],CB_GETCURSEL,0,0);processRefresh=n==1?2:n==2?5:1;showSystemProcesses=SendMessageW(controls[39],BM_GETCHECK,0,0)==BST_CHECKED;confirmProcessAction=SendMessageW(controls[41],BM_GETCHECK,0,0)==BST_CHECKED;}Save();MessageBoxW(h,L"Параметры модуля сохранены в jarvis.ini.",APP_NAME,MB_OK|MB_ICONINFORMATION);moduleSettings=-1;ShowPage(BEHAVIOR);return 0;}if(page==APPEARANCE){cfg.opacity=(int)SendMessageW(controls[11],TBM_GETPOS,0,0);cfg.width=(int)SendMessageW(controls[13],TBM_GETPOS,0,0);cfg.height=(int)SendMessageW(controls[15],TBM_GETPOS,0,0);cfg.position=(int)SendMessageW(controls[17],CB_GETCURSEL,0,0);}if(page==BEHAVIOR){int ids[]={19,20,21,22,23};bool*v[]={&cfg.animations,&cfg.mouse,&cfg.reactor,&cfg.decorations,&cfg.autostart};for(int i=0;i<5;i++)*v[i]=SendMessageW(controls[ids[i]],BM_GETCHECK,0,0)==BST_CHECKED;for(int i=0;i<MODULE_COUNT;i++)moduleEnabled[i]=SendMessageW(controls[24+i],BM_GETCHECK,0,0)==BST_CHECKED;Startup(cfg.autostart);}Save();ApplyHudOpacity();MessageBoxW(h,L"Настройки сохранены в jarvis.ini.",APP_NAME,MB_OK|MB_ICONINFORMATION);return 0;}break;}
 case WM_TIMER:UpdateMetrics();InvalidateRect(h,nullptr,FALSE);return 0;
 case WM_HOTKEY:if(wp==1){hudEditMode=!hudEditMode;for(auto&w:hud)if(w.hwnd){LONG_PTR ex=GetWindowLongPtrW(w.hwnd,GWL_EXSTYLE);if(hudEditMode)ex&=~(LONG_PTR)WS_EX_TRANSPARENT;else ex|=WS_EX_TRANSPARENT;SetWindowLongPtrW(w.hwnd,GWL_EXSTYLE,ex);SetWindowPos(w.hwnd,nullptr,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE|SWP_FRAMECHANGED);}return 0;}break;
 case WM_PAINT:{PAINTSTRUCT ps;HDC dc=BeginPaint(h,&ps);RECT r;GetClientRect(h,&r);HBRUSH b=CreateSolidBrush(bg);FillRect(dc,&r,b);DeleteObject(b);RECT s{0,0,250,r.bottom};b=CreateSolidBrush(RGB(12,21,34));FillRect(dc,&s,b);DeleteObject(b);SetBkMode(dc,TRANSPARENT);SetTextColor(dc,cyan);SelectObject(dc,titleFont);TextOutW(dc,28,35,L"J.A.R.V.I.S.",11);SetTextColor(dc,muted);SelectObject(dc,smallFont);TextOutW(dc,30,72,L"DESKTOP CONTROL CENTER",22);SelectObject(dc,normalFont);SetTextColor(dc,RGB(220,235,245));const wchar_t*head[]={L"Состояние системы",L"Настройка внешнего вида",L"Параметры поведения",L"Информация"};TextOutW(dc,280,42,head[page],(int)wcslen(head[page]));if(page==DASHBOARD){Card(dc,270,155,220,165);Card(dc,515,155,220,165);Card(dc,760,155,260,165);Card(dc,270,340,750,140);}else if(page==APPEARANCE||page==BEHAVIOR)Card(dc,270,150,750,420);EndPaint(h,&ps);return 0;}
 case WM_CTLCOLORSTATIC:case WM_CTLCOLORBTN:{HDC d=(HDC)wp;SetTextColor(d,RGB(210,225,238));SetBkColor(d,panel);static HBRUSH br=CreateSolidBrush(panel);return (LRESULT)br;}
 case WM_DESTROY:UnregisterHotKey(h,1);KillTimer(h,1);SaveHudPositions();for(auto&w:hud)if(w.hwnd)DestroyWindow(w.hwnd);PostQuitMessage(0);return 0;
 }return DefWindowProcW(h,m,wp,lp);
}
int WINAPI wWinMain(HINSTANCE inst,HINSTANCE,PWSTR,int show){iniPath=IniPath();logPath=iniPath.substr(0,iniPath.find_last_of(L"\\/"))+L"jarvis.log";scenarioPath=iniPath.substr(0,iniPath.find_last_of(L"\\/"))+L"jarvis-scenarios.txt";Load();INITCOMMONCONTROLSEX ic{sizeof(ic),ICC_BAR_CLASSES};InitCommonControlsEx(&ic);WNDCLASSEXW panelClass{sizeof(panelClass)};panelClass.lpfnWndProc=ModulePanelProc;panelClass.hInstance=inst;panelClass.lpszClassName=L"JarvisModulePanel";panelClass.hCursor=LoadCursorW(nullptr,IDC_ARROW);RegisterClassExW(&panelClass);WNDCLASSEXW wc{sizeof(wc)};wc.lpfnWndProc=Proc;wc.hInstance=inst;wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);wc.hbrBackground=CreateSolidBrush(bg);wc.lpszClassName=L"JarvisDesktopWindow";wc.hIcon=LoadIconW(nullptr,IDI_APPLICATION);if(!RegisterClassExW(&wc))return 1;mainWnd=CreateWindowExW(0,wc.lpszClassName,APP_NAME,WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN,CW_USEDEFAULT,CW_USEDEFAULT,1120,760,nullptr,nullptr,inst,nullptr);if(!mainWnd)return 2;ShowWindow(mainWnd,show);UpdateWindow(mainWnd);MSG msg;while(GetMessageW(&msg,nullptr,0,0)>0){TranslateMessage(&msg);DispatchMessageW(&msg);}return (int)msg.wParam;}
