@echo off
rem CrispPlay geliştirme başlatıcısı: derleme klasöründeki EXE'yi Qt ve FFmpeg
rem yollarıyla açar. Bir videoyu bu dosyanın (ya da kısayolun) üzerine
rem bırakırsan onu açar. Ayarlar %APPDATA%\CrispPlay altında kalır.
setlocal
set "ROOT=%~dp0..\.."
set "DEPS=%ROOT%\..\deps"
set "EXE=%ROOT%\out\build\x64-release\veyra_qml_ui.exe"
if not exist "%EXE%" (
  echo CrispPlay derlenmemis: once custom\scripts\build.ps1 calistirin.
  pause
  exit /b 1
)
set "PATH=%DEPS%\qt\6.8.3\msvc2022_64\bin;%DEPS%\vcpkg_installed\x64-windows\bin;%PATH%"
set "DATA=%APPDATA%\CrispPlay"
if not exist "%DATA%" mkdir "%DATA%"
if not exist "%DATA%\qml-preferences.v1.json" echo {"language":"tr"}> "%DATA%\qml-preferences.v1.json"
start "" "%EXE%" --data-dir "%DATA%" %*
