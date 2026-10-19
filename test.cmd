@echo off
setlocal
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars32.bat" >nul
if errorlevel 1 exit /b 1
pushd "%~dp0"
if not exist build mkdir build
cl /nologo /W4 /EHsc /O2 /MT /std:c++17 tests\hook_tests.cpp /Fobuild\hook_tests.obj /Febuild\hook_tests.exe
if errorlevel 1 goto failed
build\hook_tests.exe
if errorlevel 1 goto failed
cl /nologo /W4 /EHsc /O2 /MT /std:c++17 tests\proxy_smoke.cpp /Fobuild\proxy_smoke.obj /Febuild\proxy_smoke.exe
if errorlevel 1 goto failed
build\proxy_smoke.exe "%~dp0dist\dinput8.dll"
if errorlevel 1 goto failed
popd
exit /b 0
:failed
popd
exit /b 1
