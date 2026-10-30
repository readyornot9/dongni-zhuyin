#include "Common.h"

namespace {
const wchar_t kClass[] = L"ShunDaCandidateWindow";
const COLORREF kBg = RGB(255, 255, 255), kBorder = RGB(160, 160, 160), kHi = RGB(204, 228, 247), kText = RGB(0, 0, 0), kDim = RGB(120, 120, 120);
const UINT_PTR kHideTimer = 1;
bool g_registered = false;

int DpiOf(HWND h) {
  // Windows 10 1607 以後才有 GetDpiForWindow，舊系統退回 96
  typedef UINT(WINAPI * Fn)(HWND);
  static Fn fn = (Fn)GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow");
  UINT dpi = fn && h ? fn(h) : 0;
  return dpi ? (int)dpi : 96;
}
}  // namespace

CandidateWindow::~CandidateWindow() {
  if (hwnd_) DestroyWindow(hwnd_);
  if (font_) DeleteObject(font_);
}

void CandidateWindow::UnregisterClass() {
  if (g_registered) ::UnregisterClassW(kClass, g_hInst);
  g_registered = false;
}

bool CandidateWindow::Ensure() {
  if (hwnd_ && IsWindow(hwnd_)) return true;
  if (!g_registered) {
    WNDCLASSEXW wc = { sizeof(wc) };
    wc.style = CS_HREDRAW | CS_VREDRAW | CS_DROPSHADOW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = g_hInst;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = kClass;
    if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return false;
    g_registered = true;
  }
  // 不搶焦點、不出現在工作列、永遠在最上層
  hwnd_ = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, kClass, L"", WS_POPUP,
                          0, 0, 10, 10, nullptr, nullptr, g_hInst, this);
  return hwnd_ != nullptr;
}

void CandidateWindow::Show(const POINT& at, int lineHeight, const std::vector<std::wstring>& items, int hi, int page, int pages, const std::wstring& title,
                           int cols, int numRow) {
  if (!Ensure()) return;
  KillTimer(hwnd_, kHideTimer);
  items_ = items; hi_ = hi; page_ = page; pages_ = pages; title_ = title; message_.clear();
  cols_ = cols; numRow_ = numRow;
  Layout(at, lineHeight);
}

void CandidateWindow::MoveTo(const POINT& at, int lineHeight) {
  if (hwnd_ && IsWindowVisible(hwnd_) && message_.empty()) Layout(at, lineHeight);
}

void CandidateWindow::ShowMessage(const POINT& at, int lineHeight, const std::wstring& text, UINT ms) {
  if (!Ensure()) return;
  items_.clear(); title_.clear(); message_ = text; cols_ = 0;
  Layout(at, lineHeight);
  SetTimer(hwnd_, kHideTimer, ms, nullptr);
}

void CandidateWindow::Hide() {
  if (hwnd_) ShowWindow(hwnd_, SW_HIDE);
}

void CandidateWindow::Layout(const POINT& at, int lineHeight) {
  int dpi = DpiOf(hwnd_);
  if (!font_ || dpi != fontDpi_) {
    if (font_) DeleteObject(font_);
    font_ = CreateFontW(-MulDiv(12, dpi, 72), 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Microsoft JhengHei UI");
    fontDpi_ = dpi;
  }
  // 量出每一列需要的寬度
  HDC dc = GetDC(hwnd_);
  HGDIOBJ old = SelectObject(dc, font_);
  TEXTMETRICW tm;
  GetTextMetricsW(dc, &tm);
  int rowH = tm.tmHeight + MulDiv(6, dpi, 96), pad = MulDiv(6, dpi, 96), numW = 0, textW = 0;
  SIZE sz;
  GetTextExtentPoint32W(dc, L"9", 1, &sz);
  numW = sz.cx + pad;
  std::vector<std::wstring> lines = items_;
  for (auto& s : lines) { GetTextExtentPoint32W(dc, s.c_str(), (int)s.size(), &sz); textW = max(textW, (int)sz.cx); }
  int extra = 0;
  if (!title_.empty()) { GetTextExtentPoint32W(dc, title_.c_str(), (int)title_.size(), &sz); textW = max(textW, (int)sz.cx - numW); extra++; }
  if (pages_ > 1) extra++;
  if (!message_.empty()) { GetTextExtentPoint32W(dc, message_.c_str(), (int)message_.size(), &sz); textW = max(textW, (int)sz.cx - numW); }
  SelectObject(dc, old);
  ReleaseDC(hwnd_, dc);
  int rows = message_.empty() ? (int)items_.size() + extra : 1;
  int w = pad * 2 + numW + max(textW, MulDiv(48, dpi, 96)), h = pad * 2 + rows * rowH;
  if (cols_ > 0 && message_.empty()) {
    // 表格：每格一樣寬（數字＋最長的詞），一列 cols_ 格
    cellW_ = numW + textW + pad;
    int gridRows = ((int)items_.size() + cols_ - 1) / cols_;
    w = pad * 2 + cellW_ * cols_;
    h = pad * 2 + (gridRows + extra) * rowH;
  }
  // 放在組字區下方；超出螢幕就放上方、往左移
  MONITORINFO mi = { sizeof(mi) };
  GetMonitorInfoW(MonitorFromPoint(at, MONITOR_DEFAULTTONEAREST), &mi);
  int x = at.x, y = at.y + 2;
  if (x + w > mi.rcWork.right) x = mi.rcWork.right - w;
  if (x < mi.rcWork.left) x = mi.rcWork.left;
  if (y + h > mi.rcWork.bottom) y = at.y - lineHeight - h - 2;
  SetWindowPos(hwnd_, HWND_TOPMOST, x, y, w, h, SWP_NOACTIVATE | SWP_SHOWWINDOW);
  InvalidateRect(hwnd_, nullptr, TRUE);
}

void CandidateWindow::Paint(HDC dc) {
  RECT rc;
  GetClientRect(hwnd_, &rc);
  int dpi = DpiOf(hwnd_);
  HBRUSH bg = CreateSolidBrush(kBg);
  FillRect(dc, &rc, bg);
  DeleteObject(bg);
  HBRUSH border = CreateSolidBrush(kBorder);
  FrameRect(dc, &rc, border);
  DeleteObject(border);
  HGDIOBJ old = SelectObject(dc, font_);
  SetBkMode(dc, TRANSPARENT);
  TEXTMETRICW tm;
  GetTextMetricsW(dc, &tm);
  int rowH = tm.tmHeight + MulDiv(6, dpi, 96), pad = MulDiv(6, dpi, 96);
  SIZE sz;
  GetTextExtentPoint32W(dc, L"9", 1, &sz);
  int numW = sz.cx + pad, y = pad;
  if (!message_.empty()) {
    SetTextColor(dc, kText);
    TextOutW(dc, pad, y + (rowH - tm.tmHeight) / 2, message_.c_str(), (int)message_.size());
  } else {
    if (!title_.empty()) {
      SetTextColor(dc, kDim);
      TextOutW(dc, pad, y + (rowH - tm.tmHeight) / 2, title_.c_str(), (int)title_.size());
      y += rowH;
    }
    if (cols_ > 0) {
      for (size_t i = 0; i < items_.size(); i++) {
        int row = (int)i / cols_, col = (int)i % cols_;
        int x = pad + col * cellW_, cy = y + row * rowH;
        if ((int)i == hi_) {
          RECT r = { x - pad / 2, cy, x + cellW_ - pad / 2, cy + rowH };
          HBRUSH b = CreateSolidBrush(kHi);
          FillRect(dc, &r, b);
          DeleteObject(b);
        }
        if (row == numRow_) {
          wchar_t num[4];
          swprintf_s(num, L"%d", col + 1);
          SetTextColor(dc, kDim);
          TextOutW(dc, x, cy + (rowH - tm.tmHeight) / 2, num, (int)wcslen(num));
        }
        SetTextColor(dc, kText);
        TextOutW(dc, x + numW, cy + (rowH - tm.tmHeight) / 2, items_[i].c_str(), (int)items_[i].size());
      }
      y += (((int)items_.size() + cols_ - 1) / cols_) * rowH;
      if (pages_ > 1) {
        wchar_t p[48];
        swprintf_s(p, L"第 %d / %d 列", page_ + 1, pages_);
        SetTextColor(dc, kDim);
        TextOutW(dc, pad, y + (rowH - tm.tmHeight) / 2, p, (int)wcslen(p));
      }
      SelectObject(dc, old);
      return;
    }
    for (size_t i = 0; i < items_.size(); i++) {
      if ((int)i == hi_) {
        RECT r = { 1, y, rc.right - 1, y + rowH };
        HBRUSH b = CreateSolidBrush(kHi);
        FillRect(dc, &r, b);
        DeleteObject(b);
      }
      wchar_t num[4];
      swprintf_s(num, L"%d", (int)i + 1);
      SetTextColor(dc, kDim);
      TextOutW(dc, pad, y + (rowH - tm.tmHeight) / 2, num, (int)wcslen(num));
      SetTextColor(dc, kText);
      TextOutW(dc, pad + numW, y + (rowH - tm.tmHeight) / 2, items_[i].c_str(), (int)items_[i].size());
      y += rowH;
    }
    if (pages_ > 1) {
      wchar_t p[32];
      swprintf_s(p, L"%d / %d", page_ + 1, pages_);
      SetTextColor(dc, kDim);
      TextOutW(dc, pad, y + (rowH - tm.tmHeight) / 2, p, (int)wcslen(p));
    }
  }
  SelectObject(dc, old);
}

LRESULT CALLBACK CandidateWindow::WndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
  if (msg == WM_NCCREATE) SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)((CREATESTRUCTW*)lp)->lpCreateParams);
  auto* self = (CandidateWindow*)GetWindowLongPtrW(h, GWLP_USERDATA);
  switch (msg) {
    case WM_PAINT: {
      PAINTSTRUCT ps;
      HDC dc = BeginPaint(h, &ps);
      if (self) self->Paint(dc);
      EndPaint(h, &ps);
      return 0;
    }
    case WM_ERASEBKGND: return 1;
    case WM_MOUSEACTIVATE: return MA_NOACTIVATE;
    case WM_TIMER:
      if (wp == kHideTimer) { KillTimer(h, kHideTimer); ShowWindow(h, SW_HIDE); }
      return 0;
    case WM_NCDESTROY:
      if (self) self->hwnd_ = nullptr;
      break;
  }
  return DefWindowProcW(h, msg, wp, lp);
}
