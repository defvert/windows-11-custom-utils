#ifndef UNICODE
#define UNICODE
#endif

#ifndef _UNICODE
#define _UNICODE
#endif

#ifndef WINVER
#define WINVER 0x0A00
#endif

#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif

#include <windows.h>
#include <windowsx.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <dwmapi.h>
#include <gdiplus.h>
#include <objbase.h>
#include <ole2.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <new>
#include <string>
#include <vector>

#ifdef _MSC_VER
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "uuid.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "gdiplus.lib")
#endif

namespace DropPanel {

static ULONG_PTR gdiPlusToken = 0;

// ============================================================================
// Constants
// ============================================================================

constexpr wchar_t kWindowClassName[] = L"DropPanel.Window";
constexpr wchar_t kWindowTitle[] = L"DropPanel";
constexpr wchar_t kMutexName[] = L"Local\\DropPanel.Singleton";
constexpr wchar_t kToggleMessageName[] =
    L"DropPanel.Toggle.8C01A4A0-CC0D-48EA-B1B2-4E5BE4EE3C5C";

constexpr DWORD kStateMagic = 0x31504444; // "DDP1"
constexpr DWORD kStateVersion = 1;

constexpr int kMaxFiles = 3;

constexpr COLORREF kBackground = RGB(14, 20, 29); // #0E141D
constexpr COLORREF kBorder = RGB(39, 49, 61); // #27313D
constexpr COLORREF kCard = RGB(21, 29, 40); // #151D28
constexpr COLORREF kCardHover = RGB(29, 39, 52); // #1D2734
constexpr COLORREF kButton = RGB(26, 34, 45); // #1A222D
constexpr COLORREF kButtonHover = RGB(34, 45, 58); // #222D3A
constexpr COLORREF kText = RGB(227, 229, 233); // #E3E5E9
constexpr COLORREF kSubtleText = RGB(159, 168, 181); // #9FA8B5
constexpr COLORREF kAccentBorder = RGB(91, 140, 255); // #5B8CFF
constexpr COLORREF kDropBackground = RGB(23, 42, 58); // #172A3A

constexpr UINT_PTR kFlashTimerId = 0xD001;
constexpr int kEmptyPanelSize = 104;
constexpr int kPanelCornerRadius = 26;
constexpr int kButtonCornerRadius = 12;
constexpr int kCardCornerRadius = 16;
constexpr int kPlusSize = 16;
constexpr int kPlusYOffset = -2;

// ============================================================================
// Helpers
// ============================================================================

template <typename T>
class ComPtr {
public:
    ComPtr() = default;
    explicit ComPtr(T* ptr) : ptr_(ptr) {}

    ~ComPtr() {
        Reset();
    }

    ComPtr(const ComPtr&) = delete;
    ComPtr& operator=(const ComPtr&) = delete;

    ComPtr(ComPtr&& other) noexcept
        : ptr_(other.ptr_) {
        other.ptr_ = nullptr;
    }

    ComPtr& operator=(ComPtr&& other) noexcept {
        if (this != &other) {
            Reset();
            ptr_ = other.ptr_;
            other.ptr_ = nullptr;
        }
        return *this;
    }

    T* Get() const {
        return ptr_;
    }

    T** Put() {
        Reset();
        return &ptr_;
    }

    T* operator->() const {
        return ptr_;
    }

    explicit operator bool() const {
        return ptr_ != nullptr;
    }

    T* Detach() {
        T* result = ptr_;
        ptr_ = nullptr;
        return result;
    }

    void Reset() {
        if (ptr_) {
            ptr_->Release();
            ptr_ = nullptr;
        }
    }

private:
    T* ptr_ = nullptr;
};

struct ComInit {
    HRESULT hr = E_FAIL;

    ComInit() {
        hr = OleInitialize(nullptr);
    }

    ~ComInit() {
        if (SUCCEEDED(hr)) {
            OleUninitialize();
        }
    }

    bool Ok() const {
        return SUCCEEDED(hr);
    }

    ComInit(const ComInit&) = delete;
    ComInit& operator=(const ComInit&) = delete;
};

struct FileItem {
    std::wstring path;
    std::wstring name;
    HICON icon = nullptr;

    FileItem() = default;

    FileItem(
        std::wstring p,
        std::wstring n,
        HICON h
    )
        : path(std::move(p)),
          name(std::move(n)),
          icon(h) {
    }

    ~FileItem() {
        if (icon) {
            DestroyIcon(icon);
            icon = nullptr;
        }
    }

    FileItem(const FileItem&) = delete;
    FileItem& operator=(const FileItem&) = delete;

    FileItem(FileItem&& other) noexcept
        : path(std::move(other.path)),
          name(std::move(other.name)),
          icon(other.icon) {
        other.icon = nullptr;
    }

    FileItem& operator=(FileItem&& other) noexcept {
        if (this != &other) {
            if (icon) {
                DestroyIcon(icon);
            }

            path = std::move(other.path);
            name = std::move(other.name);
            icon = other.icon;
            other.icon = nullptr;
        }

        return *this;
    }
};

static std::wstring GetFileNamePart(const std::wstring& path) {
    const size_t pos = path.find_last_of(L"\\/");

    if (pos == std::wstring::npos) {
        return path;
    }

    return path.substr(pos + 1);
}

static bool FileOrDirectoryExists(const std::wstring& path) {
    return GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES;
}

static std::wstring NormalizePath(const std::wstring& input) {
    if (input.empty()) {
        return {};
    }

    DWORD required = GetFullPathNameW(
        input.c_str(),
        0,
        nullptr,
        nullptr
    );

    if (required == 0) {
        return input;
    }

    std::vector<wchar_t> buffer(
        static_cast<size_t>(required) + 2
    );

    DWORD written = GetFullPathNameW(
        input.c_str(),
        static_cast<DWORD>(buffer.size()),
        buffer.data(),
        nullptr
    );

    if (written == 0 || written >= buffer.size()) {
        return input;
    }

    return std::wstring(buffer.data(), written);
}

static HICON GetShellIcon(const std::wstring& path) {
    SHFILEINFOW sfi{};
    const DWORD_PTR result = SHGetFileInfoW(
        path.c_str(),
        0,
        &sfi,
        sizeof(sfi),
        SHGFI_ICON | SHGFI_LARGEICON
    );

    if (result == 0 || sfi.hIcon == nullptr) {
        return nullptr;
    }

    return sfi.hIcon;
}

// ============================================================================
// State storage
// ============================================================================

static std::wstring GetStateDirectory() {
    PWSTR raw = nullptr;

    const HRESULT hr = SHGetKnownFolderPath(
        FOLDERID_RoamingAppData,
        KF_FLAG_DEFAULT,
        nullptr,
        &raw
    );

    if (FAILED(hr) || !raw) {
        if (raw) {
            CoTaskMemFree(raw);
        }
        return {};
    }

    std::wstring path(raw);
    CoTaskMemFree(raw);

    path += L"\\DropPanel";

    CreateDirectoryW(path.c_str(), nullptr);

    return path;
}

static std::wstring GetStateFilePath() {
    const std::wstring dir = GetStateDirectory();

    if (dir.empty()) {
        return {};
    }

    return dir + L"\\state.bin";
}

static bool WriteExact(
    HANDLE file,
    const void* data,
    DWORD size
) {
    const BYTE* bytes = static_cast<const BYTE*>(data);
    DWORD remaining = size;

    while (remaining > 0) {
        DWORD written = 0;

        if (!WriteFile(
                file,
                bytes,
                remaining,
                &written,
                nullptr)) {
            return false;
        }

        if (written == 0) {
            return false;
        }

        bytes += written;
        remaining -= written;
    }

    return true;
}

static bool ReadEntireFile(
    const std::wstring& path,
    std::vector<BYTE>& data
) {
    HANDLE file = CreateFileW(
        path.c_str(),
        GENERIC_READ,
        FILE_SHARE_READ |
        FILE_SHARE_WRITE |
        FILE_SHARE_DELETE,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr
    );

    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }

    LARGE_INTEGER size{};

    if (!GetFileSizeEx(file, &size) ||
        size.QuadPart < 0 ||
        size.QuadPart > 16LL * 1024LL * 1024LL) {

        CloseHandle(file);
        return false;
    }

    data.resize(
        static_cast<size_t>(size.QuadPart)
    );

    if (!data.empty()) {
        DWORD read = 0;

        if (!ReadFile(
                file,
                data.data(),
                static_cast<DWORD>(data.size()),
                &read,
                nullptr) ||
            read != data.size()) {

            CloseHandle(file);
            return false;
        }
    }

    CloseHandle(file);
    return true;
}

#pragma pack(push, 1)
struct StateHeader {
    DWORD magic;
    DWORD version;
    DWORD count;
};
#pragma pack(pop)

static bool SavePathsToDisk(
    const std::vector<FileItem>& items
) {
    const std::wstring statePath = GetStateFilePath();

    if (statePath.empty()) {
        return false;
    }

    const std::wstring tempPath =
        statePath + L".tmp";

    HANDLE file = CreateFileW(
        tempPath.c_str(),
        GENERIC_WRITE,
        0,
        nullptr,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr
    );

    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }

    StateHeader header{};
    header.magic = kStateMagic;
    header.version = kStateVersion;
    header.count =
        static_cast<DWORD>(
            std::min<size_t>(
                items.size(),
                kMaxFiles
            )
        );

    bool ok =
        WriteExact(
            file,
            &header,
            sizeof(header)
        );

    if (ok) {
        for (DWORD i = 0; i < header.count; ++i) {
            const std::wstring& path =
                items[i].path;

            if (path.size() > 1024 * 1024) {
                ok = false;
                break;
            }

            const DWORD charCount =
                static_cast<DWORD>(path.size());

            if (!WriteExact(
                    file,
                    &charCount,
                    sizeof(charCount))) {

                ok = false;
                break;
            }

            if (charCount != 0) {
                const DWORD byteCount =
                    charCount *
                    static_cast<DWORD>(sizeof(wchar_t));

                if (!WriteExact(
                        file,
                        path.data(),
                        byteCount)) {

                    ok = false;
                    break;
                }
            }
        }
    }

    FlushFileBuffers(file);
    CloseHandle(file);

    if (!ok) {
        DeleteFileW(tempPath.c_str());
        return false;
    }

    if (!MoveFileExW(
            tempPath.c_str(),
            statePath.c_str(),
            MOVEFILE_REPLACE_EXISTING |
            MOVEFILE_WRITE_THROUGH)) {

        DeleteFileW(tempPath.c_str());
        return false;
    }

    return true;
}

static void LoadPathsFromDisk(
    std::vector<FileItem>& items
) {
    items.clear();

    const std::wstring statePath =
        GetStateFilePath();

    if (statePath.empty()) {
        return;
    }

    std::vector<BYTE> data;

    if (!ReadEntireFile(
            statePath,
            data)) {
        return;
    }

    if (data.size() < sizeof(StateHeader)) {
        return;
    }

    StateHeader header{};
    std::memcpy(
        &header,
        data.data(),
        sizeof(header)
    );

    if (header.magic != kStateMagic ||
        header.version != kStateVersion ||
        header.count > kMaxFiles) {

        return;
    }

    size_t offset = sizeof(StateHeader);

    for (DWORD i = 0; i < header.count; ++i) {
        if (offset + sizeof(DWORD) > data.size()) {
            items.clear();
            return;
        }

        DWORD charCount = 0;

        std::memcpy(
            &charCount,
            data.data() + offset,
            sizeof(charCount)
        );

        offset += sizeof(charCount);

        if (charCount > 1024 * 1024) {
            items.clear();
            return;
        }

        const size_t bytes =
            static_cast<size_t>(charCount) *
            sizeof(wchar_t);

        if (offset + bytes > data.size()) {
            items.clear();
            return;
        }

        std::wstring path;

        if (charCount != 0) {
            const wchar_t* ptr =
                reinterpret_cast<const wchar_t*>(
                    data.data() + offset
                );

            path.assign(
                ptr,
                ptr + charCount
            );
        }

        offset += bytes;

        path = NormalizePath(path);

        if (path.empty() ||
            !FileOrDirectoryExists(path)) {
            continue;
        }

        HICON icon = GetShellIcon(path);

        items.emplace_back(
            path,
            GetFileNamePart(path),
            icon
        );

        if (items.size() >= kMaxFiles) {
            break;
        }
    }
}

// ============================================================================
// IDataObject / IEnumFORMATETC
// ============================================================================

class FormatEtcEnumerator final
    : public IEnumFORMATETC {

public:
    explicit FormatEtcEnumerator(
        const FORMATETC& fmt
    )
        : refCount_(1),
          format_(fmt),
          index_(0) {
    }

    HRESULT STDMETHODCALLTYPE QueryInterface(
        REFIID riid,
        void** object
    ) override {
        if (!object) {
            return E_POINTER;
        }

        *object = nullptr;

        if (riid == IID_IUnknown ||
            riid == IID_IEnumFORMATETC) {

            *object =
                static_cast<IEnumFORMATETC*>(this);

            AddRef();

            return S_OK;
        }

        return E_NOINTERFACE;
    }

    ULONG STDMETHODCALLTYPE AddRef()
        override {

        return static_cast<ULONG>(
            InterlockedIncrement(&refCount_)
        );
    }

    ULONG STDMETHODCALLTYPE Release()
        override {

        const ULONG result =
            static_cast<ULONG>(
                InterlockedDecrement(
                    &refCount_
                )
            );

        if (result == 0) {
            delete this;
        }

        return result;
    }

    HRESULT STDMETHODCALLTYPE Next(
        ULONG celt,
        FORMATETC* rgelt,
        ULONG* pceltFetched
    ) override {
        if (!rgelt) {
            return E_POINTER;
        }

        if (celt != 1 &&
            !pceltFetched) {
            return E_INVALIDARG;
        }

        ULONG fetched = 0;

        while (fetched < celt &&
               index_ < 1) {

            rgelt[fetched] = format_;

            ++fetched;
            ++index_;
        }

        if (pceltFetched) {
            *pceltFetched = fetched;
        }

        return fetched == celt
            ? S_OK
            : S_FALSE;
    }

    HRESULT STDMETHODCALLTYPE Skip(
        ULONG celt
    ) override {
        if (celt >= 1 - index_) {
            index_ = 1;
            return S_FALSE;
        }

        index_ += celt;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE Reset()
        override {

        index_ = 0;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE Clone(
        IEnumFORMATETC** result
    ) override {
        if (!result) {
            return E_POINTER;
        }

        *result = nullptr;

        auto* copy =
            new (std::nothrow)
            FormatEtcEnumerator(format_);

        if (!copy) {
            return E_OUTOFMEMORY;
        }

        copy->index_ = index_;
        *result = copy;

        return S_OK;
    }

private:
    ~FormatEtcEnumerator() = default;

    LONG refCount_;
    FORMATETC format_{};
    ULONG index_;
};

class FileDataObject final
    : public IDataObject {

public:
    explicit FileDataObject(
        std::wstring path
    )
        : refCount_(1),
          path_(std::move(path)) {
    }

    HRESULT STDMETHODCALLTYPE QueryInterface(
        REFIID riid,
        void** object
    ) override {
        if (!object) {
            return E_POINTER;
        }

        *object = nullptr;

        if (riid == IID_IUnknown ||
            riid == IID_IDataObject) {

            *object =
                static_cast<IDataObject*>(this);

            AddRef();

            return S_OK;
        }

        return E_NOINTERFACE;
    }

    ULONG STDMETHODCALLTYPE AddRef()
        override {

        return static_cast<ULONG>(
            InterlockedIncrement(&refCount_)
        );
    }

    ULONG STDMETHODCALLTYPE Release()
        override {

        const ULONG result =
            static_cast<ULONG>(
                InterlockedDecrement(
                    &refCount_
                )
            );

        if (result == 0) {
            delete this;
        }

        return result;
    }

    HRESULT STDMETHODCALLTYPE GetData(
        FORMATETC* format,
        STGMEDIUM* medium
    ) override {
        if (!format || !medium) {
            return E_POINTER;
        }

        std::memset(
            medium,
            0,
            sizeof(*medium)
        );

        if (!IsSupportedFormat(*format)) {
            return DV_E_FORMATETC;
        }

        const size_t pathChars =
            path_.size() + 1;

        const size_t totalBytes =
            sizeof(DROPFILES) +
            (pathChars + 1) * sizeof(wchar_t);

        if (totalBytes >
            static_cast<size_t>(64) * 1024 * 1024) {

            return E_OUTOFMEMORY;
        }

        HGLOBAL global =
            GlobalAlloc(
                GMEM_MOVEABLE |
                GMEM_ZEROINIT,
                totalBytes
            );

        if (!global) {
            return E_OUTOFMEMORY;
        }

        void* raw = GlobalLock(global);

        if (!raw) {
            GlobalFree(global);
            return E_OUTOFMEMORY;
        }

        auto* drop =
            static_cast<DROPFILES*>(raw);

        drop->pFiles =
            sizeof(DROPFILES);

        drop->pt = POINT{0, 0};
        drop->fNC = FALSE;
        drop->fWide = TRUE;

        wchar_t* files =
            reinterpret_cast<wchar_t*>(
                static_cast<BYTE*>(raw) +
                sizeof(DROPFILES)
            );

        const size_t copyBytes =
            path_.size() * sizeof(wchar_t);

        if (copyBytes != 0) {
            std::memcpy(
                files,
                path_.data(),
                copyBytes
            );
        }

        // CF_HDROP requires a double terminating zero.
        files[path_.size()] = L'\0';
        files[path_.size() + 1] = L'\0';

        GlobalUnlock(global);

        medium->tymed = TYMED_HGLOBAL;
        medium->hGlobal = global;
        medium->pUnkForRelease = nullptr;

        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE GetDataHere(
        FORMATETC*,
        STGMEDIUM*
    ) override {
        return E_NOTIMPL;
    }

    HRESULT STDMETHODCALLTYPE QueryGetData(
        FORMATETC* format
    ) override {
        if (!format) {
            return E_POINTER;
        }

        return IsSupportedFormat(*format)
            ? S_OK
            : DV_E_FORMATETC;
    }

    HRESULT STDMETHODCALLTYPE GetCanonicalFormatEtc(
        FORMATETC*,
        FORMATETC* outFormat
    ) override {
        if (outFormat) {
            outFormat->ptd = nullptr;
        }

        return E_NOTIMPL;
    }

    HRESULT STDMETHODCALLTYPE SetData(
        FORMATETC*,
        STGMEDIUM*,
        BOOL
    ) override {
        return E_NOTIMPL;
    }

    HRESULT STDMETHODCALLTYPE EnumFormatEtc(
        DWORD direction,
        IEnumFORMATETC** enumerator
    ) override {
        if (!enumerator) {
            return E_POINTER;
        }

        *enumerator = nullptr;

        if (direction != DATADIR_GET) {
            return E_NOTIMPL;
        }

        FORMATETC format{};
        format.cfFormat = CF_HDROP;
        format.ptd = nullptr;
        format.dwAspect = DVASPECT_CONTENT;
        format.lindex = -1;
        format.tymed = TYMED_HGLOBAL;

        auto* result =
            new (std::nothrow)
            FormatEtcEnumerator(format);

        if (!result) {
            return E_OUTOFMEMORY;
        }

        *enumerator = result;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE DAdvise(
        FORMATETC*,
        DWORD,
        IAdviseSink*,
        DWORD*
    ) override {
        return OLE_E_ADVISENOTSUPPORTED;
    }

    HRESULT STDMETHODCALLTYPE DUnadvise(
        DWORD
    ) override {
        return OLE_E_ADVISENOTSUPPORTED;
    }

    HRESULT STDMETHODCALLTYPE EnumDAdvise(
        IEnumSTATDATA** result
    ) override {
        if (result) {
            *result = nullptr;
        }

        return OLE_E_ADVISENOTSUPPORTED;
    }

private:
    ~FileDataObject() = default;

    static bool IsSupportedFormat(
        const FORMATETC& format
    ) {
        return
            format.cfFormat == CF_HDROP &&
            format.dwAspect == DVASPECT_CONTENT &&
            (format.tymed & TYMED_HGLOBAL) != 0;
    }

    LONG refCount_;
    std::wstring path_;
};

// ============================================================================
// IDropSource
// ============================================================================

class DropSource final : public IDropSource {
public:
    DropSource()
        : refCount_(1) {
    }

    HRESULT STDMETHODCALLTYPE QueryInterface(
        REFIID riid,
        void** object
    ) override {
        if (!object) {
            return E_POINTER;
        }

        *object = nullptr;

        if (riid == IID_IUnknown ||
            riid == IID_IDropSource) {

            *object =
                static_cast<IDropSource*>(this);

            AddRef();

            return S_OK;
        }

        return E_NOINTERFACE;
    }

    ULONG STDMETHODCALLTYPE AddRef()
        override {

        return static_cast<ULONG>(
            InterlockedIncrement(&refCount_)
        );
    }

    ULONG STDMETHODCALLTYPE Release()
        override {

        const ULONG result =
            static_cast<ULONG>(
                InterlockedDecrement(
                    &refCount_
                )
            );

        if (result == 0) {
            delete this;
        }

        return result;
    }

    HRESULT STDMETHODCALLTYPE QueryContinueDrag(
        BOOL escapePressed,
        DWORD keyState
    ) override {
        if (escapePressed) {
            return DRAGDROP_S_CANCEL;
        }

        if ((keyState & MK_LBUTTON) == 0) {
            return DRAGDROP_S_DROP;
        }

        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE GiveFeedback(
        DWORD
    ) override {
        return DRAGDROP_S_USEDEFAULTCURSORS;
    }

private:
    ~DropSource() = default;

    LONG refCount_;
};

class DropPanelApp;

// ============================================================================
// IDropTarget
// ============================================================================

class DropTarget final : public IDropTarget {
public:
    DropTarget(
        DropPanelApp* app,
        HWND hwnd
    )
        : refCount_(1),
          app_(app),
          hwnd_(hwnd) {
    }

    HRESULT STDMETHODCALLTYPE QueryInterface(
        REFIID riid,
        void** object
    ) override;

    ULONG STDMETHODCALLTYPE AddRef()
        override {

        return static_cast<ULONG>(
            InterlockedIncrement(&refCount_)
        );
    }

    ULONG STDMETHODCALLTYPE Release()
        override {

        const ULONG result =
            static_cast<ULONG>(
                InterlockedDecrement(&refCount_)
            );

        if (result == 0) {
            delete this;
        }

        return result;
    }

    HRESULT STDMETHODCALLTYPE DragEnter(
        IDataObject* dataObject,
        DWORD keyState,
        POINTL point,
        DWORD* effect
    ) override;

    HRESULT STDMETHODCALLTYPE DragOver(
        DWORD keyState,
        POINTL point,
        DWORD* effect
    ) override;

    HRESULT STDMETHODCALLTYPE DragLeave()
        override;

    HRESULT STDMETHODCALLTYPE Drop(
        IDataObject* dataObject,
        DWORD keyState,
        POINTL point,
        DWORD* effect
    ) override;

private:
    ~DropTarget() = default;

    bool HasFileData(
        IDataObject* dataObject
    ) const;

    bool ExtractPaths(
        IDataObject* dataObject,
        std::vector<std::wstring>& paths
    ) const;

    LONG refCount_;
    DropPanelApp* app_;
    HWND hwnd_;
};

// ============================================================================
// Main application
// ============================================================================

class DropPanelApp {
public:
    DropPanelApp() = default;

    ~DropPanelApp() = default;

    bool AttachWindow(HWND hwnd) {
        if (gdiPlusToken == 0) {
            Gdiplus::GdiplusStartupInput input;
            Gdiplus::GdiplusStartup(&gdiPlusToken, &input, nullptr);
        }

        hwnd_ = hwnd;

        dpi_ = GetDpiForWindow(hwnd_);

        if (dpi_ == 0) {
            dpi_ = 96;
        }

        LoadState();
        RegisterShellDropTarget();
        ConfigureDwm();

        RebuildLayout(true);

        return true;
    }

    void DetachWindow() {
        if (dropTargetRegistered_) {
            RevokeDragDrop(hwnd_);
            dropTargetRegistered_ = false;
        }

        SaveState();

        if (IsWindow(hwnd_)) {
            KillTimer(
                hwnd_,
                kFlashTimerId
            );
        }

        hwnd_ = nullptr;
    }

    HWND Window() const {
        return hwnd_;
    }

    void Toggle() {
        if (hwnd_) {
            DestroyWindow(hwnd_);
        }
    }

    // Public because WindowProc dispatches WM_TIMER here.
    void HandleTimer(UINT_PTR timerId) {
        if (timerId != kFlashTimerId) {
            return;
        }

        KillTimer(
            hwnd_,
            kFlashTimerId
        );

        externalDragActive_ = false;

        InvalidateRect(
            hwnd_,
            nullptr,
            FALSE
        );
    }

    void AddFileDialog() {
        if (items_.size() >= kMaxFiles) {
            FlashFullState();
            return;
        }

        ComPtr<IFileOpenDialog> dialog;

        HRESULT hr = CoCreateInstance(
            CLSID_FileOpenDialog,
            nullptr,
            CLSCTX_INPROC_SERVER,
            IID_PPV_ARGS(dialog.Put())
        );

        if (FAILED(hr) || !dialog) {
            return;
        }

        DWORD options = 0;

        if (SUCCEEDED(
                dialog->GetOptions(
                    &options
                ))) {

            options |= FOS_FORCEFILESYSTEM;
            options |= FOS_FILEMUSTEXIST;
            options &= ~FOS_ALLOWMULTISELECT;

            dialog->SetOptions(options);
        }

        dialog->SetTitle(
            L"Add file to DropPanel"
        );

        hr = dialog->Show(hwnd_);

        if (hr != S_OK) {
            return;
        }

        ComPtr<IShellItem> shellItem;

        hr = dialog->GetResult(
            shellItem.Put()
        );

        if (FAILED(hr) || !shellItem) {
            return;
        }

        PWSTR rawPath = nullptr;

        hr = shellItem->GetDisplayName(
            SIGDN_FILESYSPATH,
            &rawPath
        );

        if (FAILED(hr) || !rawPath) {
            return;
        }

        std::wstring path(rawPath);

        CoTaskMemFree(rawPath);

        AddPath(path);
    }

    void OnExternalDragEnter() {
        externalDragActive_ = true;

        InvalidateRect(
            hwnd_,
            nullptr,
            FALSE
        );
    }

    void OnExternalDragLeave() {
        externalDragActive_ = false;

        InvalidateRect(
            hwnd_,
            nullptr,
            FALSE
        );
    }

    void OnExternalDrop(
        const std::vector<std::wstring>& paths
    ) {
        externalDragActive_ = false;

        bool changed = false;

        for (const auto& path : paths) {
            if (items_.size() >= kMaxFiles) {
                break;
            }

            if (AddPathInternal(path)) {
                changed = true;
            }
        }

        if (changed) {
            SaveState();
            RebuildLayout(true);
        } else {
            InvalidateRect(
                hwnd_,
                nullptr,
                FALSE
            );
        }
    }

    bool AcceptsExternalDrop() const {
        return items_.size() < kMaxFiles;
    }

    DWORD GetDropEffect(
        DWORD keyState
    ) const {
        UNREFERENCED_PARAMETER(keyState);

        if (!AcceptsExternalDrop()) {
            return DROPEFFECT_NONE;
        }

        // Files dropped onto the panel are registered as references.
        // Therefore the panel itself only advertises COPY.
        return DROPEFFECT_COPY;
    }

    void StartDrag(int index) {
        if (index < 0 ||
            index >= static_cast<int>(items_.size())) {
            return;
        }

        if (dragInProgress_) {
            return;
        }

        dragInProgress_ = true;
        dragIndex_ = index;

        InvalidateRect(
            hwnd_,
            nullptr,
            FALSE
        );

        if (GetCapture() == hwnd_) {
            ReleaseCapture();
        }

        const std::wstring path =
            items_[index].path;

        auto* dataObject =
            new (std::nothrow)
            FileDataObject(path);

        auto* dropSource =
            new (std::nothrow)
            DropSource();

        if (!dataObject || !dropSource) {
            if (dataObject) {
                dataObject->Release();
            }

            if (dropSource) {
                dropSource->Release();
            }

            dragInProgress_ = false;
            dragIndex_ = -1;

            InvalidateRect(
                hwnd_,
                nullptr,
                FALSE
            );

            return;
        }

        DWORD effect = DROPEFFECT_NONE;

        const HRESULT hr =
            DoDragDrop(
                dataObject,
                dropSource,
                DROPEFFECT_COPY |
                DROPEFFECT_MOVE,
                &effect
            );

        dataObject->Release();
        dropSource->Release();

        // If the receiving application actually performed MOVE and the
        // original path disappeared, the panel removes the stale entry.
        if (SUCCEEDED(hr) &&
            effect == DROPEFFECT_MOVE &&
            !FileOrDirectoryExists(path)) {

            RemoveIndex(index);
        }

        dragInProgress_ = false;
        dragIndex_ = -1;

        InvalidateRect(
            hwnd_,
            nullptr,
            FALSE
        );
    }

    void SetExternalDragState(
        bool active
    ) {
        if (externalDragActive_ != active) {
            externalDragActive_ = active;

            InvalidateRect(
                hwnd_,
                nullptr,
                FALSE
            );
        }
    }

    int HitTest(POINT point) const {
        for (int i = 0;
             i < static_cast<int>(itemRects_.size());
             ++i) {

            if (PtInRect(
                    &itemRects_[i],
                    point)) {
                return i;
            }
        }

        return -1;
    }

    bool HitPlus(POINT point) const {
        return PtInRect(
            &addRect_,
            point
        ) != FALSE;
    }

    void OnMouseMove(POINT point) {
        if (!trackingMouse_) {
            TRACKMOUSEEVENT tme{};
            tme.cbSize = sizeof(tme);
            tme.dwFlags = TME_LEAVE;
            tme.hwndTrack = hwnd_;

            if (TrackMouseEvent(&tme)) {
                trackingMouse_ = true;
            }
        }

        const int newHover =
            HitTest(point);

        const bool newPlusHover =
            HitPlus(point);

        if (hoverIndex_ != newHover ||
            plusHovered_ != newPlusHover) {

            hoverIndex_ = newHover;
            plusHovered_ = newPlusHover;

            InvalidateRect(
                hwnd_,
                nullptr,
                FALSE
            );
        }

        if (pressedIndex_ >= 0 &&
            !dragInProgress_) {

            const int dx =
                point.x - dragStartPoint_.x;

            const int dy =
                point.y - dragStartPoint_.y;

            const int thresholdX =
                GetSystemMetrics(SM_CXDRAG);

            const int thresholdY =
                GetSystemMetrics(SM_CYDRAG);

            if (std::abs(dx) >= thresholdX ||
                std::abs(dy) >= thresholdY) {

                const int index =
                    pressedIndex_;

                pressedIndex_ = -1;

                StartDrag(index);
            }
        }
    }

    void OnMouseLeave() {
        trackingMouse_ = false;

        if (hoverIndex_ != -1 ||
            plusHovered_) {

            hoverIndex_ = -1;
            plusHovered_ = false;

            InvalidateRect(
                hwnd_,
                nullptr,
                FALSE
            );
        }
    }

    void OnLeftButtonDown(
        POINT point
    ) {
        const int itemIndex =
            HitTest(point);

        if (itemIndex >= 0) {
            pressedIndex_ = itemIndex;
            dragStartPoint_ = point;

            SetCapture(hwnd_);
            return;
        }

        if (HitPlus(point)) {
            SetCapture(hwnd_);
        }
    }

    void OnLeftButtonUp(
        POINT point
    ) {
        const int pressed =
            pressedIndex_;

        if (GetCapture() == hwnd_) {
            ReleaseCapture();
        }

        pressedIndex_ = -1;

        if (pressed >= 0) {
            UNREFERENCED_PARAMETER(point);
            return;
        }

        if (HitPlus(point)) {
            AddFileDialog();
        }
    }

    void OnDoubleClick(
        POINT point
    ) {
        const int index =
            HitTest(point);

        if (index >= 0) {
            OpenFile(index);
            return;
        }

        if (HitPlus(point)) {
            AddFileDialog();
        }
    }

    void OnRightButtonUp(
        POINT point
    ) {
        const int index =
            HitTest(point);

        if (index >= 0) {
            RemoveIndex(index);
        }
    }

    void OnDpiChanged(
        UINT newDpi
    ) {
        if (newDpi == 0) {
            return;
        }

        dpi_ = newDpi;

        RebuildLayout(true);
    }

    void Paint(HDC hdc) {
        RECT client{};
        GetClientRect(
            hwnd_,
            &client
        );

        const int width =
            client.right - client.left;

        const int height =
            client.bottom - client.top;

        const COLORREF backgroundColor =
            externalDragActive_
            ? kDropBackground
            : kBackground;

        HBRUSH backgroundBrush =
            CreateSolidBrush(
                backgroundColor
            );

        if (backgroundBrush) {
            FillRect(
                hdc,
                &client,
                backgroundBrush
            );

            DeleteObject(
                backgroundBrush
            );
        }

        SetBkMode(
            hdc,
            TRANSPARENT
        );

        {
            Gdiplus::Graphics graphics(hdc);
            graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
            Gdiplus::GraphicsPath path;
            const Gdiplus::REAL inset = 1.0f;
            const Gdiplus::REAL radius = static_cast<Gdiplus::REAL>(Scale(34));
            const Gdiplus::REAL left = inset;
            const Gdiplus::REAL top = inset;
            const Gdiplus::REAL right = static_cast<Gdiplus::REAL>(width) - inset;
            const Gdiplus::REAL bottom = static_cast<Gdiplus::REAL>(height) - inset;

            path.AddArc(left, top, radius, radius, 180, 90);
            path.AddArc(right - radius, top, radius, radius, 270, 90);
            path.AddArc(right - radius, bottom - radius, radius, radius, 0, 90);
            path.AddArc(left, bottom - radius, radius, radius, 90, 90);
            path.CloseFigure();

            Gdiplus::SolidBrush fill(
                Gdiplus::Color(255,
                    GetRValue(backgroundColor),
                    GetGValue(backgroundColor),
                    GetBValue(backgroundColor)));

            Gdiplus::Pen stroke(
                Gdiplus::Color(255,
                    GetRValue(externalDragActive_ ? kAccentBorder : kBorder),
                    GetGValue(externalDragActive_ ? kAccentBorder : kBorder),
                    GetBValue(externalDragActive_ ? kAccentBorder : kBorder)),
                static_cast<Gdiplus::REAL>(Scale(1)));

            graphics.FillPath(&fill, &path);
            stroke.SetAlignment(Gdiplus::PenAlignmentInset);
            graphics.DrawPath(&stroke, &path);
        }

        for (int i = 0;
             i < static_cast<int>(items_.size());
             ++i) {

            DrawFileCard(
                hdc,
                i
            );
        }

        DrawAddButton(hdc);
    }

private:
    struct Metrics {
        int itemWidth;
        int itemHeight;
        int buttonSize;
        int gap;
        int padding;
    };

    int Scale(int px) const {
        return MulDiv(
            px,
            static_cast<int>(dpi_),
            96
        );
    }

    Metrics GetMetrics() const {
        Metrics result{};

        result.itemWidth = Scale(64);
        result.itemHeight = Scale(84);
        result.buttonSize = Scale(44);
        result.gap = Scale(8);
        result.padding = Scale(10);

        return result;
    }

    SIZE GetDesiredSize() const {
        const Metrics m =
            GetMetrics();

        if (items_.empty()) {
            return SIZE{
                Scale(kEmptyPanelSize),
                Scale(kEmptyPanelSize)
            };
        }

        const int count =
            static_cast<int>(items_.size());

        const int width =
            m.padding +
            count * m.itemWidth +
            (count - 1) * m.gap +
            m.gap +
            m.buttonSize +
            m.padding;

        const int height =
            m.padding * 2 +
            m.itemHeight;

        return SIZE{
            width,
            height
        };
    }

    static bool GetWorkAreaForMonitor(
        HMONITOR monitor,
        RECT& workArea
    ) {
        MONITORINFO mi{};
        mi.cbSize = sizeof(mi);

        if (!monitor ||
            !GetMonitorInfoW(
                monitor,
                &mi)) {

            return false;
        }

        workArea =
            mi.rcWork;

        return true;
    }

    void GetCurrentMonitorWorkArea(
        RECT& workArea
    ) const {
        HMONITOR monitor =
            MonitorFromWindow(
                hwnd_,
                MONITOR_DEFAULTTONEAREST
            );

        if (!monitor) {
            POINT cursor{};
            GetCursorPos(&cursor);

            monitor =
                MonitorFromPoint(
                    cursor,
                    MONITOR_DEFAULTTOPRIMARY
                );
        }

        if (!GetWorkAreaForMonitor(
                monitor,
                workArea)) {

            SystemParametersInfoW(
                SPI_GETWORKAREA,
                0,
                &workArea,
                0
            );
        }
    }

    void PositionWindowForCurrentMonitor() {
        if (!hwnd_) {
            return;
        }

        RECT workArea{};
        GetCurrentMonitorWorkArea(
            workArea
        );

        SIZE desired =
            GetDesiredSize();

        const int margin =
            Scale(20);

        const int availableWidth =
            static_cast<int>(
                workArea.right -
                workArea.left
            ) - 2 * margin;

        const int availableHeight =
            static_cast<int>(
                workArea.bottom -
                workArea.top
            ) - 2 * margin;

        const int maxWidth =
            std::max(
                1,
                availableWidth
            );

        const int maxHeight =
        std::max(
            1,
            availableHeight
        );

        const int desiredWidth =
            static_cast<int>(desired.cx);

        const int desiredHeight =
            static_cast<int>(desired.cy);

        int width =
            std::min(
                desiredWidth,
                maxWidth
            );

        int height =
            std::min(
                desiredHeight,
                maxHeight
            );

        int x =
            static_cast<int>(
                workArea.right
            ) - width - margin;

        int y =
            static_cast<int>(
                workArea.top
            ) +
            (
                static_cast<int>(
                    workArea.bottom -
                    workArea.top
                ) - height
            ) / 2;

        const int workLeft =
            static_cast<int>(workArea.left);

        const int workTop =
            static_cast<int>(workArea.top);

        const int workRight =
            static_cast<int>(workArea.right);

        const int workBottom =
            static_cast<int>(workArea.bottom);

        const int minX =
            workLeft + margin;

        const int maxX =
            workRight - width - margin;

        const int minY =
            workTop + margin;

        const int maxY =
            workBottom - height - margin;

        if (maxX >= minX) {
            x = std::clamp(
                x,
                minX,
                maxX
            );
        } else {
            x = static_cast<int>(
                workArea.left
            );
        }

        if (maxY >= minY) {
            y = std::clamp(
                y,
                minY,
                maxY
            );
        } else {
            y = static_cast<int>(
                workArea.top
            );
        }

        SetWindowPos(
            hwnd_,
            HWND_TOPMOST,
            x,
            y,
            width,
            height,
            SWP_NOACTIVATE |
            SWP_SHOWWINDOW
        );
    }

    void RebuildLayout(
        bool resizeWindow
    ) {
        if (resizeWindow) {
            PositionWindowForCurrentMonitor();
        }

        const Metrics m =
            GetMetrics();

        RECT client{};
        GetClientRect(
            hwnd_,
            &client
        );

        const int width =
            client.right - client.left;

        const int height =
            client.bottom - client.top;

        itemRects_.clear();
        itemRects_.reserve(items_.size());

        addRect_ = RECT{};

        if (items_.empty()) {
            const int buttonSize =
                std::max(
                    1,
                    std::min(
                        m.buttonSize,
                        std::min(
                            width,
                            height
                        ) -
                        2 * m.padding
                    )
                );

            addRect_.left =
                (width - buttonSize) / 2;

            addRect_.top =
                (height - buttonSize) / 2;

            addRect_.right =
                addRect_.left +
                buttonSize;

            addRect_.bottom =
                addRect_.top +
                buttonSize;

        } else {
            const int contentHeight =
                std::min(
                    m.itemHeight,
                    std::max(
                        1,
                        height - 2 * m.padding
                    )
                );

            const int y =
                std::max(
                    0,
                    (height - contentHeight) / 2
                );

            int x = m.padding;

            for (size_t i = 0;
                 i < items_.size();
                 ++i) {

                const int itemWidth =
                    std::min(
                        m.itemWidth,
                        std::max(
                            1,
                            width - x -
                            m.buttonSize -
                            m.gap -
                            m.padding
                        )
                    );

                RECT rect{
                    x,
                    y,
                    x + itemWidth,
                    y + contentHeight
                };

                itemRects_.push_back(rect);

                x += itemWidth +
                     m.gap;
            }

            const int addY =
                std::max(
                    0,
                    (height - m.buttonSize) / 2
                );

            addRect_.left = x;
            addRect_.top = addY;
            addRect_.right =
                x + m.buttonSize;
            addRect_.bottom =
                addY + m.buttonSize;
        }

        InvalidateRect(
            hwnd_,
            nullptr,
            FALSE
        );

        UpdateWindow(hwnd_);
    }

    void ConfigureDwm() {
        if (!hwnd_) {
            return;
        }

        const DWM_WINDOW_CORNER_PREFERENCE corner =
            DWMWCP_ROUND;

        DwmSetWindowAttribute(
            hwnd_,
            DWMWA_WINDOW_CORNER_PREFERENCE,
            &corner,
            sizeof(corner)
        );

        const BOOL darkMode = TRUE;

        DwmSetWindowAttribute(
            hwnd_,
            DWMWA_USE_IMMERSIVE_DARK_MODE,
            &darkMode,
            sizeof(darkMode)
        );

        const DWMNCRENDERINGPOLICY policy =
            DWMNCRP_ENABLED;

        DwmSetWindowAttribute(
            hwnd_,
            DWMWA_NCRENDERING_POLICY,
            &policy,
            sizeof(policy)
        );
    }

    void RegisterShellDropTarget() {
        if (!hwnd_) {
            return;
        }

        auto* target =
            new (std::nothrow)
            DropTarget(
                this,
                hwnd_
            );

        if (!target) {
            return;
        }

        const HRESULT hr =
            RegisterDragDrop(
                hwnd_,
                target
            );

        if (SUCCEEDED(hr)) {
            dropTargetRegistered_ = true;
        }

        // RegisterDragDrop holds its own reference.
        target->Release();
    }

    void LoadState() {
        LoadPathsFromDisk(items_);

        // Clean state of stale files on startup.
        SaveState();
    }

    void SaveState() {
        SavePathsToDisk(items_);
    }

    bool AddPathInternal(
        const std::wstring& rawPath
    ) {
        if (items_.size() >= kMaxFiles) {
            return false;
        }

        std::wstring path =
            NormalizePath(rawPath);

        if (path.empty() ||
            !FileOrDirectoryExists(path)) {

            return false;
        }

        auto duplicate =
            std::find_if(
                items_.begin(),
                items_.end(),
                [&](const FileItem& item) {
                    return _wcsicmp(
                        item.path.c_str(),
                        path.c_str()
                    ) == 0;
                }
            );

        if (duplicate != items_.end()) {
            return false;
        }

        const std::wstring name =
            GetFileNamePart(path);

        HICON icon =
            GetShellIcon(path);

        items_.emplace_back(
            path,
            name,
            icon
        );

        return true;
    }

    void AddPath(
        const std::wstring& path
    ) {
        if (items_.size() >= kMaxFiles) {
            FlashFullState();
            return;
        }

        if (AddPathInternal(path)) {
            SaveState();
            RebuildLayout(true);
        }
    }

    void RemoveIndex(int index) {
        if (index < 0 ||
            index >= static_cast<int>(items_.size())) {

            return;
        }

        items_.erase(
            items_.begin() + index
        );

        hoverIndex_ = -1;
        pressedIndex_ = -1;
        dragIndex_ = -1;

        SaveState();
        RebuildLayout(true);
    }

    void OpenFile(int index) {
        if (index < 0 ||
            index >= static_cast<int>(items_.size())) {

            return;
        }

        const std::wstring path =
            items_[index].path;

        const HINSTANCE result =
            ShellExecuteW(
                hwnd_,
                L"open",
                path.c_str(),
                nullptr,
                nullptr,
                SW_SHOWNORMAL
            );

        if (reinterpret_cast<INT_PTR>(result) <= 32) {
            if (!FileOrDirectoryExists(path)) {
                RemoveIndex(index);
            }
        }
    }

    void FlashFullState() {
        externalDragActive_ = true;

        InvalidateRect(
            hwnd_,
            nullptr,
            FALSE
        );

        SetTimer(
            hwnd_,
            kFlashTimerId,
            160,
            nullptr
        );
    }

    void DrawFileCard(
        HDC hdc,
        int index
    ) {
        if (index < 0 ||
            index >= static_cast<int>(items_.size()) ||
            index >= static_cast<int>(itemRects_.size())) {

            return;
        }

        const RECT rect =
            itemRects_[index];

        const bool hovered =
            hoverIndex_ == index;

        const bool dragged =
            dragIndex_ == index &&
            dragInProgress_;

        const COLORREF bg =
            hovered || dragged
            ? kCardHover
            : kCard;

        HBRUSH brush =
            CreateSolidBrush(bg);

        HPEN pen =
            CreatePen(
                PS_SOLID,
                Scale(1),
                dragged
                ? kAccentBorder
                : bg
            );

        if (!brush || !pen) {
            if (brush) {
                DeleteObject(brush);
            }

            if (pen) {
                DeleteObject(pen);
            }

            return;
        }

        HGDIOBJ oldBrush =
            SelectObject(
                hdc,
                brush
            );

        HGDIOBJ oldPen =
            SelectObject(
                hdc,
                pen
            );

        RoundRect(
            hdc,
            rect.left,
            rect.top,
            rect.right,
            rect.bottom,
            Scale(kCardCornerRadius),
            Scale(kCardCornerRadius)
        );

        SelectObject(
            hdc,
            oldPen
        );

        SelectObject(
            hdc,
            oldBrush
        );

        DeleteObject(pen);
        DeleteObject(brush);

        const int cardWidth =
            static_cast<int>(
                rect.right - rect.left
            );

        const int iconSize =
            std::min(
                Scale(38),
                std::max(
                    1,
                    cardWidth - Scale(10)
                )
            );

        const int iconX =
            rect.left +
            (
                rect.right -
                rect.left -
                iconSize
            ) / 2;

        const int iconY =
            rect.top +
            Scale(8);

        if (items_[index].icon) {
            DrawIconEx(
                hdc,
                iconX,
                iconY,
                items_[index].icon,
                iconSize,
                iconSize,
                0,
                nullptr,
                DI_NORMAL
            );
        } else {
            DrawFallbackFileIcon(
                hdc,
                iconX,
                iconY,
                iconSize
            );
        }

        RECT textRect{
            rect.left + Scale(4),
            rect.top + Scale(50),
            rect.right - Scale(4),
            rect.bottom - Scale(6)
        };

        LOGFONTW lf{};
        lf.lfHeight = -Scale(11);
        lf.lfWeight = FW_NORMAL;
        lf.lfCharSet = DEFAULT_CHARSET;

        wcscpy_s(
            lf.lfFaceName,
            L"Segoe UI"
        );

        HFONT font =
            CreateFontIndirectW(&lf);

        if (!font) {
            return;
        }

        HGDIOBJ oldFont =
            SelectObject(
                hdc,
                font
            );

        SetTextColor(
            hdc,
            hovered || dragged
            ? kText
            : kSubtleText
        );

        DrawTextW(
            hdc,
            items_[index].name.c_str(),
            -1,
            &textRect,
            DT_CENTER |
            DT_SINGLELINE |
            DT_END_ELLIPSIS |
            DT_NOPREFIX |
            DT_VCENTER
        );

        SelectObject(
            hdc,
            oldFont
        );

        DeleteObject(font);
    }

    void DrawAddButton(
        HDC hdc
    ) {
        const RECT rect = addRect_;
        if (rect.right <= rect.left || rect.bottom <= rect.top) return;

        const bool highlighted = plusHovered_ || externalDragActive_;
        Gdiplus::Graphics graphics(hdc);
        graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);

        const int size = rect.right - rect.left;
        Gdiplus::SolidBrush brush(Gdiplus::Color(255,
            GetRValue(highlighted ? kButtonHover : kButton),
            GetGValue(highlighted ? kButtonHover : kButton),
            GetBValue(highlighted ? kButtonHover : kButton)));

        Gdiplus::Pen border(Gdiplus::Color(255,
            GetRValue(highlighted ? kAccentBorder : kBorder),
            GetGValue(highlighted ? kAccentBorder : kBorder),
            GetBValue(highlighted ? kAccentBorder : kBorder)),
            static_cast<Gdiplus::REAL>(Scale(1)));

        graphics.FillEllipse(&brush, rect.left, rect.top, size, size);
        graphics.DrawEllipse(&border, rect.left, rect.top, size, size);

        Gdiplus::Pen plus(Gdiplus::Color(255,
            GetRValue(kText), GetGValue(kText), GetBValue(kText)),
            static_cast<Gdiplus::REAL>(Scale(2)));
        plus.SetLineCap(Gdiplus::LineCapRound, Gdiplus::LineCapRound, Gdiplus::DashCapRound);

        const int cx = (rect.left + rect.right) / 2;
        const int cy = (rect.top + rect.bottom) / 2 + Scale(kPlusYOffset);
        const int half = Scale(kPlusSize) / 2;

        graphics.DrawLine(&plus, cx - half, cy, cx + half, cy);
        graphics.DrawLine(&plus, cx, cy - half, cx, cy + half);
    }

    static void DrawFallbackFileIcon(
        HDC hdc,
        int x,
        int y,
        int size
    ) {
        HPEN pen =
            CreatePen(
                PS_SOLID,
                1,
                RGB(98, 110, 124)
            );

        HBRUSH brush =
            CreateSolidBrush(
                RGB(28, 38, 50)
            );

        if (!pen || !brush) {
            if (pen) {
                DeleteObject(pen);
            }

            if (brush) {
                DeleteObject(brush);
            }

            return;
        }

        HGDIOBJ oldPen =
            SelectObject(
                hdc,
                pen
            );

        HGDIOBJ oldBrush =
            SelectObject(
                hdc,
                brush
            );

        RoundRect(
            hdc,
            x + size / 5,
            y + size / 10,
            x + size * 4 / 5,
            y + size * 9 / 10,
            size / 10,
            size / 10
        );

        MoveToEx(
            hdc,
            x + size * 3 / 5,
            y + size / 10,
            nullptr
        );

        LineTo(
            hdc,
            x + size * 4 / 5,
            y + size * 3 / 10
        );

        MoveToEx(
            hdc,
            x + size * 3 / 5,
            y + size / 10,
            nullptr
        );

        LineTo(
            hdc,
            x + size * 3 / 5,
            y + size * 3 / 10
        );

        MoveToEx(
            hdc,
            x + size / 3,
            y + size / 2,
            nullptr
        );

        LineTo(
            hdc,
            x + size * 2 / 3,
            y + size / 2
        );

        MoveToEx(
            hdc,
            x + size / 3,
            y + size * 2 / 3,
            nullptr
        );

        LineTo(
            hdc,
            x + size * 2 / 3,
            y + size * 2 / 3
        );

        SelectObject(
            hdc,
            oldBrush
        );

        SelectObject(
            hdc,
            oldPen
        );

        DeleteObject(brush);
        DeleteObject(pen);
    }

private:
    HWND hwnd_ = nullptr;

    UINT dpi_ = 96;

    std::vector<FileItem> items_;

    std::vector<RECT> itemRects_;
    RECT addRect_{};

    int hoverIndex_ = -1;
    int pressedIndex_ = -1;
    int dragIndex_ = -1;

    bool plusHovered_ = false;
    bool trackingMouse_ = false;
    bool dragInProgress_ = false;
    bool externalDragActive_ = false;
    bool dropTargetRegistered_ = false;

    POINT dragStartPoint_{};
};

// ============================================================================
// DropTarget implementation
// ============================================================================

HRESULT STDMETHODCALLTYPE DropTarget::QueryInterface(
    REFIID riid,
    void** object
) {
    if (!object) {
        return E_POINTER;
    }

    *object = nullptr;

    if (riid == IID_IUnknown ||
        riid == IID_IDropTarget) {

        *object =
            static_cast<IDropTarget*>(this);

        AddRef();

        return S_OK;
    }

    return E_NOINTERFACE;
}

bool DropTarget::HasFileData(
    IDataObject* dataObject
) const {
    if (!dataObject) {
        return false;
    }

    FORMATETC format{};
    format.cfFormat = CF_HDROP;
    format.ptd = nullptr;
    format.dwAspect = DVASPECT_CONTENT;
    format.lindex = -1;
    format.tymed = TYMED_HGLOBAL;

    return SUCCEEDED(
        dataObject->QueryGetData(&format)
    );
}

bool DropTarget::ExtractPaths(
    IDataObject* dataObject,
    std::vector<std::wstring>& paths
) const {
    paths.clear();

    if (!HasFileData(dataObject)) {
        return false;
    }

    FORMATETC format{};
    format.cfFormat = CF_HDROP;
    format.ptd = nullptr;
    format.dwAspect = DVASPECT_CONTENT;
    format.lindex = -1;
    format.tymed = TYMED_HGLOBAL;

    STGMEDIUM medium{};

    HRESULT hr =
        dataObject->GetData(
            &format,
            &medium
        );

    if (FAILED(hr)) {
        return false;
    }

    bool success = false;

    if (medium.tymed == TYMED_HGLOBAL &&
        medium.hGlobal) {

        HDROP drop =
            reinterpret_cast<HDROP>(
                medium.hGlobal
            );

        const UINT count =
            DragQueryFileW(
                drop,
                0xFFFFFFFF,
                nullptr,
                0
            );

        for (UINT i = 0;
             i < count;
             ++i) {

            const UINT required =
                DragQueryFileW(
                    drop,
                    i,
                    nullptr,
                    0
                );

            if (required == 0) {
                continue;
            }

            std::vector<wchar_t> buffer(
                static_cast<size_t>(
                    required
                ) + 1
            );

            if (DragQueryFileW(
                    drop,
                    i,
                    buffer.data(),
                    static_cast<UINT>(
                        buffer.size()
                    )) == 0) {

                continue;
            }

            buffer.back() = L'\0';

            std::wstring path(
                buffer.data()
            );

            if (!path.empty()) {
                paths.push_back(
                    std::move(path)
                );
            }
        }

        success = !paths.empty();
    }

    ReleaseStgMedium(&medium);

    return success;
}

HRESULT STDMETHODCALLTYPE DropTarget::DragEnter(
    IDataObject* dataObject,
    DWORD keyState,
    POINTL,
    DWORD* effect
) {
    if (!effect) {
        return E_POINTER;
    }

    *effect = DROPEFFECT_NONE;

    if (!app_ || !dataObject) {
        return S_OK;
    }

    if (HasFileData(dataObject) &&
        app_->AcceptsExternalDrop()) {

        *effect =
            app_->GetDropEffect(
                keyState
            );

        app_->OnExternalDragEnter();
    }

    return S_OK;
}

HRESULT STDMETHODCALLTYPE DropTarget::DragOver(
    DWORD keyState,
    POINTL,
    DWORD* effect
) {
    if (!effect) {
        return E_POINTER;
    }

    *effect = DROPEFFECT_NONE;

    if (!app_) {
        return S_OK;
    }

    if (app_->AcceptsExternalDrop()) {
        *effect =
            app_->GetDropEffect(
                keyState
            );
    }

    app_->SetExternalDragState(
        *effect != DROPEFFECT_NONE
    );

    return S_OK;
}

HRESULT STDMETHODCALLTYPE DropTarget::DragLeave() {
    if (app_) {
        app_->OnExternalDragLeave();
    }

    return S_OK;
}

HRESULT STDMETHODCALLTYPE DropTarget::Drop(
    IDataObject* dataObject,
    DWORD,
    POINTL,
    DWORD* effect
) {
    if (!effect) {
        return E_POINTER;
    }

    *effect = DROPEFFECT_NONE;

    if (!app_ || !dataObject) {
        return S_OK;
    }

    std::vector<std::wstring> paths;

    if (!ExtractPaths(
            dataObject,
            paths)) {

        app_->OnExternalDragLeave();
        return S_OK;
    }

    if (!app_->AcceptsExternalDrop()) {
        app_->OnExternalDragLeave();
        return S_OK;
    }

    app_->OnExternalDrop(paths);

    *effect = DROPEFFECT_COPY;

    return S_OK;
}

// ============================================================================
// Singleton
// ============================================================================

class SingletonMutex {
public:
    SingletonMutex() = default;

    ~SingletonMutex() {
        if (handle_) {
            CloseHandle(handle_);
            handle_ = nullptr;
        }
    }

    SingletonMutex(const SingletonMutex&) = delete;
    SingletonMutex& operator=(const SingletonMutex&) = delete;

    bool Create(bool& alreadyExists) {
        alreadyExists = false;

        handle_ = CreateMutexW(
            nullptr,
            TRUE,
            kMutexName
        );

        if (!handle_) {
            return false;
        }

        const DWORD error =
            GetLastError();

        alreadyExists =
            error == ERROR_ALREADY_EXISTS;

        return true;
    }

private:
    HANDLE handle_ = nullptr;
};

// ============================================================================
// Window globals
// ============================================================================

static DropPanelApp* g_app = nullptr;
static UINT g_toggleMessage = 0;

// ============================================================================
// Window procedure
// ============================================================================

static LRESULT CALLBACK WindowProc(
    HWND hwnd,
    UINT message,
    WPARAM wParam,
    LPARAM lParam
) {
    if (message == g_toggleMessage &&
        g_toggleMessage != 0) {

        if (g_app) {
            g_app->Toggle();
        }

        return 0;
    }

    switch (message) {
    case WM_NCCREATE: {
        auto* createStruct =
            reinterpret_cast<
                CREATESTRUCTW*
            >(lParam);

        g_app =
            static_cast<DropPanelApp*>(
                createStruct->lpCreateParams
            );

        return DefWindowProcW(
            hwnd,
            message,
            wParam,
            lParam
        );
    }

    case WM_CREATE: {
        if (g_app) {
            if (!g_app->AttachWindow(hwnd)) {
                return -1;
            }
        }

        return 0;
    }

    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT: {
        PAINTSTRUCT ps{};

        HDC hdc =
            BeginPaint(
                hwnd,
                &ps
            );

        if (hdc) {
            if (g_app) {
                g_app->Paint(hdc);
            }

            EndPaint(
                hwnd,
                &ps
            );
        }

        return 0;
    }

    case WM_MOUSEMOVE: {
        if (g_app) {
            POINT point{
                GET_X_LPARAM(lParam),
                GET_Y_LPARAM(lParam)
            };

            g_app->OnMouseMove(point);

            SetCursor(
                LoadCursorW(
                    nullptr,
                    IDC_HAND
                )
            );
        }

        return 0;
    }

    case WM_MOUSELEAVE:
        if (g_app) {
            g_app->OnMouseLeave();
        }
        return 0;

    case WM_LBUTTONDOWN: {
        if (g_app) {
            POINT point{
                GET_X_LPARAM(lParam),
                GET_Y_LPARAM(lParam)
            };

            g_app->OnLeftButtonDown(
                point
            );
        }

        return 0;
    }

    case WM_LBUTTONUP: {
        if (g_app) {
            POINT point{
                GET_X_LPARAM(lParam),
                GET_Y_LPARAM(lParam)
            };

            g_app->OnLeftButtonUp(
                point
            );
        }

        return 0;
    }

    case WM_LBUTTONDBLCLK: {
        if (g_app) {
            POINT point{
                GET_X_LPARAM(lParam),
                GET_Y_LPARAM(lParam)
            };

            g_app->OnDoubleClick(
                point
            );
        }

        return 0;
    }

    case WM_RBUTTONUP: {
        if (g_app) {
            POINT point{
                GET_X_LPARAM(lParam),
                GET_Y_LPARAM(lParam)
            };

            g_app->OnRightButtonUp(
                point
            );
        }

        return 0;
    }

    case WM_TIMER:
        if (g_app) {
            g_app->HandleTimer(wParam);
        }

        return 0;

    case WM_DPICHANGED:
        if (g_app) {
            g_app->OnDpiChanged(
                HIWORD(wParam)
            );
        }

        return 0;

    case WM_NCHITTEST:
        return HTCLIENT;

    case WM_DESTROY:
        if (g_app) {
            g_app->DetachWindow();
        }

        PostQuitMessage(0);
        return 0;

    case WM_NCDESTROY:
        g_app = nullptr;

        return DefWindowProcW(
            hwnd,
            message,
            wParam,
            lParam
        );

    default:
        break;
    }

    return DefWindowProcW(
        hwnd,
        message,
        wParam,
        lParam
    );
}

// ============================================================================
// Startup
// ============================================================================

static bool RegisterMainWindowClass(
    HINSTANCE instance
) {
    WNDCLASSEXW wc{};

    wc.cbSize = sizeof(wc);

    wc.style =
        CS_HREDRAW |
        CS_VREDRAW |
        CS_DBLCLKS |
        CS_DROPSHADOW;

    wc.lpfnWndProc = WindowProc;
    wc.hInstance = instance;

    wc.hCursor =
        LoadCursorW(
            nullptr,
            IDC_ARROW
        );

    wc.hbrBackground = nullptr;
    wc.lpszClassName =
        kWindowClassName;

    if (RegisterClassExW(&wc)) {
        return true;
    }

    return GetLastError() ==
           ERROR_CLASS_ALREADY_EXISTS;
}

static HWND FindExistingWindow() {
    constexpr int kAttempts = 20;

    for (int i = 0;
         i < kAttempts;
         ++i) {

        HWND hwnd =
            FindWindowW(
                kWindowClassName,
                nullptr
            );

        if (hwnd) {
            return hwnd;
        }

        Sleep(25);
    }

    return nullptr;
}

static void SetProcessDpiAwareness() {
    // Available on Windows 10 1703+.
    SetProcessDpiAwarenessContext(
        DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2
    );
}

static HWND CreateMainWindow(
    HINSTANCE instance,
    DropPanelApp* app
) {
    // Do not use WS_EX_NOREDIRECTIONBITMAP here: this window is rendered
    // directly with GDI in WM_PAINT. Disabling the DWM redirection surface
    // makes the GDI client area appear transparent/empty on Windows 11.
    constexpr DWORD exStyle =
        WS_EX_TOOLWINDOW |
        WS_EX_TOPMOST |
        WS_EX_NOACTIVATE;

    return CreateWindowExW(
        exStyle,
        kWindowClassName,
        kWindowTitle,
        WS_POPUP,
        0,
        0,
        100,
        100,
        nullptr,
        nullptr,
        instance,
        app
    );
}

} // namespace DropPanel

// ============================================================================
// Entry point
// ============================================================================

int WINAPI wWinMain(
    HINSTANCE hInstance,
    HINSTANCE,
    PWSTR,
    int
) {
    using namespace DropPanel;

    SetProcessDpiAwareness();

    ComInit com;

    if (!com.Ok()) {
        MessageBoxW(
            nullptr,
            L"OLE/COM initialization failed.",
            L"DropPanel",
            MB_ICONERROR | MB_OK
        );

        return 1;
    }

    g_toggleMessage =
        RegisterWindowMessageW(
            kToggleMessageName
        );

    if (g_toggleMessage == 0) {
        MessageBoxW(
            nullptr,
            L"Failed to register the toggle message.",
            L"DropPanel",
            MB_ICONERROR | MB_OK
        );

        return 1;
    }

    SingletonMutex mutex;
    bool alreadyExists = false;

    if (!mutex.Create(
            alreadyExists)) {

        MessageBoxW(
            nullptr,
            L"Failed to create the singleton mutex.",
            L"DropPanel",
            MB_ICONERROR | MB_OK
        );

        return 1;
    }

    if (alreadyExists) {
        HWND existing =
            FindExistingWindow();

        if (existing) {
            DWORD_PTR result = 0;

            SendMessageTimeoutW(
                existing,
                g_toggleMessage,
                0,
                0,
                SMTO_ABORTIFHUNG |
                SMTO_BLOCK,
                1500,
                &result
            );
        }

        return 0;
    }

    if (!RegisterMainWindowClass(
            hInstance)) {

        MessageBoxW(
            nullptr,
            L"Failed to register the window class.",
            L"DropPanel",
            MB_ICONERROR | MB_OK
        );

        return 1;
    }

    DropPanelApp app;

    HWND hwnd =
        CreateMainWindow(
            hInstance,
            &app
        );

    if (!hwnd) {
        MessageBoxW(
            nullptr,
            L"Failed to create the DropPanel window.",
            L"DropPanel",
            MB_ICONERROR | MB_OK
        );

        return 1;
    }

    ShowWindow(
        hwnd,
        SW_SHOWNOACTIVATE
    );

    UpdateWindow(hwnd);

    MSG message{};

    while (true) {
        const BOOL result =
            GetMessageW(
                &message,
                nullptr,
                0,
                0
            );

        if (result == 0) {
            break;
        }

        if (result == -1) {
            break;
        }

        TranslateMessage(&message);
        DispatchMessageW(&message);
    }

    return static_cast<int>(
        message.wParam
    );
}