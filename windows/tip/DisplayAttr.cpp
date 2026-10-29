// 組字區的外觀（虛線底線）：告訴程式組字中的字要怎麼畫。
// 沒提供的話，Chrome、Teams 這類程式會自己畫預設底線，但記事本什麼都不畫。
#include "Common.h"

// {3606FA1F-6201-496C-9A97-7CBF3EE4FB5C}
const GUID GUID_DongNiInputAttr = { 0x3606fa1f, 0x6201, 0x496c, { 0x9a, 0x97, 0x7c, 0xbf, 0x3e, 0xe4, 0xfb, 0x5c } };

namespace {

class DisplayAttrInfo : public ITfDisplayAttributeInfo {
public:
  STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
    if (!ppv) return E_INVALIDARG;
    if (riid == IID_IUnknown || riid == IID_ITfDisplayAttributeInfo) { *ppv = this; AddRef(); return S_OK; }
    *ppv = nullptr;
    return E_NOINTERFACE;
  }
  STDMETHODIMP_(ULONG) AddRef() override { return InterlockedIncrement(&ref_); }
  STDMETHODIMP_(ULONG) Release() override {
    LONG r = InterlockedDecrement(&ref_);
    if (!r) delete this;
    return r;
  }
  STDMETHODIMP GetGUID(GUID* guid) override {
    if (!guid) return E_INVALIDARG;
    *guid = GUID_DongNiInputAttr;
    return S_OK;
  }
  STDMETHODIMP GetDescription(BSTR* desc) override {
    if (!desc) return E_INVALIDARG;
    *desc = SysAllocString(L"懂你注音組字中");
    return *desc ? S_OK : E_OUTOFMEMORY;
  }
  STDMETHODIMP GetAttributeInfo(TF_DISPLAYATTRIBUTE* da) override {
    if (!da) return E_INVALIDARG;
    *da = {};
    da->crText.type = TF_CT_NONE;
    da->crBk.type = TF_CT_NONE;
    da->crLine.type = TF_CT_NONE;  // 底線用文字的顏色
    da->lsStyle = TF_LS_DOT;       // 虛線
    da->fBoldLine = FALSE;
    da->bAttr = TF_ATTR_INPUT;
    return S_OK;
  }
  STDMETHODIMP SetAttributeInfo(const TF_DISPLAYATTRIBUTE*) override { return E_NOTIMPL; }
  STDMETHODIMP Reset() override { return S_OK; }
private:
  LONG ref_ = 1;
};

class DisplayAttrEnum : public IEnumTfDisplayAttributeInfo {
public:
  STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
    if (!ppv) return E_INVALIDARG;
    if (riid == IID_IUnknown || riid == IID_IEnumTfDisplayAttributeInfo) { *ppv = this; AddRef(); return S_OK; }
    *ppv = nullptr;
    return E_NOINTERFACE;
  }
  STDMETHODIMP_(ULONG) AddRef() override { return InterlockedIncrement(&ref_); }
  STDMETHODIMP_(ULONG) Release() override {
    LONG r = InterlockedDecrement(&ref_);
    if (!r) delete this;
    return r;
  }
  STDMETHODIMP Clone(IEnumTfDisplayAttributeInfo** out) override {
    if (!out) return E_INVALIDARG;
    DisplayAttrEnum* e = new DisplayAttrEnum();
    e->pos_ = pos_;
    *out = e;
    return S_OK;
  }
  // 只有一種外觀
  STDMETHODIMP Next(ULONG count, ITfDisplayAttributeInfo** infos, ULONG* fetched) override {
    if (!infos) return E_INVALIDARG;
    ULONG n = 0;
    if (count > 0 && pos_ == 0) { infos[0] = new DisplayAttrInfo(); n = 1; pos_ = 1; }
    if (fetched) *fetched = n;
    return n == count ? S_OK : S_FALSE;
  }
  STDMETHODIMP Reset() override { pos_ = 0; return S_OK; }
  STDMETHODIMP Skip(ULONG count) override {
    if (count > 0 && pos_ == 0) { pos_ = 1; return count == 1 ? S_OK : S_FALSE; }
    return count == 0 ? S_OK : S_FALSE;
  }
private:
  LONG ref_ = 1;
  ULONG pos_ = 0;
};

}  // namespace

HRESULT CreateDisplayAttrEnum(IEnumTfDisplayAttributeInfo** out) {
  if (!out) return E_INVALIDARG;
  *out = new DisplayAttrEnum();
  return S_OK;
}

HRESULT CreateDisplayAttrInfo(REFGUID guid, ITfDisplayAttributeInfo** out) {
  if (!out) return E_INVALIDARG;
  *out = nullptr;
  if (guid != GUID_DongNiInputAttr) return E_INVALIDARG;
  *out = new DisplayAttrInfo();
  return S_OK;
}
