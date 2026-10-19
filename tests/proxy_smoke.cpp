#define WIN32_LEAN_AND_MEAN
#define DIRECTINPUT_VERSION 0x0800
#include <windows.h>
#include <dinput.h>
#include <stdio.h>
#include <assert.h>
#pragma comment(lib,"dxguid.lib")
int wmain(int argc, wchar_t** argv) {
    assert(argc==2);
    HMODULE proxy=LoadLibraryW(argv[1]); assert(proxy);
    using Factory=HRESULT(WINAPI*)(HINSTANCE,DWORD,REFIID,void**,IUnknown*);
    auto factory=reinterpret_cast<Factory>(GetProcAddress(proxy,"DirectInput8Create"));
    assert(factory && reinterpret_cast<FARPROC>(factory)==GetProcAddress(proxy,MAKEINTRESOURCEA(1)));
    for(int n=0;n<2;++n) {
        IDirectInput8W* input=nullptr;
        HRESULT result=factory(GetModuleHandleW(nullptr),0x0800,IID_IDirectInput8W,
            reinterpret_cast<void**>(&input),nullptr);
        assert(SUCCEEDED(result) && input);
        IDirectInputDevice8W* keyboard=nullptr;
        assert(SUCCEEDED(input->CreateDevice(GUID_SysKeyboard,&keyboard,nullptr)) && keyboard);
        DIDEVICEINSTANCEW info={}; info.dwSize=sizeof info;
        assert(SUCCEEDED(keyboard->GetDeviceInfo(&info)));
        keyboard->Release(); input->Release();
    }
    IDirectInput8W* invalid=nullptr;
    assert(FAILED(factory(GetModuleHandleW(nullptr),0,IID_IDirectInput8W,
        reinterpret_cast<void**>(&invalid),nullptr)) && !invalid);
    wchar_t path[MAX_PATH]; assert(GetSystemDirectoryW(path,MAX_PATH));
    wcscat_s(path,L"\\dinput8.dll"); HMODULE original=LoadLibraryW(path);
    assert(original && original!=proxy);
    using Format=const DIDATAFORMAT*(WINAPI*)();
    auto forwarded=reinterpret_cast<Format>(GetProcAddress(proxy,"GetdfDIJoystick"));
    auto genuine=reinterpret_cast<Format>(GetProcAddress(original,"GetdfDIJoystick"));
    assert(forwarded && genuine && forwarded()==genuine());
    puts("PASS: named/ordinal factory, repeated DirectInput creation, keyboard device metadata, error passthrough, original Windows DLL, joystick format forwarding.");
    FreeLibrary(original); FreeLibrary(proxy);
}
