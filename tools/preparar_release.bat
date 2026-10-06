@echo off
setlocal EnableExtensions EnableDelayedExpansion

rem ============================================================
rem ES32Lab Eleicoes - Preparar e Publicar Release
rem ============================================================
rem
rem O script:
rem   1. Localiza o firmware gerado pelo PlatformIO.
rem   2. Le APP_VERSION de src\main.cpp.
rem   3. Copia firmware.bin para dist\ES32Lab-Eleicoes.bin.
rem   4. Calcula tamanho e SHA-256.
rem   5. Gera dist\manifest.json.
rem   6. Confere Git/GitHub CLI.
rem   7. Faz push da branch main.
rem   8. Cria a GitHub Release ou atualiza seus assets.
rem
rem Repositorio:
rem   https://github.com/ESDeveloperBR/ES32Lab-Eleicoes
rem
rem Requisitos para publicacao automatica:
rem   - Git instalado.
rem   - GitHub CLI (gh) instalado.
rem   - Executar uma vez: gh auth login
rem
rem ============================================================

set "REPO=ESDeveloperBR/ES32Lab-Eleicoes"
set "BRANCH=main"

cd /d "%~dp0.."

set "SOURCE=.pio\build\nodemcu-32s\firmware.bin"
set "MAIN=src\main.cpp"
set "DIST=dist"
set "BIN=%DIST%\ES32Lab-Eleicoes.bin"
set "MANIFEST=%DIST%\manifest.json"
set "INSTALLER=installer\ES32Lab_Eleicoes_Installer.zip"

echo.
echo ============================================================
echo   ES32Lab Eleicoes - Preparar e Publicar Release
echo ============================================================
echo.

if not exist "%MAIN%" (
    echo ERRO: nao encontrei %MAIN%.
    echo.
    pause
    exit /b 1
)

if not exist "%SOURCE%" (
    echo ERRO: firmware.bin nao encontrado.
    echo Compile primeiro o projeto no PlatformIO.
    echo Esperado:
    echo   %SOURCE%
    echo.
    pause
    exit /b 1
)

for /f "usebackq delims=" %%V in (`powershell -NoProfile -Command "$c = Get-Content -Raw '%MAIN%'; $m = [regex]::Match($c, 'APP_VERSION\[\]\s*=\s*\"([^\"]+)\"'); if (-not $m.Success) { exit 2 }; $m.Groups[1].Value"`) do (
    set "VERSION=%%V"
)

if not defined VERSION (
    echo ERRO: nao consegui localizar APP_VERSION em %MAIN%.
    echo Exemplo esperado:
    echo   constexpr char APP_VERSION[] = "2.3.3";
    echo.
    pause
    exit /b 1
)

set "TAG=v%VERSION%"

echo Versao detectada: %VERSION%
echo Tag da Release:    %TAG%
echo.

if not exist "%DIST%" mkdir "%DIST%"

copy /Y "%SOURCE%" "%BIN%" >nul

if errorlevel 1 (
    echo ERRO: nao foi possivel copiar o firmware.
    pause
    exit /b 1
)

for %%A in ("%BIN%") do set "SIZE=%%~zA"

for /f "usebackq delims=" %%H in (`powershell -NoProfile -Command "(Get-FileHash '%BIN%' -Algorithm SHA256).Hash.ToLower()"`) do (
    set "SHA256=%%H"
)

if not defined SHA256 (
    echo ERRO: nao foi possivel calcular SHA-256.
    pause
    exit /b 1
)

echo Firmware:
echo   %BIN%
echo Tamanho:
echo   %SIZE% bytes
echo SHA-256:
echo   %SHA256%
echo.

powershell -NoProfile -Command ^
  "$m = [ordered]@{" ^
  "project='ES32Lab-Eleicoes';" ^
  "version='%VERSION%';" ^
  "tag='%TAG%';" ^
  "file='ES32Lab-Eleicoes.bin';" ^
  "size=[int64]%SIZE%;" ^
  "sha256='%SHA256%';" ^
  "firmware_url='https://github.com/%REPO%/releases/latest/download/ES32Lab-Eleicoes.bin';" ^
  "manifest_url='https://github.com/%REPO%/releases/latest/download/manifest.json';" ^
  "release_url='https://github.com/%REPO%/releases/tag/%TAG%'" ^
  "};" ^
  "$m | ConvertTo-Json -Depth 3 | Set-Content -Encoding UTF8 '%MANIFEST%'"

if errorlevel 1 (
    echo ERRO: nao foi possivel gerar manifest.json.
    pause
    exit /b 1
)

echo Manifesto criado:
echo   %MANIFEST%
echo.
type "%MANIFEST%"
echo.
echo.

where git >nul 2>&1

if errorlevel 1 (
    echo AVISO: Git nao encontrado.
    echo Os arquivos foram preparados, mas nao publicados.
    echo.
    pause
    exit /b 0
)

for /f "delims=" %%S in ('git status --porcelain --untracked-files=normal') do (
    set "DIRTY=1"
)

if defined DIRTY (
    echo ============================================================
    echo   PUBLICACAO INTERROMPIDA
    echo ============================================================
    echo.
    echo Existem alteracoes nao commitadas:
    echo.
    git status --short
    echo.
    echo Faca o commit antes de publicar a Release.
    echo O BIN e o manifest.json ja estao prontos em dist\.
    echo.
    pause
    exit /b 1
)

where gh >nul 2>&1

if errorlevel 1 (
    echo ============================================================
    echo   ARQUIVOS PREPARADOS - PUBLICACAO MANUAL NECESSARIA
    echo ============================================================
    echo.
    echo GitHub CLI ^(gh^) nao instalado.
    echo.
    echo Instale uma vez com:
    echo   winget install --id GitHub.cli
    echo.
    echo Depois autentique:
    echo   gh auth login
    echo.
    pause
    exit /b 0
)

gh auth status -h github.com >nul 2>&1

if errorlevel 1 (
    echo GitHub CLI nao autenticado.
    echo Execute:
    echo   gh auth login
    echo.
    pause
    exit /b 1
)

for /f "usebackq delims=" %%B in (`git branch --show-current`) do (
    set "CURRENT_BRANCH=%%B"
)

if /I not "%CURRENT_BRANCH%"=="%BRANCH%" (
    echo ERRO: branch atual e "%CURRENT_BRANCH%".
    echo Utilize a branch "%BRANCH%".
    echo.
    pause
    exit /b 1
)

echo Enviando branch %BRANCH% para o GitHub...
git push origin "%BRANCH%"

if errorlevel 1 (
    echo ERRO: git push falhou.
    echo A Release nao sera publicada.
    echo.
    pause
    exit /b 1
)

echo.
echo Branch sincronizada.
echo.

gh release view "%TAG%" -R "%REPO%" >nul 2>&1

if errorlevel 1 (
    echo Criando nova Release %TAG%...
    echo.

    if exist "%INSTALLER%" (
        gh release create "%TAG%" ^
          "%BIN%" ^
          "%MANIFEST%" ^
          "%INSTALLER%" ^
          -R "%REPO%" ^
          --target "%BRANCH%" ^
          --title "ES32Lab Eleicoes %TAG%" ^
          --generate-notes ^
          --latest
    ) else (
        gh release create "%TAG%" ^
          "%BIN%" ^
          "%MANIFEST%" ^
          -R "%REPO%" ^
          --target "%BRANCH%" ^
          --title "ES32Lab Eleicoes %TAG%" ^
          --generate-notes ^
          --latest
    )

    if errorlevel 1 (
        echo ERRO: nao foi possivel criar a Release.
        echo.
        pause
        exit /b 1
    )

) else (
    echo A Release %TAG% ja existe.
    echo Atualizando os assets...
    echo.

    if exist "%INSTALLER%" (
        gh release upload "%TAG%" ^
          "%BIN%" ^
          "%MANIFEST%" ^
          "%INSTALLER%" ^
          -R "%REPO%" ^
          --clobber
    ) else (
        gh release upload "%TAG%" ^
          "%BIN%" ^
          "%MANIFEST%" ^
          -R "%REPO%" ^
          --clobber
    )

    if errorlevel 1 (
        echo ERRO: nao foi possivel atualizar os assets.
        echo.
        pause
        exit /b 1
    )

    gh release edit "%TAG%" -R "%REPO%" --latest >nul 2>&1
)

echo.
echo ============================================================
echo   RELEASE PUBLICADA COM SUCESSO
echo ============================================================
echo.
echo Versao:
echo   %VERSION%
echo.
echo Release:
echo   https://github.com/%REPO%/releases/tag/%TAG%
echo.
echo Firmware latest:
echo   https://github.com/%REPO%/releases/latest/download/ES32Lab-Eleicoes.bin
echo.
echo Manifest latest:
echo   https://github.com/%REPO%/releases/latest/download/manifest.json
echo.
echo Arquivos locais:
echo   %BIN%
echo   %MANIFEST%
if exist "%INSTALLER%" echo   %INSTALLER%
echo.
pause

endlocal
