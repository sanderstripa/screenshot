#pragma once
#include <dwrite.h>
#include <wrl.h>
#include <string>
using Microsoft::WRL::ComPtr; using Microsoft::WRL::Make; using Microsoft::WRL::RuntimeClass; using Microsoft::WRL::RuntimeClassFlags; using Microsoft::WRL::ClassicCom;
class FontEnumerator final : public RuntimeClass<RuntimeClassFlags<ClassicCom>,IDWriteFontFileEnumerator> {
    ComPtr<IDWriteFactory> factory;
    std::wstring path;
    bool visited=false;
public:
    FontEnumerator(IDWriteFactory* f,const wchar_t* p):factory(f),path(p){}
    HRESULT STDMETHODCALLTYPE MoveNext(BOOL* next) override {*next=!visited;visited=true;return S_OK;}
    HRESULT STDMETHODCALLTYPE GetCurrentFontFile(IDWriteFontFile** file) override {
        return factory->CreateFontFileReference(path.c_str(),nullptr,file);
    }
};
class FontLoader final : public RuntimeClass<RuntimeClassFlags<ClassicCom>,IDWriteFontCollectionLoader> {
public:
    HRESULT STDMETHODCALLTYPE CreateEnumeratorFromKey(IDWriteFactory* factory,const void* key,
        UINT32 bytes,IDWriteFontFileEnumerator** enumerator) override {
        if(!key || bytes<sizeof(wchar_t))return E_INVALIDARG;
        return Make<FontEnumerator>(factory,static_cast<const wchar_t*>(key)).CopyTo(enumerator);
    }
};

