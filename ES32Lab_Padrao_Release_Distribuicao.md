# Padrão de Release e Distribuição de Aplicações ES32Lab

> **Documento oficial de referência** para organização, geração, empacotamento e publicação de aplicações ES32Lab usando PlatformIO, `preparar_release.bat`, pasta `dist/`, `manifest.json` e GitHub Releases.

---

## Índice

1. [Objetivo](#1-objetivo)
2. [Visão geral da arquitetura](#2-visão-geral-da-arquitetura)
3. [Estrutura recomendada do projeto](#3-estrutura-recomendada-do-projeto)
4. [Responsabilidade de cada componente](#4-responsabilidade-de-cada-componente)
5. [Seção `[release]` do `platformio.ini`](#5-seção-release-do-platformioini)
6. [Configuração do ambiente PlatformIO](#6-configuração-do-ambiente-platformio)
7. [Versionamento do firmware](#7-versionamento-do-firmware)
8. [Nome-base dos arquivos binários](#8-nome-base-dos-arquivos-binários)
9. [Artefatos binários suportados](#9-artefatos-binários-suportados)
10. [Geração da imagem de dados](#10-geração-da-imagem-de-dados)
11. [Geração da imagem Full](#11-geração-da-imagem-full)
12. [Pasta `dist/`](#12-pasta-dist)
13. [Manifesto da aplicação](#13-manifesto-da-aplicação)
14. [Campos obrigatórios e opcionais](#14-campos-obrigatórios-e-opcionais)
15. [Palavras-chave (`keywords`)](#15-palavras-chave-keywords)
16. [SHA-256](#16-sha-256)
17. [Campos que não fazem parte do manifesto](#17-campos-que-não-fazem-parte-do-manifesto)
18. [Fluxo completo do `preparar_release.bat`](#18-fluxo-completo-do-preparar_releasebat)
19. [Modo interativo e campos ausentes](#19-modo-interativo-e-campos-ausentes)
20. [Integração com Git](#20-integração-com-git)
21. [Integração com GitHub Releases](#21-integração-com-github-releases)
22. [Compatibilidade com projetos sem PlatformIO](#22-compatibilidade-com-projetos-sem-platformio)
23. [Regras para instaladores e atualizadores](#23-regras-para-instaladores-e-atualizadores)
24. [Exemplo completo](#24-exemplo-completo)
25. [Resumo das regras oficiais](#25-resumo-das-regras-oficiais)

---

# 1. Objetivo

Este padrão foi criado para facilitar a publicação de aplicações ES32Lab de forma reproduzível, organizada e simples para desenvolvedores iniciantes ou avançados.

O objetivo principal é evitar que o desenvolvedor tenha que preparar manualmente:

- nomes de arquivos binários;
- URLs de download;
- hashes SHA-256;
- `manifest.json`;
- imagens de dados;
- imagem Full;
- tags Git;
- GitHub Releases;
- associação entre versão publicada e código-fonte.

A maior parte dessas tarefas deve ser automatizada pelo arquivo:

```text
preparar_release.bat
```

O desenvolvedor concentra as informações principais no `platformio.ini`, e o processo de Release cuida do restante.

---

# 2. Visão geral da arquitetura

O fluxo básico é:

```text
platformio.ini
      │
      ▼
preparar_release.bat
      │
      ├── compila o firmware
      ├── gera imagem de dados, quando aplicável
      ├── gera imagem Full, quando solicitado
      ├── calcula SHA-256
      ├── gera manifest.json
      ├── organiza dist/
      ├── verifica Git
      ├── cria commit, quando autorizado
      ├── cria tag
      └── cria GitHub Release
      │
      ▼
dist/
      │
      ├── aplicativo.bin
      ├── aplicativo-data.bin
      ├── aplicativo-full.bin
      └── manifest.json
```

O `platformio.ini` é a principal fonte de metadados do projeto.

O `preparar_release.bat` é o orquestrador.

A pasta `dist/` contém os arquivos finais de distribuição.

O `manifest.json` descreve publicamente a aplicação e os artefatos disponíveis.

O GitHub Release hospeda os arquivos publicados.

---

# 3. Estrutura recomendada do projeto

Exemplo genérico:

```text
MeuProjeto/
├── src/
│   └── main.cpp
├── include/
├── lib/
├── data/
├── tools/
│   └── preparar_release.bat
├── dist/
├── platformio.ini
├── partitions.csv
├── README.md
└── .gitignore
```

Nem todos os projetos precisam conter todos esses elementos.

A pasta `data/`, por exemplo, só é necessária quando a aplicação possui arquivos que deverão compor uma imagem de filesystem.

A tabela de partições personalizada também é opcional.

---

# 4. Responsabilidade de cada componente

## `platformio.ini`

Contém:

- metadados da aplicação;
- versão;
- data da versão;
- nome-base dos BINs;
- repositório;
- configuração do ambiente;
- filesystem;
- particionamento;
- macros de compilação.

## `preparar_release.bat`

Responsável por:

- ler o `platformio.ini`;
- solicitar informações ausentes;
- compilar;
- gerar artefatos;
- calcular SHA-256;
- gerar o manifesto;
- administrar `dist/`;
- validar o estado Git;
- publicar uma GitHub Release.

## `dist/`

É a pasta de distribuição.

Ela contém apenas os arquivos finais que poderão ser publicados.

## `manifest.json`

É o documento público que informa:

- qual é a aplicação;
- quem a desenvolveu;
- qual é a versão;
- quais artefatos estão disponíveis;
- onde esses artefatos podem ser obtidos.

---

# 5. Seção `[release]` do `platformio.ini`

A seção `[release]` contém as informações usadas para preparar e publicar uma versão.

Exemplo:

```ini
[release]
name = Minha Aplicação ES32Lab
developer = Minha Empresa
version = 1.2.3
version_date = 2026-10-09
description = Descrição resumida da aplicação.
thumbnail_url = https://exemplo.com/imagem.jpg
video_url = https://exemplo.com/video
keywords = ES32Lab, ESP32, Wi-Fi, Automação
bin_name = minha_aplicacao
repository = Organizacao/Repositorio
website = https://exemplo.com/
```

A seção se chama `[release]` porque ela representa a configuração de publicação da versão.

O arquivo gerado recebe o nome `manifest.json`.

Esses dois conceitos não devem ser confundidos:

```text
[release]
→ configuração de origem

manifest.json
→ documento público gerado
```

---

# 6. Configuração do ambiente PlatformIO

As informações específicas do microcontrolador permanecem dentro do ambiente PlatformIO.

Exemplo:

```ini
[env:nodemcu-32s]
platform = espressif32
board = nodemcu-32s
framework = arduino
monitor_speed = 115200

board_build.filesystem = littlefs
board_build.partitions = partitions.csv

build_flags =
    -D APP_VERSION=\"${release.version}\"
    -D APP_VERSION_DATE=\"${release.version_date}\"
```

Informações como filesystem e tabela de partições **não pertencem ao `[release]`**.

Elas são características técnicas do ambiente de compilação.

---

# 7. Versionamento do firmware

A versão deve possuir uma única fonte principal:

```ini
version = 1.2.3
```

A data também:

```ini
version_date = 2026-10-09
```

Formato recomendado da versão:

```text
X.Y.Z
```

Formato obrigatório da data:

```text
AAAA-MM-DD
```

O código-fonte pode receber esses valores por `build_flags`:

```ini
build_flags =
    -D APP_VERSION=\"${release.version}\"
    -D APP_VERSION_DATE=\"${release.version_date}\"
```

No código:

```cpp
#ifndef APP_VERSION
#define APP_VERSION "0.0.0"
#endif

#ifndef APP_VERSION_DATE
#define APP_VERSION_DATE "0000-00-00"
#endif
```

Dessa forma, a mesma versão é utilizada:

```text
platformio.ini
      │
      ├── firmware
      ├── manifest.json
      ├── tag Git
      └── GitHub Release
```

---

# 8. Nome-base dos arquivos binários

O parâmetro utilizado é:

```ini
bin_name = minha_aplicacao
```

Esse valor representa o **nome-base** dos arquivos.

O recomendado é usar:

- letras;
- números;
- `_`;
- `-`.

Evite:

- espaços;
- acentos;
- caracteres especiais.

Exemplo:

```ini
bin_name = sistema_automacao
```

O desenvolvedor pode informar acidentalmente:

```ini
bin_name = sistema_automacao.bin
```

Nesse caso, o batch deve remover automaticamente a extensão `.bin` e continuar com:

```text
sistema_automacao
```

---

# 9. Artefatos binários suportados

O padrão suporta até três imagens.

## Firmware

Contém somente a aplicação.

```text
<bin_name>.bin
```

Exemplo:

```text
sistema_automacao.bin
```

## Dados

Contém a imagem do filesystem do projeto.

```text
<bin_name>-data.bin
```

Exemplo:

```text
sistema_automacao-data.bin
```

## Full

Imagem completa preparada para instalação integral da flash.

```text
<bin_name>-full.bin
```

Exemplo:

```text
sistema_automacao-full.bin
```

Não é obrigatório publicar os três artefatos.

São combinações válidas:

```text
Firmware
Full
Firmware + Dados
Firmware + Full
Firmware + Dados + Full
```

Para uma aplicação normalmente instalável, deve existir pelo menos:

```text
Firmware
ou
Full
```

Uma imagem de dados isolada não representa uma aplicação completa.

---

# 10. Geração da imagem de dados

A existência da pasta:

```text
data/
```

indica que o projeto possui conteúdo destinado ao filesystem.

Exemplo:

```text
data/
├── index.html
├── app.js
├── style.css
└── imagens/
```

Ao detectar `data/`, o batch solicita ao PlatformIO a geração da imagem de filesystem.

Conceitualmente:

```text
data/
   │
   ▼
PlatformIO buildfs
   │
   ▼
littlefs.bin / spiffs.bin / outra imagem
   │
   ▼
dist/<bin_name>-data.bin
```

O tipo de filesystem vem da configuração do ambiente:

```ini
board_build.filesystem = littlefs
```

O manifesto não precisa informar se a imagem é LittleFS, SPIFFS ou outro formato.

---

# 11. Geração da imagem Full

A imagem Full reúne os elementos necessários para uma instalação completa.

Dependendo do projeto, ela poderá combinar:

```text
bootloader
+ tabela de partições
+ estrutura OTA necessária
+ firmware
+ dados, quando existirem
```

O batch deve utilizar as informações geradas pelo próprio ambiente de compilação.

Os offsets não devem ficar codificados manualmente no manifesto.

A tabela de partições do projeto é utilizada para identificar onde os componentes pertencem.

A geração da Full pode ser feita com a ferramenta de merge do ecossistema ESP32.

Resultado:

```text
dist/<bin_name>-full.bin
```

A imagem Full é especialmente adequada para:

- primeira instalação;
- recuperação;
- reinstalação completa;
- Web Installer.

---

# 12. Pasta `dist/`

`dist` significa **distribution**.

Esse é o nome oficial recomendado para a pasta de artefatos finais.

Exemplo:

```text
dist/
├── minha_aplicacao.bin
├── minha_aplicacao-data.bin
├── minha_aplicacao-full.bin
└── manifest.json
```

## Regras

A pasta `dist/` é totalmente gerenciada pelo batch.

O batch pode:

- verificar se ela existe;
- criá-la;
- limpar artefatos antigos;
- substituir arquivos;
- renomear arquivos;
- gerar o manifesto.

Nenhum arquivo que precise ser preservado manualmente deve ser armazenado em `dist/`.

Recomenda-se incluir:

```gitignore
dist/
```

no `.gitignore`.

Os arquivos de `dist/` serão publicados como assets da GitHub Release, e não como arquivos normais do repositório.

---

# 13. Manifesto da aplicação

O arquivo público recebe o nome:

```text
manifest.json
```

O termo **manifesto** é utilizado porque o arquivo descreve a aplicação e os artefatos distribuídos.

Exemplo:

```json
{
  "name": "Minha Aplicação ES32Lab",
  "developer": "Minha Empresa",
  "version": "1.2.3",
  "version_date": "2026-10-09",

  "description": "Descrição resumida da aplicação.",

  "keywords": [
    "ES32Lab",
    "ESP32",
    "Wi-Fi",
    "Automação"
  ],

  "thumbnail_url": "https://exemplo.com/imagem.jpg",
  "video_url": "https://exemplo.com/video",
  "website": "https://exemplo.com/",
  "repository": "https://github.com/Organizacao/Repositorio",

  "firmware_url": "https://github.com/Organizacao/Repositorio/releases/latest/download/minha_aplicacao.bin",
  "firmware_sha256": "HASH_SHA256",

  "data_url": "https://github.com/Organizacao/Repositorio/releases/latest/download/minha_aplicacao-data.bin",
  "data_sha256": "HASH_SHA256",

  "full_url": "https://github.com/Organizacao/Repositorio/releases/latest/download/minha_aplicacao-full.bin",
  "full_sha256": "HASH_SHA256"
}
```

Somente os artefatos realmente existentes devem aparecer.

---

# 14. Campos obrigatórios e opcionais

## Obrigatórios para o processo padrão de Release

| Campo | Função |
|---|---|
| `name` | Nome público da aplicação |
| `developer` | Desenvolvedor, empresa ou organização |
| `version` | Versão publicada |
| `version_date` | Data da versão |
| `description` | Descrição pública |
| `bin_name` | Nome-base dos arquivos binários |
| `repository` | Repositório de publicação |

## Opcionais

| Campo | Função |
|---|---|
| `thumbnail_url` | Imagem de apresentação |
| `video_url` | Vídeo relacionado |
| `keywords` | Busca e classificação |
| `website` | Site oficial |

A ausência de thumbnail pode ser tratada pela loja com uma imagem genérica.

---

# 15. Palavras-chave (`keywords`)

No `platformio.ini`, as palavras-chave são escritas de maneira simples:

```ini
keywords = ES32Lab, ESP32, Wi-Fi, OTA
```

O batch deve convertê-las para uma matriz JSON:

```json
"keywords": [
  "ES32Lab",
  "ESP32",
  "Wi-Fi",
  "OTA"
]
```

Esse é o formato oficial gerado automaticamente.

Entretanto, um manifesto criado manualmente também poderá utilizar:

```json
"keywords": "ES32Lab, ESP32, Wi-Fi, OTA"
```

Ambas as formas são válidas para consumidores do manifesto.

Portanto:

```text
Batch
→ sempre gera matriz

Manifesto manual
→ matriz ou string separada por vírgulas

Instalador / Loja
→ deve compreender as duas formas
```

---

# 16. SHA-256

O campo SHA-256 é opcional no padrão geral de manifesto.

Exemplo:

```json
"firmware_url": "https://exemplo.com/app.bin",
"firmware_sha256": "..."
```

Também é válido:

```json
"firmware_url": "https://exemplo.com/app.bin"
```

## Quando o manifesto é gerado pelo batch

O batch deve calcular automaticamente SHA-256 para todos os artefatos que publicar.

Exemplo:

```text
firmware_sha256
data_sha256
full_sha256
```

O desenvolvedor não precisa calcular nem digitar esses valores.

## Quando o manifesto é criado manualmente

O SHA-256 pode ser omitido.

Se estiver presente, o instalador deve validá-lo.

Se estiver ausente, o instalador poderá continuar a instalação, mas deve informar que a integridade não poderá ser confirmada por meio de um hash previamente conhecido.

---

# 17. Campos que não fazem parte do manifesto

O padrão atual deliberadamente não utiliza:

```text
size
schema
offset
filesystem
partition
partition_label
partitions_url
```

## `size`

O tamanho do arquivo não é mantido no manifesto.

O consumidor deve descobrir o tamanho por HTTP ou durante a transferência.

Isso evita informações desatualizadas quando um BIN é substituído.

## `offset`

A lógica de gravação pertence ao instalador.

## Filesystem

Informações como:

```text
LittleFS
SPIFFS
FAT
```

não precisam estar no manifesto para simplesmente gravar uma imagem já pronta.

## Tabela de partições

A tabela de partições pertence ao processo de build e geração dos artefatos.

Ela não é parte do manifesto público atual.

---

# 18. Fluxo completo do `preparar_release.bat`

Fluxo recomendado:

```text
INÍCIO
  │
  ▼
Localizar o projeto
  │
  ▼
Existe platformio.ini?
  │
  ├── SIM → ler configurações
  │
  └── NÃO → modo assistido/manual
  │
  ▼
Validar campos
  │
  ▼
Perguntar campos ausentes
  │
  ▼
Normalizar bin_name
  │
  ▼
Criar ou limpar dist/
  │
  ▼
Compilar firmware
  │
  ▼
Existe data/?
  │
  ├── SIM → gerar imagem de dados
  │
  └── NÃO → seguir
  │
  ▼
Gerar Full?
  │
  ├── SIM → montar imagem Full
  │
  └── NÃO → seguir
  │
  ▼
Calcular SHA-256
  │
  ▼
Gerar manifest.json
  │
  ▼
Mostrar resumo
  │
  ▼
Publicar no GitHub?
  │
  ├── NÃO → encerrar mantendo dist/
  │
  └── SIM
        │
        ▼
      Verificar Git
        │
        ▼
      Commit se necessário
        │
        ▼
      Push
        │
        ▼
      Criar tag vX.Y.Z
        │
        ▼
      Publicar tag
        │
        ▼
      Criar GitHub Release
        │
        ▼
      Anexar manifest + BINs
```

---

# 19. Modo interativo e campos ausentes

O batch deve ser autoexplicativo.

Se um campo obrigatório não estiver preenchido:

```text
Campo: version
Versão da aplicação.
Exemplo: 1.2.3

Obrigatório. Digite CANCELAR para encerrar.
>
```

Se o usuário apenas pressionar Enter, a pergunta deve ser repetida.

Exemplo:

```text
[ATENCAO] O campo 'version' e obrigatorio.
```

Campos opcionais podem ser pulados com Enter.

Exemplo:

```text
Campo: video_url
URL de vídeo relacionado ao projeto.

Opcional. Pressione Enter para ignorar.
>
```

## Cancelamento

Devem ser aceitas duas formas:

```text
CANCELAR
```

ou:

```text
Ctrl+C
```

O usuário deve sempre possuir uma forma clara de abandonar o processo.

---

# 20. Integração com Git

Antes da publicação, o batch verifica o estado do repositório.

O objetivo é impedir esta situação:

```text
main.cpp local modificado
      │
      ▼
BIN compilado
      │
      ▼
tag aponta para código antigo
```

O BIN publicado deve corresponder ao código-fonte associado à tag.

Se existirem alterações fora de `dist/`, o batch pode oferecer:

```text
1 - Criar commit e continuar
2 - Não publicar agora
0 - Cancelar
```

Ao criar o commit, uma mensagem padrão pode ser:

```text
Release v1.2.3
```

A pasta `dist/` não deve entrar no commit.

---

# 21. Integração com GitHub Releases

Após o código estar sincronizado:

```text
git push
```

o batch cria uma tag:

```text
v1.2.3
```

A tag deve apontar exatamente para o commit que gerou os BINs.

Depois a tag é enviada ao GitHub.

Em seguida, o batch cria uma GitHub Release baseada nessa tag.

Os assets são obtidos de `dist/`.

Exemplo:

```text
manifest.json
minha_aplicacao.bin
minha_aplicacao-data.bin
minha_aplicacao-full.bin
```

Somente os arquivos existentes são enviados.

A Release mais recente poderá ser marcada como `Latest`.

As URLs do manifesto podem utilizar:

```text
/releases/latest/download/
```

o que permite aos instaladores encontrar automaticamente os artefatos da versão mais recente.

---

# 22. Compatibilidade com projetos sem PlatformIO

O padrão prioriza PlatformIO, mas o batch pode oferecer modo assistido.

Se não existir:

```text
platformio.ini
```

o batch poderá perguntar:

- nome;
- desenvolvedor;
- versão;
- data;
- descrição;
- nome-base;
- repositório;
- arquivos BIN já compilados.

Nesse modo, o desenvolvedor poderá fornecer manualmente:

```text
firmware.bin
data.bin
full.bin
```

O batch poderá então:

- copiá-los para `dist/`;
- normalizar os nomes;
- calcular SHA-256;
- gerar o manifesto;
- publicar a Release.

Essa possibilidade facilita a participação de usuários que trabalham principalmente com Arduino IDE.

---

# 23. Regras para instaladores e atualizadores

O manifesto foi propositalmente mantido simples.

Um consumidor deve considerar que:

## Firmware

```text
firmware_url
```

é utilizado para atualização normal da aplicação.

## Dados

```text
data_url
```

representa a imagem de dados, quando existir.

## Full

```text
full_url
```

representa uma imagem completa para instalação ou recuperação.

## SHA-256

Campos possíveis:

```text
firmware_sha256
data_sha256
full_sha256
```

Se o hash existir:

```text
calcular hash do arquivo
        │
        ▼
comparar com o manifesto
        │
        ├── igual → continuar
        └── diferente → interromper
```

Se não existir, o consumidor poderá continuar com aviso.

## Keywords

O consumidor deve aceitar:

```json
"keywords": ["ES32Lab", "ESP32"]
```

e:

```json
"keywords": "ES32Lab, ESP32"
```

---

# 24. Exemplo completo

## `platformio.ini`

```ini
[release]
name = Minha Aplicação ES32Lab
developer = Minha Empresa
version = 1.2.3
version_date = 2026-10-09
description = Aplicação de exemplo para ES32Lab.
thumbnail_url = https://exemplo.com/imagem.jpg
video_url = https://exemplo.com/video
keywords = ES32Lab, ESP32, Wi-Fi, Automação
bin_name = minha_aplicacao
repository = MinhaEmpresa/MinhaAplicacao
website = https://exemplo.com/

[env:nodemcu-32s]
platform = espressif32
board = nodemcu-32s
framework = arduino

monitor_speed = 115200

board_build.filesystem = littlefs
board_build.partitions = partitions.csv

build_flags =
    -D APP_VERSION=\"${release.version}\"
    -D APP_VERSION_DATE=\"${release.version_date}\"
```

## Estrutura antes do Release

```text
MinhaAplicacao/
├── src/
│   └── main.cpp
├── data/
│   ├── index.html
│   └── style.css
├── tools/
│   └── preparar_release.bat
├── platformio.ini
├── partitions.csv
└── README.md
```

## Estrutura produzida

```text
dist/
├── minha_aplicacao.bin
├── minha_aplicacao-data.bin
├── minha_aplicacao-full.bin
└── manifest.json
```

## `manifest.json`

```json
{
  "name": "Minha Aplicação ES32Lab",
  "developer": "Minha Empresa",
  "version": "1.2.3",
  "version_date": "2026-10-09",
  "description": "Aplicação de exemplo para ES32Lab.",

  "keywords": [
    "ES32Lab",
    "ESP32",
    "Wi-Fi",
    "Automação"
  ],

  "thumbnail_url": "https://exemplo.com/imagem.jpg",
  "video_url": "https://exemplo.com/video",
  "website": "https://exemplo.com/",
  "repository": "https://github.com/MinhaEmpresa/MinhaAplicacao",

  "firmware_url": "https://github.com/MinhaEmpresa/MinhaAplicacao/releases/latest/download/minha_aplicacao.bin",
  "firmware_sha256": "HASH_CALCULADO_AUTOMATICAMENTE",

  "data_url": "https://github.com/MinhaEmpresa/MinhaAplicacao/releases/latest/download/minha_aplicacao-data.bin",
  "data_sha256": "HASH_CALCULADO_AUTOMATICAMENTE",

  "full_url": "https://github.com/MinhaEmpresa/MinhaAplicacao/releases/latest/download/minha_aplicacao-full.bin",
  "full_sha256": "HASH_CALCULADO_AUTOMATICAMENTE"
}
```

---

# 25. Resumo das regras oficiais

```text
[release]
→ metadados e informações de publicação

[env:...]
→ hardware, compilação, filesystem e particionamento

bin_name
→ nome-base sem .bin
→ se .bin for informado, o batch remove

data/
→ se existir, o batch gera imagem de dados

dist/
→ criada automaticamente se necessário
→ totalmente gerenciada pelo batch
→ não deve conter arquivos manuais importantes

firmware
→ <bin_name>.bin

dados
→ <bin_name>-data.bin

full
→ <bin_name>-full.bin

manifest.json
→ gerado automaticamente
→ UTF-8 sem BOM
→ sem size
→ sem schema
→ sem offset
→ sem filesystem
→ sem tabela de partições

keywords
→ batch gera matriz JSON
→ consumidores aceitam matriz ou string

SHA-256
→ opcional no padrão de manifesto
→ sempre gerado pelo batch
→ se presente, deve ser validado pelo consumidor

Git
→ garante que BIN e código-fonte correspondam

GitHub Release
→ hospeda manifest.json e os BINs produzidos
```

---

## Princípio geral

O desenvolvedor deve informar apenas aquilo que realmente conhece e precisa manter.

Informações derivadas devem ser produzidas automaticamente.

```text
Desenvolvedor
      │
      ▼
configura o projeto
      │
      ▼
automação prepara a distribuição
      │
      ▼
manifesto descreve a distribuição
      │
      ▼
instaladores e loja consomem o manifesto
```

Esse modelo reduz erros manuais, facilita contribuições de terceiros e cria uma estrutura previsível para distribuição de aplicações ES32Lab.
