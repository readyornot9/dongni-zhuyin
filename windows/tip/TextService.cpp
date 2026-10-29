#include "TextService.h"
#include <mmsystem.h>

namespace {
class EditSession : public ITfEditSession {
public:
  explicit EditSession(std::function<void(TfEditCookie)> fn) : fn_(std::move(fn)) {}
  STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
    if (riid == IID_IUnknown || riid == IID_ITfEditSession) { *ppv = this; AddRef(); return S_OK; }
    *ppv = nullptr;
    return E_NOINTERFACE;
  }
  STDMETHODIMP_(ULONG) AddRef() override { return InterlockedIncrement(&ref_); }
  STDMETHODIMP_(ULONG) Release() override {
    LONG r = InterlockedDecrement(&ref_);
    if (!r) delete this;
    return r;
  }
  STDMETHODIMP DoEditSession(TfEditCookie ec) override { fn_(ec); return S_OK; }
private:
  LONG ref_ = 1;
  std::function<void(TfEditCookie)> fn_;
};

template <class T> void SafeRelease(T*& p) { if (p) { p->Release(); p = nullptr; } }
bool Down(int vk) { return (GetKeyState(vk) & 0x8000) != 0; }
// 計時器回呼找得到是哪個 TextService：每條 UI 執行緒只有一個
thread_local TextService* t_settleOwner = nullptr;
thread_local TextService* t_candOwner = nullptr;
const UINT kSettleDelayMs = 200;   // 停手多久開始問
const UINT kSettlePollMs = 100;    // 複審還沒好，隔多久再問
const int kSettleMaxTries = 15;
}  // namespace

TextService::TextService() { DllAddRef(); }
TextService::~TextService() { DllRelease(); }

STDMETHODIMP TextService::QueryInterface(REFIID riid, void** ppv) {
  if (!ppv) return E_INVALIDARG;
  *ppv = nullptr;
  if (riid == IID_IUnknown || riid == IID_ITfTextInputProcessor || riid == IID_ITfTextInputProcessorEx) *ppv = (ITfTextInputProcessorEx*)this;
  else if (riid == IID_ITfKeyEventSink) *ppv = (ITfKeyEventSink*)this;
  else if (riid == IID_ITfCompositionSink) *ppv = (ITfCompositionSink*)this;
  else if (riid == IID_ITfThreadMgrEventSink) *ppv = (ITfThreadMgrEventSink*)this;
  else if (riid == IID_ITfDisplayAttributeProvider) *ppv = (ITfDisplayAttributeProvider*)this;
  if (!*ppv) return E_NOINTERFACE;
  AddRef();
  return S_OK;
}
STDMETHODIMP_(ULONG) TextService::AddRef() { return InterlockedIncrement(&ref_); }
STDMETHODIMP_(ULONG) TextService::Release() {
  LONG r = InterlockedDecrement(&ref_);
  if (!r) delete this;
  return r;
}

// ── 啟用／停用 ──
STDMETHODIMP TextService::ActivateEx(ITfThreadMgr* tm, TfClientId tid, DWORD flags) {
  TipLog(L"ActivateEx 開始 flags=0x%X", (unsigned)flags);
  tm_ = tm;
  tm_->AddRef();
  tid_ = tid;
  ITfCategoryMgr* cm = nullptr;
  if (SUCCEEDED(CoCreateInstance(CLSID_TF_CategoryMgr, nullptr, CLSCTX_INPROC_SERVER, IID_ITfCategoryMgr, (void**)&cm))) {
    cm->RegisterGUID(GUID_DongNiInputAttr, &inputAttr_);
    cm->Release();
  }
  ITfSource* src = nullptr;
  if (SUCCEEDED(tm_->QueryInterface(IID_ITfSource, (void**)&src))) {
    src->AdviseSink(IID_ITfThreadMgrEventSink, (ITfThreadMgrEventSink*)this, &tmSinkCookie_);
    src->Release();
  }
  ITfKeystrokeMgr* km = nullptr;
  if (SUCCEEDED(tm_->QueryInterface(IID_ITfKeystrokeMgr, (void**)&km))) {
    HRESULT hrKey = km->AdviseKeyEventSink(tid_, (ITfKeyEventSink*)this, TRUE);
    if (FAILED(hrKey)) TipLog(L"AdviseKeyEventSink 失敗 0x%08X", (unsigned)hrKey);
    km->Release();
  } else {
    TipLog(L"取不到 ITfKeystrokeMgr");
  }
  ITfLangBarItemMgr* lbm = nullptr;
  if (SUCCEEDED(tm_->QueryInterface(IID_ITfLangBarItemMgr, (void**)&lbm))) {
    langBar_ = new LangBarButton([this]() { ToggleFromLangBar(); });
    HRESULT hrBar = lbm->AddItem(langBar_);
    if (FAILED(hrBar)) { TipLog(L"AddItem（右下角圖示）失敗 0x%08X", (unsigned)hrBar); SafeRelease(langBar_); }
    lbm->Release();
  }
  NotifyApp();
  TipLog(L"ActivateEx 完成");
  return S_OK;
}

STDMETHODIMP TextService::Deactivate() {
  TipLog(L"Deactivate");
  if (candMoveTimer_) { KillTimer(nullptr, candMoveTimer_); candMoveTimer_ = 0; }
  CancelSettle();
  CommitOnFocusLoss();
  cand_.Hide();
  if (tm_ && langBar_) {
    ITfLangBarItemMgr* lbm = nullptr;
    if (SUCCEEDED(tm_->QueryInterface(IID_ITfLangBarItemMgr, (void**)&lbm))) {
      lbm->RemoveItem(langBar_);
      lbm->Release();
    }
  }
  SafeRelease(langBar_);
  if (tm_) {
    ITfKeystrokeMgr* km = nullptr;
    if (SUCCEEDED(tm_->QueryInterface(IID_ITfKeystrokeMgr, (void**)&km))) {
      km->UnadviseKeyEventSink(tid_);
      km->Release();
    }
    ITfSource* src = nullptr;
    if (tmSinkCookie_ != TF_INVALID_COOKIE && SUCCEEDED(tm_->QueryInterface(IID_ITfSource, (void**)&src))) {
      src->UnadviseSink(tmSinkCookie_);
      src->Release();
    }
    tmSinkCookie_ = TF_INVALID_COOKIE;
    SafeRelease(tm_);
  }
  pipe_.Close();
  tid_ = TF_CLIENTID_NULL;
  return S_OK;
}

HRESULT TextService::EditSync(ITfContext* pic, DWORD flags, std::function<void(TfEditCookie)> fn) {
  EditSession* es = new EditSession(std::move(fn));
  HRESULT hrSession = E_FAIL;
  HRESULT hr = pic->RequestEditSession(tid_, es, TF_ES_SYNC | flags, &hrSession);
  es->Release();
  return FAILED(hr) ? hr : hrSession;
}

// ── 按鍵 ──
// 把 Windows 的虛擬鍵碼轉成網頁的 KeyboardEvent.code（引擎服務用跟測試台一樣的鍵名）
bool TextService::KeyToCode(WPARAM vk, LPARAM lp, std::string& code) {
  char b[16];
  if (vk >= 'A' && vk <= 'Z') { sprintf_s(b, "Key%c", (char)vk); code = b; return true; }
  if (vk >= '0' && vk <= '9') { sprintf_s(b, "Digit%c", (char)vk); code = b; return true; }
  if (vk >= VK_NUMPAD0 && vk <= VK_NUMPAD9) { sprintf_s(b, "Numpad%d", (int)(vk - VK_NUMPAD0)); code = b; return true; }
  switch (vk) {
    case VK_OEM_1: code = "Semicolon"; return true;
    case VK_OEM_COMMA: code = "Comma"; return true;
    case VK_OEM_PERIOD: code = "Period"; return true;
    case VK_OEM_2: code = "Slash"; return true;
    case VK_OEM_MINUS: code = "Minus"; return true;
    case VK_OEM_PLUS: code = "Equal"; return true;
    case VK_OEM_7: code = "Quote"; return true;
    case VK_OEM_4: code = "BracketLeft"; return true;
    case VK_OEM_6: code = "BracketRight"; return true;
    case VK_OEM_5: code = "Backslash"; return true;
    case VK_OEM_3: code = "Backquote"; return true;
    case VK_SPACE: code = "Space"; return true;
    case VK_RETURN: code = (lp & (1 << 24)) ? "NumpadEnter" : "Enter"; return true;
    case VK_BACK: code = "Backspace"; return true;
    case VK_DELETE: code = "Delete"; return true;
    case VK_ESCAPE: code = "Escape"; return true;
    case VK_LEFT: code = "ArrowLeft"; return true;
    case VK_RIGHT: code = "ArrowRight"; return true;
    case VK_UP: code = "ArrowUp"; return true;
    case VK_DOWN: code = "ArrowDown"; return true;
    case VK_HOME: code = "Home"; return true;
    case VK_END: code = "End"; return true;
    case VK_PRIOR: code = "PageUp"; return true;
    case VK_NEXT: code = "PageDown"; return true;
    case VK_TAB: code = "Tab"; return true;
  }
  return false;  // 其他鍵（F1、音量鍵…）輸入法不管
}

std::string TextService::KeyRequest(const char* type, const std::string& code) {
  std::string r = "{\"t\":\"";
  r += type;
  r += "\",\"code\":" + JsonQuote(code);
  r += Down(VK_SHIFT) ? ",\"shift\":true" : "";
  r += Down(VK_CONTROL) ? ",\"ctrl\":true" : "";
  r += Down(VK_MENU) ? ",\"alt\":true" : "";
  return r + "}";
}

STDMETHODIMP TextService::OnTestKeyDown(ITfContext* pic, WPARAM wp, LPARAM lp, BOOL* eaten) {
  *eaten = FALSE;
  static bool firstKey = true;
  if (firstKey) { firstKey = false; TipLog(L"收到第一個按鍵（輸入法有在接鍵盤）"); }
  if (wp == VK_SHIFT) {
    if (!shiftDown_) { shiftDown_ = true; shiftClean_ = true; shiftAt_ = GetTickCount(); }
    return S_OK;
  }
  shiftClean_ = false;
  // 單按 Ctrl、Alt、Win、Caps Lock 不用管（要等它跟別的鍵一起按）
  if (wp == VK_CONTROL || wp == VK_MENU || wp == VK_LWIN || wp == VK_RWIN || wp == VK_CAPITAL || wp == VK_LSHIFT || wp == VK_RSHIFT) return S_OK;
  std::string code;
  if (!KeyToCode(wp, lp, code)) {
    // 不認得的鍵：在組字中就先接手（引擎會把組字區送出），不然交給程式
    if (!comp_ && !candVisible_) return S_OK;
    code = "Other";
  }
  // Caps Lock 開著：打英文（跟微軟注音一樣），組字中除外
  if ((GetKeyState(VK_CAPITAL) & 1) && !comp_ && !candVisible_) return S_OK;
  JVal reply;
  if (pipe_.Call(KeyRequest("test", code), reply)) *eaten = reply["handled"].truthy();
  return S_OK;
}

STDMETHODIMP TextService::OnKeyDown(ITfContext* pic, WPARAM wp, LPARAM lp, BOOL* eaten) {
  BOOL test = FALSE;
  OnTestKeyDown(pic, wp, lp, &test);
  *eaten = FALSE;
  if (!test) return S_OK;
  std::string code;
  if (!KeyToCode(wp, lp, code)) code = "Other";
  JVal reply;
  if (!pipe_.Call(KeyRequest("key", code), reply)) return S_OK;
  *eaten = reply["handled"].truthy() || !reply["commit"].s.empty();
  Apply(pic, reply);
  if (wp == VK_SPACE && Down(VK_CONTROL)) ShowMode(pic, reply["mode"].s);  // Ctrl＋空白：切換中英
  if (comp_ && !candVisible_) ScheduleSettle(pic); else CancelSettle();
  return S_OK;
}

// ── 停手後更新 AI 複審結果 ──
void TextService::ScheduleSettle(ITfContext* pic) {
  CancelSettle();
  settleCtx_ = pic;
  settleCtx_->AddRef();
  settleTries_ = 0;
  t_settleOwner = this;
  settleTimer_ = SetTimer(nullptr, 0, kSettleDelayMs, SettleTimerProc);
}

void TextService::CancelSettle() {
  if (settleTimer_) { KillTimer(nullptr, settleTimer_); settleTimer_ = 0; }
  SafeRelease(settleCtx_);
}

void CALLBACK TextService::SettleTimerProc(HWND, UINT, UINT_PTR id, DWORD) {
  KillTimer(nullptr, id);
  TextService* self = t_settleOwner;
  if (self && self->settleTimer_ == id) { self->settleTimer_ = 0; self->Settle(); }
}

void TextService::ApplyAsync(ITfContext* pic, const std::wstring& commit, const std::wstring& preedit, int caret) {
  lastPreedit_ = preedit;
  pic->AddRef();  // 編輯可能晚點才執行，在裡面才放掉
  EditSession* es = new EditSession([this, pic, commit, preedit, caret](TfEditCookie ec) {
    if (!commit.empty()) {
      if (comp_) { SetCompositionText(ec, pic, commit, (int)commit.size()); EndComposition(ec, pic); }
      else InsertPlain(ec, pic, commit);
    }
    if (!preedit.empty()) {
      if (!comp_) StartComposition(ec, pic);
      if (comp_) SetCompositionText(ec, pic, preedit, caret);
    } else if (comp_) {
      SetCompositionText(ec, pic, L"", 0);
      EndComposition(ec, pic);
    }
    pic->Release();
  });
  HRESULT hr = S_OK;
  if (FAILED(pic->RequestEditSession(tid_, es, TF_ES_ASYNCDONTCARE | TF_ES_READWRITE, &hr))) pic->Release();
  es->Release();
}

// 點右下角的「懂／英」圖示：切換中英（組字中的字會先送出）
void TextService::ToggleFromLangBar() {
  JVal reply;
  if (!pipe_.Call("{\"t\":\"toggle\"}", reply)) return;
  if (langBar_) langBar_->SetMode(reply["mode"].s == "en");
  if (candVisible_) { cand_.Hide(); candVisible_ = false; }
  CancelSettle();
  std::wstring commit = Utf8ToWide(reply["commit"].s), preedit = Utf8ToWide(reply["preedit"].s);
  if (commit.empty() && preedit.empty() && !comp_) return;
  ITfDocumentMgr* dm = nullptr;
  ITfContext* pic = nullptr;
  if (tm_ && SUCCEEDED(tm_->GetFocus(&dm)) && dm && SUCCEEDED(dm->GetTop(&pic)) && pic) {
    ApplyAsync(pic, commit, preedit, (int)reply["caret"].n);
    pic->Release();
  }
  if (dm) dm->Release();
}

void TextService::Settle() {
  ITfContext* pic = settleCtx_;
  settleCtx_ = nullptr;
  if (!pic) return;
  JVal reply;
  // 引擎服務馬上回（不等複審），所以這裡不會卡住打字；複審還在算就過 kSettlePollMs 再問
  if (comp_ && !candVisible_ && pipe_.Call("{\"t\":\"settle\"}", reply)) {
    std::wstring preedit = Utf8ToWide(reply["preedit"].s);
    int caret = (int)reply["caret"].n;
    if (!preedit.empty() && preedit != lastPreedit_ && reply["cand"].type != JVal::Obj) ApplyAsync(pic, L"", preedit, caret);
    if (reply["busy"].truthy() && ++settleTries_ < kSettleMaxTries) {
      settleCtx_ = pic;  // 交給下一次計時器，不在這裡放掉
      settleTimer_ = SetTimer(nullptr, 0, kSettlePollMs, SettleTimerProc);
      return;
    }
  }
  pic->Release();
}

STDMETHODIMP TextService::OnTestKeyUp(ITfContext* pic, WPARAM wp, LPARAM, BOOL* eaten) {
  *eaten = FALSE;
  if (wp == VK_SHIFT && shiftDown_) {
    bool tap = shiftClean_ && GetTickCount() - shiftAt_ < 500;
    shiftDown_ = false;
    shiftClean_ = false;
    if (tap) {
      JVal reply;
      if (pipe_.Call("{\"t\":\"toggle\"}", reply)) {
        Apply(pic, reply);
        ShowMode(pic, reply["mode"].s);
      }
    }
  }
  return S_OK;
}

STDMETHODIMP TextService::OnKeyUp(ITfContext* pic, WPARAM wp, LPARAM lp, BOOL* eaten) {
  return OnTestKeyUp(pic, wp, lp, eaten);
}

// ── 套用回覆 ──
void TextService::Apply(ITfContext* pic, const JVal& reply) {
  // 注音湊不出音（ㄈㄦ）：播內建的短提示音，非同步不卡打字
  if (reply["beep"].truthy()) PlaySoundW(MAKEINTRESOURCEW(2), g_hInst, SND_RESOURCE | SND_ASYNC | SND_NODEFAULT);
  std::wstring commit = Utf8ToWide(reply["commit"].s), preedit = Utf8ToWide(reply["preedit"].s);
  int caret = (int)reply["caret"].n;
  lastPreedit_ = preedit;
  if (langBar_ && reply["mode"].type == JVal::Str) langBar_->SetMode(reply["mode"].s == "en");
  bool gotPoint = false, charOk = false;
  POINT pt = {};
  int lh = lineHeight_;
  EditSync(pic, TF_ES_READWRITE, [&](TfEditCookie ec) {
    if (!commit.empty()) {
      if (!comp_) StartComposition(ec, pic);
      if (comp_) {
        SetCompositionText(ec, pic, commit, (int)commit.size());
        EndComposition(ec, pic);
      } else {
        InsertPlain(ec, pic, commit);  // 不支援組字區的程式：直接插入
      }
    }
    if (!preedit.empty()) {
      if (!comp_) StartComposition(ec, pic);
      if (comp_) {
        SetCompositionText(ec, pic, preedit, caret);
        ITfRange* r = nullptr;
        if (SUCCEEDED(comp_->GetRange(&r))) {
          // 選字中：選字窗對齊要換的那個字；不然對齊組字區開頭
          const JVal& at = reply["cand"]["at"];
          candCharAt_ = at.type == JVal::Num ? (int)at.n : 0;
          if (candCharAt_ > 0) charOk = CharPoint(ec, pic, candCharAt_, pt, lh);
          gotPoint = charOk || CaretPoint(ec, pic, r, pt, lh);
          r->Release();
        }
      }
    } else if (comp_) {
      SetCompositionText(ec, pic, L"", 0);
      EndComposition(ec, pic);
    }
  });
  if (gotPoint) { candAt_ = pt; lineHeight_ = lh; }

  const JVal& cand = reply["cand"];
  if (cand.type == JVal::Obj) {
    std::vector<std::wstring> items;
    for (const JVal& v : cand["items"].a) items.push_back(Utf8ToWide(v.s));
    cand_.Show(candAt_, lineHeight_, items, (int)cand["hi"].n, (int)cand["page"].n, (int)cand["pages"].n, Utf8ToWide(cand["title"].s));
    candVisible_ = true;
    if (candCharAt_ > 0 && !charOk) { candMoveTries_ = 0; ScheduleCandMove(); }
  } else if (candVisible_) {
    cand_.Hide();
    candVisible_ = false;
  }
}

bool TextService::CharPoint(TfEditCookie ec, ITfContext* pic, int charAt, POINT& pt, int& lh) {
  ITfRange* r = nullptr;
  ITfRange* ch = nullptr;
  bool ok = false;
  if (comp_ && SUCCEEDED(comp_->GetRange(&r)) && SUCCEEDED(r->Clone(&ch))) {
    LONG moved = 0;
    ch->Collapse(ec, TF_ANCHOR_START);
    ch->ShiftStart(ec, charAt, &moved, nullptr);
    ch->Collapse(ec, TF_ANCHOR_START);
    ch->ShiftEnd(ec, 1, &moved, nullptr);
    POINT start = {};
    int startLh = 0;
    ok = moved == 1 && CaretPoint(ec, pic, ch, pt, lh);
    // 有些程式對還沒排版的字回傳整個組字區的位置：跟開頭一樣就當作沒算到（第一個字以外）
    if (ok && CaretPoint(ec, pic, r, start, startLh) && start.x == pt.x && start.y == pt.y) ok = false;
  }
  SafeRelease(ch);
  SafeRelease(r);
  return ok;
}

void TextService::ScheduleCandMove() {
  if (candMoveTimer_) KillTimer(nullptr, candMoveTimer_);
  t_candOwner = this;
  candMoveTimer_ = SetTimer(nullptr, 0, 40, CandMoveTimerProc);
}

void CALLBACK TextService::CandMoveTimerProc(HWND, UINT, UINT_PTR id, DWORD) {
  KillTimer(nullptr, id);
  TextService* self = t_candOwner;
  if (!self || self->candMoveTimer_ != id) return;
  self->candMoveTimer_ = 0;
  if (!self->candVisible_ || !self->comp_ || !self->tm_) return;
  ITfDocumentMgr* dm = nullptr;
  ITfContext* pic = nullptr;
  bool ok = false;
  POINT pt = {};
  int lh = self->lineHeight_;
  if (SUCCEEDED(self->tm_->GetFocus(&dm)) && dm && SUCCEEDED(dm->GetTop(&pic)) && pic)
    self->EditSync(pic, TF_ES_READ, [&](TfEditCookie ec) { ok = self->CharPoint(ec, pic, self->candCharAt_, pt, lh); });
  SafeRelease(pic);
  SafeRelease(dm);
  if (ok) { self->candAt_ = pt; self->cand_.MoveTo(pt, lh); }
  else if (++self->candMoveTries_ < 10) self->ScheduleCandMove();
  else TipLog(L"選字窗：算不到第 %d 個字的位置，留在組字區開頭", self->candCharAt_);
}

void TextService::StartComposition(TfEditCookie ec, ITfContext* pic) {
  ITfInsertAtSelection* ias = nullptr;
  ITfContextComposition* cc = nullptr;
  ITfRange* range = nullptr;
  if (SUCCEEDED(pic->QueryInterface(IID_ITfInsertAtSelection, (void**)&ias)) &&
      SUCCEEDED(ias->InsertTextAtSelection(ec, TF_IAS_QUERYONLY, nullptr, 0, &range)) &&
      SUCCEEDED(pic->QueryInterface(IID_ITfContextComposition, (void**)&cc))) {
    ITfComposition* comp = nullptr;
    if (SUCCEEDED(cc->StartComposition(ec, range, (ITfCompositionSink*)this, &comp)) && comp) comp_ = comp;
  }
  SafeRelease(range);
  SafeRelease(cc);
  SafeRelease(ias);
}

void TextService::SetCompositionText(TfEditCookie ec, ITfContext* pic, const std::wstring& text, int caret) {
  ITfRange* r = nullptr;
  if (FAILED(comp_->GetRange(&r))) return;
  // 字沒變（例如按 ↓ 開選字窗、移游標）就不重設：重設會讓程式重新排版，一時算不到字的位置
  std::wstring cur(text.size() + 1, wchar_t(0));
  ULONG got = 0;
  if (FAILED(r->GetText(ec, 0, &cur[0], (ULONG)cur.size(), &got)) || got != text.size() || cur.compare(0, got, text) != 0)
    r->SetText(ec, 0, text.c_str(), (LONG)text.size());
  SetInputAttr(ec, pic, r, true);
  // 把程式的游標放到組字區裡對應的位置
  ITfRange* at = nullptr;
  if (SUCCEEDED(r->Clone(&at))) {
    at->Collapse(ec, TF_ANCHOR_START);
    LONG moved = 0;
    at->ShiftStart(ec, caret, &moved, nullptr);
    at->Collapse(ec, TF_ANCHOR_START);
    TF_SELECTION sel = {};
    sel.range = at;
    sel.style.ase = TF_AE_NONE;
    sel.style.fInterimChar = FALSE;
    pic->SetSelection(ec, 1, &sel);
    at->Release();
  }
  r->Release();
}

void TextService::SetInputAttr(TfEditCookie ec, ITfContext* pic, ITfRange* range, bool on) {
  if (inputAttr_ == TF_INVALID_GUIDATOM) return;
  ITfProperty* prop = nullptr;
  if (FAILED(pic->GetProperty(GUID_PROP_ATTRIBUTE, &prop))) return;
  if (on) {
    VARIANT v;
    VariantInit(&v);
    v.vt = VT_I4;
    v.lVal = (LONG)inputAttr_;
    prop->SetValue(ec, range, &v);
  } else {
    prop->Clear(ec, range);
  }
  prop->Release();
}

void TextService::EndComposition(TfEditCookie ec, ITfContext* pic) {
  if (!comp_) return;
  ITfRange* r = nullptr;
  if (SUCCEEDED(comp_->GetRange(&r))) {
    SetInputAttr(ec, pic, r, false);  // 送出的字不要留著底線
    r->Collapse(ec, TF_ANCHOR_END);
    TF_SELECTION sel = {};
    sel.range = r;
    sel.style.ase = TF_AE_NONE;
    pic->SetSelection(ec, 1, &sel);
    r->Release();
  }
  comp_->EndComposition(ec);
  SafeRelease(comp_);
}

void TextService::InsertPlain(TfEditCookie ec, ITfContext* pic, const std::wstring& text) {
  ITfInsertAtSelection* ias = nullptr;
  if (FAILED(pic->QueryInterface(IID_ITfInsertAtSelection, (void**)&ias))) return;
  ITfRange* r = nullptr;
  if (SUCCEEDED(ias->InsertTextAtSelection(ec, 0, text.c_str(), (LONG)text.size(), &r)) && r) {
    r->Collapse(ec, TF_ANCHOR_END);
    TF_SELECTION sel = {};
    sel.range = r;
    sel.style.ase = TF_AE_NONE;
    pic->SetSelection(ec, 1, &sel);
    r->Release();
  }
  ias->Release();
}

bool TextService::CaretPoint(TfEditCookie ec, ITfContext* pic, ITfRange* range, POINT& pt, int& lineHeight) {
  ITfContextView* view = nullptr;
  if (FAILED(pic->GetActiveView(&view))) return false;
  RECT rc = {};
  BOOL clipped = FALSE;
  bool ok = SUCCEEDED(view->GetTextExt(ec, range, &rc, &clipped)) && (rc.bottom > rc.top || rc.right > rc.left);
  view->Release();
  if (ok) { pt.x = rc.left; pt.y = rc.bottom; lineHeight = max(1, (int)(rc.bottom - rc.top)); }
  return ok;
}

// 程式自己結束組字（例如滑鼠點到別的地方）：把組字區照引擎的結果送出
STDMETHODIMP TextService::OnCompositionTerminated(TfEditCookie ec, ITfComposition* comp) {
  JVal reply;
  if (pipe_.Call("{\"t\":\"commit\"}", reply) && comp) {
    std::wstring text = Utf8ToWide(reply["commit"].s);
    ITfRange* r = nullptr;
    if (!text.empty() && SUCCEEDED(comp->GetRange(&r))) {
      r->SetText(ec, 0, text.c_str(), (LONG)text.size());
      ITfContext* ctx = nullptr;
      if (SUCCEEDED(r->GetContext(&ctx))) { SetInputAttr(ec, ctx, r, false); ctx->Release(); }
      r->Release();
    }
  }
  if (comp == comp_) SafeRelease(comp_);
  cand_.Hide();
  candVisible_ = false;
  return S_OK;
}

// 換到別的輸入框或視窗：先把打到一半的字送出，再照這個程式切換中英
STDMETHODIMP TextService::OnSetFocus(ITfDocumentMgr* focus, ITfDocumentMgr*) {
  CommitOnFocusLoss();
  if (focus) NotifyApp();
  return S_OK;
}

void TextService::NotifyApp() {
  // 輸入法 DLL 載在每個程式自己裡面，所以自己的執行檔名稱就是現在用的程式
  static std::string app;
  if (app.empty()) {
    wchar_t path[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    const wchar_t* name = wcsrchr(path, L'\\');
    app = WideToUtf8(name ? name + 1 : path);
  }
  JVal reply;
  if (pipe_.CallIfRunning("{\"t\":\"focus\",\"app\":" + JsonQuote(app) + "}", reply) && langBar_ && reply["mode"].type == JVal::Str)
    langBar_->SetMode(reply["mode"].s == "en");
}

void TextService::CommitOnFocusLoss() {
  if (!comp_ && !candVisible_) return;
  cand_.Hide();
  candVisible_ = false;
  JVal reply;
  bool ok = pipe_.Call("{\"t\":\"commit\"}", reply);
  if (!comp_) return;
  ITfRange* r = nullptr;
  ITfContext* pic = nullptr;
  if (SUCCEEDED(comp_->GetRange(&r)) && SUCCEEDED(r->GetContext(&pic))) {
    std::wstring text = ok ? Utf8ToWide(reply["commit"].s) : L"";
    EditSync(pic, TF_ES_READWRITE, [&](TfEditCookie ec) {
      if (!comp_) return;
      SetCompositionText(ec, pic, text, (int)text.size());
      EndComposition(ec, pic);
    });
  }
  SafeRelease(pic);
  SafeRelease(r);
  SafeRelease(comp_);
}

// 切換中英時，在游標旁邊短暫顯示「中」「英」
void TextService::ShowMode(ITfContext* pic, const std::string& mode) {
  POINT pt = candAt_;
  int lh = lineHeight_;
  EditSync(pic, TF_ES_READ, [&](TfEditCookie ec) {
    TF_SELECTION sel = {};
    ULONG n = 0;
    if (SUCCEEDED(pic->GetSelection(ec, TF_DEFAULT_SELECTION, 1, &sel, &n)) && n == 1) {
      CaretPoint(ec, pic, sel.range, pt, lh);
      sel.range->Release();
    }
  });
  cand_.ShowMessage(pt, lh, mode == "en" ? L"英" : L"中", 800);
}
