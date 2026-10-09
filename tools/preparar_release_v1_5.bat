@echo off
setlocal EnableExtensions
set "ES32LAB_SELF=%~f0"
set "ES32LAB_SCRIPT_DIR=%~dp0"
set "ES32LAB_TMPPS=%TEMP%\es32lab_release_%RANDOM%_%RANDOM%.ps1"

powershell -NoProfile -ExecutionPolicy Bypass -Command ^
 "$p=$env:ES32LAB_SELF;$t=$env:ES32LAB_TMPPS;$l=[IO.File]::ReadAllLines($p);$m=[Array]::IndexOf($l,'#===ES32LAB_POWERSHELL===');if($m-lt 0){exit 99};[IO.File]::WriteAllLines($t,$l[($m+1)..($l.Length-1)],(New-Object Text.UTF8Encoding($true)))"
if errorlevel 1 (
  echo ERRO: nao foi possivel iniciar o preparador de release.
  exit /b 1
)

powershell -NoProfile -ExecutionPolicy Bypass -File "%ES32LAB_TMPPS%"
set "RC=%ERRORLEVEL%"
del /q "%ES32LAB_TMPPS%" >nul 2>nul
exit /b %RC%

#===ES32LAB_POWERSHELL===
$ErrorActionPreference="Stop"
Set-StrictMode -Version 2.0

function Title($t){Write-Host "";Write-Host "============================================================";Write-Host " $t";Write-Host "============================================================"}
function Cancel($v){if($null-ne $v -and $v.Trim().ToUpperInvariant()-eq "CANCELAR"){throw [OperationCanceledException]::new("Cancelado")}}
function Ask($key,$label,$current,$required,$example){
  if($null-ne $current -and $current.Trim()){return $current.Trim()}
  while($true){
    Write-Host "";Write-Host "Campo: $key";Write-Host $label
    if($example){Write-Host "Exemplo: $example"}
    if($required){Write-Host "Obrigatorio. Digite CANCELAR para encerrar."}
    else{Write-Host "Opcional. Enter para pular ou CANCELAR para encerrar."}
    $v=Read-Host ">";Cancel $v;$v=$v.Trim()
    if($v){return $v}
    if(-not $required){return ""}
    Write-Host "[ATENCAO] O campo '$key' e obrigatorio."
  }
}
function YesNo($q,$defaultYes=$true){
  while($true){
    $s=if($defaultYes){"[S/n]"}else{"[s/N]"}
    $v=Read-Host "$q $s (ou CANCELAR)";Cancel $v;$v=$v.Trim().ToLowerInvariant()
    if($v-eq ""){return $defaultYes}
    if($v-eq "s"-or$v-eq "sim"){return $true}
    if($v-eq "n"-or$v-eq "nao"-or$v-eq "não"){return $false}
    Write-Host "Use S, N, Enter ou CANCELAR."
  }
}
function ReadIni($path){
  $ini=@{};$sec=""
  if(-not(Test-Path -LiteralPath $path)){return $ini}
  foreach($raw in Get-Content -LiteralPath $path -Encoding UTF8){
    $l=$raw.Trim()
    if(-not$l -or $l.StartsWith(";") -or $l.StartsWith("#")){continue}
    if($l-match '^\[(.+)\]$'){$sec=$matches[1].Trim();if(-not$ini.ContainsKey($sec)){$ini[$sec]=@{}};continue}
    if($sec -and $l-match '^([^=]+)=(.*)$'){$ini[$sec][$matches[1].Trim()]=$matches[2].Trim()}
  }
  return $ini
}
function Exe($names){foreach($n in $names){$c=Get-Command $n -ErrorAction SilentlyContinue;if($c){return $c.Source}};return $null}

function FindPlatformIO {
  # 1) Primeiro tenta pelo PATH.
  $cmd=Exe @("pio","platformio")
  if($cmd){return $cmd}

  # 2) Caminho padrao usado pelo PlatformIO Core/VS Code no Windows.
  $candidates=@()

  if($env:PLATFORMIO_CORE_DIR){
    $candidates += (Join-Path $env:PLATFORMIO_CORE_DIR "penv\Scripts\platformio.exe")
    $candidates += (Join-Path $env:PLATFORMIO_CORE_DIR "penv\Scripts\pio.exe")
  }

  if($env:USERPROFILE){
    $candidates += (Join-Path $env:USERPROFILE ".platformio\penv\Scripts\platformio.exe")
    $candidates += (Join-Path $env:USERPROFILE ".platformio\penv\Scripts\pio.exe")
  }

  if($HOME){
    $candidates += (Join-Path $HOME ".platformio\penv\Scripts\platformio.exe")
    $candidates += (Join-Path $HOME ".platformio\penv\Scripts\pio.exe")
  }

  foreach($candidate in ($candidates | Select-Object -Unique)){
    if(Test-Path -LiteralPath $candidate -PathType Leaf){
      return [IO.Path]::GetFullPath($candidate)
    }
  }

  return $null
}
function Run($exe,$argumentList,$desc){
  Write-Host ""
  Write-Host "[EXEC] $desc"
  Write-Host ("       " + $exe + " " + ($argumentList -join " "))
  & $exe @argumentList
  if($LASTEXITCODE -ne 0){
    throw "Falha em '$desc' (codigo $LASTEXITCODE)."
  }
}
function Sha($p){return (Get-FileHash -LiteralPath $p -Algorithm SHA256).Hash.ToLowerInvariant()}
function NoBomJson($p,$o){$j=$o|ConvertTo-Json -Depth 8;[IO.File]::WriteAllText($p,$j+[Environment]::NewLine,(New-Object Text.UTF8Encoding($false)))}
function Num($v){$x=$v.Trim().ToLowerInvariant();if($x.StartsWith("0x")){return [Convert]::ToInt64($x.Substring(2),16)};if($x.EndsWith("k")){return [int64]([double]$x.TrimEnd("k")*1024)};if($x.EndsWith("m")){return [int64]([double]$x.TrimEnd("m")*1024*1024)};return [int64]$x}
function Hex($n){return ("0x{0:X}"-f$n)}
function BinName($v){
  $n=$v.Trim();while($n-match '(?i)\.bin$'){$n=$n.Substring(0,$n.Length-4).TrimEnd()}
  if(-not$n){throw "bin_name vazio."}
  foreach($c in [IO.Path]::GetInvalidFileNameChars()){if($n.Contains([string]$c)){throw "bin_name contem caractere invalido: $c"}}
  return $n
}
function Repo($v){
  $r=$v.Trim()-replace '^https?://github\.com/','' -replace '^git@github\.com:','' -replace '\.git$',''
  $r=$r.Trim("/")
  if($r-notmatch '^[^/\s]+/[^/\s]+$'){throw "repository deve usar ORGANIZACAO/REPOSITORIO."}
  return $r
}
function Root(){
  $cur=(Get-Location).Path;$sd=$env:ES32LAB_SCRIPT_DIR.TrimEnd("\","/")
  $c=@($cur,$sd,(Split-Path -Parent $sd))|Where-Object{$_}|Select-Object -Unique
  foreach($x in $c){if(Test-Path -LiteralPath (Join-Path $x "platformio.ini")){return [IO.Path]::GetFullPath($x)}}
  if((Split-Path -Leaf $sd)-ieq "tools"){return [IO.Path]::GetFullPath((Split-Path -Parent $sd))}
  return [IO.Path]::GetFullPath($cur)
}
function ChooseEnv($ini){
  $e=@($ini.Keys|Where-Object{$_-like "env:*"}|Sort-Object)
  if($e.Count-eq 0){throw "Nenhum [env:...] encontrado."}
  if($e.Count-eq 1){return $e[0].Substring(4)}
  Write-Host "";Write-Host "Ambientes:"
  for($i=0;$i-lt$e.Count;$i++){Write-Host "$($i+1) - $($e[$i].Substring(4))"}
  while($true){$v=Read-Host "Numero (ou CANCELAR)";Cancel $v;$n=0;if([int]::TryParse($v,[ref]$n)-and$n-ge 1-and$n-le$e.Count){return $e[$n-1].Substring(4)};Write-Host "Opcao invalida."}
}
function FindFs($build,$fs){
  $a=@();if($fs){$a+=Join-Path $build ($fs.Trim()+".bin")}
  $a+=@("littlefs.bin","spiffs.bin","fatfs.bin","ffat.bin")|ForEach-Object{Join-Path $build $_}
  foreach($x in $a|Select-Object -Unique){if(Test-Path -LiteralPath $x){return $x}}
  return $null
}
function FirstFile($root,$filter,$hint){
  if(-not(Test-Path -LiteralPath $root)){return $null}
  $x=Get-ChildItem -LiteralPath $root -Filter $filter -File -Recurse -ErrorAction SilentlyContinue
  if($hint){$x=$x|Where-Object{$_.FullName-like "*$hint*"}}
  $f=$x|Select-Object -First 1;if($f){return $f.FullName};return $null
}

function FullImage($build,$fw,$data,$out){
  $boot=Join-Path $build "bootloader.bin";$part=Join-Path $build "partitions.bin"
  if(-not(Test-Path $boot)){throw "bootloader.bin nao encontrado."}
  if(-not(Test-Path $part)){throw "partitions.bin nao encontrado."}
  $core=if($env:PLATFORMIO_CORE_DIR){$env:PLATFORMIO_CORE_DIR}else{Join-Path $HOME ".platformio"}
  $packs=Join-Path $core "packages";$py=Join-Path $core "penv\Scripts\python.exe"
  if(-not(Test-Path $py)){$py=Exe @("python","py")}
  $esp=FirstFile $packs "esptool.py" "tool-esptoolpy"
  $gen=FirstFile $packs "gen_esp32part.py" "framework-arduinoespressif32"
  if(-not$py-or-not$esp-or-not$gen){throw "Ferramentas ESP32 do PlatformIO nao foram localizadas."}
  $csv=Join-Path $env:TEMP ("es32lab_part_"+[guid]::NewGuid().ToString("N")+".csv")
  try{
    Run $py @($gen,$part,$csv) "Interpretando tabela de particoes"
    $lines=@(Get-Content $csv -Encoding UTF8|Where-Object{$_.Trim()-and-not$_.Trim().StartsWith("#")})
    $rows=@($lines|ConvertFrom-Csv -Header Name,Type,SubType,Offset,Size,Flags)
    $app=$rows|Where-Object{$_.Type.Trim().ToLowerInvariant()-in @("app","0x00")}|Select-Object -First 1
    if(-not$app){throw "Particao da aplicacao nao identificada."}
    $fsp=$null
    if($data){
      $fsp=$rows|Where-Object{
        $t=$_.Type.Trim().ToLowerInvariant();$s=$_.SubType.Trim().ToLowerInvariant();$n=$_.Name.Trim().ToLowerInvariant()
        ($t-in @("data","0x01"))-and($s-match 'spiffs|fat|littlefs'-or$n-match 'spiffs|littlefs|fatfs|ffat|filesystem')
      }|Select-Object -First 1
      if(-not$fsp){throw "Imagem de dados existe, mas a particao de filesystem nao foi identificada."}
    }
    $ota=$rows|Where-Object{$_.Name.Trim().ToLowerInvariant()-eq"otadata"-or$_.SubType.Trim().ToLowerInvariant()-eq"ota"}|Select-Object -First 1
    $max=[int64]0
    foreach($r in $rows){try{$end=(Num $r.Offset)+(Num $r.Size);if($end-gt$max){$max=$end}}catch{}}
    $mb=1;while(($mb*1MB)-lt$max){$mb*=2}
    $pairs=@("0x1000",$boot,"0x8000",$part)
    if($ota){$ba=FirstFile $packs "boot_app0.bin" "framework-arduinoespressif32";if($ba){$pairs+=@((Hex (Num $ota.Offset)),$ba)}}
    $pairs+=@((Hex (Num $app.Offset)),$fw)
    if($data){$pairs+=@((Hex (Num $fsp.Offset)),$data)}
    Remove-Item $out -Force -ErrorAction SilentlyContinue
    Write-Host "";Write-Host "[EXEC] Gerando imagem FULL"
    $a4=@($esp,"--chip","esp32","merge_bin","-o",$out,"--fill-flash-size",($mb.ToString()+"MB"))+$pairs
    & $py @a4
    if($LASTEXITCODE-ne 0){
      Remove-Item $out -Force -ErrorAction SilentlyContinue
      $a5=@($esp,"--chip","esp32","merge-bin","-o",$out,"--pad-to-size",($mb.ToString()+"MB"))+$pairs
      & $py @a5
    }
    if($LASTEXITCODE-ne 0-or-not(Test-Path $out)){throw "Falha ao gerar imagem FULL."}
    Write-Host "Imagem FULL gerada com $mb MB."
  }finally{Remove-Item $csv -Force -ErrorAction SilentlyContinue}
}
function AssetUrl($repo,$name){return "https://github.com/$repo/releases/latest/download/"+[Uri]::EscapeDataString($name)}
function Optional($obj,$k,$v){if($v-and$v.Trim()){$obj[$k]=$v.Trim()}}
function ResolvePath($p,$base){if([IO.Path]::IsPathRooted($p)){return [IO.Path]::GetFullPath($p)};return [IO.Path]::GetFullPath((Join-Path $base $p))}

try{
  Title "ES32Lab - Preparar Release"
  $root=Root;$pioIni=Join-Path $root "platformio.ini";$hasPio=Test-Path $pioIni
  Write-Host "Projeto: $root";Write-Host ("PlatformIO: "+$(if($hasPio){"encontrado"}else{"nao encontrado - modo manual"}))
  $ini=@{};$rel=@{};$envName="";$envCfg=@{}
  if($hasPio){
    $ini=ReadIni $pioIni;if($ini.ContainsKey("release")){$rel=$ini["release"]}
    $envName=ChooseEnv $ini;$sec="env:$envName";if($ini.ContainsKey($sec)){$envCfg=$ini[$sec]}
    Write-Host "Ambiente: $envName"
  }
  function GetReleaseValue($table,$key){
    if($null -eq $table){ return "" }
    if($table.ContainsKey($key)){ return [string]$table[$key] }
    return ""
  }

  $name=Ask "name" "Nome publico da aplicacao." (GetReleaseValue $rel "name") $true "ES32Lab nas Eleicoes 2026"
  $developer=Ask "developer" "Responsavel pela aplicacao." (GetReleaseValue $rel "developer") $true "ES Developer"
  $version=Ask "version" "Versao da aplicacao." (GetReleaseValue $rel "version") $true "2.5.3"
  $date=Ask "version_date" "Data no formato AAAA-MM-DD." (GetReleaseValue $rel "version_date") $true "2026-10-09"
  while($true){
    $d=[datetime]::MinValue
    if([datetime]::TryParseExact($date,"yyyy-MM-dd",[Globalization.CultureInfo]::InvariantCulture,[Globalization.DateTimeStyles]::None,[ref]$d)){break}
    Write-Host "[ATENCAO] Data invalida."
    $date=Ask "version_date" "Use AAAA-MM-DD." "" $true "2026-10-09"
  }
  $desc=Ask "description" "Descricao publica." (GetReleaseValue $rel "description") $true "Descricao resumida da aplicacao."
  $thumb=Ask "thumbnail_url" "Imagem de apresentacao." (GetReleaseValue $rel "thumbnail_url") $false "https://exemplo.com/imagem.jpg"
  $video=Ask "video_url" "Video relacionado." (GetReleaseValue $rel "video_url") $false "https://youtube.com/watch?v=..."
  $kwRaw=Ask "keywords" "Palavras-chave separadas por virgula." (GetReleaseValue $rel "keywords") $false "ES32Lab, ESP32, Wi-Fi, OTA"
  while($true){
    try{$bn=BinName(Ask "bin_name" "Nome-base dos BINs; .bin sera removido automaticamente." (GetReleaseValue $rel "bin_name") $true "es32lab_eleicoes");break}
    catch{Write-Host "[ATENCAO] $($_.Exception.Message)";$rel.Remove("bin_name")}
  }
  while($true){
    try{$repo=Repo(Ask "repository" "Repositorio GitHub." (GetReleaseValue $rel "repository") $true "ESDeveloperBR/ES32Lab-Eleicoes");break}
    catch{Write-Host "[ATENCAO] $($_.Exception.Message)";$rel.Remove("repository")}
  }
  $site=Ask "website" "Site oficial." (GetReleaseValue $rel "website") $false "https://www.esdeveloper.com.br/"
  $keywords=@();if($kwRaw){$keywords=@($kwRaw.Split(",")|ForEach-Object{$_.Trim()}|Where-Object{$_}|Select-Object -Unique)}

  $dist=Join-Path $root "dist"
  if(-not(Test-Path $dist)){
    New-Item -ItemType Directory -Path $dist|Out-Null
    Write-Host "Pasta dist/ criada."
  }else{
    Get-ChildItem $dist -Force -ErrorAction SilentlyContinue|Remove-Item -Recurse -Force
  }

  $fwName="$bn.bin";$dataName="$bn-data.bin";$fullName="$bn-full.bin"
  $fwOut=Join-Path $dist $fwName;$dataOut=Join-Path $dist $dataName;$fullOut=Join-Path $dist $fullName
  $hasFw=$false;$hasData=$false;$hasFull=$false

  if($hasPio){
    Title "Compilacao PlatformIO"
    $pio=FindPlatformIO
    if(-not$pio){
      throw "PlatformIO CLI nao encontrado. O batch tentou o PATH e tambem a pasta .platformio\penv\Scripts do usuario."
    }
    Write-Host ("PlatformIO CLI: " + $pio)

    Push-Location $root
    try{
      Run $pio @("run","-e",$envName) "Compilando firmware"
      $build=Join-Path $root ".pio\build\$envName"
      $fwBuild=Join-Path $build "firmware.bin"
      if(-not(Test-Path $fwBuild)){throw "firmware.bin nao encontrado em $build"}
      Copy-Item $fwBuild $fwOut -Force
      $hasFw=$true

      if(Test-Path (Join-Path $root "data") -PathType Container){
        Write-Host "Pasta data/ encontrada: gerando imagem de dados."
        Run $pio @("run","-e",$envName,"-t","buildfs") "Gerando filesystem"
        $fs=if($envCfg.ContainsKey("board_build.filesystem")){[string]$envCfg["board_build.filesystem"]}else{""}
        $fsImg=FindFs $build $fs
        if(-not$fsImg){throw "Imagem de filesystem nao localizada."}
        Copy-Item $fsImg $dataOut -Force
        $hasData=$true
      }else{
        Write-Host "Pasta data/ nao encontrada: sem imagem de dados."
      }

      if(YesNo "Gerar imagem FULL?" $true){
        $fullData=if($hasData){$dataOut}else{""}
        FullImage $build $fwBuild $fullData $fullOut
        $hasFull=$true
      }
    }finally{
      Pop-Location
    }
  }else{
    Title "Modo manual"
    while($true){
      $f=Ask "firmware_bin" "Caminho para firmware BIN." "" $false "build\firmware.bin"
      $d=Ask "data_bin" "Caminho para dados BIN." "" $false "build\data.bin"
      $u=Ask "full_bin" "Caminho para FULL BIN." "" $false "build\full.bin"
      if($f-or$u){break}
      Write-Host "[ATENCAO] Informe firmware_bin ou full_bin."
    }
    if($f){
      $x=ResolvePath $f $root
      if(-not(Test-Path $x)){throw "Firmware nao encontrado: $x"}
      Copy-Item $x $fwOut
      $hasFw=$true
    }
    if($d){
      $x=ResolvePath $d $root
      if(-not(Test-Path $x)){throw "Dados nao encontrados: $x"}
      Copy-Item $x $dataOut
      $hasData=$true
    }
    if($u){
      $x=ResolvePath $u $root
      if(-not(Test-Path $x)){throw "FULL nao encontrado: $x"}
      Copy-Item $x $fullOut
      $hasFull=$true
    }
  }

  if(-not$hasFw-and-not$hasFull){throw "E necessario firmware ou FULL."}

  Title "Manifesto"
  $m=[ordered]@{
    name=$name
    developer=$developer
    version=$version
    version_date=$date
    description=$desc
  }
  if($keywords.Count){$m["keywords"]=$keywords}
  Optional $m "thumbnail_url" $thumb
  Optional $m "video_url" $video
  Optional $m "website" $site
  $m["repository"]="https://github.com/$repo"

  if($hasFw){
    $m["firmware_url"]=AssetUrl $repo $fwName
    $m["firmware_sha256"]=Sha $fwOut
  }
  if($hasData){
    $m["data_url"]=AssetUrl $repo $dataName
    $m["data_sha256"]=Sha $dataOut
  }
  if($hasFull){
    $m["full_url"]=AssetUrl $repo $fullName
    $m["full_sha256"]=Sha $fullOut
  }

  $manifest=Join-Path $dist "manifest.json"
  NoBomJson $manifest $m
  Get-Content $manifest -Encoding UTF8

  Title "Resumo"
  Write-Host "Versao: $version"
  Write-Host "dist/: $dist"
  if($hasFw){Write-Host "Firmware: $fwName";Write-Host "SHA-256: $(Sha $fwOut)"}
  if($hasData){Write-Host "Dados: $dataName";Write-Host "SHA-256: $(Sha $dataOut)"}
  if($hasFull){Write-Host "Full: $fullName";Write-Host "SHA-256: $(Sha $fullOut)"}

  if(-not(YesNo "Publicar agora como GitHub Release?" $true)){
    Write-Host "Release preparada localmente."
    exit 0
  }

  Title "Git / GitHub"
  $git=Exe @("git")
  $gh=Exe @("gh")
  if(-not$git){throw "Git nao encontrado."}
  if(-not$gh){throw "GitHub CLI (gh) nao encontrado."}

  Run $gh @("auth","status") "Verificando autenticacao GitHub"

  Push-Location $root
  try{
    & $git "rev-parse" "--is-inside-work-tree" *> $null
    if($LASTEXITCODE-ne 0){throw "Nao e um repositorio Git."}

    $origin=(& $git "remote" "get-url" "origin").Trim()
    $originRepo=$origin-replace '^git@github\.com:','' -replace '^https?://github\.com/','' -replace '\.git$',''
    $originRepo=$originRepo.Trim("/")
    if($originRepo-ine$repo){
      throw "origin aponta para '$originRepo', mas repository informa '$repo'."
    }

    $dirty=@()
    foreach($line in @(& $git "status" "--porcelain" "--untracked-files=all")){
      if($line.Length-lt 4){continue}
      $pathPart=$line.Substring(3).Trim('"')-replace '\\','/'
      if($pathPart-eq"dist"-or$pathPart.StartsWith("dist/")){continue}
      $dirty+=$line
    }
    if($dirty.Count){
      Write-Host ""
      Write-Host "[ATENCAO] Existem alteracoes fora de dist/:"
      $dirty|ForEach-Object{Write-Host $_}

      Write-Host ""
      Write-Host "Para garantir que os BINs publicados correspondam exatamente"
      Write-Host "ao codigo-fonte da tag, essas alteracoes precisam estar commitadas."
      Write-Host ""
      Write-Host "1 - Adicionar as alteracoes listadas, criar o commit e continuar"
      Write-Host "2 - Nao publicar agora e manter os arquivos gerados em dist/"
      Write-Host "0 - Cancelar"
      Write-Host ""
      Write-Host "Voce tambem pode digitar CANCELAR ou usar Ctrl+C."

      while($true){
        $choice=Read-Host "Opcao"
        Cancel $choice
        $choice=$choice.Trim()

        if($choice-eq "1"){
          $defaultCommit="Release v$version"

          Write-Host ""
          Write-Host "Mensagem do commit."
          Write-Host "Pressione Enter para usar: $defaultCommit"
          Write-Host "Ou digite CANCELAR para encerrar."
          $commitMessage=Read-Host ">"
          Cancel $commitMessage
          $commitMessage=$commitMessage.Trim()
          if(-not $commitMessage){$commitMessage=$defaultCommit}

          Write-Host ""
          Write-Host "A pasta dist/ nao sera adicionada ao commit."

          # Faz o stage normal do projeto. Arquivos ignorados por .gitignore,
          # como dist/, nao sao adicionados por "git add -A -- .".
          Run $git @("add","-A","--",".") "Preparando alteracoes para o commit"

          # Protecao adicional: se dist/ tiver sido rastreada em algum momento,
          # remove qualquer alteracao de dist/ do stage sem apagar os arquivos.
          $trackedDist=@(& $git "ls-files" "--" "dist")
          if($trackedDist.Count -gt 0){
            Write-Host ""
            Write-Host "Removendo dist/ do stage para manter os artefatos fora do commit..."
            & $git "reset" "-q" "HEAD" "--" "dist"
            if($LASTEXITCODE -ne 0){
              throw "Nao foi possivel remover dist/ do stage."
            }
          }

          & $git "diff" "--cached" "--quiet"
          if($LASTEXITCODE-eq 0){
            throw "Nenhuma alteracao fora de dist/ foi preparada para commit."
          }

          Run $git @("commit","-m",$commitMessage) "Criando commit '$commitMessage'"
          break
        }

        if($choice-eq "2"){
          Write-Host ""
          Write-Host "Publicacao nao realizada."
          Write-Host "Os arquivos gerados permanecem em dist/."
          throw [OperationCanceledException]::new("Publicacao adiada pelo usuario.")
        }

        if($choice-eq "0"){
          throw [OperationCanceledException]::new("Operacao cancelada pelo usuario.")
        }

        Write-Host "Opcao invalida. Use 1, 2, 0 ou CANCELAR."
      }
    }

    Run $gh @("repo","view",$repo,"--json","nameWithOwner") "Validando repositorio GitHub"
    Run $git @("push","origin","HEAD") "Enviando commits existentes"

    $tag="v$version"
    & $git "fetch" "--tags" "origin" *> $null
    if($LASTEXITCODE-ne 0){throw "Falha ao atualizar tags."}

    $head=(& $git "rev-parse" "HEAD").Trim()
    & $git "rev-parse" "-q" "--verify" "refs/tags/$tag" *> $null
    if($LASTEXITCODE-eq 0){
      $tagCommit=(& $git "rev-list" "-n" "1" $tag).Trim()
      if($tagCommit-ne$head){throw "Tag $tag ja existe em outro commit."}
    }else{
      Run $git @("tag","-a",$tag,"-m","Release $tag") "Criando tag $tag"
    }

    Run $git @("push","origin","refs/tags/$tag") "Enviando tag $tag"

    & $gh "release" "view" $tag "--repo" $repo *> $null
    if($LASTEXITCODE-eq 0){
      throw "GitHub Release $tag ja existe. Altere a versao."
    }

    $assets=@($manifest)
    if($hasFw){$assets+=$fwOut}
    if($hasData){$assets+=$dataOut}
    if($hasFull){$assets+=$fullOut}

    $args=@("release","create",$tag)+$assets+@(
      "--repo",$repo,
      "--title",$tag,
      "--generate-notes",
      "--latest",
      "--verify-tag"
    )
    Run $gh $args "Criando GitHub Release $tag"

    Write-Host ""
    Write-Host "Release publicada:"
    Write-Host "https://github.com/$repo/releases/tag/$tag"
  }finally{
    Pop-Location
  }

  Title "Concluido"
  Write-Host "Release preparada e publicada com sucesso."
  exit 0
}
catch [OperationCanceledException]{
  Write-Host ""
  Write-Host "Operacao cancelada."
  exit 2
}
catch{
  Write-Host ""
  Write-Host "ERRO: $($_.Exception.Message)"
  if($_.InvocationInfo -and $_.InvocationInfo.PositionMessage){
    Write-Host ""
    Write-Host "Detalhes:"
    Write-Host $_.InvocationInfo.PositionMessage
  }
  Write-Host "Arquivos existentes em dist/ foram preservados para diagnostico."
  exit 1
}
