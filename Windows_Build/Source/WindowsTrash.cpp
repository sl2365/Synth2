// Windows-only: recycle or cancel. Never request a permanent delete.
// IFileOperation + FOFX_RECYCLEONDELETE explicitly requests recycling;
// no caller may fall back to std::filesystem::remove on Windows.
#include "EditorCore/Trash.hpp"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#include <shobjidl.h>
#include <shlobj.h>
#include <objbase.h>
#include <filesystem>
#include <string>
#include <system_error>

#ifndef FOFX_RECYCLEONDELETE
#define FOFX_RECYCLEONDELETE 0x00080000
#endif

namespace oxo {
bool MoveToTrash(const std::filesystem::path& path) {
    if (path.empty()) return false;
    std::error_code ec;
    const auto absolute = std::filesystem::absolute(path, ec).lexically_normal();
    if (ec || !std::filesystem::exists(absolute, ec) || ec) return false;
    const std::wstring full = absolute.wstring();
    if (full.empty() || full.rfind(L"\\\\?\\", 0) == 0) return false;

    wchar_t volume[MAX_PATH]{};
    if (!GetVolumePathNameW(full.c_str(), volume, MAX_PATH)) return false;
    if (GetDriveTypeW(volume) != DRIVE_FIXED) return false;
    SHQUERYRBINFO bin{};
    bin.cbSize = sizeof(bin);
    if (FAILED(SHQueryRecycleBinW(volume, &bin))) return false;

    // File operations are an STA COM service. On an incompatible host
    // thread, fail safely rather than switching to another delete API.
    const HRESULT apartment = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(apartment)) return false;
    bool recycled = false;
    IFileOperation* operation = nullptr;
    IShellItem* item = nullptr;
    if (SUCCEEDED(CoCreateInstance(CLSID_FileOperation, nullptr,
                                   CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&operation)))) {
        // FOFX_RECYCLEONDELETE is more specific than the legacy
        // FOF_ALLOWUNDO hint. No confirmation flag auto-approves nuking.
        const DWORD flags = FOF_ALLOWUNDO | FOFX_RECYCLEONDELETE |
                            FOF_NOERRORUI | FOFX_EARLYFAILURE | FOF_SILENT;
        if (SUCCEEDED(operation->SetOperationFlags(flags)) &&
            SUCCEEDED(SHCreateItemFromParsingName(full.c_str(), nullptr,
                                                  IID_PPV_ARGS(&item))) &&
            SUCCEEDED(operation->DeleteItem(item, nullptr)) &&
            SUCCEEDED(operation->PerformOperations())) {
            BOOL aborted = TRUE;
            if (SUCCEEDED(operation->GetAnyOperationsAborted(&aborted)) && !aborted) {
                std::error_code check;
                recycled = !std::filesystem::exists(absolute, check) && !check;
            }
        }
    }
    if (item) item->Release();
    if (operation) operation->Release();
    CoUninitialize();
    return recycled;
}
} // namespace oxo
