# ES_WifiScanService — Instruções para IA

Este documento orienta assistentes a integrar o serviço de manutenção **ES_WifiScanService** em projetos para a **ES32Lab**. Para instruções de instalação e o exemplo completo, consulte o [Guia de uso](./ES_WifiScanService-README.md). O arquivo `ES_WifiScanService.h` é a referência para APIs e comportamentos; **não invente métodos nem comandos**.

## Objetivo e funcionamento

O serviço abre uma janela de recepção serial no início do `setup()` para reconhecer **apenas comandos próprios**.

- `wifiScanner.begin(Serial)` abre a janela padrão de **500 ms**.
- `wifiScanner.begin(Serial, 1000)` define **1000 ms**; `begin(Serial, 0)` pula a janela.
- Sem comando reconhecido, `begin()` retorna e não há monitoramento serial posterior pelo módulo.
- Com comando reconhecido, `begin()` mantém o `setup()` aguardando em **modo de manutenção**, sem timeout de inatividade.
- `exit` libera a continuação do `setup()`; `reset` reinicia o ESP32.

Não adicionar callbacks, tarefas próprias ou chamadas no `loop()`. Manter o scanner opcional e discreto. O bloqueio em `begin()` não suspende as tarefas internas do ESP32.

## Como gerar código de integração

Sempre usar a **LIB ES32Lab** e estes includes:

```cpp
#include <Arduino.h>
#include <ES32Lab.h>
#include "ES_WifiScanService.h"
```

Criar `ES_WifiScanService wifiScanner;` e chamar `Serial.begin(115200)` seguido de `wifiScanner.begin(Serial)` o mais cedo possível no `setup()`.

**Instalação:** o módulo é um arquivo `.h` único, sem ArduinoJson. Na Arduino IDE, deixá-lo na pasta do sketch; usar a placa **ES Developer ES32Lab** com o pacote ESP32 3.3.11 ou superior. No PlatformIO, usar `board = nodemcu-32s` com as bibliotecas oficiais ES32Lab.

**API disponível:** `void begin(HardwareSerial&, uint32_t windowMs = 500)`, `bool scan(Print&)` (busca síncrona para uso direto, retorna JSON `{"networks":[...]}` sem `cmd/status/data`) e `void end()` (sem ação adicional necessária nesta implementação). Não supor a existência de `update()`, `poll()`, conexão Wi-Fi, credenciais, comandos extras ou registro de handlers externos.

## Protocolo JSON serial

As mensagens de entrada são **objetos JSON completos, em uma única linha com LF (`\n`)**, até **255 bytes por linha**. Velocidade recomendada: **115200 baud**, conforme o `Serial.begin()`.

| Comando exato | Comportamento |
|---|---|
| `{"cmd":"hello"}` | Responde `done` com `device`, `protocol_version:1`, `firmware_version` e `capabilities`. |
| `{"cmd":"wifi_scan"}` | Responde `done` com `data.networks`, contendo `ssid`, `rssi` e `secure`. |
| `{"cmd":"exit"}` | Responde `done` com `data:{}` e libera a aplicação. |
| `{"cmd":"reset"}` | Responde `done` com `data:{}` e executa `ESP.restart()`. |

**`hello` não é obrigatório** para ativar a manutenção; `wifi_scan`, `exit` e `reset` também são reconhecidos na janela inicial. Os comandos são uma **lista fechada**; outros comandos ou dados inválidos são ignorados.

O resultado pode apresentar `status:"error"` com `data.code` e `data.message`, além de `data.truncated:true` em buscas truncadas. A lista tem limite de **128 redes**; não contém senhas.

## Orientações para desenvolver um cliente serial

1. Abrir a serial e enviar um comando reconhecido, finalizado em LF, durante a janela inicial. Se necessário, reiniciar a placa e sincronizar o envio; o reset automático ao conectar depende do hardware.
2. Ler **bytes em fluxo**, não presumindo uma resposta inteira por leitura. Acumular e separar por LF com decodificação UTF-8 apropriada.
3. Ignorar logs do ESP32, linhas vazias e textos não JSON. Validar `cmd`, `status` e `data`; em `hello`, verificar `protocol_version == 1` e `wifi_scan` em `capabilities`.
4. Aguardar a resposta de cada comando antes de enviar o próximo. Tratar falhas e truncamento; encerrar com `exit` ou `reset`.

**Cuidados:** durante a janela inicial, o serviço consome até mesmo mensagens não reconhecidas; depois do retorno de `begin()`, a aplicação pode usar `Serial.read()` normalmente. O módulo não executa escaneamentos periódicos; o rádio Wi-Fi só é usado na busca solicitada. Não criar timeout de manutenção nem exigir ajustes no `loop()`.

**Para exemplos de código completos**, reproduza a integração mínima mostrada no [Guia de uso](./ES_WifiScanService-README.md), com o LED verde `P_LED_GREEN` alternando a cada 500 ms.
