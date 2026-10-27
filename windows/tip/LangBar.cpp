// 右下角的輸入模式圖示：中文模式顯示「懂」、英文模式顯示「英」，點一下切換中英
// 沒有提供這個圖示時，Windows 只會顯示語言簡稱「繁」
#include "Common.h"

// GUID_LBI_INPUTMODE：Windows 8 之後的輸入指示器用這個 GUID 找輸入法的模式圖示
static const GUID kGuidLbiInputMode = { 0x2c77a81e, 0x41cc, 0x4178, { 0xa3, 0xa7, 0x5f, 0x8a, 0x98, 0x75, 0x68, 0xe6 } };
static const DWORD kSinkCookie = 1;

LangBarButton::LangBarButton(std::function<void()> onClick) : onClick_(std::move(onClick)) {
  DllAddRef();
  info_.clsidService = CLSID_ShunDa;
  info_.guidItem = kGuidLbiInputMode;
  info_.dwStyle = TF_LBI_STYLE_BTN_BUTTON | TF_LBI_STYLE_SHOWNINTRAY;
  info_.ulSort = 0;
  wcscpy_s(info_.szDescription, SHUNDA_NAME);
}
LangBarButton::~LangBarButton() {
  if (sink_) sink_->Release();
  DllRelease();
}

STDMETHODIMP LangBarButton::QueryInterface(REFIID riid, void** ppv) {
  if (!ppv) return E_INVALIDARG;
  *ppv = nullptr;
  if (riid == IID_IUnknown || riid == IID_ITfLangBarItem || riid == IID_ITfLangBarItemButton) *ppv = (ITfLangBarItemButton*)this;
  else if (riid == IID_ITfSource) *ppv = (ITfSource*)this;
  if (!*ppv) return E_NOINTERFACE;
  AddRef();
  return S_OK;
}
STDMETHODIMP_(ULONG) LangBarButton::AddRef() { return InterlockedIncrement(&ref_); }
STDMETHODIMP_(ULONG) LangBarButton::Release() {
  LONG r = InterlockedDecrement(&ref_);
  if (!r) delete this;
  return r;
}

STDMETHODIMP LangBarButton::GetInfo(TF_LANGBARITEMINFO* info) {
  if (!info) return E_INVALIDARG;
  *info = info_;
  return S_OK;
}
STDMETHODIMP LangBarButton::GetStatus(DWORD* status) {
  if (!status) return E_INVALIDARG;
  *status = 0;
  return S_OK;
}
STDMETHODIMP LangBarButton::GetTooltipString(BSTR* tip) {
  if (!tip) return E_INVALIDARG;
  *tip = SysAllocString(en_ ? L"懂你注音：英文（按一下或單按 Shift 切換）" : L"懂你注音：中文（按一下或單按 Shift 切換）");
  return *tip ? S_OK : E_OUTOFMEMORY;
}
STDMETHODIMP LangBarButton::OnClick(TfLBIClick click, POINT, const RECT*) {
  if (click == TF_LBI_CLK_LEFT && onClick_) onClick_();
  return S_OK;
}
STDMETHODIMP LangBarButton::GetIcon(HICON* icon) {
  if (!icon) return E_INVALIDARG;
  // 資源 1：「懂」（中文）；資源 3：「英」（英文）。呼叫的人負責 DestroyIcon
  *icon = (HICON)LoadImageW(g_hInst, MAKEINTRESOURCEW(en_ ? 3 : 1), IMAGE_ICON,
                            GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), 0);
  return *icon ? S_OK : E_FAIL;
}
STDMETHODIMP LangBarButton::GetText(BSTR* text) {
  if (!text) return E_INVALIDARG;
  *text = SysAllocString(en_ ? L"英" : L"中");
  return *text ? S_OK : E_OUTOFMEMORY;
}

STDMETHODIMP LangBarButton::AdviseSink(REFIID riid, IUnknown* punk, DWORD* cookie) {
  if (riid != IID_ITfLangBarItemSink) return CONNECT_E_CANNOTCONNECT;
  if (sink_) return CONNECT_E_ADVISELIMIT;
  if (FAILED(punk->QueryInterface(IID_ITfLangBarItemSink, (void**)&sink_))) { sink_ = nullptr; return E_NOINTERFACE; }
  *cookie = kSinkCookie;
  return S_OK;
}
STDMETHODIMP LangBarButton::UnadviseSink(DWORD cookie) {
  if (cookie != kSinkCookie || !sink_) return CONNECT_E_NOCONNECTION;
  sink_->Release();
  sink_ = nullptr;
  return S_OK;
}

void LangBarButton::SetMode(bool en) {
  if (en == en_) return;
  en_ = en;
  if (sink_) sink_->OnUpdate(TF_LBI_ICON | TF_LBI_TEXT | TF_LBI_TOOLTIP);
}
