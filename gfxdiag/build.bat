@echo off
setlocal
cd /d "%~dp0"
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvarsall.bat" x86 >nul 2>nul
cl /nologo /O2 /W3 /MT /LD /D_CRT_SECURE_NO_WARNINGS tcnyc_gfxdiag.c /link /OUT:TCNYCGfxDiag.asi user32.lib || exit /b 1
echo built
