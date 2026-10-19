// Single-file installation: game imports DirectInput8Create from dinput8.dll.
// DllMain remains minimal; all work happens on the first normal input call.
#include "plus.cpp"
#include <tlhelp32.h>
#include <bcrypt.h>
#include <unknwn.h>
#pragma comment(lib,"bcrypt.lib")

static INIT_ONCE inputOnce = INIT_ONCE_STATIC_INIT;
static INIT_ONCE patchOnce = INIT_ONCE_STATIC_INIT;
static HMODULE systemInput;

static BOOL CALLBACK loadInput(PINIT_ONCE, PVOID, PVOID*) {
    wchar_t path[MAX_PATH];
    UINT length = GetSystemDirectoryW(path, MAX_PATH);
    if (!length || length >= MAX_PATH - 13) return FALSE;
    wcscat_s(path, L"\\dinput8.dll");
    systemInput = LoadLibraryExW(path, nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    return systemInput != nullptr && systemInput != selfModule;
}
static FARPROC inputProc(const char* name) {
    if (!InitOnceExecuteOnce(&inputOnce, loadInput, nullptr, nullptr)) return nullptr;
    return GetProcAddress(systemInput, name);
}

static bool supportedHost(const wchar_t* path) {
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE |
        FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    BCRYPT_ALG_HANDLE algorithm = nullptr; BCRYPT_HASH_HANDLE hash = nullptr;
    bool ok = BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) >= 0;
    if (ok) ok = BCryptCreateHash(algorithm, &hash, nullptr, 0, nullptr, 0, 0) >= 0;
    BYTE buffer[65536], digest[32]; DWORD got;
    while (ok) {
        ok = ReadFile(file, buffer, sizeof buffer, &got, nullptr) != 0;
        if (!ok || !got) break;
        ok = BCryptHashData(hash, buffer, got, 0) >= 0;
    }
    if (ok) ok = BCryptFinishHash(hash, digest, sizeof digest, 0) >= 0;
    if (hash) BCryptDestroyHash(hash);
    if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0);
    CloseHandle(file);
    const BYTE expected[32] = {
        0x69,0x02,0xb7,0x2b,0xd9,0xe4,0x67,0x11,0x46,0xb4,0xca,0xed,0x69,0xe5,0xcd,0xfc,
        0xa0,0x81,0x57,0x52,0x23,0xed,0x58,0xa1,0x7c,0x87,0xc6,0xf5,0x8b,0xcd,0xd5,0x91
    };
    return ok && memcmp(digest, expected, sizeof expected) == 0;
}

// No allocation or loader calls after suspending another thread. Initialization,
// system DLL loading and hash checking have all completed before this scope.
struct HoldThreads {
    HANDLE held[512] = {}; unsigned count = 0;
    ~HoldThreads() {
        for (unsigned i = 0; i < count; ++i) { ResumeThread(held[i]); CloseHandle(held[i]); }
    }
    bool acquire() {
        HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
        if (snapshot == INVALID_HANDLE_VALUE) return false;
        THREADENTRY32 entry = {sizeof entry}; bool ok = true;
        DWORD pid = GetCurrentProcessId(), tid = GetCurrentThreadId();
        BOOL more = Thread32First(snapshot, &entry);
        if (!more) ok = false;
        while (more && ok) {
            if (entry.th32OwnerProcessID == pid && entry.th32ThreadID != tid) {
                if (count == _countof(held)) { ok = false; break; }
                HANDLE thread = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT |
                    THREAD_QUERY_INFORMATION, FALSE, entry.th32ThreadID);
                if (!thread) { ok = false; break; }
                if (SuspendThread(thread) == DWORD(-1)) { CloseHandle(thread); ok = false; break; }
                held[count++] = thread;
            }
            more = Thread32Next(snapshot, &entry);
        }
        CloseHandle(snapshot);
        if (!ok) return false;
        auto headers = reinterpret_cast<IMAGE_NT_HEADERS*>(reinterpret_cast<BYTE*>(selfModule) +
            reinterpret_cast<IMAGE_DOS_HEADER*>(selfModule)->e_lfanew);
        uintptr_t begin = reinterpret_cast<uintptr_t>(selfModule);
        for (unsigned i = 0; i < count; ++i) {
            CONTEXT context = {}; context.ContextFlags = CONTEXT_CONTROL;
            if (!GetThreadContext(held[i], &context)) return false;
            if (context.Eip >= begin && context.Eip < begin + headers->OptionalHeader.SizeOfImage) return false;
            for (auto& patch : patches)
                if (context.Eip >= base + patch.rva && context.Eip < base + patch.rva + 5) return false;
        }
        return true;
    }
};

static BOOL CALLBACK enableAutomatic(PINIT_ONCE, PVOID, PVOID*) {
    wchar_t path[MAX_PATH];
    if (!GetModuleFileNameW(nullptr, path, MAX_PATH)) return TRUE;
    const wchar_t* name = wcsrchr(path, L'\\'); name = name ? name + 1 : path;
    // Test hosts and other applications receive ordinary input forwarding only.
    if (_wcsicmp(name, L"Worms W.M.D.exe")) return TRUE;
    bool supported = supportedHost(path);
    if (!prepare(supported)) return TRUE;
    if (!supported) { log("auto-unsupported-build"); return TRUE; }
    if (!settings.skipIntros && !settings.rapidPlay && !settings.rareHats && !settings.unusedHats && !settings.unlockAll && !settings.joannaDarkAttack) { log("auto-disabled"); return TRUE; }
    DWORD result = 999;
    for (unsigned attempt = 0; attempt < 40; ++attempt) {
        bool acquired;
        {
            HoldThreads paused;
            acquired = paused.acquire();
            if (acquired) result = control(1);
        }
        if (acquired) break;
        Sleep(25);
    }
    log(result == 0 ? "auto-enabled" : "auto-enable-failed", result);
    return TRUE;
}

extern "C" HRESULT WINAPI ProxyDirectInput8Create(HINSTANCE instance, DWORD version,
    REFIID iid, void** output, IUnknown* outer) {
    using Factory = HRESULT(WINAPI*)(HINSTANCE, DWORD, REFIID, void**, IUnknown*);
    auto factory = reinterpret_cast<Factory>(inputProc("DirectInput8Create"));
    if (!factory) { if (output) *output = nullptr; return HRESULT_FROM_WIN32(ERROR_MOD_NOT_FOUND); }
    InitOnceExecuteOnce(&patchOnce, enableAutomatic, nullptr, nullptr);
    return factory(instance, version, iid, output, outer);
}
extern "C" HRESULT WINAPI ProxyDllGetClassObject(REFCLSID clsid, REFIID iid, void** output) {
    using Method = HRESULT(WINAPI*)(REFCLSID, REFIID, void**);
    auto method = reinterpret_cast<Method>(inputProc("DllGetClassObject"));
    if (!method) { if (output) *output = nullptr; return CLASS_E_CLASSNOTAVAILABLE; }
    return method(clsid, iid, output);
}
extern "C" HRESULT WINAPI ProxyDllCanUnloadNow() { return S_FALSE; }
extern "C" HRESULT WINAPI ProxyDllRegisterServer() {
    using Method = HRESULT(WINAPI*)();
    auto method = reinterpret_cast<Method>(inputProc("DllRegisterServer"));
    return method ? method() : E_FAIL;
}
extern "C" HRESULT WINAPI ProxyDllUnregisterServer() {
    using Method = HRESULT(WINAPI*)();
    auto method = reinterpret_cast<Method>(inputProc("DllUnregisterServer"));
    return method ? method() : E_FAIL;
}
extern "C" const void* WINAPI ProxyGetdfDIJoystick() {
    using Method = const void*(WINAPI*)();
    auto method = reinterpret_cast<Method>(inputProc("GetdfDIJoystick"));
    return method ? method() : nullptr;
}
