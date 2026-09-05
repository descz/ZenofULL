@echo off
setlocal
cd /d "%~dp0wails-app"
set "PATH=C:\Program Files\Go\bin;%USERPROFILE%\go\bin;%PATH%"
wails build -clean
if errorlevel 1 goto :fail
echo.
echo Executavel criado em: %CD%\build\bin\ZenoAgent.exe
echo Configure .env ao lado do executavel para habilitar o Deepgram.
exit /b 0
:fail
echo.
echo Falha ao compilar o Zeno Agent.
pause
exit /b 1
