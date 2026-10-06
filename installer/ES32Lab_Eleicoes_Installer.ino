/*
  =====================================================================
  ES32Lab Eleicoes - Instalador OTA
  =====================================================================
  Versao do instalador: 0.3.0

  Repositorio oficial:
  https://github.com/ESDeveloperBR/ES32Lab-Eleicoes

  ---------------------------------------------------------------------
  ANTES DE COMPILAR
  ---------------------------------------------------------------------

  1. Instale uma versao atual do pacote:

       esp32 by Espressif Systems

     A definicao oficial "ES Developer ES32Lab" esta disponivel
     a partir da versao 3.3.11.

  2. Na Arduino IDE, selecione:

       ES Developer ES32Lab

  3. Instale a biblioteca oficial ES32Lab pelo
     Gerenciador de Bibliotecas da propria Arduino IDE:

       Procure por: ES32Lab
       Instale:     ES32Lab

     Se a Arduino IDE perguntar sobre dependencias,
     confirme a instalacao.

  4. Se precisar de ajuda para preparar a Arduino IDE:

     https://www.esdeveloper.com.br/como-instalar-e-configurar-arduino-ide

  5. O arquivo partitions.csv deve estar NA MESMA PASTA deste sketch.

     O usuario nao precisa abrir nem modificar partitions.csv.

  ---------------------------------------------------------------------
  COMO USAR
  ---------------------------------------------------------------------

  1. Informe WIFI_SSID e WIFI_PASSWORD abaixo.
  2. Conecte a ES32Lab ao computador pelo cabo USB.
  3. Clique em Carregar na Arduino IDE.
  4. Depois da gravacao, acompanhe todo o processo pelo display.
  5. O instalador:
       - conecta ao Wi-Fi;
       - salva a rede para o firmware final;
       - baixa o firmware mais recente do GitHub;
       - instala o firmware;
       - reinicia automaticamente.

  A Release mais recente precisa possuir um arquivo chamado exatamente:

       ES32Lab-Eleicoes.bin

  =====================================================================
*/

#include <Arduino.h>
#include <ES32Lab.h>

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <HTTPUpdate.h>
#include <Preferences.h>

// =====================================================================
// CONFIGURACAO DO USUARIO
// ALTERE SOMENTE ESTAS DUAS LINHAS
// =====================================================================

const char* WIFI_SSID     = "NOME_DO_SEU_WIFI";
const char* WIFI_PASSWORD = "SENHA_DO_SEU_WIFI";

// =====================================================================
// FIRMWARE OFICIAL
// =====================================================================

const char* FIRMWARE_URL =
  "https://github.com/ESDeveloperBR/ES32Lab-Eleicoes/"
  "releases/latest/download/ES32Lab-Eleicoes.bin";

// =====================================================================
// OBJETOS ES32Lab
// =====================================================================

ES_TFT display;

ES_TimeInterval wifiTimeout;
ES_TimeInterval wifiAnimation;

// =====================================================================
// NVS
// =====================================================================

Preferences preferences;

// =====================================================================
// CORES RGB565
// =====================================================================

constexpr uint16_t C_BG     = 0x0010;
constexpr uint16_t C_PANEL  = 0x0841;
constexpr uint16_t C_BLUE   = 0x051F;
constexpr uint16_t C_CYAN   = 0x07FF;
constexpr uint16_t C_GREEN  = 0x07E0;
constexpr uint16_t C_YELLOW = 0xFFE0;
constexpr uint16_t C_RED    = 0xF800;
constexpr uint16_t C_WHITE  = 0xFFFF;
constexpr uint16_t C_GRAY   = 0x8410;

// =====================================================================
// TEXTO CURTO
// =====================================================================

String shortText(
  const String& text,
  size_t maxLength
) {
  if (
    text.length() <= maxLength
  ) {
    return text;
  }

  if (
    maxLength <= 3
  ) {
    return text.substring(
      0,
      maxLength
    );
  }

  return
    text.substring(
      0,
      maxLength - 3
    ) +
    "...";
}

// =====================================================================
// BASE DA TELA
// =====================================================================

void drawScreenBase(
  const String& title
) {
  display.fillScreen(
    C_BG
  );

  // Barra superior

  display.fillRect(
    0,
    0,
    160,
    20,
    C_BLUE
  );

  display.setTextColor(
    C_WHITE,
    C_BLUE
  );

  display.drawCentreScreenString(
    title,
    6,
    1
  );

  // Moldura central

  display.drawRect(
    5,
    25,
    150,
    92,
    C_CYAN
  );
}

// =====================================================================
// TELA DE STATUS
// =====================================================================

void showStatus(
  const String& title,
  const String& line1,
  const String& line2 = "",
  uint16_t color = C_WHITE
) {
  drawScreenBase(
    title
  );

  display.setTextColor(
    color,
    C_BG
  );

  display.drawCentreScreenString(
    shortText(
      line1,
      24
    ),
    48,
    1
  );

  if (
    line2.length()
  ) {
    display.setTextColor(
      C_GRAY,
      C_BG
    );

    display.drawCentreScreenString(
      shortText(
        line2,
        24
      ),
      68,
      1
    );
  }
}

// =====================================================================
// BARRA DE PROGRESSO
// =====================================================================

void drawProgressBar(
  int percent
) {
  percent = constrain(
    percent,
    0,
    100
  );

  const int x = 12;
  const int y = 77;
  const int w = 136;
  const int h = 15;

  // Fundo

  display.fillRect(
    x,
    y,
    w,
    h,
    C_PANEL
  );

  // Moldura

  display.drawRect(
    x,
    y,
    w,
    h,
    C_CYAN
  );

  // Parte preenchida

  int filled =
    ((w - 4) * percent) /
    100;

  if (
    filled > 0
  ) {
    display.fillRect(
      x + 2,
      y + 2,
      filled,
      h - 4,
      C_GREEN
    );
  }

  // Percentual

  display.fillRect(
    55,
    97,
    50,
    12,
    C_BG
  );

  display.setTextColor(
    C_WHITE,
    C_BG
  );

  display.drawCentreScreenString(
    String(percent) + "%",
    99,
    1
  );
}

// =====================================================================
// SALVA WI-FI PARA O FIRMWARE FINAL
// =====================================================================

bool saveWifiCredentials() {
  if (
    !preferences.begin(
      "es32-eleicao",
      false
    )
  ) {
    return false;
  }

  preferences.putUChar(
    "wcount",
    1
  );

  preferences.putString(
    "ws0",
    WIFI_SSID
  );

  preferences.putString(
    "wp0",
    WIFI_PASSWORD
  );

  preferences.end();

  return true;
}

// =====================================================================
// PROGRESSO OTA
// =====================================================================

void otaProgress(
  int current,
  int total
) {
  if (
    total <= 0
  ) {
    return;
  }

  static int lastPercent = -5;

  int percent =
    (current * 100) /
    total;

  // Atualiza a tela somente a cada 5%.
  // Diminui o trabalho do TFT durante a gravacao da flash.

  if (
    percent != 100 &&
    percent < lastPercent + 5
  ) {
    return;
  }

  lastPercent = percent;

  Serial.printf(
    "Download / instalacao: %d%%\n",
    percent
  );

  drawProgressBar(
    percent
  );
}

// =====================================================================
// CONECTA AO WI-FI
// =====================================================================

bool connectWifi() {
  showStatus(
    "INSTALADOR",
    "CONECTANDO WI-FI",
    shortText(
      WIFI_SSID,
      22
    ),
    C_YELLOW
  );

  Serial.print(
    "Conectando ao Wi-Fi: "
  );

  Serial.println(
    WIFI_SSID
  );

  WiFi.mode(
    WIFI_STA
  );

  WiFi.setAutoReconnect(
    true
  );

  WiFi.begin(
    WIFI_SSID,
    WIFI_PASSWORD
  );

  wifiTimeout.resetMillis();
  wifiAnimation.resetMillis();

  uint8_t dots = 0;

  while (
    WiFi.status() !=
    WL_CONNECTED
  ) {
    // Timeout de 30 segundos

    if (
      wifiTimeout.intervalMillis(
        30000
      )
    ) {
      return false;
    }

    // Animacao simples

    if (
      wifiAnimation.intervalMillis(
        350
      )
    ) {
      dots++;

      if (
        dots > 3
      ) {
        dots = 0;
      }

      String animation;

      for (
        uint8_t i = 0;
        i < dots;
        i++
      ) {
        animation += ".";
      }

      display.fillRect(
        55,
        88,
        50,
        12,
        C_BG
      );

      display.setTextColor(
        C_CYAN,
        C_BG
      );

      display.drawCentreScreenString(
        animation,
        89,
        1
      );
    }

    delay(
      10
    );
  }

  return true;
}

// =====================================================================
// SETUP
// =====================================================================

void setup() {
  Serial.begin(
    115200
  );

  // -------------------------------------------------------------------
  // DISPLAY
  // -------------------------------------------------------------------

  display.init();

  display.setRotation(
    3
  );

  showStatus(
    "INSTALADOR",
    "ES32Lab ELEICOES",
    "INICIALIZANDO...",
    C_CYAN
  );

  delay(
    700
  );

  Serial.println();
  Serial.println(
    "========================================"
  );
  Serial.println(
    " ES32Lab Eleicoes - Instalador OTA"
  );
  Serial.println(
    " Versao: 0.3.0"
  );
  Serial.println(
    "========================================"
  );
  Serial.println();

  // -------------------------------------------------------------------
  // VERIFICA CONFIGURACAO DO USUARIO
  // -------------------------------------------------------------------

  if (
    String(WIFI_SSID).length() == 0 ||
    String(WIFI_SSID) ==
      "NOME_DO_SEU_WIFI"
  ) {
    showStatus(
      "ERRO",
      "CONFIGURE O WI-FI",
      "ANTES DE CARREGAR",
      C_RED
    );

    Serial.println(
      "ERRO: configure WIFI_SSID e WIFI_PASSWORD."
    );

    return;
  }

  // -------------------------------------------------------------------
  // CONECTA AO WI-FI
  // -------------------------------------------------------------------

  if (
    !connectWifi()
  ) {
    showStatus(
      "ERRO WI-FI",
      "NAO FOI POSSIVEL",
      "VERIFIQUE REDE/SENHA",
      C_RED
    );

    Serial.println();
    Serial.println(
      "ERRO: nao foi possivel conectar ao Wi-Fi."
    );

    return;
  }

  // -------------------------------------------------------------------
  // CONECTADO
  // -------------------------------------------------------------------

  showStatus(
    "WI-FI",
    "CONECTADO",
    shortText(
      WIFI_SSID,
      22
    ),
    C_GREEN
  );

  Serial.println();
  Serial.println(
    "Wi-Fi conectado."
  );

  Serial.print(
    "IP: "
  );

  Serial.println(
    WiFi.localIP()
  );

  delay(
    700
  );

  // -------------------------------------------------------------------
  // SALVA CREDENCIAIS
  // -------------------------------------------------------------------

  showStatus(
    "CONFIGURACAO",
    "SALVANDO REDE",
    "PARA O FIRMWARE",
    C_CYAN
  );

  if (
    saveWifiCredentials()
  ) {
    Serial.println(
      "Rede salva para o firmware final."
    );
  } else {
    Serial.println(
      "AVISO: nao foi possivel salvar a rede."
    );
  }

  delay(
    500
  );

  // -------------------------------------------------------------------
  // PREPARA DOWNLOAD
  // -------------------------------------------------------------------

  showStatus(
    "ATUALIZACAO",
    "BAIXANDO FIRMWARE",
    "AGUARDE...",
    C_CYAN
  );

  drawProgressBar(
    0
  );

  Serial.println();
  Serial.println(
    "Baixando firmware:"
  );

  Serial.println(
    FIRMWARE_URL
  );

  WiFiClientSecure client;

  /*
    Este e um instalador temporario.

    Para manter a ferramenta simples,
    o certificado HTTPS nao e validado localmente.

    O futuro atualizador definitivo podera utilizar
    validacao adicional de certificado e SHA-256.
  */

  client.setInsecure();

  client.setTimeout(
    15000
  );

  // GitHub Releases utiliza redirecionamentos.

  httpUpdate.setFollowRedirects(
    HTTPC_FORCE_FOLLOW_REDIRECTS
  );

  /*
    O reboot automatico fica desativado para conseguirmos
    mostrar a mensagem de sucesso no TFT antes de reiniciar.
  */

  httpUpdate.rebootOnUpdate(
    false
  );

  httpUpdate.onProgress(
    otaProgress
  );

  // -------------------------------------------------------------------
  // BAIXA E INSTALA
  // -------------------------------------------------------------------

  t_httpUpdate_return result =
    httpUpdate.update(
      client,
      FIRMWARE_URL
    );

  // -------------------------------------------------------------------
  // RESULTADO
  // -------------------------------------------------------------------

  switch (
    result
  ) {
    case HTTP_UPDATE_FAILED: {
      String errorText =
        httpUpdate
          .getLastErrorString();

      showStatus(
        "FALHA",
        "ERRO NA INSTALACAO",
        shortText(
          errorText,
          22
        ),
        C_RED
      );

      Serial.println();

      Serial.printf(
        "Erro OTA (%d): %s\n",
        httpUpdate.getLastError(),
        errorText.c_str()
      );

      break;
    }

    case HTTP_UPDATE_NO_UPDATES:
      showStatus(
        "ATUALIZACAO",
        "NENHUMA ALTERACAO",
        "TENTE NOVAMENTE",
        C_YELLOW
      );

      Serial.println(
        "Nenhuma atualizacao realizada."
      );

      break;

    case HTTP_UPDATE_OK:
      drawProgressBar(
        100
      );

      delay(
        300
      );

      showStatus(
        "CONCLUIDO",
        "FIRMWARE INSTALADO",
        "REINICIANDO...",
        C_GREEN
      );

      Serial.println();
      Serial.println(
        "Firmware instalado com sucesso."
      );

      Serial.println(
        "Reiniciando..."
      );

      delay(
        2500
      );

      ESP.restart();

      break;
  }
}

// =====================================================================
// LOOP
// =====================================================================

void loop() {
  delay(
    1000
  );
}