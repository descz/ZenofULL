@echo off
rem Compila o zeno-server.exe (server.c + ponte zeno-bridge.c + runtime libzenoc.a)
rem Uso: build-server.cmd
cd /d "%~dp0"
gcc -O2 -o zeno-server.exe server.c zeno-bridge.c ^
  -IZenoC/include -IZenoC/src ^
  ZenoC/build/preset-offline-debug/libzenoc.a ^
  -lws2_32 -luserenv -lwinhttp
if errorlevel 1 (
  echo Falha no build.
  exit /b 1
)
echo Build OK: zeno-server.exe
