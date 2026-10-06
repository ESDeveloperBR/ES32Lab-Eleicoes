@echo off
setlocal

set "SOURCE=.pio\build\nodemcu-32s\firmware.bin"
set "DIST=dist"
set "TARGET=%DIST%\ES32Lab-Eleicoes.bin"

echo.
echo ============================================
echo   ES32Lab Eleicoes - Preparar Release
echo ============================================
echo.

if not exist "%SOURCE%" (
    echo ERRO: firmware.bin nao encontrado.
    echo.
    echo Compile primeiro o projeto no PlatformIO.
    echo Esperado:
    echo %SOURCE%
    echo.
    pause
    exit /b 1
)

if not exist "%DIST%" mkdir "%DIST%"

copy /Y "%SOURCE%" "%TARGET%" >nul

if errorlevel 1 (
    echo ERRO ao copiar o firmware.
    pause
    exit /b 1
)

echo Firmware preparado:
echo %TARGET%
echo.

for %%A in ("%TARGET%") do echo Tamanho: %%~zA bytes

echo.
echo SHA-256:
powershell -NoProfile -Command "(Get-FileHash '%TARGET%' -Algorithm SHA256).Hash"

echo.
echo ============================================
echo   Release pronta para publicacao
echo ============================================
echo.
echo Anexe este arquivo na GitHub Release:
echo ES32Lab-Eleicoes.bin
echo.
pause
