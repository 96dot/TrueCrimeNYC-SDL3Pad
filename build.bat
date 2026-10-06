@echo off
rem Builds TCNYCSDL3Pad.asi (32-bit) with Visual Studio's x86 compiler.
setlocal
cd /d "%~dp0"
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvarsall.bat" x86 >nul || exit /b 1
if not exist build mkdir build
cl /nologo /O2 /W4 /wd4100 /wd4201 /MT /LD /std:c11 /Fobuild\ tcnyc_sdl3pad.c ^
   /link /OUT:build\TCNYCSDL3Pad.asi /IMPLIB:build\TCNYCSDL3Pad.lib user32.lib gdi32.lib dxguid.lib || exit /b 1
echo Built build\TCNYCSDL3Pad.asi
