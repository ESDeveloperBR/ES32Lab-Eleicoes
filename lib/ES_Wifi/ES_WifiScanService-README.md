# ES_WifiScanService — Guia de uso

O **ES_WifiScanService** é um serviço de manutenção para a **ES32Lab**. Ele permite identificar a placa, listar redes Wi-Fi próximas, reiniciar o dispositivo ou continuar a execução do programa, usando comandos JSON pela porta serial.

O serviço só verifica comandos durante uma breve **janela na inicialização**. Sem comando reconhecido, a aplicação continua normalmente. Com um comando reconhecido, entra em **modo de manutenção** até receber uma ordem para sair ou reiniciar.

Para orientar uma IA a integrar o módulo, consulte [ES_WifiScanService-AI_TRAINING.md](./ES_WifiScanService-AI_TRAINING.md).

## 1. Instalação

**Itens necessários:** ES32Lab, cabo USB, LIB ES32Lab, arquivo `ES_WifiScanService.h`, Arduino IDE ou PlatformIO e um terminal serial para os testes.

O serviço é distribuído em um **único arquivo `.h`** e não exige ArduinoJson.

**Arduino IDE:** copie `ES_WifiScanService.h` para a pasta do sketch. Instale a LIB ES32Lab. Com o pacote `esp32 por Espressif Systems` **3.3.11 ou superior**, selecione **ES Developer ES32Lab**.

**PlatformIO:** copie o `.h` para `include/` ou `src/`. Utilize `board = nodemcu-32s` e as bibliotecas `esdeveloper/ES32Lab` e `esdeveloper/TFT_eSPI_ES32Lab`.

## 2. Ativação no firmware

Depois de `Serial.begin(115200)`, chame `wifiScanner.begin(Serial)` **no início do `setup()`**:

```cpp
ES_WifiScanService wifiScanner;

// Dentro de setup(), depois de Serial.begin(115200):
wifiScanner.begin(Serial);        // Janela padrão: 500 ms.
// wifiScanner.begin(Serial, 1000); // Janela personalizada: 1000 ms.
// wifiScanner.begin(Serial, 0);    // Sem janela de manutenção.
```

**Funcionamento:**

- **Nenhum comando reconhecido:** após a janela, o `setup()` continua; o serviço não monitora mais a serial.
- **Comando reconhecido:** o `setup()` permanece dentro de `begin()`, atendendo exclusivamente aos comandos de manutenção. O `loop()` só começará após `exit`.
- **Sem timeout de inatividade:** o modo de manutenção permanece ativo até `exit`, `reset` ou reinicialização física.

Não é necessário colocar chamadas do serviço no `loop()`. Após a janela normal, o firmware pode utilizar `Serial.read()` e `Serial.available()`.

## 3. Comandos seriais

Abra um terminal em **115200 baud** (ou na velocidade configurada no `Serial.begin()`). Envie cada comando como **uma linha JSON terminada em LF (`\n`)**.

| Comando | Função |
|---|---|
| `{"cmd":"hello"}` | Identifica o serviço e mantém a manutenção ativa. |
| `{"cmd":"wifi_scan"}` | Busca redes Wi-Fi e retorna a lista. |
| `{"cmd":"exit"}` | Encerra a manutenção e continua o `setup()`. |
| `{"cmd":"reset"}` | Reinicia a ES32Lab. |

**Não é obrigatório enviar `hello` primeiro:** qualquer comando reconhecido recebido durante a janela inicial ativa o modo de manutenção. Comandos desconhecidos são ignorados.

**Exemplo de resposta a `hello`:**

```json
{"cmd":"hello","status":"done","data":{"device":"es32lab-app","protocol_version":1,"firmware_version":"unspecified","capabilities":["wifi_scan","reset","exit"]}}
```

**Exemplo de resposta a `wifi_scan`** (redes ilustrativas):

```json
{"cmd":"wifi_scan","status":"done","data":{"networks":[{"ssid":"MinhaRede","rssi":-51,"secure":true},{"ssid":"RedeAberta","rssi":-75,"secure":false}]}}
```

`ssid` é o nome da rede; `rssi` é o sinal em dBm; `secure` indica se a rede utiliza proteção. Falhas podem retornar `"status":"error"` com detalhes em `data`. Os comandos `exit` e `reset` respondem `{"cmd":"exit","status":"done","data":{}}` e `{"cmd":"reset","status":"done","data":{}}`, respectivamente.

## 4. Como testar

1. Grave o programa na ES32Lab e abra o terminal serial.
2. Reinicie a placa, se necessário, e envie `{"cmd":"hello"}` ou `{"cmd":"wifi_scan"}` **dentro da janela inicial**, sempre com LF.
3. Com a manutenção ativa, execute `wifi_scan` quantas vezes quiser, aguardando cada resposta.
4. Envie `{"cmd":"exit"}` para iniciar o restante da aplicação ou `{"cmd":"reset"}` para reiniciar.

No Terminal Serial Web ES32Lab, a opção **enviar automaticamente ao conectar** permite configurar o comando e seu atraso. Abrir a porta pode reiniciar algumas placas, mas isso depende do hardware.

**Observações:** a janela de inicialização consome os dados recebidos, mesmo os não reconhecidos; portanto, evite transmitir comandos da aplicação principal nesse período. Respostas são linhas JSON com LF, podendo haver logs de boot e uma linha vazia antes do JSON. O limite de entrada é de **255 bytes por linha**; a lista pode conter até **128 redes** e sinalizar `data.truncated:true`. O escaneamento usa o rádio Wi-Fi do ESP32 e não é executado automaticamente.

## 5. Exemplo completo — LED verde

O LED alterna a cada 500 ms durante a execução normal e envia `LED ON` / `LED OFF` pela serial. Se a manutenção for ativada, o `loop()` fica aguardando a liberação por `exit`.

```cpp
#include <Arduino.h>
#include <ES32Lab.h>
#include "ES_WifiScanService.h"

ES_WifiScanService wifiScanner;

void setup() {
    Serial.begin(115200);
    wifiScanner.begin(Serial);

    pinMode(P_LED_GREEN, OUTPUT);
}

void loop() {
    digitalWrite(P_LED_GREEN, HIGH);
    Serial.println("LED ON");
    delay(500);

    digitalWrite(P_LED_GREEN, LOW);
    Serial.println("LED OFF");
    delay(500);
}
```
