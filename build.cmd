@echo off
setlocal
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars32.bat" >nul
if errorlevel 1 exit /b 1
pushd "%~dp0"
if not exist build mkdir build
if not exist dist mkdir dist
rem Preserve the Windows DirectInput export ordinals, including COM entries.
cl /nologo /W4 /EHsc /O2 /MT /std:c++17 /LD automatic.cpp /Fobuild\automatic.obj /link /DEF:dinput8.def /OUT:dist\dinput8.dll /IMPLIB:build\dinput8.lib /DYNAMICBASE /NXCOMPAT /IGNORE:4222
set "build_result=%errorlevel%"
if not "%build_result%"=="0" goto finished
copy /y Plus.ini dist\Plus.ini >nul
:finished
popd
exit /b %build_result%
