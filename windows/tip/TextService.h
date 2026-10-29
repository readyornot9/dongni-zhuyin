#pragma once
#include "Common.h"
#include <functional>

class TextService : public ITfTextInputProcessorEx,
                    public ITfKeyEventSink,
                    public ITfCompositionSink,
                    public ITfThreadMgrEventSink,
                    public ITfDisplayAttributeProvider {
public:
  TextService();
  ~TextService();

  // IUnknown
  STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override;
  STDMETHODIMP_(ULONG) AddRef() override;
  STDMETHODIMP_(ULONG) Release() override;

  // ITfTextInputProcessor(Ex)
  STDMETHODIMP Activate(ITfThreadMgr* tm, TfClientId tid) override { return ActivateEx(tm, tid, 0); }
  STDMETHODIMP ActivateEx(ITfThreadMgr* tm, TfClientId tid, DWORD flags) override;
  STDMETHODIMP Deactivate() override;

  // ITfKeyEventSink
  STDMETHODIMP OnSetFocus(BOOL) override { return S_OK; }
  STDMETHODIMP OnTestKeyDown(ITfContext* pic, WPARAM wp, LPARAM lp, BOOL* eaten) override;
  STDMETHODIMP OnKeyDown(ITfContext* pic, WPARAM wp, LPARAM lp, BOOL* eaten) override;
  STDMETHODIMP OnTestKeyUp(ITfContext* pic, WPARAM wp, LPARAM lp, BOOL* eaten) override;
  STDMETHODIMP OnKeyUp(ITfContext* pic, WPARAM wp, LPARAM lp, BOOL* eaten) override;
  STDMETHODIMP OnPreservedKey(ITfContext*, REFGUID, BOOL* eaten) override { *eaten = FALSE; return S_OK; }

  // ITfCompositionSink
  STDMETHODIMP OnCompositionTerminated(TfEditCookie ec, ITfComposition* comp) override;

  // ITfDisplayAttributeProvider：告訴程式組字區畫虛線底線
  STDMETHODIMP EnumDisplayAttributeInfo(IEnumTfDisplayAttributeInfo** out) override { return CreateDisplayAttrEnum(out); }
  STDMETHODIMP GetDisplayAttributeInfo(REFGUID guid, ITfDisplayAttributeInfo** out) override { return CreateDisplayAttrInfo(guid, out); }

  // ITfThreadMgrEventSink
  STDMETHODIMP OnInitDocumentMgr(ITfDocumentMgr*) override { return S_OK; }
  STDMETHODIMP OnUninitDocumentMgr(ITfDocumentMgr*) override { return S_OK; }
  STDMETHODIMP OnSetFocus(ITfDocumentMgr* focus, ITfDocumentMgr* prev) override;
  STDMETHODIMP OnPushContext(ITfContext*) override { return S_OK; }
  STDMETHODIMP OnPopContext(ITfContext*) override { return S_OK; }

private:
  // 把一段程式包成 TSF 的 edit session 同步執行（只有在 edit session 裡才能改文件內容）
  HRESULT EditSync(ITfContext* pic, DWORD flags, std::function<void(TfEditCookie)> fn);
  bool KeyToCode(WPARAM wp, LPARAM lp, std::string& code);
  std::string KeyRequest(const char* type, const std::string& code);
  // 套用引擎服務的回覆：送出確定的字、更新組字區、顯示候選字窗
  void Apply(ITfContext* pic, const JVal& reply);
  void StartComposition(TfEditCookie ec, ITfContext* pic);
  void SetCompositionText(TfEditCookie ec, ITfContext* pic, const std::wstring& text, int caret);
  void EndComposition(TfEditCookie ec, ITfContext* pic);
  void InsertPlain(TfEditCookie ec, ITfContext* pic, const std::wstring& text);
  bool CaretPoint(TfEditCookie ec, ITfContext* pic, ITfRange* range, POINT& pt, int& lineHeight);
  void CommitOnFocusLoss();
  void ShowMode(ITfContext* pic, const std::string& mode);
  // 組字區標上虛線底線的外觀；送出前清掉
  void SetInputAttr(TfEditCookie ec, ITfContext* pic, ITfRange* range, bool on);
  // 告訴引擎現在在哪個程式（cmd、Teams…），引擎照這個程式預設或上次用的模式切換中英
  void NotifyApp();
  // 停手後跟引擎服務要 AI 複審完的結果，有改字就更新組字區（不用等下一個按鍵）
  void ScheduleSettle(ITfContext* pic);
  void CancelSettle();
  void Settle();
  // 不在按鍵事件裡（計時器、點右下角圖示）時更新文件：只能要求非同步編輯，參數都用值傳
  void ApplyAsync(ITfContext* pic, const std::wstring& commit, const std::wstring& preedit, int caret);
  void ToggleFromLangBar();
  // 選字窗對齊組字區裡第 charAt 個字；算得到回傳 true
  bool CharPoint(TfEditCookie ec, ITfContext* pic, int charAt, POINT& pt, int& lh);
  // 有些程式（Chrome、Teams）剛改完字還沒排版，算不到字的位置：等一下再算一次，把選字窗移過去
  void ScheduleCandMove();
  static void CALLBACK CandMoveTimerProc(HWND, UINT, UINT_PTR, DWORD);
  static void CALLBACK SettleTimerProc(HWND, UINT, UINT_PTR, DWORD);

  LONG ref_ = 1;
  ITfThreadMgr* tm_ = nullptr;
  TfClientId tid_ = TF_CLIENTID_NULL;
  DWORD tmSinkCookie_ = TF_INVALID_COOKIE;
  ITfComposition* comp_ = nullptr;
  PipeClient pipe_;
  CandidateWindow cand_;
  bool candVisible_ = false;
  POINT candAt_ = {};
  int lineHeight_ = 20;
  // Shift 單按切換中英：按下後沒有按別的鍵、0.5 秒內放開才算
  bool shiftDown_ = false, shiftClean_ = false;
  DWORD shiftAt_ = 0;
  std::wstring lastPreedit_;
  ITfContext* settleCtx_ = nullptr;
  UINT_PTR settleTimer_ = 0;
  UINT_PTR candMoveTimer_ = 0;
  TfGuidAtom inputAttr_ = TF_INVALID_GUIDATOM;
  int candCharAt_ = 0;   // 選字窗要對齊的字（組字區裡第幾個字，0 = 開頭）
  int candMoveTries_ = 0;
  int settleTries_ = 0;
  LangBarButton* langBar_ = nullptr;
};
