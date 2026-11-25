#define NOMINMAX
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
#include <cstdint>

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
struct Api {
    HMODULE dll{};
    ModelNew modelNew{};
    ModelFree modelFree{};
    RecNew recNew{};
    RecFree recFree{};
    Accept accept{};
    Result result{};
    Result partial{};
    Result finalResult{};
};
struct Macro { std::wstring phrase, target; };

HWND g_window{};
std::wstring g_base, g_macroFile;
std::vector<Macro> g_macros;
std::mutex g_macroMutex, g_statusMutex;
std::thread g_worker;
std::atomic<bool> g_running{false}, g_stop{false};
std::wstring g_status=L"Распознавание выключено";

void SetStatus(const std::wstring& s) {
    { std::lock_guard<std::mutex> lock(g_statusMutex); g_status=s; }
    HWND w=g_window;
    if(w && IsWindow(w)) PostMessageW(w,WM_VOICE_STATUS,0,0);
}
std::wstring GetStatus() {
    std::lock_guard<std::mutex> lock(g_statusMutex);
    return g_status;
}
std::wstring TrimLower(std::wstring s) {
    while(!s.empty() && iswspace(s.front())) s.erase(s.begin());
    while(!s.empty() && iswspace(s.back())) s.pop_back();
    std::transform(s.begin(),s.end(),s.begin(),[](wchar_t c){return (wchar_t)towlower(c);});
    return s;
}
std::wstring Utf8ToWide(const std::string& s) {
    if(s.empty()) return L"";
    int n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.c_str(),(int)s.size(),nullptr,0);
    if(!n) return L"";
    std::wstring out((size_t)n,L'\0');
    if(!MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.c_str(),(int)s.size(),&out[0],n)) return L"";
    return out;
}
std::string WideToUtf8(const std::wstring& s) {
    if(s.empty()) return {};
    int n=WideCharToMultiByte(CP_UTF8,0,s.c_str(),(int)s.size(),nullptr,0,nullptr,nullptr);
    if(!n) return {};
    std::string out((size_t)n,'\0');
    if(!WideCharToMultiByte(CP_UTF8,0,s.c_str(),(int)s.size(),&out[0],n,nullptr,nullptr)) return {};
    return out;
}
bool ReadAll(const std::wstring& path,std::string& data) {
    HANDLE f=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(f==INVALID_HANDLE_VALUE) return false;
    DWORD size=GetFileSize(f,nullptr);
    if(size==INVALID_FILE_SIZE || size>1024*1024){CloseHandle(f);return false;}
    data.resize(size);
    DWORD got=0;
    BOOL ok=ReadFile(f,data.empty()?nullptr:&data[0],size,&got,nullptr);
    CloseHandle(f);
    data.resize(got);
    return ok!=FALSE;
}
bool WriteAll(const std::wstring& path,const std::string& data) {
    HANDLE f=CreateFileW(path.c_str(),GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(f==INVALID_HANDLE_VALUE) return false;
    DWORD wrote=0;
    BOOL ok=WriteFile(f,data.data(),(DWORD)data.size(),&wrote,nullptr);
    CloseHandle(f);
    return ok && wrote==data.size();
}
void LoadMacros() {
    std::string raw;
    if(!ReadAll(g_macroFile,raw)) return;
    std::wstring all=Utf8ToWide(raw);
    size_t p=0;
    std::lock_guard<std::mutex> lock(g_macroMutex);
    g_macros.clear();
    while(p<all.size() && g_macros.size()<100) {
        size_t e=all.find(L'\n',p);
        if(e==std::wstring::npos) e=all.size();
        std::wstring line=all.substr(p,e-p);
        p=e+1;
        if(!line.empty() && line.back()==L'\r') line.pop_back();
        size_t sep=line.find(L'|');
        if(sep==std::wstring::npos) continue;
        Macro m{line.substr(0,sep),line.substr(sep+1)};
        if(!m.phrase.empty() && !m.target.empty() &&
           m.phrase.find_first_of(L"\r\n|")==std::wstring::npos &&
           m.target.find_first_of(L"\r\n|")==std::wstring::npos)
            g_macros.push_back(m);
    }
}
bool SaveMacros() {
    std::wstring all;
    {
        std::lock_guard<std::mutex> lock(g_macroMutex);
        for(const auto&m:g_macros) all+=m.phrase+L"|"+m.target+L"\n";
    }
    return WriteAll(g_macroFile,WideToUtf8(all));
}
void ExecuteExact(const std::wstring& recognized) {
    const std::wstring key=TrimLower(recognized);
    if(key.empty()) return;
    Macro found;
    bool matched=false;
    {
        std::lock_guard<std::mutex> lock(g_macroMutex);
        for(const auto&m:g_macros) {
            if(TrimLower(m.phrase)==key) { found=m; matched=true; break; }
        }
    }
    if(!matched) {
        SetStatus(L"Распознано: "+recognized+L" — макрос не найден");
        return;
    }
    const std::wstring& target=found.target;
    if(target.find(L"://")!=std::wstring::npos &&
       target.rfind(L"https://",0)!=0 && target.rfind(L"http://",0)!=0) {
        SetStatus(L"Команда отклонена: разрешены только HTTP/HTTPS");
        return;
    }
    HINSTANCE result=ShellExecuteW(g_window,L"open",target.c_str(),nullptr,nullptr,SW_SHOWNORMAL);
    SetStatus((INT_PTR)result>32 ? L"Команда выполнена: "+found.phrase : L"Не удалось открыть цель макроса");
}

// Extracts a JSON string property and correctly handles escaped quotes and backslashes.
void AppendUtf8(std::string& out,uint32_t cp) {
    if(cp<=0x7f) out.push_back((char)cp);
    else if(cp<=0x7ff) { out.push_back((char)(0xc0|(cp>>6))); out.push_back((char)(0x80|(cp&0x3f))); }
    else if(cp<=0xffff) { out.push_back((char)(0xe0|(cp>>12))); out.push_back((char)(0x80|((cp>>6)&0x3f))); out.push_back((char)(0x80|(cp&0x3f))); }
    else { out.push_back((char)(0xf0|(cp>>18))); out.push_back((char)(0x80|((cp>>12)&0x3f))); out.push_back((char)(0x80|((cp>>6)&0x3f))); out.push_back((char)(0x80|(cp&0x3f))); }
}
int HexDigit(char c) {
    if(c>='0'&&c<='9') return c-'0';
    if(c>='a'&&c<='f') return c-'a'+10;
    if(c>='A'&&c<='F') return c-'A'+10;
    return -1;
}
bool ReadHex4(const std::string& s,size_t& p,uint32_t& value) {
    if(p+4>s.size()) return false;
    value=0;
    for(int i=0;i<4;++i) { int h=HexDigit(s[p++]); if(h<0)return false; value=(value<<4)|(uint32_t)h; }
    return true;
}
bool JsonStringField(const std::string& json,const char* key,std::string& value) {
    const std::string token=std::string("\"")+key+"\"";
    size_t p=json.find(token);
    if(p==std::string::npos) return false;
    p=json.find(':',p+token.size());
    if(p==std::string::npos) return false;
    ++p;
    while(p<json.size() && (json[p]==' '||json[p]=='\t'||json[p]=='\r'||json[p]=='\n')) ++p;
    if(p>=json.size()||json[p++]!='"') return false;
    value.clear();
    while(p<json.size()) {
        unsigned char c=(unsigned char)json[p++];
        if(c=='"') return true;
        if(c!='\\') { value.push_back((char)c); continue; }
        if(p>=json.size()) return false;
        char e=json[p++];
        switch(e) {
        case '"': value.push_back('"'); break;
        case '\\': value.push_back('\\'); break;
        case '/': value.push_back('/'); break;
        case 'b': value.push_back('\b'); break;
        case 'f': value.push_back('\f'); break;
        case 'n': value.push_back('\n'); break;
        case 'r': value.push_back('\r'); break;
        case 't': value.push_back('\t'); break;
        case 'u': {
            uint32_t cp=0;
            if(!ReadHex4(json,p,cp)) return false;
            if(cp>=0xd800 && cp<=0xdbff && p+6<=json.size() && json[p]=='\\' && json[p+1]=='u') {
                p+=2; uint32_t low=0;
                if(!ReadHex4(json,p,low)||low<0xdc00||low>0xdfff) return false;
                cp=0x10000+((cp-0xd800)<<10)+(low-0xdc00);
            }
            AppendUtf8(value,cp);
            break;
        }
        default: return false;
        }
    }
    return false;
}
void ProcessResultJson(const char* json) {
    if(!json) return;
    std::string text;
    if(JsonStringField(json,"text",text)) {
        std::wstring phrase=Utf8ToWide(text);
        if(!TrimLower(phrase).empty()) ExecuteExact(phrase);
    }
}
void ProcessPartialJson(const char* json) {
    if(!json) return;
    std::string partial;
    if(JsonStringField(json,"partial",partial) && !partial.empty()) {
        std::wstring phrase=Utf8ToWide(partial);
        if(!phrase.empty()) SetStatus(L"Слышу: "+phrase);
    }
}
bool LoadApi(Api& a) {
    const std::wstring dllPath=g_base+L"\\libvosk.dll";
    // Load dependencies from the same directory without changing the process-wide DLL search path.
    a.dll=LoadLibraryExW(dllPath.c_str(),nullptr,LOAD_WITH_ALTERED_SEARCH_PATH);
    if(!a.dll) return false;
    a.modelNew=(ModelNew)GetProcAddress(a.dll,"vosk_model_new");
    a.modelFree=(ModelFree)GetProcAddress(a.dll,"vosk_model_free");
    a.recNew=(RecNew)GetProcAddress(a.dll,"vosk_recognizer_new");
    a.recFree=(RecFree)GetProcAddress(a.dll,"vosk_recognizer_free");
    a.accept=(Accept)GetProcAddress(a.dll,"vosk_recognizer_accept_waveform");
    a.result=(Result)GetProcAddress(a.dll,"vosk_recognizer_result");
    a.partial=(Result)GetProcAddress(a.dll,"vosk_recognizer_partial_result");
    a.finalResult=(Result)GetProcAddress(a.dll,"vosk_recognizer_final_result");
    return a.modelNew&&a.modelFree&&a.recNew&&a.recFree&&a.accept&&a.result&&a.partial&&a.finalResult;
}
void Worker() {
    Api api{};
    void* model=nullptr;
    void* rec=nullptr;
    HWAVEIN input=nullptr;
    HANDLE event=CreateEventW(nullptr,FALSE,FALSE,nullptr);
    std::vector<std::vector<char>> buffers(4,std::vector<char>(8192));
    std::vector<WAVEHDR> headers(4);
    bool failed=false;
    auto cleanup=[&]() {
        if(input) {
            waveInStop(input);
            waveInReset(input);
            for(auto& h:headers) if(h.dwFlags&WHDR_PREPARED) waveInUnprepareHeader(input,&h,sizeof(WAVEHDR));
            waveInClose(input);
            input=nullptr;
        }
        if(rec&&api.recFree) api.recFree(rec);
        if(model&&api.modelFree) api.modelFree(model);
        if(api.dll) FreeLibrary(api.dll);
        if(event) CloseHandle(event);
    };

    if(!event) { SetStatus(L"Не удалось создать событие аудиозахвата"); g_running=false; return; }
    if(!LoadApi(api)) {
        SetStatus(L"Не найдена libvosk.dll или одна из её DLL-зависимостей");
        cleanup(); g_running=false; return;
    }
    const std::wstring modelPath=g_base+L"\\models\\vosk-model-small-ru-0.22";
    const std::string modelUtf8=WideToUtf8(modelPath);
    model=api.modelNew(modelUtf8.c_str());
    if(!model) {
        SetStatus(L"Не удалось загрузить русскую модель Vosk. Проверьте папку models.");
        cleanup(); g_running=false; return;
    }
    rec=api.recNew(model,16000.0f);
    if(!rec) {
        SetStatus(L"Не удалось создать распознаватель Vosk");
        cleanup(); g_running=false; return;
    }

    WAVEFORMATEX fmt{};
    fmt.wFormatTag=WAVE_FORMAT_PCM;
    fmt.nChannels=1;
    fmt.nSamplesPerSec=16000;
    fmt.wBitsPerSample=16;
    fmt.nBlockAlign=2;
    fmt.nAvgBytesPerSec=32000;
    MMRESULT mr=waveInOpen(&input,WAVE_MAPPER,&fmt,(DWORD_PTR)event,0,CALLBACK_EVENT);
    if(mr!=MMSYSERR_NOERROR) {
        SetStatus(L"Микрофон недоступен. Проверьте устройство и разрешения Windows.");
        cleanup(); g_running=false; return;
    }
    for(size_t i=0;i<headers.size();++i) {
        WAVEHDR& h=headers[i];
        h={};
        h.lpData=buffers[i].data();
        h.dwBufferLength=(DWORD)buffers[i].size();
        mr=waveInPrepareHeader(input,&h,sizeof(WAVEHDR));
        if(mr!=MMSYSERR_NOERROR) { failed=true; break; }
        mr=waveInAddBuffer(input,&h,sizeof(WAVEHDR));
        if(mr!=MMSYSERR_NOERROR) { failed=true; break; }
    }
    if(failed) {
        SetStatus(L"Не удалось подготовить аудиобуферы микрофона");
        cleanup(); g_running=false; return;
    }
    if(waveInStart(input)!=MMSYSERR_NOERROR) {
        SetStatus(L"Не удалось включить микрофон");
        cleanup(); g_running=false; return;
    }
    SetStatus(L"Микрофон активен — ожидание голосовой команды");

    while(!g_stop.load()) {
        DWORD wait=WaitForSingleObject(event,250);
        if(wait==WAIT_TIMEOUT) continue;
        if(wait!=WAIT_OBJECT_0) {
            SetStatus(L"Ошибка ожидания аудиоданных микрофона");
            break;
        }
        for(size_t i=0;i<headers.size();++i) {
            WAVEHDR& h=headers[i];
            if(!(h.dwFlags&WHDR_DONE)) continue;
            if(h.dwBytesRecorded>0) {
                int accepted=api.accept(rec,h.lpData,(int)h.dwBytesRecorded);
                if(accepted<0) {
                    SetStatus(L"Vosk сообщил об ошибке обработки аудио");
                    failed=true;
                    break;
                }
                if(accepted==1) ProcessResultJson(api.result(rec));
                else ProcessPartialJson(api.partial(rec));
            }
            h.dwBytesRecorded=0;
            if(!g_stop.load() && waveInAddBuffer(input,&h,sizeof(WAVEHDR))!=MMSYSERR_NOERROR) {
                SetStatus(L"Не удалось повторно поставить аудиобуфер");
                failed=true;
                break;
            }
        }
        if(failed) break;
    }
    if(rec && !failed && g_stop.load()) ProcessResultJson(api.finalResult(rec));
    cleanup();
    g_running=false;
    if(failed) SetStatus(L"Распознавание остановлено из-за ошибки аудиопотока");
    else SetStatus(L"Распознавание выключено");
}
void RefreshList() {
    HWND list=GetDlgItem(g_window,ID_VOICE_LIST);
    if(!list) return;
    SendMessageW(list,LB_RESETCONTENT,0,0);
    std::lock_guard<std::mutex> lock(g_macroMutex);
    for(const auto&m:g_macros) {
        std::wstring line=m.phrase+L"  →  "+m.target;
        SendMessageW(list,LB_ADDSTRING,0,(LPARAM)line.c_str());
    }
}
void Start() {
    if(g_running.exchange(true)) return;
    if(g_worker.joinable()) g_worker.join();
    g_stop=false;
    try { g_worker=std::thread(Worker); }
    catch(...) { g_running=false; SetStatus(L"Не удалось запустить поток распознавания"); }
}
void Stop() { g_stop=true; }

LRESULT CALLBACK VoiceProc(HWND h,UINT msg,WPARAM wp,LPARAM lp) {
    if(msg==WM_CREATE) {
        g_window=h;
        LoadMacros();
        CreateWindowW(L"STATIC",L"Голосовые макросы: команда срабатывает после точного совпадения фразы.",WS_CHILD|WS_VISIBLE,18,14,590,28,h,nullptr,nullptr,nullptr);
        CreateWindowW(L"STATIC",L"Команда",WS_CHILD|WS_VISIBLE,18,52,120,22,h,nullptr,nullptr,nullptr);
        CreateWindowExW(WS_EX_CLIENTEDGE,L"EDIT",L"открой YouTube",WS_CHILD|WS_VISIBLE|ES_AUTOHSCROLL,18,76,270,28,h,(HMENU)(INT_PTR)ID_VOICE_PHRASE,nullptr,nullptr);
        CreateWindowW(L"STATIC",L"Сайт или путь к приложению/папке",WS_CHILD|WS_VISIBLE,306,52,300,22,h,nullptr,nullptr,nullptr);
        CreateWindowExW(WS_EX_CLIENTEDGE,L"EDIT",L"https://www.youtube.com",WS_CHILD|WS_VISIBLE|ES_AUTOHSCROLL,306,76,300,28,h,(HMENU)(INT_PTR)ID_VOICE_TARGET,nullptr,nullptr);
        CreateWindowW(L"BUTTON",L"Добавить макрос",WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON,18,116,150,34,h,(HMENU)(INT_PTR)ID_VOICE_ADD,nullptr,nullptr);
        CreateWindowW(L"BUTTON",L"Удалить выбранный",WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON,180,116,160,34,h,(HMENU)(INT_PTR)ID_VOICE_DELETE,nullptr,nullptr);
        CreateWindowW(L"BUTTON",L"Включить микрофон",WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON,360,116,145,34,h,(HMENU)(INT_PTR)ID_VOICE_START,nullptr,nullptr);
        CreateWindowW(L"BUTTON",L"Выключить",WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON,515,116,100,34,h,(HMENU)(INT_PTR)ID_VOICE_STOP,nullptr,nullptr);
        CreateWindowExW(WS_EX_CLIENTEDGE,L"LISTBOX",L"",WS_CHILD|WS_VISIBLE|WS_VSCROLL|LBS_NOTIFY,18,164,598,260,h,(HMENU)(INT_PTR)ID_VOICE_LIST,nullptr,nullptr);
        CreateWindowW(L"STATIC",GetStatus().c_str(),WS_CHILD|WS_VISIBLE,18,434,598,42,h,(HMENU)(INT_PTR)708,nullptr,nullptr);
        RefreshList();
        return 0;
    }
    if(msg==WM_COMMAND) {
        switch(LOWORD(wp)) {
        case ID_VOICE_START: Start(); return 0;
        case ID_VOICE_STOP: Stop(); return 0;
        case ID_VOICE_ADD: {
            wchar_t phrase[256]{},target[512]{};
            GetDlgItemTextW(h,ID_VOICE_PHRASE,phrase,256);
            GetDlgItemTextW(h,ID_VOICE_TARGET,target,512);
            std::wstring normalized=TrimLower(phrase);
            if(normalized.empty() || !target[0] ||
               wcschr(phrase,L'|') || wcschr(target,L'|') ||
               wcschr(phrase,L'\r') || wcschr(phrase,L'\n') ||
               wcschr(target,L'\r') || wcschr(target,L'\n')) {
                MessageBoxW(h,L"Введите фразу и адрес/путь. Символы | и переносы строк не допускаются.",L"J.A.R.V.I.S.",MB_OK|MB_ICONWARNING);
                return 0;
            }
            bool duplicate=false,full=false;
            {
                std::lock_guard<std::mutex> lock(g_macroMutex);
                full=g_macros.size()>=100;
                for(const auto&m:g_macros) if(TrimLower(m.phrase)==normalized) duplicate=true;
                if(!duplicate&&!full) g_macros.push_back({phrase,target});
            }
            if(full) { MessageBoxW(h,L"Лимит — 100 макросов.",L"J.A.R.V.I.S.",MB_OK|MB_ICONINFORMATION); return 0; }
            if(duplicate) { MessageBoxW(h,L"Такая голосовая фраза уже существует.",L"J.A.R.V.I.S.",MB_OK|MB_ICONINFORMATION); return 0; }
            if(!SaveMacros()) {
                { std::lock_guard<std::mutex> lock(g_macroMutex); if(!g_macros.empty()) g_macros.pop_back(); }
                MessageBoxW(h,L"Не удалось сохранить макрос. Проверьте права на запись в папку программы.",L"J.A.R.V.I.S.",MB_OK|MB_ICONERROR);
                return 0;
            }
            RefreshList();
            return 0;
        }
        case ID_VOICE_DELETE: {
            int n=(int)SendDlgItemMessageW(h,ID_VOICE_LIST,LB_GETCURSEL,0,0);
            if(n!=LB_ERR) {
                Macro removed; bool didRemove=false;
                {
                    std::lock_guard<std::mutex> lock(g_macroMutex);
                    if(n<(int)g_macros.size()) { removed=g_macros[(size_t)n]; g_macros.erase(g_macros.begin()+n); didRemove=true; }
                }
                if(didRemove && !SaveMacros()) {
                    { std::lock_guard<std::mutex> lock(g_macroMutex); g_macros.insert(g_macros.begin()+std::min((size_t)n,g_macros.size()),removed); }
                    MessageBoxW(h,L"Не удалось сохранить удаление макроса.",L"J.A.R.V.I.S.",MB_OK|MB_ICONERROR);
                }
                RefreshList();
            }
            return 0;
        }
        }
    }
    if(msg==WM_VOICE_STATUS) {
        HWND st=GetDlgItem(h,708);
        if(st) { const std::wstring status=GetStatus(); SetWindowTextW(st,status.c_str()); }
        return 0;
    }
    if(msg==WM_CLOSE) {
        Stop();
        if(g_worker.joinable()) g_worker.join();
        DestroyWindow(h);
        return 0;
    }
    if(msg==WM_DESTROY) {
        Stop();
        if(g_worker.joinable()) g_worker.join();
        g_window=nullptr;
        return 0;
    }
    return DefWindowProcW(h,msg,wp,lp);
}
}

void OpenVoiceCommands(HWND owner,const std::wstring& basePath) {
    g_base=basePath;
    if(!g_base.empty() && g_base.back()==L'\\') g_base.pop_back();
    g_macroFile=g_base+L"\\jarvis-voice-commands.txt";
    if(g_window && IsWindow(g_window)) {
        ShowWindow(g_window,SW_SHOWNORMAL);
        SetForegroundWindow(g_window);
        return;
    }
    static bool registered=false;
    if(!registered) {
        WNDCLASSW wc{};
        wc.lpfnWndProc=VoiceProc;
        wc.hInstance=GetModuleHandleW(nullptr);
        wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);
        wc.hbrBackground=(HBRUSH)(COLOR_BTNFACE+1);
        wc.lpszClassName=L"JarvisVoiceCommandsWindow";
        if(!RegisterClassW(&wc) && GetLastError()!=ERROR_CLASS_ALREADY_EXISTS) return;
        registered=true;
    }
    HWND w=CreateWindowExW(WS_EX_TOOLWINDOW,L"JarvisVoiceCommandsWindow",L"J.A.R.V.I.S. — Голосовые макросы",WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX,CW_USEDEFAULT,CW_USEDEFAULT,650,525,owner,nullptr,GetModuleHandleW(nullptr),nullptr);
    if(w) { ShowWindow(w,SW_SHOWNORMAL); UpdateWindow(w); }
}
