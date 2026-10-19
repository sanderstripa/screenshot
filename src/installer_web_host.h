#pragma once
#include <WebView2.h>
#include <shlwapi.h>
#include <functional>
#include <string>
#include <wrl.h>
class InstallerWebHost {
    Microsoft::WRL::ComPtr<ICoreWebView2Environment> environment;
    Microsoft::WRL::ComPtr<ICoreWebView2Controller> controller;
    Microsoft::WRL::ComPtr<ICoreWebView2> web;
    HWND window{};bool ready=false;std::wstring pending;
public:
    std::function<void(const std::wstring&)> message;
    static std::wstring quote(const std::wstring& text){std::wstring out=L"\"";for(wchar_t c:text){if(c==L'\\'||c==L'\"'){out+=L'\\';out+=c;}else if(c==L'\n')out+=L"\\n";else if(c==L'\r')out+=L"\\r";else if(c==L'\t')out+=L"\\t";else if(c<32){wchar_t escaped[7]{};swprintf_s(escaped,L"\\u%04x",static_cast<unsigned>(c));out+=escaped;}else out+=c;}return out+L"\"";}
    void resize(){if(controller){RECT r{};GetClientRect(window,&r);controller->put_Bounds(r);controller->put_IsVisible(TRUE);}}
    void update(const std::wstring& state){pending=state;if(ready&&web)web->ExecuteScript((L"window.update&&window.update("+state+L")").c_str(),nullptr);}
    void close(){if(controller)controller->Close();web.Reset();controller.Reset();environment.Reset();ready=false;}
    HRESULT start(HWND hwnd,HINSTANCE instance){
        window=hwnd;wchar_t local[MAX_PATH]{};SHGetFolderPathW(nullptr,CSIDL_LOCAL_APPDATA,nullptr,0,local);
        std::wstring profile=std::wstring(local)+L"\\Screenshot\\InstallerWebView2\\"+std::to_wstring(GetCurrentProcessId());
        return CreateCoreWebView2EnvironmentWithOptions(nullptr,profile.c_str(),nullptr,
          Microsoft::WRL::Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>([this,instance](HRESULT result,ICoreWebView2Environment* env)->HRESULT{
            if(FAILED(result)||!env){MessageBoxW(window,L"Microsoft Edge WebView2 Runtime is required to open Screenshot Setup.",L"Screenshot",MB_OK|MB_ICONERROR);DestroyWindow(window);return S_OK;}
            environment=env;
            return env->CreateCoreWebView2Controller(window,Microsoft::WRL::Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>([this,instance](HRESULT result,ICoreWebView2Controller* value)->HRESULT{
                if(FAILED(result)||!value){DestroyWindow(window);return S_OK;}
                controller=value;controller->get_CoreWebView2(&web);controller->put_ZoomFactor(1.0);resize();
                Microsoft::WRL::ComPtr<ICoreWebView2Settings> settings;web->get_Settings(&settings);
                settings->put_IsStatusBarEnabled(FALSE);settings->put_AreDefaultContextMenusEnabled(FALSE);settings->put_IsZoomControlEnabled(FALSE);settings->put_AreDevToolsEnabled(FALSE);
                web->AddWebResourceRequestedFilter(L"*",COREWEBVIEW2_WEB_RESOURCE_CONTEXT_ALL);
                EventRegistrationToken token{};
                web->add_WebResourceRequested(Microsoft::WRL::Callback<ICoreWebView2WebResourceRequestedEventHandler>([this,instance](ICoreWebView2*,ICoreWebView2WebResourceRequestedEventArgs* args)->HRESULT{
                    Microsoft::WRL::ComPtr<ICoreWebView2WebResourceRequest> request;args->get_Request(&request);LPWSTR uri{};request->get_Uri(&uri);std::wstring url=uri?uri:L"";CoTaskMemFree(uri);
                    int id=0;const wchar_t* mime=L"text/plain";
                    if(url==L"https://screenshot.setup/"){id=204;mime=L"text/html; charset=utf-8";}
                    else if(url==L"https://screenshot.setup/connect.css"){id=205;mime=L"text/css; charset=utf-8";}
                    else if(url==L"https://screenshot.setup/screenshot.css"){id=206;mime=L"text/css; charset=utf-8";}
                    else if(url==L"https://screenshot.setup/setup.js"){id=207;mime=L"text/javascript; charset=utf-8";}
                    else if(url==L"https://screenshot.setup/assets/Montserrat.ttf"){id=202;mime=L"font/ttf";}
                    else if(url==L"https://screenshot.setup/assets/mobius.png"){id=203;mime=L"image/png";}
                    HRSRC resource=id?FindResourceW(instance,MAKEINTRESOURCEW(id),RT_RCDATA):nullptr;
                    Microsoft::WRL::ComPtr<IStream> stream;
                    if(resource)stream.Attach(SHCreateMemStream(static_cast<const BYTE*>(LockResource(LoadResource(instance,resource))),SizeofResource(instance,resource)));
                    Microsoft::WRL::ComPtr<ICoreWebView2WebResourceResponse> response;
                    std::wstring headers=std::wstring(L"Content-Type: ")+mime+L"\r\nCache-Control: no-store\r\nContent-Security-Policy: default-src 'self'; script-src 'self'; style-src 'self'; img-src 'self'; font-src 'self'; connect-src 'none'\r\n";
                    environment->CreateWebResourceResponse(stream.Get(),resource?200:404,resource?L"OK":L"Not Found",headers.c_str(),&response);args->put_Response(response.Get());return S_OK;
                }).Get(),&token);
                web->add_WebMessageReceived(Microsoft::WRL::Callback<ICoreWebView2WebMessageReceivedEventHandler>([this](ICoreWebView2*,ICoreWebView2WebMessageReceivedEventArgs* args)->HRESULT{
                    LPWSTR source{};args->get_Source(&source);bool trusted=source&&std::wstring(source)==L"https://screenshot.setup/";CoTaskMemFree(source);if(!trusted)return S_OK;
                    LPWSTR data{};if(SUCCEEDED(args->TryGetWebMessageAsString(&data))&&data){std::wstring text=data;CoTaskMemFree(data);if(message)message(text);}return S_OK;
                }).Get(),&token);
                web->add_NavigationStarting(Microsoft::WRL::Callback<ICoreWebView2NavigationStartingEventHandler>([](ICoreWebView2*,ICoreWebView2NavigationStartingEventArgs* args)->HRESULT{LPWSTR uri{};args->get_Uri(&uri);if(!uri||std::wstring(uri)!=L"https://screenshot.setup/")args->put_Cancel(TRUE);CoTaskMemFree(uri);return S_OK;}).Get(),&token);
                web->add_NewWindowRequested(Microsoft::WRL::Callback<ICoreWebView2NewWindowRequestedEventHandler>([](ICoreWebView2*,ICoreWebView2NewWindowRequestedEventArgs* args)->HRESULT{args->put_Handled(TRUE);return S_OK;}).Get(),&token);
                web->add_NavigationCompleted(Microsoft::WRL::Callback<ICoreWebView2NavigationCompletedEventHandler>([this](ICoreWebView2*,ICoreWebView2NavigationCompletedEventArgs* args)->HRESULT{BOOL success{};args->get_IsSuccess(&success);ready=success!=FALSE;if(ready)update(pending);return S_OK;}).Get(),&token);
                return web->Navigate(L"https://screenshot.setup/");
            }).Get());
          }).Get());
    }
};
