// DLL 進入點、COM class factory、註冊／解除註冊
#include "TextService.h"
#include <olectl.h>
#include <tlhelp32.h>

HINSTANCE g_hInst = nullptr;
LONG g_cRefDll = 0;

namespace {
const wchar_t kClsidKey[] = L"SOFTWARE\\Classes\\CLSID\\{EFD18DC6-22A2-41AE-B1DD-EC32380B91E9}";

class ClassFactory : public IClassFactory {
public:
  STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
    if (riid == IID_IUnknown || riid == IID_IClassFactory) { *ppv = this; AddRef(); return S_OK; }
    *ppv = nullptr;
    return E_NOINTERFACE;
  }
  STDMETHODIMP_(ULONG) AddRef() override { DllAddRef(); return 2; }
  STDMETHODIMP_(ULONG) Release() override { DllRelease(); return 1; }
  STDMETHODIMP CreateInstance(IUnknown* outer, REFIID riid, void** ppv) override {
    if (outer) return CLASS_E_NOAGGREGATION;
    TextService* ts = new (std::nothrow) TextService();
    if (!ts) return E_OUTOFMEMORY;
    HRESULT hr = ts->QueryInterface(riid, ppv);
    ts->Release();
    return hr;
  }
  STDMETHODIMP LockServer(BOOL lock) override {
    if (lock) DllAddRef(); else DllRelease();
    return S_OK;
  }
};
ClassFactory g_factory;

// {13A016DF-560B-46CD-947A-4C3AF1E0E35D}
const GUID GUID_TFCAT_TIPCAP_IMMERSIVESUPPORT = { 0x13a016df, 0x560b, 0x46cd, { 0x94, 0x7a, 0x4c, 0x3a, 0xf1, 0xe0, 0xe3, 0x5d } };

const GUID* kCategories[] = {
  &GUID_TFCAT_TIP_KEYBOARD,
  &GUID_TFCAT_TIPCAP_SYSTRAYSUPPORT,  // 出現在工作列的輸入法切換清單
  &GUID_TFCAT_TIPCAP_IMMERSIVESUPPORT,  // 新式文字框架的程式（新版記事本、Teams、搜尋框）只載入有這項的輸入法
};
}  // namespace

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID) {
  if (reason == DLL_PROCESS_ATTACH) { g_hInst = inst; DisableThreadLibraryCalls(inst); TipLog(L"DLL 載入"); }
  if (reason == DLL_PROCESS_DETACH) CandidateWindow::UnregisterClass();
  return TRUE;
}

STDAPI DllGetClassObject(REFCLSID clsid, REFIID riid, void** ppv) {
  if (clsid != CLSID_ShunDa) { *ppv = nullptr; return CLASS_E_CLASSNOTAVAILABLE; }
  HRESULT hr = g_factory.QueryInterface(riid, ppv);
  TipLog(L"DllGetClassObject → 0x%08X", (unsigned)hr);
  return hr;
}

namespace {
// 把懂你注音加進／移出目前使用者的鍵盤清單（微軟建議的 InstallLayoutOrTip，不直接寫登錄）
void LayoutOrTip(DWORD flags) {
  HMODULE input = LoadLibraryW(L"input.dll");
  if (!input) return;
  using Fn = BOOL(WINAPI*)(LPCWSTR, DWORD);
  if (auto fn = (Fn)GetProcAddress(input, "InstallLayoutOrTip"))
    fn(L"0404:{EFD18DC6-22A2-41AE-B1DD-EC32380B91E9}{E3DBE072-9942-41DE-AB4F-29373C1BC196}", flags);
  FreeLibrary(input);
}

// 目前使用者的一次性設定（要用使用者自己的身分跑）：
// - 清掉舊安裝包版的設定（登入時會啟動舊引擎、外殼會找到舊路徑）
// - 第一次才把懂你注音加進鍵盤清單；之後使用者自己拿掉就尊重，不再加回去
void SetupCurrentUser() {
  RegDeleteKeyValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", L"DongNiZhuyin");
  RegDeleteKeyValueW(HKEY_CURRENT_USER, L"Software\\ShunDa", L"NodePath");
  RegDeleteKeyValueW(HKEY_CURRENT_USER, L"Software\\ShunDa", L"ServerScript");
  DWORD added = 0, size = sizeof(added);
  if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\ShunDa", L"KeyboardAdded", RRF_RT_REG_DWORD, nullptr, &added, &size) == ERROR_SUCCESS && added) return;
  LayoutOrTip(0);
  added = 1;
  RegSetKeyValueW(HKEY_CURRENT_USER, L"Software\\ShunDa", L"KeyboardAdded", REG_DWORD, &added, sizeof(added));
}
}  // namespace

// 登入 Windows 時由 rundll32 呼叫（HKLM Run，每個使用者各跑一次）：使用者設定＋在背景啟動引擎
extern "C" void CALLBACK RunServerW(HWND, HINSTANCE, LPWSTR, int) {
  SetupCurrentUser();
  LaunchServer();
}

// ── MSI 自訂動作（失敗也不讓安裝失敗） ──
// 安裝畫面結束時（安裝者自己的 msiexec 裡）啟動引擎，裝完或升級完馬上能打字，不用登出再登入
extern "C" UINT __stdcall MsiStartEngine(unsigned long) {
  LaunchServer();
  return ERROR_SUCCESS;
}
// 停掉懂你注音的引擎（node.exe 在 Program Files\DongNiZhuyin 或舊安裝包的 %LOCALAPPDATA%\ShunDa\app 底下），
// 安裝新版、移除前先停，檔案才不會被占用
extern "C" UINT __stdcall MsiStopEngine(unsigned long) {
  HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
  if (snap == INVALID_HANDLE_VALUE) return ERROR_SUCCESS;
  PROCESSENTRY32W pe = { sizeof(pe) };
  for (BOOL ok = Process32FirstW(snap, &pe); ok; ok = Process32NextW(snap, &pe)) {
    if (_wcsicmp(pe.szExeFile, L"node.exe") != 0) continue;
    HANDLE p = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_TERMINATE, FALSE, pe.th32ProcessID);
    if (!p) continue;
    wchar_t path[MAX_PATH * 2];
    DWORD n = MAX_PATH * 2;
    if (QueryFullProcessImageNameW(p, 0, path, &n)) {
      std::wstring s(path, n);
      for (auto& c : s) c = towlower(c);
      if (s.find(L"\\dongnizhuyin\\node\\") != std::wstring::npos || s.find(L"\\shunda\\app\\node\\") != std::wstring::npos) TerminateProcess(p, 0);
    }
    CloseHandle(p);
  }
  CloseHandle(snap);
  return ERROR_SUCCESS;
}
// 從目前使用者的鍵盤清單拿掉，清掉「已加過」的記號（重裝時會再加）：rundll32 "<這個 DLL>",RemoveForUserW
extern "C" void CALLBACK RemoveForUserW(HWND, HINSTANCE, LPWSTR, int) {
  LayoutOrTip(0x00000001);  // ILOT_UNINSTALL
  RegDeleteKeyValueW(HKEY_CURRENT_USER, L"Software\\ShunDa", L"KeyboardAdded");
}


STDAPI DllCanUnloadNow() { return g_cRefDll <= 0 ? S_OK : S_FALSE; }

STDAPI DllRegisterServer() {
  wchar_t path[MAX_PATH];
  DWORD len = GetModuleFileNameW(g_hInst, path, MAX_PATH);
  if (!len || len >= MAX_PATH) return E_FAIL;

  // COM 物件
  HKEY key = nullptr, inproc = nullptr;
  if (RegCreateKeyExW(HKEY_LOCAL_MACHINE, kClsidKey, 0, nullptr, 0, KEY_WRITE, nullptr, &key, nullptr) != ERROR_SUCCESS) return SELFREG_E_CLASS;
  RegSetValueExW(key, nullptr, 0, REG_SZ, (const BYTE*)SHUNDA_NAME, (DWORD)sizeof(SHUNDA_NAME));
  if (RegCreateKeyExW(key, L"InprocServer32", 0, nullptr, 0, KEY_WRITE, nullptr, &inproc, nullptr) != ERROR_SUCCESS) { RegCloseKey(key); return SELFREG_E_CLASS; }
  RegSetValueExW(inproc, nullptr, 0, REG_SZ, (const BYTE*)path, (len + 1) * sizeof(wchar_t));
  RegSetValueExW(inproc, L"ThreadingModel", 0, REG_SZ, (const BYTE*)L"Apartment", sizeof(L"Apartment"));
  RegCloseKey(inproc);
  RegCloseKey(key);

  // 輸入法設定檔（中文（台灣））
  HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  bool uninit = SUCCEEDED(hr);
  ITfInputProcessorProfileMgr* pm = nullptr;
  hr = CoCreateInstance(CLSID_TF_InputProcessorProfiles, nullptr, CLSCTX_INPROC_SERVER, IID_ITfInputProcessorProfileMgr, (void**)&pm);
  if (SUCCEEDED(hr)) {
    hr = pm->RegisterProfile(CLSID_ShunDa, LANGID_ZH_TW, GUID_ShunDaProfile, SHUNDA_NAME, (ULONG)wcslen(SHUNDA_NAME),
                             path, len, 0, nullptr, 0, TRUE, 0);
    pm->Release();
  }
  if (SUCCEEDED(hr)) {
    ITfCategoryMgr* cm = nullptr;
    hr = CoCreateInstance(CLSID_TF_CategoryMgr, nullptr, CLSCTX_INPROC_SERVER, IID_ITfCategoryMgr, (void**)&cm);
    if (SUCCEEDED(hr)) {
      for (const GUID* c : kCategories) cm->RegisterCategory(CLSID_ShunDa, *c, CLSID_ShunDa);
      cm->Release();
    }
  }
  if (uninit) CoUninitialize();
  return SUCCEEDED(hr) ? S_OK : SELFREG_E_CLASS;
}

STDAPI DllUnregisterServer() {
  HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  bool uninit = SUCCEEDED(hr);
  ITfCategoryMgr* cm = nullptr;
  if (SUCCEEDED(CoCreateInstance(CLSID_TF_CategoryMgr, nullptr, CLSCTX_INPROC_SERVER, IID_ITfCategoryMgr, (void**)&cm))) {
    for (const GUID* c : kCategories) cm->UnregisterCategory(CLSID_ShunDa, *c, CLSID_ShunDa);
    cm->Release();
  }
  ITfInputProcessorProfileMgr* pm = nullptr;
  if (SUCCEEDED(CoCreateInstance(CLSID_TF_InputProcessorProfiles, nullptr, CLSCTX_INPROC_SERVER, IID_ITfInputProcessorProfileMgr, (void**)&pm))) {
    pm->UnregisterProfile(CLSID_ShunDa, LANGID_ZH_TW, GUID_ShunDaProfile, 0);
    pm->Release();
  }
  if (uninit) CoUninitialize();
  RegDeleteTreeW(HKEY_LOCAL_MACHINE, kClsidKey);
  return S_OK;
}
