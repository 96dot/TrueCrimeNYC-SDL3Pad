@echo off
setlocal
cd /d "%~dp0"
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvarsall.bat" x86 >nul 2>nul
cl /nologo /O1 /W3 /MT harness.c /link /OUT:harness.exe dinput8.lib dxguid.lib user32.lib || exit /b 1
copy /y ..\build\TCNYCSDL3Pad.asi . >nul || (echo build\TCNYCSDL3Pad.asi not found - run build.bat first & exit /b 1)
copy /y ..\..\SDL3.dll . >nul || (echo SDL3.dll not found next to tcnyc.exe & exit /b 1)
(echo [Settings]& echo InputInBackground=1& echo LogInput=1) > TCNYCSDL3Pad.ini
echo ready
