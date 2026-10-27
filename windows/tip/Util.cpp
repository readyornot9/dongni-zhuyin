#include "Common.h"

std::wstring Utf8ToWide(const std::string& s) {
  if (s.empty()) return L"";
  int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
  std::wstring w(n, L'\0');
  MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), &w[0], n);
  return w;
}

std::string WideToUtf8(const std::wstring& w) {
  if (w.empty()) return "";
  int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
  std::string s(n, '\0');
  WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), &s[0], n, nullptr, nullptr);
  return s;
}

// ── JSON ──
static const JVal kNull;
const JVal& JVal::operator[](const char* key) const {
  if (type != Obj) return kNull;
  auto it = o.find(key);
  return it == o.end() ? kNull : it->second;
}

namespace {
struct Parser {
  const std::string& t;
  size_t i = 0;
  explicit Parser(const std::string& text) : t(text) {}
  void ws() { while (i < t.size() && (t[i] == ' ' || t[i] == '\t' || t[i] == '\r' || t[i] == '\n')) i++; }
  static void putUtf8(std::string& out, unsigned cp) {
    if (cp < 0x80) out += (char)cp;
    else if (cp < 0x800) { out += (char)(0xC0 | (cp >> 6)); out += (char)(0x80 | (cp & 0x3F)); }
    else if (cp < 0x10000) { out += (char)(0xE0 | (cp >> 12)); out += (char)(0x80 | ((cp >> 6) & 0x3F)); out += (char)(0x80 | (cp & 0x3F)); }
    else { out += (char)(0xF0 | (cp >> 18)); out += (char)(0x80 | ((cp >> 12) & 0x3F)); out += (char)(0x80 | ((cp >> 6) & 0x3F)); out += (char)(0x80 | (cp & 0x3F)); }
  }
  bool hex4(unsigned& v) {
    if (i + 4 > t.size()) return false;
    v = 0;
    for (int k = 0; k < 4; k++) {
      char c = t[i++];
      v <<= 4;
      if (c >= '0' && c <= '9') v |= c - '0';
      else if (c >= 'a' && c <= 'f') v |= c - 'a' + 10;
      else if (c >= 'A' && c <= 'F') v |= c - 'A' + 10;
      else return false;
    }
    return true;
  }
  bool str(std::string& out) {
    if (t[i] != '"') return false;
    i++;
    while (i < t.size()) {
      char c = t[i++];
      if (c == '"') return true;
      if (c != '\\') { out += c; continue; }
      if (i >= t.size()) return false;
      char e = t[i++];
      switch (e) {
        case 'n': out += '\n'; break;
        case 't': out += '\t'; break;
        case 'r': out += '\r'; break;
        case 'b': out += '\b'; break;
        case 'f': out += '\f'; break;
        case 'u': {
          unsigned cp;
          if (!hex4(cp)) return false;
          if (cp >= 0xD800 && cp < 0xDC00 && i + 6 <= t.size() && t[i] == '\\' && t[i + 1] == 'u') {
            i += 2;
            unsigned lo;
            if (!hex4(lo)) return false;
            cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
          }
          putUtf8(out, cp);
          break;
        }
        default: out += e;
      }
    }
    return false;
  }
  bool val(JVal& v) {
    ws();
    if (i >= t.size()) return false;
    char c = t[i];
    if (c == '{') {
      v.type = JVal::Obj; i++; ws();
      if (i < t.size() && t[i] == '}') { i++; return true; }
      while (true) {
        ws();
        std::string k;
        if (i >= t.size() || !str(k)) return false;
        ws();
        if (i >= t.size() || t[i] != ':') return false;
        i++;
        if (!val(v.o[k])) return false;
        ws();
        if (i < t.size() && t[i] == ',') { i++; continue; }
        if (i < t.size() && t[i] == '}') { i++; return true; }
        return false;
      }
    }
    if (c == '[') {
      v.type = JVal::Arr; i++; ws();
      if (i < t.size() && t[i] == ']') { i++; return true; }
      while (true) {
        v.a.emplace_back();
        if (!val(v.a.back())) return false;
        ws();
        if (i < t.size() && t[i] == ',') { i++; continue; }
        if (i < t.size() && t[i] == ']') { i++; return true; }
        return false;
      }
    }
    if (c == '"') { v.type = JVal::Str; return str(v.s); }
    if (t.compare(i, 4, "true") == 0) { v.type = JVal::Bool; v.b = true; i += 4; return true; }
    if (t.compare(i, 5, "false") == 0) { v.type = JVal::Bool; v.b = false; i += 5; return true; }
    if (t.compare(i, 4, "null") == 0) { v.type = JVal::Null; i += 4; return true; }
    size_t start = i;
    while (i < t.size() && strchr("-+.eE0123456789", t[i])) i++;
    if (start == i) return false;
    v.type = JVal::Num;
    v.n = atof(t.substr(start, i - start).c_str());
    return true;
  }
};
}  // namespace

bool JsonParse(const std::string& text, JVal& out) {
  Parser p(text);
  return p.val(out);
}

std::string JsonQuote(const std::string& s) {
  std::string out = "\"";
  for (char c : s) {
    if (c == '"' || c == '\\') { out += '\\'; out += c; }
    else if ((unsigned char)c < 0x20) { char b[8]; sprintf_s(b, "\\u%04x", c); out += b; }
    else out += c;
  }
  return out + "\"";
}

// ── 除錯紀錄 ──
#include <cstdarg>
#include <cstdio>
void TipLog(const wchar_t* fmt, ...) {
  wchar_t dir[MAX_PATH];
  DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", dir, MAX_PATH);
  if (!n || n >= MAX_PATH - 32) return;
  std::wstring path = std::wstring(dir) + L"\\ShunDa\\tip.log";
  wchar_t msg[600];
  va_list ap;
  va_start(ap, fmt);
  _vsnwprintf_s(msg, _TRUNCATE, fmt, ap);
  va_end(ap);
  wchar_t exe[MAX_PATH] = L"?";
  GetModuleFileNameW(nullptr, exe, MAX_PATH);
  const wchar_t* base = wcsrchr(exe, L'\\');
  base = base ? base + 1 : exe;
  SYSTEMTIME st;
  GetLocalTime(&st);
  wchar_t line[900];
  swprintf_s(line, L"%02d:%02d:%02d.%03d [%lu %s] %s\r\n", st.wHour, st.wMinute, st.wSecond, st.wMilliseconds, GetCurrentProcessId(), base, msg);
  std::string utf8 = WideToUtf8(line);
  HANDLE h = CreateFileW(path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (h == INVALID_HANDLE_VALUE) return;
  LARGE_INTEGER size = {};
  if (GetFileSizeEx(h, &size) && size.QuadPart > 512 * 1024) { SetFilePointer(h, 0, nullptr, FILE_BEGIN); SetEndOfFile(h); }  // 太大就從頭來
  DWORD w = 0;
  WriteFile(h, utf8.data(), (DWORD)utf8.size(), &w, nullptr);
  CloseHandle(h);
}
