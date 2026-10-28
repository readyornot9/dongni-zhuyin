#include "Common.h"
#include <exdisp.h>
#include <shlobj.h>
#include <shldisp.h>

namespace {
const DWORD kTimeoutMs = 2500;  // 送出前 AI 複審最多等 1.5 秒，再留一點餘裕

std::wstring PipeName() {
  wchar_t name[256] = L"";
  DWORD n = 256;
  GetUserNameW(name, &n);
  // 跟 ime-server.js 一樣：英數、底線、點、減號以外的字元換成底線
  std::wstring safe;
  for (wchar_t* p = name; *p; p++) {
    wchar_t c = *p;
    bool ok = (c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z') || (c >= L'0' && c <= L'9') || c == L'_' || c == L'.' || c == L'-';
    safe += ok ? c : L'_';
  }
  return L"\\\\.\\pipe\\shunda-ime-" + safe;
}

std::wstring RegString(const wchar_t* value) {
  wchar_t buf[MAX_PATH * 2] = L"";
  DWORD size = sizeof(buf);
  if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\ShunDa", value, RRF_RT_REG_SZ, nullptr, buf, &size) != ERROR_SUCCESS) return L"";
  return buf;
}

// 等一個重疊 I/O 做完；逾時就取消
bool Finish(HANDLE h, OVERLAPPED& ov, DWORD& done) {
  if (WaitForSingleObject(ov.hEvent, kTimeoutMs) != WAIT_OBJECT_0) {
    CancelIoEx(h, &ov);
    GetOverlappedResult(h, &ov, &done, TRUE);
    return false;
  }
  return GetOverlappedResult(h, &ov, &done, FALSE) != FALSE;
}
}  // namespace

void PipeClient::Close() {
  if (pipe_ != INVALID_HANDLE_VALUE) CloseHandle(pipe_);
  pipe_ = INVALID_HANDLE_VALUE;
  buf_.clear();
}

// 請檔案總管代為啟動程式：程式就跟從桌面開的一樣，是檔案總管的子程序，
// 不會被呼叫的程式（Teams、Chrome、安裝程式）的工作群組關掉，也不會帶到它們的沙盒或權限
// （做法：從桌面視窗拿到 Shell.Application，呼叫 ShellExecute）
namespace {
template <class T> struct Com {
  T* p = nullptr;
  ~Com() { if (p) p->Release(); }
  T** operator&() { return &p; }
  T* operator->() { return p; }
};
}  // namespace

bool LaunchViaExplorer(const std::wstring& file, const std::wstring& args) {
  HRESULT init = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  bool ok = false;
  {
    Com<IShellWindows> windows;
    Com<IDispatch> desktop;
    Com<IServiceProvider> sp;
    Com<IShellBrowser> browser;
    Com<IShellView> view;
    Com<IDispatch> background;
    Com<IShellFolderViewDual> folderView;
    Com<IDispatch> app;
    Com<IShellDispatch2> shell;
    VARIANT loc; VariantInit(&loc); loc.vt = VT_I4; loc.lVal = CSIDL_DESKTOP;
    VARIANT empty; VariantInit(&empty);
    long hwnd = 0;
    if (SUCCEEDED(CoCreateInstance(CLSID_ShellWindows, nullptr, CLSCTX_LOCAL_SERVER, IID_PPV_ARGS(&windows))) &&
        windows->FindWindowSW(&loc, &empty, SWC_DESKTOP, &hwnd, SWFO_NEEDDISPATCH, &desktop) == S_OK && desktop.p &&
        SUCCEEDED(desktop->QueryInterface(IID_PPV_ARGS(&sp))) &&
        SUCCEEDED(sp->QueryService(SID_STopLevelBrowser, IID_PPV_ARGS(&browser))) &&
        SUCCEEDED(browser->QueryActiveShellView(&view)) &&
        SUCCEEDED(view->GetItemObject(SVGIO_BACKGROUND, IID_PPV_ARGS(&background))) &&
        SUCCEEDED(background->QueryInterface(IID_PPV_ARGS(&folderView))) &&
        SUCCEEDED(folderView->get_Application(&app)) &&
        SUCCEEDED(app->QueryInterface(IID_PPV_ARGS(&shell)))) {
      BSTR bfile = SysAllocString(file.c_str());
      VARIANT vargs; VariantInit(&vargs); vargs.vt = VT_BSTR; vargs.bstrVal = SysAllocString(args.c_str());
      VARIANT vnone; VariantInit(&vnone);
      VARIANT vshow; VariantInit(&vshow); vshow.vt = VT_I4; vshow.lVal = SW_HIDE;
      ok = SUCCEEDED(shell->ShellExecute(bfile, vargs, vnone, vnone, vshow));
      SysFreeString(bfile);
      VariantClear(&vargs);
    }
  }
  if (SUCCEEDED(init)) CoUninitialize();
  return ok;
}

// 啟動引擎服務（外殼找不到服務時、開機登入時的 RunServer、安裝完的 MsiStartEngine 都用這個）
bool LaunchServer() {
  // MSI 版：引擎跟這個 DLL 裝在同一個資料夾（Program Files\DongNiZhuyin）；舊的安裝包版才看 HKCU 設定
  std::wstring node, script;
  wchar_t self[MAX_PATH];
  DWORD len = GetModuleFileNameW(g_hInst, self, MAX_PATH);
  if (len && len < MAX_PATH) {
    std::wstring dir(self, len);
    dir.resize(dir.find_last_of(L'\\') + 1);
    if (GetFileAttributesW((dir + L"node\\node.exe").c_str()) != INVALID_FILE_ATTRIBUTES &&
        GetFileAttributesW((dir + L"windows\\server\\ime-server.js").c_str()) != INVALID_FILE_ATTRIBUTES) {
      node = dir + L"node\\node.exe";
      script = dir + L"windows\\server\\ime-server.js";
    }
  }
  if (node.empty()) { node = RegString(L"NodePath"); script = RegString(L"ServerScript"); }
  if (node.empty() || script.empty()) return false;
  // 先請檔案總管代開；不行（例如沙盒程式、沒有桌面）才自己開
  if (LaunchViaExplorer(node, L"\"" + script + L"\"")) return true;
  std::wstring cmd = L"\"" + node + L"\" \"" + script + L"\"";
  // 服務要獨立於啟動它的程式：
  // - 從 Microsoft Store／MSIX 安裝的程式（例如 Claude 桌面版）裡啟動時，子程序預設被關在那個程式的套件環境，
  //   用 DESKTOP_APP_BREAKAWAY_OVERRIDE 讓服務在套件外面執行
  // - 脫離程式的工作群組（有些程式關掉時會連帶結束子程序）
  SIZE_T size = 0;
  InitializeProcThreadAttributeList(nullptr, 1, 0, &size);
  std::vector<BYTE> buf(size);
  auto attrs = (LPPROC_THREAD_ATTRIBUTE_LIST)buf.data();
  DWORD policy = 0x04;  // PROCESS_CREATION_DESKTOP_APP_BREAKAWAY_OVERRIDE
  bool haveAttrs = InitializeProcThreadAttributeList(attrs, 1, 0, &size) &&
                   UpdateProcThreadAttribute(attrs, 0, ProcThreadAttributeValue(18, FALSE, TRUE, FALSE) /* PROC_THREAD_ATTRIBUTE_DESKTOP_APP_POLICY */,
                                             &policy, sizeof(policy), nullptr, nullptr);
  STARTUPINFOEXW si = {};
  si.StartupInfo.cb = sizeof(si);
  si.lpAttributeList = haveAttrs ? attrs : nullptr;
  PROCESS_INFORMATION pi = {};
  DWORD base = CREATE_NO_WINDOW | (haveAttrs ? EXTENDED_STARTUPINFO_PRESENT : 0);
  BOOL ok = CreateProcessW(node.c_str(), &cmd[0], nullptr, nullptr, FALSE, base | CREATE_BREAKAWAY_FROM_JOB, nullptr, nullptr, &si.StartupInfo, &pi);
  if (!ok) ok = CreateProcessW(node.c_str(), &cmd[0], nullptr, nullptr, FALSE, base, nullptr, nullptr, &si.StartupInfo, &pi);
  if (!ok && haveAttrs) {
    STARTUPINFOW plain = { sizeof(plain) };
    ok = CreateProcessW(node.c_str(), &cmd[0], nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &plain, &pi);
  }
  if (haveAttrs) DeleteProcThreadAttributeList(attrs);
  if (!ok) return false;
  CloseHandle(pi.hThread);
  CloseHandle(pi.hProcess);
  return true;
}

bool PipeClient::StartServer() {
  // 啟動失敗的話，3 秒內不要每個鍵都重試
  DWORD now = GetTickCount();
  if (lastStartTry_ && now - lastStartTry_ < 3000) return false;
  lastStartTry_ = now;
  return LaunchServer();
}

bool PipeClient::Connect() {
  if (pipe_ != INVALID_HANDLE_VALUE) return true;
  std::wstring name = PipeName();
  bool started = false;
  // 服務啟動到能接連線大約 1 秒，最多等 2 秒；等不到就先放過這個鍵，不要讓程式卡住
  for (int attempt = 0; attempt < 20; attempt++) {
    pipe_ = CreateFileW(name.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
    if (pipe_ != INVALID_HANDLE_VALUE) return true;
    DWORD err = GetLastError();
    if (err == ERROR_PIPE_BUSY) { WaitNamedPipeW(name.c_str(), 200); continue; }
    if (!started) {
      if (!StartServer()) return false;
      started = true;
    }
    Sleep(100);  // 服務載入詞庫和模型要幾秒
  }
  TipLog(L"連不上引擎服務（pipe 錯誤 %lu）", GetLastError());
  return false;
}

bool PipeClient::CallIfRunning(const std::string& request, JVal& reply) {
  if (pipe_ == INVALID_HANDLE_VALUE) {
    pipe_ = CreateFileW(PipeName().c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
    if (pipe_ == INVALID_HANDLE_VALUE) return false;
  }
  return Call(request, reply);
}

bool PipeClient::Call(const std::string& request, JVal& reply) {
  for (int retry = 0; retry < 2; retry++) {
    if (!Connect()) return false;
    std::string line = request + "\n";
    OVERLAPPED ov = {};
    ov.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    DWORD done = 0;
    bool ok = WriteFile(pipe_, line.data(), (DWORD)line.size(), nullptr, &ov) || GetLastError() == ERROR_IO_PENDING;
    if (ok) ok = Finish(pipe_, ov, done) && done == line.size();
    if (!ok) {
      // 寫不出去＝連線斷了（服務重開）：重連再送一次
      CloseHandle(ov.hEvent);
      Close();
      continue;
    }
    while (ok && buf_.find('\n') == std::string::npos) {
      char chunk[4096];
      ResetEvent(ov.hEvent);
      ok = ReadFile(pipe_, chunk, sizeof(chunk), nullptr, &ov) || GetLastError() == ERROR_IO_PENDING || GetLastError() == ERROR_MORE_DATA;
      if (ok) ok = Finish(pipe_, ov, done) || GetLastError() == ERROR_MORE_DATA;
      if (ok) buf_.append(chunk, done);
      if (done == 0) ok = false;
    }
    CloseHandle(ov.hEvent);
    if (ok) {
      size_t nl = buf_.find('\n');
      std::string res = buf_.substr(0, nl);
      buf_.erase(0, nl + 1);
      return JsonParse(res, reply);
    }
    // 讀不到回覆（逾時或斷線）：不要重送，免得同一個鍵被處理兩次
    Close();
    return false;
  }
  return false;
}
