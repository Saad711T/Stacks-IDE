#include <iostream>
#include <windows.h>
#include <wrl.h>
#include <WebView2.h>
#include <shlwapi.h>
#include <algorithm>


#pragma comment(lib, "Shlwapi.lib")




using namespace Microsoft::WRL;
using namespace std;




HWND hWnd;
ComPtr<ICoreWebView2Controller> webViewController;
ComPtr<ICoreWebView2> webView;



wstring GetAppDir()
{





    wchar_t path[MAX_PATH];
    GetModuleFileNameW(NULL, path, MAX_PATH);


    PathRemoveFileSpecW(path);
    return wstring(path);
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg)
    {
    case WM_SIZE:
        if (webViewController)
        {


            RECT bounds;
            GetClientRect(hwnd, &bounds);


            webViewController->put_Bounds(bounds);
        }
        return 0;
    case WM_CLOSE:
        DestroyWindow(hwnd);




        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);

        return 0;
    }


    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}



void InitializeWebView()
{


    CreateCoreWebView2EnvironmentWithOptions(nullptr, nullptr, nullptr,




        Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
            [](HRESULT result, ICoreWebView2Environment* env) -> HRESULT
            {
                env->CreateCoreWebView2Controller(hWnd,
                    Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                        [](HRESULT result, ICoreWebView2Controller* controller) -> HRESULT
                        {
                            if (controller)

                            {
                                webViewController = controller;
                                webViewController->get_CoreWebView2(&webView);
                            }

                            RECT bounds;
                            GetClientRect(hWnd, &bounds);
                            webViewController->put_Bounds(bounds);



                            webView->Navigate(L"https://papaya-zabaione-f0e1d7.netlify.app/");

                            return S_OK;

                        }).Get());
                return S_OK;
            }).Get());
}


int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int nCmdShow)
{




    WNDCLASS wc = {};
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = L"STACKS-IDE";

    RegisterClass(&wc);

    hWnd = CreateWindowEx(0, L"TempConverterApp", L"STACKS-IDE",







        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
        800, 600, nullptr, nullptr, hInstance, nullptr);

    ShowWindow(hWnd, nCmdShow);

    InitializeWebView();


    MSG msg = {};
    while (GetMessage(&msg, nullptr, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }





    return 0;


}
