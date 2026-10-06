/*
  ES32Lab Eleicoes - Instalador temporario via Arduino IDE
  Versao do instalador: 0.2.0

  Fluxo:
    1. O usuario informa SSID e senha abaixo.
    2. Faz Upload deste sketch pela Arduino IDE via USB.
    3. O instalador conecta ao Wi-Fi.
    4. Salva a rede na NVS utilizada pelo firmware final.
    5. Baixa ES32Lab-Eleicoes.bin da Release mais recente no GitHub.
    6. Instala o firmware na particao OTA e reinicia.

  IMPORTANTE:
    - O arquivo partitions.csv deve permanecer na mesma pasta do sketch.
    - O usuario nao precisa abrir nem editar partitions.csv.
    - A Release mais recente precisa possuir o asset:
        ES32Lab-Eleicoes.bin

  Repositorio:
    https://github.com/ESDeveloperBR/ES32Lab-Eleicoes
*/

#include <Arduino.h>
#include <ES32Lab.h>

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <HTTPUpdate.h>
#include <Preferences.h>

// ============================================================
// CONFIGURACAO DO USUARIO
// Altere somente estas duas linhas.
// ============================================================

const char* WIFI_SSID     = "NOME_DO_SEU_WIFI";
const char* WIFI_PASSWORD = "SENHA_DO_SEU_WIFI";

// ============================================================
// FIRMWARE OFICIAL
// ============================================================

const char* FIRMWARE_URL =
  "https://github.com/ESDeveloperBR/ES32Lab-Eleicoes/"
  "releases/latest/download/ES32Lab-Eleicoes.bin";

Preferences preferences;

// ============================================================
// SALVA A REDE PARA O FIRMWARE FINAL
// Mesmas chaves utilizadas pelo ES32Lab Eleicoes.
// ============================================================

bool saveWifiCredentials() {
  if (!preferences.begin("es32-eleicao", false)) {
    return false;
  }

  preferences.putUChar("wcount", 1);
  preferences.putString("ws0", WIFI_SSID);
  preferences.putString("wp0", WIFI_PASSWORD);
  preferences.end();

  return true;
}

// ============================================================
// PROGRESSO
// ============================================================

void otaProgress(int current, int total) {
  if (total <= 0) return;

  static int lastPercent = -10;
  int percent = (current * 100) / total;

  if (percent >= lastPercent + 10 || percent == 100) {
    Serial.printf("Download/gravacao: %d%%\n", percent);
    lastPercent = percent;
  }
}

// ============================================================
// SETUP
// ============================================================

void setup() {
  Serial.begin(115200);
  delay(500);

  Serial.println();
  Serial.println("========================================");
  Serial.println(" ES32Lab Eleicoes - Instalador");
  Serial.println("========================================");
  Serial.println();

  if (String(WIFI_SSID).length() == 0 ||
      String(WIFI_SSID) == "NOME_DO_SEU_WIFI") {
    Serial.println("ERRO: configure WIFI_SSID e WIFI_PASSWORD no sketch.");
    return;
  }

  Serial.print("Conectando ao Wi-Fi: ");
  Serial.println(WIFI_SSID);

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  wl_status_t status = WiFi.waitForConnectResult();

  if (status != WL_CONNECTED) {
    Serial.println();
    Serial.println("ERRO: nao foi possivel conectar ao Wi-Fi.");
    Serial.println("Confira SSID e senha e faca o Upload novamente.");
    return;
  }

  Serial.println("Wi-Fi conectado.");
  Serial.print("IP: ");
  Serial.println(WiFi.localIP());

  if (saveWifiCredentials()) {
    Serial.println("Rede salva para o firmware ES32Lab Eleicoes.");
  } else {
    Serial.println("AVISO: nao foi possivel salvar a rede na NVS.");
  }

  Serial.println();
  Serial.println("Baixando firmware:");
  Serial.println(FIRMWARE_URL);
  Serial.println();

  WiFiClientSecure client;

  // Instalador temporario:
  // simplifica a primeira instalacao sem armazenar certificado no sketch.
  // O mecanismo OTA definitivo podera usar verificacao adicional.
  client.setInsecure();
  client.setTimeout(15000);

  httpUpdate.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
  httpUpdate.rebootOnUpdate(true);
  httpUpdate.onProgress(otaProgress);

  t_httpUpdate_return result = httpUpdate.update(client, FIRMWARE_URL);

  switch (result) {
    case HTTP_UPDATE_FAILED:
      Serial.println();
      Serial.printf(
        "ERRO OTA (%d): %s\n",
        httpUpdate.getLastError(),
        httpUpdate.getLastErrorString().c_str()
      );
      Serial.println("O instalador continua gravado. Corrija o problema e tente novamente.");
      break;

    case HTTP_UPDATE_NO_UPDATES:
      Serial.println("Nenhuma instalacao foi realizada.");
      break;

    case HTTP_UPDATE_OK:
      // Com rebootOnUpdate(true), normalmente o reinicio ocorre
      // antes de esta mensagem chegar a ser exibida.
      Serial.println("Firmware instalado. Reiniciando...");
      break;
  }
}

// ============================================================
// LOOP
// ============================================================

void loop() {
  delay(1000);
}
