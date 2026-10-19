#pragma once
#include <windows.h>
#include <wchar.h>
#include <stdlib.h>

struct Settings {
    bool skipIntros = true;
    bool rapidPlay = true;
    bool rareHats = true;
    bool unusedHats = true;
    bool unlockAll = false;
    bool joannaDarkAttack = false;
    bool log = false;
};

// Resolve beside the loaded DLL, independently of the game's working directory.
template<size_t N> static bool siblingPath(HMODULE module, const wchar_t* name, wchar_t (&path)[N]) {
    DWORD length = GetModuleFileNameW(module, path, static_cast<DWORD>(N));
    if (!length || length >= N) return false;
    wchar_t* separator = wcsrchr(path, L'\\');
    return separator && wcscpy_s(separator + 1, N - (separator + 1 - path), name) == 0;
}

static bool readOption(const wchar_t* path, const wchar_t* key, bool defaultValue = true) {
    wchar_t value[64];
    GetPrivateProfileStringW(L"Settings", key, defaultValue ? L"1" : L"0", value, _countof(value), path);
    // Only 1 enables a feature. UnlockAllRewards is opt-in for existing installations.
    return wcscmp(value, L"1") == 0;
}

static Settings readSettings(const wchar_t* path) {
    return {readOption(path, L"SkipIntros"), readOption(path, L"RapidPlay"), readOption(path, L"RareHats"), readOption(path, L"UnusedHats"), readOption(path, L"UnlockAllRewards", false), readOption(path, L"JoannaDarkAttack", false), readOption(path, L"Log", false)};
}
