#include <windows.h>
#include <windowsx.h>
#include <mmsystem.h>
#include <shellapi.h>
#include <string>
#include <vector>
#include <algorithm>
#include <thread>
#include <atomic>
#include <mutex>
#include <cwctype>
#include <cstring>

#pragma comment(lib, "Winmm.lib")
#pragma comment(lib, "Shell32.lib")

namespace {
constexpr UINT WM_VOICE_STATUS = WM_APP + 71;
constexpr int ID_VOICE_START=701, ID_VOICE_STOP=702, ID_VOICE_PHRASE=703,
              ID_VOICE_TARGET=704, ID_VOICE_ADD=705, ID_VOICE_LIST=706,
              ID_VOICE_DELETE=707;
using ModelNew = void* (__cdecl*)(const char*);
using ModelFree = void (__cdecl*)(void*);
using RecNew = void* (__cdecl*)(void*, float);
using RecFree = void (__cdecl*)(void*);
using Accept = int (__cdecl*)(void*, const char*, int);
using Result = const char* (__cdecl*)(void*);
struct Api { HMODULE dll{}; ModelNew modelNew{}; ModelFree modelFree{}; RecNew recNew{}; RecFree recFree{}; Accept accept{}; Result result{}; };
struct Macro { std::wstring phrase, target; };
HWND g_window{};
std::wstring g_base, g_macroFile;
std::vector<Macro> g_macros;
std::mutex g_macroMutex;
std::thread g_worker;
std::atomic<bool> g_running{false}, g_stop{false};
std::wstring g_status=L"Распознавание выключено";
void SetStatus(const std::wstring& s) { g_status=s; if(g_window) PostMessageW(g_window,WM_VOICE_STATUS,0,0); }
std::wstring TrimLower(std::wstring s) {
 while(!s.empty()&&iswspace(s.front())) s.erase(s.begin());
 while(!s.empty()&&iswspace(s.back())) s.pop_back();
 std::transform(s.begin(),s.end(),s.begin(),[](wchar_t c){return (wchar_t)towlower(c);});
 return s;
}
std::wstring Utf8ToWide(const std::string& s) {
 if(s.empty()) return L"";
 int n=MultiByteToWideChar(CP_UTF8,0,s.c_str(),(int)s.size(),nullptr,0);
 std::wstring out(n,L'\0'); if(n) MultiByteToWideChar(CP_UTF8,0,s.c_str(),(int)s.size(),&out[0],n); return out;
}
std::string WideToUtf8(const std::wstring& s) {
 if(s.empty()) return {};
 int n=WideCharToMultiByte(CP_UTF8,0,s.c_str(),(int)s.size(),nullptr,0,nullptr,nullptr);
 std::string out(n,'\0'); if(n) WideCharToMultiByte(CP_UTF8,0,s.c_str(),(int)s.size(),&out[0],n,nullptr,nullptr); return out;
}
bool ReadAll(const std::wstring& path,std::string& data) {
 HANDLE f=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
 if(f==INVALID_HANDLE_VALUE)return false; DWORD size=GetFileSize(f,nullptr); if(size==INVALID_FILE_SIZE||size>1024*1024){CloseHandle(f);return false;}
 data.resize(size); DWORD got=0; BOOL ok=ReadFile(f,data.empty()?nullptr:&data[0],size,&got,nullptr);CloseHandle(f);data.resize(got);return ok!=FALSE;
}
bool WriteAll(const std::wstring& path,const std::string& data) {
 HANDLE f=CreateFileW(path.c_str(),GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
 if(f==INVALID_HANDLE_VALUE)return false;DWORD wrote=0;BOOL ok=WriteFile(f,data.data(),(DWORD)data.size(),&wrote,nullptr);CloseHandle(f);return ok&&wrote==data.size();
}
void LoadMacros() {
 std::string raw; if(!ReadAll(g_macroFile,raw))return; std::wstring all=Utf8ToWide(raw); size_t p=0;
 std::lock_guard<std::mutex> lock(g_macroMutex);g_macros.clear();
 while(p<all.size()&&g_macros.size()<100){size_t e=all.find(L'\n',p);if(e==std::wstring::npos)e=all.size();std::wstring line=all.substr(p,e-p);p=e+1;
  size_t sep=line.find(L'|');if(sep==std::wstring::npos)continue;Macro m{line.substr(0,sep),line.substr(sep+1)};
  if(!m.phrase.empty()&&!m.target.empty()&&m.phrase.find_first_of(L"\r\n|")==std::wstring::npos&&m.target.find_first_of(L"\r\n|")==std::wstring::npos)g_macros.push_back(m);
 }
}
bool SaveMacros() {
 std::wstring all;{std::lock_guard<std::mutex> lock(g_macroMutex);for(const auto&m:g_macros)all+=m.phrase+L"|"+m.target+L"\n";}
 return WriteAll(g_macroFile,WideToUtf8(all));
}
void ExecuteExact(const std::wstring& recognized) {
 const std::wstring key=TrimLower(recognized); if(key.empty())return;
 Macro found;bool matched=false;
 {std::lock_guard<std::mutex> lock(g_macroMutex);for(const auto&m:g_macros)if(TrimLower(m.phrase)==key){found=m;matched=true;break;}}
 if(!matched)return;
 std::wstring target=found.target;
 if(target.find(L"://")!=std::wstring::npos && target.rfind(L"https://",0)!=0 && target.rfind(L"http://",0)!=0){SetStatus(L"Команда отклонена: разрешены только HTTP/HTTPS");return;}
 HINSTANCE result=ShellExecuteW(g_window,L"open",target.c_str(),nullptr,nullptr,SW_SHOWNORMAL);
 SetStatus((INT_PTR)result>32?L"Команда выполнена: "+found.phrase:L"Не удалось открыть цель макроса");
}
bool LoadApi(Api& a) {
 SetDllDirectoryW(g_base.c_str()); std::wstring dllPath=g_base+L"\\libvosk.dll";a.dll=LoadLibraryW(dllPath.c_str());
 if(!a.dll)return false;
 a.modelNew=(ModelNew)GetProcAddress(a.dll,"vosk_model_new");
 a.modelFree=(ModelFree)GetProcAddress(a.dll,"vosk_model_free");
 a.recNew=(RecNew)GetProcAddress(a.dll,"vosk_recognizer_new");
 a.recFree=(RecFree)GetProcAddress(a.dll,"vosk_recognizer_free");
 a.accept=(Accept)GetProcAddress(a.dll,"vosk_recognizer_accept_waveform");
 a.result=(Result)GetProcAddress(a.dll,"vosk_recognizer_result");
 return a.modelNew&&a.modelFree&&a.recNew&&a.recFree&&a.accept&&a.result;
}
void Worker() {
 Api api{}; void* model=nullptr; void* rec=nullptr; HWAVEIN input=nullptr; HANDLE event=CreateEventW(nullptr,FALSE,FALSE,nullptr);
 std::vector<std::vector<char>> buffers(4,std::vector<char>(8192)); std::vector<WAVEHDR> headers(4);
 auto cleanup=[&](){if(input){waveInStop(input);waveInReset(input);for(auto&h:headers)if(h.dwFlags&WHDR_PREPARED)waveInUnprepareHeader(input,&h,sizeof(WAVEHDR));waveInClose(input);input=nullptr;}if(rec&&api.recFree)api.recFree(rec);if(model&&api.modelFree)api.modelFree(model);if(api.dll)FreeLibrary(api.dll);if(event)CloseHandle(event);SetDllDirectoryW(nullptr);};
 if(!LoadApi(api)){SetStatus(L"Не найдена libvosk.dll или её зависимости");cleanup();g_running=false;return;}
 std::wstring modelPath=g_base+L"\\models\\vosk-model-small-ru-0.22";
 std::string modelUtf8=WideToUtf8(modelPath);model=api.modelNew(modelUtf8.c_str());
 if(!model){SetStatus(L"Не удалось загрузить русскую модель Vosk");cleanup();g_running=false;return;}
 rec=api.recNew(model,16000.0f);if(!rec){SetStatus(L"Не удалось создать распознаватель Vosk");cleanup();g_running=false;return;}
 WAVEFORMATEX fmt{};fmt.wFormatTag=WAVE_FORMAT_PCM;fmt.nChannels=1;fmt.nSamplesPerSec=16000;fmt.wBitsPerSample=16;fmt.nBlockAlign=2;fmt.nAvgBytesPerSec=32000;
 MMRESULT mr=waveInOpen(&input,WAVE_MAPPER,&fmt,(DWORD_PTR)event,0,CALLBACK_EVENT);
 if(mr!=MMSYSERR_NOERROR){SetStatus(L"Микрофон недоступен. Проверьте разрешения Windows.");cleanup();g_running=false;return;}
 for(size_t i=0;i<headers.size();++i){headers[i]={};headers[i].lpData=buffers[i].data();headers[i].dwBufferLength=(DWORD)buffers[i].size();waveInPrepareHeader(input,&headers[i],sizeof(WAVEHDR));waveInAddBuffer(input,&headers[i],sizeof(WAVEHDR));}
 if(waveInStart(input)!=MMSYSERR_NOERROR){SetStatus(L"Не удалось включить микрофон");cleanup();g_running=false;return;}
 SetStatus(L"Микрофон активен — ожидание точной голосовой команды");
 while(!g_stop.load()){
  DWORD wait=WaitForSingleObject(event,250);if(wait!=WAIT_OBJECT_0)continue;
  for(size_t i=0;i<headers.size();++i){WAVEHDR& h=headers[i];if(!(h.dwFlags&WHDR_DONE))continue;
   if(h.dwBytesRecorded>0){api.accept(rec,h.lpData,(int)h.dwBytesRecorded);const char* json=api.result(rec);if(json){std::string j=json;size_t p=j.find("\"text\"");if(p!=std::string::npos){p=j.find(':',p);p=j.find('"',p);if(p!=std::string::npos){size_t e=j.find('"',p+1);if(e!=std::string::npos){std::wstring phrase=Utf8ToWide(j.substr(p+1,e-p-1));if(!phrase.empty())ExecuteExact(phrase);}}}}}
   h.dwBytesRecorded=0;waveInAddBuffer(input,&h,sizeof(WAVEHDR));
  }
 }
 cleanup();g_running=false;SetStatus(L"Распознавание выключено");
}
void RefreshList() {
 HWND list=GetDlgItem(g_window,ID_VOICE_LIST);if(!list)return;SendMessageW(list,LB_RESETCONTENT,0,0);
 std::lock_guard<std::mutex> lock(g_macroMutex);for(const auto&m:g_macros){std::wstring line=m.phrase+L"  →  "+m.target;SendMessageW(list,LB_ADDSTRING,0,(LPARAM)line.c_str());}
}
void Start() {if(g_running.exchange(true))return;if(g_worker.joinable())g_worker.join();g_stop=false;g_worker=std::thread(Worker);}
void Stop() {g_stop=true;}
LRESULT CALLBACK VoiceProc(HWND h,UINT msg,WPARAM wp,LPARAM lp) {
 if(msg==WM_CREATE){g_window=h;LoadMacros();
  CreateWindowW(L"STATIC",L"Голосовые макросы: фраза должна совпасть точно (без нечёткого поиска).",WS_CHILD|WS_VISIBLE,18,14,590,28,h,nullptr,nullptr,nullptr);
  CreateWindowW(L"STATIC",L"Команда",WS_CHILD|WS_VISIBLE,18,52,120,22,h,nullptr,nullptr,nullptr);
  CreateWindowExW(WS_EX_CLIENTEDGE,L"EDIT",L"открой YouTube",WS_CHILD|WS_VISIBLE|ES_AUTOHSCROLL,18,76,270,28,h,(HMENU)ID_VOICE_PHRASE,nullptr,nullptr);
  CreateWindowW(L"STATIC",L"Сайт или путь к приложению/папке",WS_CHILD|WS_VISIBLE,306,52,300,22,h,nullptr,nullptr,nullptr);
  CreateWindowExW(WS_EX_CLIENTEDGE,L"EDIT",L"https://www.youtube.com",WS_CHILD|WS_VISIBLE|ES_AUTOHSCROLL,306,76,300,28,h,(HMENU)ID_VOICE_TARGET,nullptr,nullptr);
  CreateWindowW(L"BUTTON",L"Добавить макрос",WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON,18,116,150,34,h,(HMENU)ID_VOICE_ADD,nullptr,nullptr);
  CreateWindowW(L"BUTTON",L"Удалить выбранный",WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON,180,116,160,34,h,(HMENU)ID_VOICE_DELETE,nullptr,nullptr);
  CreateWindowW(L"BUTTON",L"Включить микрофон",WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON,360,116,145,34,h,(HMENU)ID_VOICE_START,nullptr,nullptr);
  CreateWindowW(L"BUTTON",L"Выключить",WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON,515,116,100,34,h,(HMENU)ID_VOICE_STOP,nullptr,nullptr);
  CreateWindowExW(WS_EX_CLIENTEDGE,L"LISTBOX",L"",WS_CHILD|WS_VISIBLE|WS_VSCROLL|LBS_NOTIFY,18,164,598,260,h,(HMENU)ID_VOICE_LIST,nullptr,nullptr);
  CreateWindowW(L"STATIC",g_status.c_str(),WS_CHILD|WS_VISIBLE,18,434,598,42,h,(HMENU)708,nullptr,nullptr);
  RefreshList();return 0;}
 if(msg==WM_COMMAND){switch(LOWORD(wp)){
  case ID_VOICE_START:Start();return 0;case ID_VOICE_STOP:Stop();return 0;
  case ID_VOICE_ADD:{wchar_t phrase[256]{},target[512]{};GetDlgItemTextW(h,ID_VOICE_PHRASE,phrase,256);GetDlgItemTextW(h,ID_VOICE_TARGET,target,512);
   std::wstring p=TrimLower(phrase);if(p.empty()||!target[0]||wcschr(phrase,L'|')||wcschr(target,L'|')){MessageBoxW(h,L"Введите фразу и адрес/путь. Символ | не допускается.",L"J.A.R.V.I.S.",MB_OK|MB_ICONWARNING);return 0;}
   {std::lock_guard<std::mutex> lock(g_macroMutex);if(g_macros.size()>=100){MessageBoxW(h,L"Лимит — 100 макросов.",L"J.A.R.V.I.S.",MB_OK|MB_ICONINFORMATION);return 0;}g_macros.push_back({phrase,target});}
   SaveMacros();RefreshList();return 0;}
  case ID_VOICE_DELETE:{int n=(int)SendDlgItemMessageW(h,ID_VOICE_LIST,LB_GETCURSEL,0,0);if(n!=LB_ERR){{std::lock_guard<std::mutex> lock(g_macroMutex);if(n<(int)g_macros.size())g_macros.erase(g_macros.begin()+n);}SaveMacros();RefreshList();}return 0;}
 }}
 if(msg==WM_VOICE_STATUS){HWND st=GetDlgItem(h,708);if(st)SetWindowTextW(st,g_status.c_str());return 0;}
 if(msg==WM_CLOSE){Stop();if(g_worker.joinable())g_worker.join();DestroyWindow(h);return 0;}
 if(msg==WM_DESTROY){Stop();if(g_worker.joinable())g_worker.join();g_window=nullptr;return 0;}
 return DefWindowProcW(h,msg,wp,lp);
}
}
void OpenVoiceCommands(HWND owner,const std::wstring& basePath) {
 g_base=basePath;if(!g_base.empty()&&g_base.back()==L'\\')g_base.pop_back();g_macroFile=g_base+L"\\jarvis-voice-commands.txt";
 static bool registered=false;if(!registered){WNDCLASSW wc{};wc.lpfnWndProc=VoiceProc;wc.hInstance=GetModuleHandleW(nullptr);wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);wc.hbrBackground=(HBRUSH)(COLOR_BTNFACE+1);wc.lpszClassName=L"JarvisVoiceCommandsWindow";if(!RegisterClassW(&wc)&&GetLastError()!=ERROR_CLASS_ALREADY_EXISTS)return;registered=true;}
 HWND w=CreateWindowExW(WS_EX_TOOLWINDOW,L"JarvisVoiceCommandsWindow",L"J.A.R.V.I.S. — Голосовые макросы",WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX,CW_USEDEFAULT,CW_USEDEFAULT,650,525,owner,nullptr,GetModuleHandleW(nullptr),nullptr);
 if(w){ShowWindow(w,SW_SHOWNORMAL);UpdateWindow(w);}
}
