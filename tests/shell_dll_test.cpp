#include <windows.h>
#include <shobjidl.h>

#include <cwchar>
#include <iostream>

namespace {
using DllCanUnloadNowFn = HRESULT(__stdcall*)();
using DllGetClassObjectFn = HRESULT(__stdcall*)(REFCLSID, REFIID, void**);

constexpr CLSID CLSID_VelocityCopyCopy =
    {0x7e1d27a7, 0xba17, 0x4eea, {0x9b, 0x93, 0x96, 0x7e, 0xe7, 0x77, 0xbd, 0x21}};
constexpr CLSID CLSID_VelocityCopyPaste =
    {0xcbba1a7e, 0x35b4, 0x4708, {0x9d, 0x03, 0x94, 0x46, 0xd0, 0x3f, 0xc8, 0x43}};

bool verify_command(
    DllGetClassObjectFn get_class_object,
    REFCLSID clsid,
    const wchar_t* expected_title) {
    IClassFactory* factory = nullptr;
    if (FAILED(get_class_object(clsid, IID_IClassFactory, reinterpret_cast<void**>(&factory))) || factory == nullptr) {
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

int wmain(int argc, wchar_t* argv[]) {
    if (argc != 2) {
        return 10;
    }

    HMODULE module = LoadLibraryW(argv[1]);
    if (module == nullptr) {
        return 11;
    }

    const auto get_class_object = reinterpret_cast<DllGetClassObjectFn>(
        GetProcAddress(module, "DllGetClassObject"));
    const auto can_unload = reinterpret_cast<DllCanUnloadNowFn>(
        GetProcAddress(module, "DllCanUnloadNow"));
    if (get_class_object == nullptr || can_unload == nullptr) {
        FreeLibrary(module);
        return 12;
    }

    SetThreadUILanguage(MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US));
    if (!verify_command(get_class_object, CLSID_VelocityCopyCopy, L"Copy with VelocityCopy") ||
        !verify_command(get_class_object, CLSID_VelocityCopyPaste, L"Paste with VelocityCopy")) {
        FreeLibrary(module);
        return 1;
    }

    SetThreadUILanguage(MAKELANGID(LANG_SPANISH, SUBLANG_SPANISH_MODERN));
    if (!verify_command(get_class_object, CLSID_VelocityCopyCopy, L"Copiar con VelocityCopy") ||
        !verify_command(get_class_object, CLSID_VelocityCopyPaste, L"Pegar con VelocityCopy")) {
        FreeLibrary(module);
        return 2;
    }

    if (can_unload() != S_OK) {
        FreeLibrary(module);
        return 3;
    }

    FreeLibrary(module);
    std::wcout << L"VelocityCopy shell DLL localization test passed.\n";
    return 0;
}
