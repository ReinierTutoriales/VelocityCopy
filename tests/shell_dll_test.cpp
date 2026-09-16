#include <windows.h>
#include <shobjidl.h>

#include <cwchar>
#include <iostream>

extern "C" HRESULT __stdcall DllCanUnloadNow();
extern "C" HRESULT __stdcall DllGetClassObject(REFCLSID, REFIID, void**);

namespace {
constexpr CLSID CLSID_VelocityCopyCopy =
    {0x7e1d27a7, 0xba17, 0x4eea, {0x9b, 0x93, 0x96, 0x7e, 0xe7, 0x77, 0xbd, 0x21}};
constexpr CLSID CLSID_VelocityCopyPaste =
    {0xcbba1a7e, 0x35b4, 0x4708, {0x9d, 0x03, 0x94, 0x46, 0xd0, 0x3f, 0xc8, 0x43}};

bool verify_command(REFCLSID clsid, const wchar_t* expected_title) {
    IClassFactory* factory = nullptr;
    if (FAILED(DllGetClassObject(clsid, IID_IClassFactory, reinterpret_cast<void**>(&factory))) || factory == nullptr) {
        return false;
    }

    IExplorerCommand* command = nullptr;
    const HRESULT create_hr = factory->CreateInstance(
        nullptr,
        __uuidof(IExplorerCommand),
        reinterpret_cast<void**>(&command));
    factory->Release();
    if (FAILED(create_hr) || command == nullptr) {
        return false;
    }

    PWSTR title = nullptr;
    const HRESULT title_hr = command->GetTitle(nullptr, &title);
    const bool ok = SUCCEEDED(title_hr) && title != nullptr && std::wcscmp(title, expected_title) == 0;
    CoTaskMemFree(title);
    command->Release();
    return ok;
}
} // namespace

int wmain() {
    if (!verify_command(CLSID_VelocityCopyCopy, L"Copiar con VelocityCopy")) {
        return 1;
    }
    if (!verify_command(CLSID_VelocityCopyPaste, L"Pegar con VelocityCopy")) {
        return 2;
    }
    if (DllCanUnloadNow() != S_OK) {
        return 3;
    }
    std::wcout << L"VelocityCopy shell DLL test passed.\n";
    return 0;
}
