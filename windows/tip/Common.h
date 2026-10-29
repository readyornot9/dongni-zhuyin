// 懂你注音 Windows 輸入法外殼（TSF Text Input Processor）
// 外殼只負責：接鍵盤、把按鍵送給本機引擎服務（windows/server/ime-server.js）、
// 把回來的組字區和候選字畫出來、把確定的字送進程式。選字邏輯全部在引擎服務裡，跟測試台共用。
#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <msctf.h>
#include <string>
#include <vector>
#include <map>
#include <memory>
#include <functional>
#include <olectl.h>

// {EFD18DC6-22A2-41AE-B1DD-EC32380B91E9}
static const CLSID CLSID_ShunDa = { 0xefd18dc6, 0x22a2, 0x41ae, { 0xb1, 0xdd, 0xec, 0x32, 0x38, 0x0b, 0x91, 0xe9 } };
// {E3DBE072-9942-41DE-AB4F-29373C1BC196}
static const GUID GUID_ShunDaProfile = { 0xe3dbe072, 0x9942, 0x41de, { 0xab, 0x4f, 0x29, 0x37, 0x3c, 0x1b, 0xc1, 0x96 } };
static const LANGID LANGID_ZH_TW = MAKELANGID(LANG_CHINESE, SUBLANG_CHINESE_TRADITIONAL); // 0x0404
#define SHUNDA_NAME L"懂你注音"

extern HINSTANCE g_hInst;
extern LONG g_cRefDll;
inline void DllAddRef() { InterlockedIncrement(&g_cRefDll); }
inline void DllRelease() { InterlockedDecrement(&g_cRefDll); }

// ── 除錯紀錄：%LOCALAPPDATA%\ShunDa\tip.log（只記載入、啟用、連線失敗等事件，不記打的字） ──
void TipLog(const wchar_t* fmt, ...);

// ── 字串轉換 ──
std::wstring Utf8ToWide(const std::string& s);
std::string WideToUtf8(const std::wstring& s);

// ── 很小的 JSON（只處理引擎服務回傳的格式：物件、陣列、字串、數字、true/false/null） ──
struct JVal {
  enum Type { Null, Bool, Num, Str, Arr, Obj } type = Null;
  bool b = false;
  double n = 0;
  std::string s;
  std::vector<JVal> a;
  std::map<std::string, JVal> o;
  const JVal& operator[](const char* key) const;
  bool truthy() const { return type == Bool ? b : type == Num ? n != 0 : type == Str ? !s.empty() : type != Null; }
};
bool JsonParse(const std::string& text, JVal& out);
std::string JsonQuote(const std::string& s);

// 組字區外觀（DisplayAttr.cpp）
extern const GUID GUID_DongNiInputAttr;
HRESULT CreateDisplayAttrEnum(IEnumTfDisplayAttributeInfo** out);
HRESULT CreateDisplayAttrInfo(REFGUID guid, ITfDisplayAttributeInfo** out);

// ── 跟引擎服務溝通 ──
bool LaunchServer();
// 請檔案總管代為啟動程式（不會被呼叫者的工作群組、沙盒、權限影響）
bool LaunchViaExplorer(const std::wstring& file, const std::wstring& args);
class PipeClient {
public:
  ~PipeClient() { Close(); }
  // 送一行 JSON、等一行回覆；服務沒開就試著啟動它。失敗回傳 false（外殼就不接手這個鍵）
  bool Call(const std::string& request, JVal& reply);
  // 跟 Call 一樣，但服務沒在跑就直接放棄（不啟動、不等），用在切換視窗這種不能卡住的地方
  bool CallIfRunning(const std::string& request, JVal& reply);
  void Close();
private:
  bool Connect();
  bool StartServer();
  HANDLE pipe_ = INVALID_HANDLE_VALUE;
  std::string buf_;
  DWORD lastStartTry_ = 0;
};

// ── 候選字窗 ──
class CandidateWindow {
public:
  ~CandidateWindow();
  // items：這一頁的候選字；hi：高亮第幾個（-1 不高亮）
  void Show(const POINT& at, int lineHeight, const std::vector<std::wstring>& items, int hi, int page, int pages, const std::wstring& title);
  void ShowMessage(const POINT& at, int lineHeight, const std::wstring& text, UINT ms);
  void Hide();
  // 選字窗開著時換位置（內容不變）
  void MoveTo(const POINT& at, int lineHeight);
  static void UnregisterClass();
private:
  bool Ensure();
  void Layout(const POINT& at, int lineHeight);
  static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
  void Paint(HDC dc);
  HWND hwnd_ = nullptr;
  HFONT font_ = nullptr;
  int fontDpi_ = 0;
  std::vector<std::wstring> items_;
  int hi_ = -1, page_ = 0, pages_ = 1;
  std::wstring title_, message_;
};

// ── 右下角的輸入模式圖示（LangBar.cpp） ──
class LangBarButton : public ITfLangBarItemButton, public ITfSource {
public:
  explicit LangBarButton(std::function<void()> onClick);
  ~LangBarButton();
  STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override;
  STDMETHODIMP_(ULONG) AddRef() override;
  STDMETHODIMP_(ULONG) Release() override;
  // ITfLangBarItem
  STDMETHODIMP GetInfo(TF_LANGBARITEMINFO* info) override;
  STDMETHODIMP GetStatus(DWORD* status) override;
  STDMETHODIMP Show(BOOL) override { return E_NOTIMPL; }
  STDMETHODIMP GetTooltipString(BSTR* tip) override;
  // ITfLangBarItemButton
  STDMETHODIMP OnClick(TfLBIClick click, POINT pt, const RECT* area) override;
  STDMETHODIMP InitMenu(ITfMenu*) override { return E_NOTIMPL; }
  STDMETHODIMP OnMenuSelect(UINT) override { return E_NOTIMPL; }
  STDMETHODIMP GetIcon(HICON* icon) override;
  STDMETHODIMP GetText(BSTR* text) override;
  // ITfSource
  STDMETHODIMP AdviseSink(REFIID riid, IUnknown* punk, DWORD* cookie) override;
  STDMETHODIMP UnadviseSink(DWORD cookie) override;
  void SetMode(bool en);
private:
  LONG ref_ = 1;
  ITfLangBarItemSink* sink_ = nullptr;
  bool en_ = false;
  std::function<void()> onClick_;
  TF_LANGBARITEMINFO info_ = {};
};
