@echo off
setlocal EnableExtensions EnableDelayedExpansion
cd /d "%~dp0.."

set "REPO=ESDeveloperBR/ES32Lab-Eleicoes"
set "BRANCH=main"
set "SOURCE=.pio\build\nodemcu-32s\firmware.bin"
set "MAIN=src\main.cpp"
set "DIST=dist"
set "BIN=%DIST%\ES32Lab-Eleicoes.bin"
set "MANIFEST=%DIST%\manifest.json"
set "INSTALLER=installer\ES32Lab_Eleicoes_Installer.zip"
set "TMP_SHA=%TEMP%\es32lab_sha.txt"

echo.
echo ============================================================
echo   ES32Lab Eleicoes - Preparar e Publicar Release
echo ============================================================
echo.

if not exist "%MAIN%" (
  echo ERRO: %MAIN% nao encontrado.
  pause
  exit /b 1
)

if not exist "%SOURCE%" (
  echo ERRO: %SOURCE% nao encontrado.
  echo Compile primeiro pelo PlatformIO.
  pause
  exit /b 1
)

set "VERSION="

for /f "tokens=5" %%V in ('findstr /C:"constexpr char APP_VERSION" "%MAIN%"') do (
  set "VERSION=%%V"
)

if not defined VERSION (
  echo ERRO: APP_VERSION nao encontrado em %MAIN%.
  echo.
  echo Linha esperada:
  echo   constexpr char APP_VERSION[] = "2.4.1";
  echo.
  pause
  exit /b 1
)

set "VERSION=!VERSION:"=!"
set "VERSION=!VERSION:;=!"

set "TAG=v%VERSION%"

echo Versao detectada: %VERSION%
echo Tag: %TAG%
echo.

if not exist "%DIST%" mkdir "%DIST%"
copy /Y "%SOURCE%" "%BIN%" >nul
if errorlevel 1 (
  echo ERRO ao copiar firmware.
  pause
  exit /b 1
)

for %%A in ("%BIN%") do set "SIZE=%%~zA"

powershell -NoProfile -ExecutionPolicy Bypass -Command "$h=Get-FileHash '%BIN%' -Algorithm SHA256;$h.Hash.ToLower()" > "%TMP_SHA%"
if errorlevel 1 (
  echo ERRO ao calcular SHA-256.
  if exist "%TMP_SHA%" del /q "%TMP_SHA%"
  pause
  exit /b 1
)

set /p SHA256=<"%TMP_SHA%"
del /q "%TMP_SHA%" >nul 2>&1

echo Firmware: %BIN%
echo Tamanho: %SIZE% bytes
echo SHA-256: %SHA256%
echo.

powershell -NoProfile -ExecutionPolicy Bypass -Command "$m=[ordered]@{project='ES32Lab-Eleicoes';version='%VERSION%';tag='%TAG%';file='ES32Lab-Eleicoes.bin';size=[int64]%SIZE%;sha256='%SHA256%';firmware_url='https://github.com/%REPO%/releases/latest/download/ES32Lab-Eleicoes.bin';manifest_url='https://github.com/%REPO%/releases/latest/download/manifest.json';release_url='https://github.com/%REPO%/releases/tag/%TAG%'};$json=$m|ConvertTo-Json -Depth 3;$utf8=New-Object System.Text.UTF8Encoding($false);[System.IO.File]::WriteAllText((Join-Path (Get-Location) '%MANIFEST%'),$json,$utf8)"

if errorlevel 1 (
  echo ERRO ao gerar manifest.json.
  pause
  exit /b 1
)

echo Manifesto criado: %MANIFEST%
echo.

set "DIRTY="
for /f "delims=" %%S in ('git status --porcelain --untracked-files=normal') do set "DIRTY=1"

if defined DIRTY (
  echo PUBLICACAO INTERROMPIDA: existem alteracoes sem commit.
  git status --short
  echo.
  echo Faca o commit e execute novamente.
  pause
  exit /b 1
)

where gh >nul 2>&1
if errorlevel 1 (
  echo ERRO: GitHub CLI nao encontrado.
  echo Instale com: winget install --id GitHub.cli
  pause
  exit /b 1
)

gh auth status -h github.com >nul 2>&1
if errorlevel 1 (
  echo ERRO: GitHub CLI nao autenticado.
  echo Execute: gh auth login
  pause
  exit /b 1
)

set "CURRENT_BRANCH="
for /f "delims=" %%B in ('git branch --show-current') do set "CURRENT_BRANCH=%%B"

if /I not "%CURRENT_BRANCH%"=="%BRANCH%" (
  echo ERRO: branch atual "%CURRENT_BRANCH%". Use "%BRANCH%".
  pause
  exit /b 1
)

echo Enviando branch %BRANCH%...
git push origin "%BRANCH%"
if errorlevel 1 (
  echo ERRO no git push.
  pause
  exit /b 1
)

gh release view "%TAG%" -R "%REPO%" >nul 2>&1
if errorlevel 1 goto CREATE
goto UPDATE

:CREATE
echo Criando Release %TAG%...
if exist "%INSTALLER%" (
  gh release create "%TAG%" "%BIN%" "%MANIFEST%" "%INSTALLER%" -R "%REPO%" --target "%BRANCH%" --title "ES32Lab Eleicoes %TAG%" --generate-notes --latest
) else (
  gh release create "%TAG%" "%BIN%" "%MANIFEST%" -R "%REPO%" --target "%BRANCH%" --title "ES32Lab Eleicoes %TAG%" --generate-notes --latest
)
if errorlevel 1 (
  echo ERRO ao criar Release.
  pause
  exit /b 1
)
goto DONE

:UPDATE
echo Release %TAG% ja existe. Atualizando assets...
if exist "%INSTALLER%" (
  gh release upload "%TAG%" "%BIN%" "%MANIFEST%" "%INSTALLER%" -R "%REPO%" --clobber
) else (
  gh release upload "%TAG%" "%BIN%" "%MANIFEST%" -R "%REPO%" --clobber
)
if errorlevel 1 (
  echo ERRO ao atualizar assets.
  pause
  exit /b 1
)
gh release edit "%TAG%" -R "%REPO%" --latest >nul 2>&1

:DONE
echo.
echo ============================================================
echo   RELEASE PUBLICADA COM SUCESSO
echo ============================================================
echo https://github.com/%REPO%/releases/tag/%TAG%
echo.
echo Firmware:
echo https://github.com/%REPO%/releases/latest/download/ES32Lab-Eleicoes.bin
echo.
echo Manifesto:
echo https://github.com/%REPO%/releases/latest/download/manifest.json
echo.
pause
endlocal
