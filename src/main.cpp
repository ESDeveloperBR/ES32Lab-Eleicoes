#include <Arduino.h>
#include <ES32Lab.h>
#include <ES_Wifi.h>
#include "ESDeveloper_QR.h"
#include "ES32Lab_Eleicoes_Splash.h"
#include "ES32Lab_UI_Background.h"

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <HTTPUpdate.h>
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <Preferences.h>
#include <time.h>
#include <ctype.h>

// ============================================================
// ES32Lab - VISUALIZADOR DE RESULTADOS ELEITORAIS TSE
// ============================================================
// Versao 2.4.3
//
// 1.0.0 - Resultados TSE 2026
// 1.1.0 - Correcao ArduinoJson/NestingLimit
// 1.2.0 - Interface grafica + fotos
// 1.3.0 - LittleFS + aviso pre-apuracao
// 1.4.0 - Relogio NTP + versionamento
// 2.0.0 - Menu, perfil eleitoral, UF, turnos, multi-WiFi,
//         tarefa de rede em background e parser streaming.
// 2.0.1 - Corrige selecao prematura do 2o turno, prioriza a
//         foto exibida, preserva caixa do SSID e mostra senha.
// 2.1.0 - Foco exclusivo na eleicao geral de 2026, configuracao
//         eleitoral centralizada, editor de senha com cursor
//         piscante e pre-cache oportunista da proxima foto.
// 2.2.0 - QR Code ES Developer na abertura, fluxo inicial de Wi-Fi
//         com scan automatico quando nenhuma rede salva conecta,
//         e remocao de credenciais privadas compiladas no firmware.
// 2.3.0 - Desliga LEDs EX0/EX1 no primeiro instante possivel,
//         adiciona splash Eleicoes 2026 e background grafico padrao
//         em todas as telas da interface.
// 2.3.1 - Refinamento visual para 160x128: reduz sobreposicoes,
//         simplifica rodapes, organiza Status, Wi-Fi, senha e
//         editor de eleicao em paineis mais legiveis.
// 2.3.2 - Ajusta geometria dos campos TURNO/UF, centraliza o botao
//         SALVAR E INICIAR e corrige alinhamento dos indicadores
//         na margem direita das listas de Wi-Fi.
// 2.4.0 - Atualizacao OTA pelo GitHub com controle de versao via
//         manifest.json e correcao definitiva do editor de senha:
//         posicao vazia apos inserir e caractere pendente incluido ao salvar.
// 2.4.1 - Confirma conexao Wi-Fi antes de voltar ao menu, substitui
//         simbolos ^v/<> por setas graficas reais nos rodapes e verifica
//         automaticamente novas versoes durante a tela de abertura.
// 2.4.2 - Reduz redesenhos desnecessarios na tela de resultados:
//         cada cargo atualiza somente a pagina correspondente, fotos
//         pre-carregadas nao forcam refresh e dados identicos nao redesenham.
// 2.4.3 - Padroniza o armazenamento de credenciais Wi-Fi no namespace
//         NVS "ES_Wifi" e migra automaticamente redes salvas pelo formato
//         anterior "es32-eleicao", preservando compatibilidade.
//
// ============================================================

constexpr char APP_NAME[] = "ES32Lab Eleicoes";
constexpr char APP_VERSION[] = "2.4.3";
constexpr char APP_BUILD_DATE[] = "2026-10-08";
constexpr uint32_t APP_BUILD_YYYYMMDD = 20261008UL;

// Nao ha credenciais privadas compiladas no firmware publico.
// Redes sao cadastradas pela interface e persistidas em NVS.
constexpr char DEFAULT_WIFI_SSID[] = "";
constexpr char DEFAULT_WIFI_PASSWORD[] = "";

// Distribuicao oficial / atualizacao OTA.
constexpr char OTA_MANIFEST_URL[] =
  "https://github.com/ESDeveloperBR/ES32Lab-Eleicoes/"
  "releases/latest/download/manifest.json";

constexpr char OTA_FIRMWARE_FALLBACK_URL[] =
  "https://github.com/ESDeveloperBR/ES32Lab-Eleicoes/"
  "releases/latest/download/ES32Lab-Eleicoes.bin";

// ============================================================
// CONFIGURACAO ELEITORAL - ALTERAR AQUI NA PROXIMA ELEICAO GERAL
// ============================================================
//
// O restante do programa monta as URLs a partir destes valores.
// O ele-c.json continua sendo consultado para confirmar quando
// o 2o turno estiver realmente PUBLICADO pelo TSE.
//
// Para uma futura eleicao geral, este e o bloco principal a revisar.
//
constexpr int APP_ELECTION_YEAR = 2026;
constexpr char APP_ELECTION_CYCLE[] = "ele2026";
constexpr int APP_ELECTION_PLEITO = 3220;

constexpr int APP_FEDERAL_T1 = 6257;
constexpr int APP_FEDERAL_T2_PLANNED = 6258;

constexpr int APP_STATE_T1 = 6259;
constexpr int APP_STATE_T2_PLANNED = 6260;
constexpr char APP_FIRST_ROUND_DATE[] = "04/10/2026";

// TSE
constexpr char TSE_BASE[] = "https://resultados.tse.jus.br";
constexpr char TSE_ENV[] = "oficial";
constexpr char TSE_CONFIG_URL[] =
  "https://resultados.tse.jus.br/oficial/comum/config/ele-c.json";

// Limites
constexpr uint8_t MAX_CANDIDATES = 20;
constexpr uint8_t MAX_ELECTIONS = 1;
constexpr uint8_t MAX_WIFI_NETWORKS = 6;
constexpr uint8_t MAX_SCAN_NETWORKS = 12;

// Padrao compartilhado de credenciais Wi-Fi da ES32Lab.
// O namespace fica independente deste aplicativo para permitir que
// futuros firmwares que adotem o mesmo formato reutilizem as redes.
constexpr char WIFI_PREFS_NAMESPACE[] = "ES_Wifi";
constexpr uint8_t WIFI_STORAGE_VERSION = 1;
constexpr uint8_t RACE_SLOTS = 4;
constexpr uint8_t MAX_MISSING_PHOTOS = 30;

// Atualizacao
const uint32_t POLL_OPTIONS_MS[] = {
  30000UL, 60000UL, 90000UL, 120000UL
};
constexpr uint8_t POLL_OPTION_COUNT = 4;

// Abertura
constexpr uint32_t QR_MIN_MS = 3000UL;
constexpr uint32_t PRESENTATION_MIN_MS = 2500UL;

// Cores
constexpr uint16_t C_BG = 0x0006;
constexpr uint16_t C_PANEL = 0x0844;
constexpr uint16_t C_HEADER = 0x0013;
constexpr uint16_t C_LINE = 0x03FF;
constexpr uint16_t C_SELECT = 0x10A5;

// ============================================================
// OBJETOS
// ============================================================

ES_PCF8574 expander(0x20);
ES_TFT display;
ES_AnalogKeyboard keyboard(P_KEYBOARD);
ES_TimeInterval pollTimer;
ES_TimeInterval wifiAttemptTimer;
ES_TimeInterval wifiRetryTimer;
ES_TimeInterval clockUiTimer;
ES_TimeInterval centerHoldTimer;
ES_TimeInterval passwordHoldDelay;
ES_TimeInterval passwordRepeatTimer;
ES_TimeInterval passwordCursorTimer;
ES_TimeInterval photoPreloadTimer;
ES_TimeInterval splashTimer;
ES_TimeInterval wifiSuccessTimer;
ES_File files;
ES_WiFi esWifi;
Preferences prefs;      // Preferencias especificas do aplicativo.
Preferences wifiPrefs;  // Credenciais Wi-Fi no padrao compartilhado ES_Wifi.

// ============================================================
// UF / FUSO
// ============================================================

struct UfInfo {
  const char* code;
  const char* name;
  int8_t utcOffset;
};

// Fuso principal/capital da UF. Amazonas possui mais de um fuso;
// para selecao apenas por UF usamos Manaus (-4).
const UfInfo UFS[] = {
  {"AC","Acre",-5},
  {"AL","Alagoas",-3},
  {"AP","Amapa",-3},
  {"AM","Amazonas",-4},
  {"BA","Bahia",-3},
  {"CE","Ceara",-3},
  {"DF","Distrito Federal",-3},
  {"ES","Espirito Santo",-3},
  {"GO","Goias",-3},
  {"MA","Maranhao",-3},
  {"MT","Mato Grosso",-4},
  {"MS","Mato Grosso do Sul",-4},
  {"MG","Minas Gerais",-3},
  {"PA","Para",-3},
  {"PB","Paraiba",-3},
  {"PR","Parana",-3},
  {"PE","Pernambuco",-3},
  {"PI","Piaui",-3},
  {"RJ","Rio de Janeiro",-3},
  {"RN","Rio Grande do Norte",-3},
  {"RS","Rio Grande do Sul",-3},
  {"RO","Rondonia",-4},
  {"RR","Roraima",-4},
  {"SC","Santa Catarina",-3},
  {"SP","Sao Paulo",-3},
  {"SE","Sergipe",-3},
  {"TO","Tocantins",-3}
};
constexpr uint8_t UF_COUNT = sizeof(UFS) / sizeof(UFS[0]);

// ============================================================
// ESTRUTURAS
// ============================================================

struct Candidate {
  String number;
  String name;
  String party;
  String percentage;
  String sqcand;
  uint32_t votes = 0;
  uint16_t sourceOrder = 0;
};

struct RaceData {
  String title;
  String shortTitle;
  uint8_t office = 0;
  bool usePhotos = false;

  Candidate candidates[MAX_CANDIDATES];
  uint8_t candidateCount = 0;
  uint8_t selected = 0;
  uint8_t scroll = 0;

  bool hasData = false;
  bool stale = false;
  bool countStarted = false;
  int httpCode = 0;

  String sectionPercent = "0,00";
  String andamento;
  String divulgacao;
  String date;
  String time;
  String error = "Aguardando TSE";
  uint64_t totalCandidateVotes = 0;
};

struct ElectionProfile {
  int year = 0;
  char cycle[12] = "";
  int pleito = 0;

  int federal1 = 0;
  int federal2 = 0;
  int state1 = 0;
  int state2 = 0;

  // cdt2 informa o codigo planejado, mas isso nao significa que
  // o arquivo do 2o turno ja foi publicado. Estas flags so ficam
  // true quando o ele-c.json traz uma eleicao explicitamente com t=2.
  bool federal2Available = false;
  bool state2Available = false;

  // Quando o TSE informar as UFs de uma eleicao estadual de 2o turno,
  // guardamos um bit por UF. Zero significa "abrangencia nao detalhada".
  uint32_t state2UfMask = 0;

  char firstDate[11] = "";  // DD/MM/YYYY
  char secondDate[11] = ""; // DD/MM/YYYY

};

struct WifiCredential {
  String ssid;
  String password;
};

struct ScannedNetwork {
  String ssid;
  int rssi = 0;
  bool secure = false;
};

struct RuntimeSelection {
  ElectionProfile profile;
  uint8_t round = 1;
  uint8_t ufIndex = 24; // SP
  uint32_t revision = 0;
};

struct PhotoRequest {
  bool pending = false;
  bool notifyUi = false;
  uint8_t slot = 255;
  String url;
  String path;
  String key;
  uint32_t revision = 0;
};

enum View {
  VIEW_MAIN,
  VIEW_ELECTION,
  VIEW_RESULTS,
  VIEW_SETTINGS,
  VIEW_WIFI,
  VIEW_WIFI_SCAN,
  VIEW_WIFI_DELETE,
  VIEW_PASSWORD,
  VIEW_UPDATE_INTERVAL,
  VIEW_SYSTEM_UPDATE,
  VIEW_CACHE_CONFIRM,
  VIEW_ABOUT
};

enum NetworkWorkerState {
  NET_IDLE,
  NET_CONFIG,
  NET_RESULTS,
  NET_PHOTO,
  NET_SCAN
};

// ============================================================
// ESTADO GLOBAL
// ============================================================

RaceData races[RACE_SLOTS];

ElectionProfile electionCatalog[MAX_ELECTIONS];
uint8_t electionCount = 0;

WifiCredential wifiList[MAX_WIFI_NETWORKS];
uint8_t wifiCount = 0;

ScannedNetwork scanList[MAX_SCAN_NETWORKS];
uint8_t scanCount = 0;
volatile bool scanReady = false;
volatile bool scanRequested = false;

PhotoRequest photoRequest;

volatile bool catalogRequested = false;
volatile bool refreshRequested = false;
volatile bool refreshRunning = false;
volatile NetworkWorkerState networkWorkerState = NET_IDLE;

// Atualizacoes da UI de resultados sao rastreadas por cargo.
// Isso evita redesenhar a tela inteira quando um cargo que nao esta
// visivel termina de atualizar no worker de rede.
volatile uint8_t uiRaceDirtyMask = 0;
volatile bool uiStatusDirty = false;
volatile bool catalogUiDirty = false;

SemaphoreHandle_t raceMutex = nullptr;
SemaphoreHandle_t catalogMutex = nullptr;
SemaphoreHandle_t selectionMutex = nullptr;
SemaphoreHandle_t scanMutex = nullptr;
SemaphoreHandle_t photoMutex = nullptr;

TaskHandle_t networkTaskHandle = nullptr;

View view = VIEW_MAIN;
uint8_t menuIndex = 0;
uint8_t settingsIndex = 0;
uint8_t wifiMenuIndex = 0;
uint8_t scanIndex = 0;
uint8_t resultPage = 0;
uint8_t pollOptionIndex = 1;

int activeYear = APP_ELECTION_YEAR;
uint8_t activeRound = 1;
uint8_t activeUf = 24; // SP
uint32_t selectionRevision = 1;

bool profileWasSaved = false;
bool littleFsReady = false;
bool clockConfigured = false;
bool expanderReady = false;

// Fluxo de abertura / provisionamento Wi-Fi
bool startupSplashActive = true;
bool startupPresentationActive = false;
bool splashMinimumElapsed = false;
bool startupWifiCycleFinished = false;
bool startupWifiProvisioning = false;
bool startupAppEntered = false;
bool startupUpdateChecked = false;

// Confirmacao visual de conexao Wi-Fi manual
bool wifiSuccessPending = false;
bool wifiSuccessActive = false;
String wifiSuccessSsid;
String wifiSuccessIp;

// Editor de eleicao
uint8_t editRound = 1;
uint8_t editUf = 24;
uint8_t electionEditRow = 0;

// Editor de senha
String passwordSsid;
String passwordValue;
uint8_t passwordGroup = 0;
uint8_t passwordCharIndex = 0;
bool passwordSecure = true;
uint16_t passwordRepeatCount = 0;
bool passwordRepeatActive = false;
bool passwordCursorVisible = true;
bool passwordHasPendingChar = false;

// Atualizacao OTA
enum OtaUiState {
  OTA_UI_IDLE,
  OTA_UI_CHECKING,
  OTA_UI_CURRENT,
  OTA_UI_AVAILABLE,
  OTA_UI_ERROR,
  OTA_UI_INSTALLING
};

volatile bool otaModeActive = false;
OtaUiState otaUiState = OTA_UI_IDLE;
String otaAvailableVersion;
String otaFirmwareUrl = OTA_FIRMWARE_FALLBACK_URL;
String otaExpectedSha256;
uint32_t otaRemoteSize = 0;
String otaMessage;
int otaLastProgress = -5;
bool otaEnteredFromStartup = false;

// Confirmacao exclusao Wi-Fi
uint8_t wifiDeleteIndex = 0;

// Gesture centro
bool centerTracking = false;
bool centerLongFired = false;

// Wi-Fi nonblocking
bool wifiConnecting = false;
uint8_t wifiTryIndex = 0;
bool wifiManualConnect = false;
String manualSsid;
String manualPassword;

// Fotos 404
String missingPhotoKeys[MAX_MISSING_PHOTOS];
uint8_t missingPhotoCount = 0;

// ============================================================
// HELPERS BASICOS
// ============================================================

uint8_t findUfIndex(const String& code) {
  for (uint8_t i = 0; i < UF_COUNT; i++) {
    if (code.equalsIgnoreCase(UFS[i].code)) return i;
  }
  return 24; // SP
}

String asciiText(String s) {
  s.replace("Á","A"); s.replace("À","A"); s.replace("Â","A"); s.replace("Ã","A");
  s.replace("á","a"); s.replace("à","a"); s.replace("â","a"); s.replace("ã","a");
  s.replace("É","E"); s.replace("Ê","E"); s.replace("é","e"); s.replace("ê","e");
  s.replace("Í","I"); s.replace("í","i");
  s.replace("Ó","O"); s.replace("Ô","O"); s.replace("Õ","O");
  s.replace("ó","o"); s.replace("ô","o"); s.replace("õ","o");
  s.replace("Ú","U"); s.replace("Ü","U"); s.replace("ú","u"); s.replace("ü","u");
  s.replace("Ç","C"); s.replace("ç","c");
  return s;
}

String fitText(String s, uint8_t maxChars) {
  s = asciiText(s);
  s.toUpperCase();
  if (s.length() <= maxChars) return s;
  if (maxChars <= 1) return s.substring(0, maxChars);
  return s.substring(0, maxChars - 1) + ".";
}

String fitRawText(String s, uint8_t maxChars) {
  // Usado principalmente em SSID/senha: preserva maiusculas,
  // minusculas e a grafia recebida. Apenas limita o comprimento.
  if (s.length() <= maxChars)
    return s;

  if (maxChars <= 1)
    return s.substring(0, maxChars);

  return s.substring(0, maxChars - 1) + ".";
}

String tailRawText(const String& s, uint8_t maxChars) {
  if (s.length() <= maxChars)
    return s;

  if (maxChars <= 1)
    return s.substring(s.length() - maxChars);

  return "<" + s.substring(s.length() - (maxChars - 1));
}

String padNumber(int value, uint8_t width) {
  String s(value);
  while (s.length() < width) s = "0" + s;
  return s;
}

String formatVotes(uint32_t value) {
  String raw(value), out;
  int len = raw.length();
  for (int i = 0; i < len; i++) {
    out += raw[i];
    int remain = len - i - 1;
    if (remain > 0 && remain % 3 == 0) out += ".";
  }
  return out;
}

float percentFloat(String s) {
  s.replace(",", ".");
  return s.toFloat();
}

uint32_t dateDDMMYYYYToInt(const char* d) {
  if (!d || strlen(d) < 10) return 0;
  int day = String(d).substring(0,2).toInt();
  int mon = String(d).substring(3,5).toInt();
  int yr  = String(d).substring(6,10).toInt();
  return (uint32_t)yr * 10000UL + mon * 100UL + day;
}

// ============================================================
// RELOGIO
// ============================================================

void configureClockForUf(uint8_t ufIndex) {
  if (ufIndex >= UF_COUNT) ufIndex = 24;
  long offset = (long)UFS[ufIndex].utcOffset * 3600L;
  configTime(offset, 0, "pool.ntp.org", "time.google.com", "time.nist.gov");
  clockConfigured = true;
}

bool clockValid() {
  return time(nullptr) > 1700000000;
}

String clockText() {
  if (!clockValid()) return "--:--";
  time_t now = time(nullptr);
  struct tm t;
  localtime_r(&now, &t);
  char b[6];
  strftime(b, sizeof(b), "%H:%M", &t);
  return String(b);
}

String dateText() {
  if (!clockValid()) return "--/--/----";
  time_t now = time(nullptr);
  struct tm t;
  localtime_r(&now, &t);
  char b[11];
  strftime(b, sizeof(b), "%d/%m/%Y", &t);
  return String(b);
}

uint32_t currentDateNumber() {
  if (!clockValid()) return APP_BUILD_YYYYMMDD;
  time_t now = time(nullptr);
  struct tm t;
  localtime_r(&now, &t);
  return (uint32_t)(t.tm_year + 1900) * 10000UL +
         (uint32_t)(t.tm_mon + 1) * 100UL +
         (uint32_t)t.tm_mday;
}

// ============================================================
// CATALOGO ELEITORAL
// ============================================================

void copyText(char* dest, size_t size, const String& src) {
  if (!dest || size == 0) return;
  strncpy(dest, src.c_str(), size - 1);
  dest[size - 1] = '\0';
}

void copyText(char* dest, size_t size, const char* src) {
  if (!dest || size == 0) return;
  strncpy(dest, src ? src : "", size - 1);
  dest[size - 1] = '\0';
}

void sortElectionCatalogLocked() {
  for (uint8_t i = 0; i < electionCount; i++) {
    for (uint8_t j = i + 1; j < electionCount; j++) {
      if (electionCatalog[j].year < electionCatalog[i].year) {
        ElectionProfile tmp = electionCatalog[i];
        electionCatalog[i] = electionCatalog[j];
        electionCatalog[j] = tmp;
      }
    }
  }
}

void upsertElectionProfile(const ElectionProfile& p) {
  xSemaphoreTake(catalogMutex, portMAX_DELAY);

  for (uint8_t i = 0; i < electionCount; i++) {
    if (electionCatalog[i].year == p.year) {
      electionCatalog[i] = p;
      sortElectionCatalogLocked();
      xSemaphoreGive(catalogMutex);
      return;
    }
  }

  if (electionCount < MAX_ELECTIONS) {
    electionCatalog[electionCount++] = p;
    sortElectionCatalogLocked();
  }

  xSemaphoreGive(catalogMutex);
}

void mergeElectionProfile(const ElectionProfile& p) {
  xSemaphoreTake(catalogMutex, portMAX_DELAY);

  ElectionProfile* target = nullptr;

  for (uint8_t i = 0; i < electionCount; i++) {
    if (electionCatalog[i].year == p.year) {
      target = &electionCatalog[i];
      break;
    }
  }

  if (!target && electionCount < MAX_ELECTIONS) {
    electionCatalog[electionCount] = ElectionProfile();
    electionCatalog[electionCount].year = p.year;
    target = &electionCatalog[electionCount++];
  }

  if (target) {
    if (p.year > 0) target->year = p.year;
    if (strlen(p.cycle) > 0) copyText(target->cycle, sizeof(target->cycle), p.cycle);
    if (p.pleito > 0) target->pleito = p.pleito;

    if (p.federal1 > 0) target->federal1 = p.federal1;
    if (p.state1 > 0) target->state1 = p.state1;
    if (p.federal2 > 0) target->federal2 = p.federal2;
    if (p.state2 > 0) target->state2 = p.state2;

    target->federal2Available = target->federal2Available || p.federal2Available;
    target->state2Available = target->state2Available || p.state2Available;

    if (p.state2UfMask != 0)
      target->state2UfMask |= p.state2UfMask;

    if (strlen(p.firstDate) > 0)
      copyText(target->firstDate, sizeof(target->firstDate), p.firstDate);

    if (strlen(p.secondDate) > 0)
      copyText(target->secondDate, sizeof(target->secondDate), p.secondDate);


    sortElectionCatalogLocked();
  }

  xSemaphoreGive(catalogMutex);
}

void seedElectionCatalog() {
  // A v2.1.0 trabalha somente com a eleicao geral configurada acima.
  // Nao ha mais catalogo de eleicoes historicas dentro do firmware.
  ElectionProfile current;

  current.year = APP_ELECTION_YEAR;
  copyText(current.cycle, sizeof(current.cycle), APP_ELECTION_CYCLE);
  current.pleito = APP_ELECTION_PLEITO;

  current.federal1 = APP_FEDERAL_T1;
  current.federal2 = APP_FEDERAL_T2_PLANNED;
  current.state1 = APP_STATE_T1;
  current.state2 = APP_STATE_T2_PLANNED;

  // cdt2 informa apenas o codigo planejado. O 2o turno somente sera
  // liberado depois que ele-c.json publicar uma entrada explicita t=2.
  current.federal2Available = false;
  current.state2Available = false;
  current.state2UfMask = 0;

  copyText(current.firstDate, sizeof(current.firstDate), APP_FIRST_ROUND_DATE);

  upsertElectionProfile(current);
}

bool getElectionProfile(int year, ElectionProfile& out) {
  bool found = false;
  xSemaphoreTake(catalogMutex, portMAX_DELAY);
  for (uint8_t i = 0; i < electionCount; i++) {
    if (electionCatalog[i].year == year) {
      out = electionCatalog[i];
      found = true;
      break;
    }
  }
  xSemaphoreGive(catalogMutex);
  return found;
}

int latestElectionYear() {
  return APP_ELECTION_YEAR;
}

int stepElectionYear(int current, int delta) {
  (void)current;
  (void)delta;
  return APP_ELECTION_YEAR;
}

bool stateSecondRoundAvailableForUf(
  const ElectionProfile& p,
  uint8_t ufIndex
) {
  if (!p.state2Available || p.state2 <= 0)
    return false;

  // Alguns catalogos antigos nao detalham a abrangencia por UF.
  if (p.state2UfMask == 0)
    return true;

  if (ufIndex >= UF_COUNT)
    return false;

  return (p.state2UfMask & (1UL << ufIndex)) != 0;
}

bool secondRoundAvailableForUf(
  const ElectionProfile& p,
  uint8_t ufIndex
) {
  if (p.federal2Available && p.federal2 > 0)
    return true;

  return stateSecondRoundAvailableForUf(p, ufIndex);
}

uint8_t suggestedRoundForYear(
  int year,
  uint8_t ufIndex = 24
) {
  ElectionProfile p;

  if (!getElectionProfile(year, p))
    return 1;

  // Nao usamos mais apenas a data nem a existencia de cdt2.
  // O 2o turno so e sugerido depois de o catalogo do TSE publicar
  // uma eleicao explicitamente marcada como t=2.
  return secondRoundAvailableForUf(p, ufIndex) ? 2 : 1;
}

// ============================================================
// PERSISTENCIA
// ============================================================

void saveProfile() {
  // Ano nao e mais uma preferencia do usuario. Ele fica centralizado
  // no bloco APP_ELECTION_* no inicio do arquivo.
  prefs.putInt("year", APP_ELECTION_YEAR);
  prefs.putUChar("round", activeRound);
  prefs.putUChar("uf", activeUf);
  prefs.putBool("profile", true);
  profileWasSaved = true;
}

void loadProfile() {
  profileWasSaved = prefs.getBool("profile", false);

  // Ignora qualquer ano gravado por versoes antigas.
  activeYear = APP_ELECTION_YEAR;

  activeUf = prefs.getUChar("uf", 24);
  if (activeUf >= UF_COUNT) activeUf = 24;

  activeRound = prefs.getUChar("round", 1);

  if (activeRound != 1 && activeRound != 2)
    activeRound = 1;

  // Nao rebaixa o turno 2 aqui: primeiro o worker consulta ele-c.json.
  // Isso permite que uma futura publicacao oficial do 2o turno seja
  // reconhecida antes de qualquer requisicao aos resultados.
}

void savePollSetting() {
  prefs.putUChar("poll", pollOptionIndex);
}

void loadPollSetting() {
  pollOptionIndex = prefs.getUChar("poll", 1);
  if (pollOptionIndex >= POLL_OPTION_COUNT) pollOptionIndex = 1;
}

// ============================================================
// MULTI-WIFI EM NVS - PADRAO ES_Wifi
// ============================================================
//
// Estrutura logica:
//   namespace: ES_Wifi
//   version = 1
//   wcount  = quantidade de redes
//   ws0/wp0 = SSID/senha da rede 0
//   ws1/wp1 = SSID/senha da rede 1
//   ...
//
// O namespace e o formato sao independentes do aplicativo Eleicoes,
// permitindo que outros firmwares ES32Lab adotem o mesmo padrao.
//
void persistWifiList() {
  wifiPrefs.putUChar("version", WIFI_STORAGE_VERSION);
  wifiPrefs.putUChar("wcount", wifiCount);

  for (uint8_t i = 0; i < MAX_WIFI_NETWORKS; i++) {
    String ks = "ws" + String(i);
    String kp = "wp" + String(i);

    if (i < wifiCount) {
      wifiPrefs.putString(ks.c_str(), wifiList[i].ssid);
      wifiPrefs.putString(kp.c_str(), wifiList[i].password);
    } else {
      wifiPrefs.remove(ks.c_str());
      wifiPrefs.remove(kp.c_str());
    }
  }
}

void loadWifiList() {
  wifiCount = 0;

  // Migracao unica do formato usado ate a v2.4.2.
  // O namespace antigo e mantido intacto para permitir rollback.
  uint8_t storageVersion = wifiPrefs.getUChar("version", 0);

  if (storageVersion == 0) {
    uint8_t legacyCount = prefs.getUChar("wcount", 0);
    if (legacyCount > MAX_WIFI_NETWORKS) legacyCount = MAX_WIFI_NETWORKS;

    for (uint8_t i = 0; i < legacyCount; i++) {
      wifiList[i].ssid =
        prefs.getString(("ws" + String(i)).c_str(), "");
      wifiList[i].password =
        prefs.getString(("wp" + String(i)).c_str(), "");
    }

    wifiCount = legacyCount;

    // Marca o novo armazenamento como inicializado mesmo quando nao ha
    // redes antigas. Isso impede que redes legadas apagadas/obsoletas
    // sejam importadas novamente no futuro.
    persistWifiList();

    if (legacyCount > 0) {
      Serial.print("Wi-Fi: migradas ");
      Serial.print(legacyCount);
      Serial.println(" rede(s) de es32-eleicao para ES_Wifi.");
    } else {
      Serial.println("Wi-Fi: armazenamento ES_Wifi inicializado.");
    }
  }

  wifiCount = wifiPrefs.getUChar("wcount", 0);
  if (wifiCount > MAX_WIFI_NETWORKS) wifiCount = MAX_WIFI_NETWORKS;

  for (uint8_t i = 0; i < wifiCount; i++) {
    wifiList[i].ssid =
      wifiPrefs.getString(("ws" + String(i)).c_str(), "");
    wifiList[i].password =
      wifiPrefs.getString(("wp" + String(i)).c_str(), "");
  }

  // Primeira execucao: preserva o "melhor dos dois mundos".
  if (wifiCount == 0 && strlen(DEFAULT_WIFI_SSID) > 0) {
    wifiList[0].ssid = DEFAULT_WIFI_SSID;
    wifiList[0].password = DEFAULT_WIFI_PASSWORD;
    wifiCount = 1;
    persistWifiList();
  }
}

int findSavedWifi(const String& ssid) {
  for (uint8_t i = 0; i < wifiCount; i++) {
    if (wifiList[i].ssid == ssid) return i;
  }
  return -1;
}

void promoteWifi(uint8_t index) {
  if (index == 0 || index >= wifiCount) return;
  WifiCredential c = wifiList[index];
  for (int i = index; i > 0; i--) wifiList[i] = wifiList[i - 1];
  wifiList[0] = c;
  persistWifiList();
}

void addOrUpdateWifi(const String& ssid, const String& password) {
  int existing = findSavedWifi(ssid);

  if (existing >= 0) {
    wifiList[existing].password = password;
    promoteWifi(existing);
    persistWifiList();
    return;
  }

  if (wifiCount < MAX_WIFI_NETWORKS) {
    for (int i = wifiCount; i > 0; i--) wifiList[i] = wifiList[i - 1];
    wifiList[0].ssid = ssid;
    wifiList[0].password = password;
    wifiCount++;
  } else {
    for (int i = MAX_WIFI_NETWORKS - 1; i > 0; i--) wifiList[i] = wifiList[i - 1];
    wifiList[0].ssid = ssid;
    wifiList[0].password = password;
  }

  persistWifiList();
}

void deleteWifi(uint8_t index) {
  if (index >= wifiCount) return;
  for (uint8_t i = index; i + 1 < wifiCount; i++) wifiList[i] = wifiList[i + 1];
  if (wifiCount > 0) wifiCount--;
  persistWifiList();
}

// ============================================================
// WIFI NAO BLOQUEANTE
// ============================================================

void startWifiAttempt(const String& ssid, const String& password) {
  if (ssid.length() == 0) return;

  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  WiFi.begin(ssid.c_str(), password.c_str());

  wifiConnecting = true;
  wifiAttemptTimer.resetMillis();

  Serial.print("WiFi tentando: ");
  Serial.println(ssid);
}

void startWifiAutoCycle() {
  if (wifiCount == 0) return;
  wifiManualConnect = false;
  wifiTryIndex = 0;
  startupWifiCycleFinished = false;
  startWifiAttempt(wifiList[0].ssid, wifiList[0].password);
}

void requestManualWifi(const String& ssid, const String& password) {
  manualSsid = ssid;
  manualPassword = password;
  wifiManualConnect = true;
  startWifiAttempt(manualSsid, manualPassword);
}

void serviceWifiConnection() {
  static bool wasConnected = false;
  bool connected = esWifi.isConnected();

  // Nao inicia/troca conexao enquanto a classe ES_WiFi esta escaneando.
  if (!connected && (scanRequested || networkWorkerState == NET_SCAN))
    return;

  if (connected) {
    // A confirmacao manual nao depende apenas da transicao de status.
    // Isso cobre inclusive reconexao rapida a uma rede ja conhecida.
    if (wifiManualConnect &&
        WiFi.SSID() == manualSsid) {
      wifiManualConnect = false;
      wifiSuccessSsid = WiFi.SSID();
      wifiSuccessIp = WiFi.localIP().toString();
      wifiSuccessPending = true;
    }

    if (!wasConnected) {
      wasConnected = true;
      wifiConnecting = false;

      Serial.print("WiFi conectado: ");
      Serial.println(WiFi.SSID());
      Serial.print("IP: ");
      Serial.println(WiFi.localIP());

      int idx = findSavedWifi(WiFi.SSID());
      if (idx > 0) promoteWifi(idx);

      configureClockForUf(activeUf);

      // Durante a abertura, preserva a rede livre para a verificacao
      // silenciosa de firmware. Os dados do TSE sao solicitados assim
      // que a aplicacao efetivamente entra.
      if (startupAppEntered) {
        catalogRequested = true;
        refreshRequested = true;
      }
    }
    return;
  }

  if (wasConnected) {
    wasConnected = false;
    wifiConnecting = false;
    wifiRetryTimer.resetMillis();
  }

  if (wifiConnecting) {
    if (wifiAttemptTimer.intervalMillis(8000UL)) {
      wifiConnecting = false;

      if (wifiManualConnect) {
        wifiManualConnect = false;
      } else if (wifiCount > 0) {
        wifiTryIndex++;
        if (wifiTryIndex < wifiCount) {
          startWifiAttempt(wifiList[wifiTryIndex].ssid, wifiList[wifiTryIndex].password);
          return;
        }

        // Todas as redes salvas foram tentadas sem sucesso.
        // Durante a abertura isso aciona automaticamente o scan Wi-Fi.
        startupWifiCycleFinished = true;
      }

      wifiRetryTimer.resetMillis();
    }
    return;
  }

  // Durante o fluxo inicial, apos esgotar as redes salvas, a interface
  // assume o provisionamento por scan em vez de repetir indefinidamente.
  if (!startupAppEntered && startupWifiCycleFinished)
    return;

  if (wifiRetryTimer.intervalMillis(15000UL)) {
    startWifiAutoCycle();
  }
}

// ============================================================
// LITTLEFS / CACHE
// ============================================================

void initLittleFs() {
  littleFsReady = LittleFS.begin(true);
  if (!littleFsReady) {
    Serial.println("LittleFS: FALHA");
    return;
  }

  files.begin(LittleFS);

  if (!files.directoryExists(LittleFS, "/photos"))
    files.createDirectory(LittleFS, "/photos");

  Serial.print("LittleFS total: ");
  Serial.println(files.getTotalSpace(LittleFS));
}

void clearPhotoCache() {
  if (!littleFsReady) return;
  files.remove(LittleFS, "/photos", true);
  files.createDirectory(LittleFS, "/photos");
  missingPhotoCount = 0;
}

bool ensurePhotoSpace(size_t required) {
  if (!littleFsReady) return false;
  size_t available = files.getAvailableSpace(LittleFS);
  if (available >= required + 32768UL) return true;

  clearPhotoCache();
  available = files.getAvailableSpace(LittleFS);
  return available >= required + 32768UL;
}

bool photoMarkedMissing(const String& key) {
  for (uint8_t i = 0; i < missingPhotoCount; i++)
    if (missingPhotoKeys[i] == key) return true;
  return false;
}

void markPhotoMissing(const String& key) {
  if (photoMarkedMissing(key)) return;
  if (missingPhotoCount < MAX_MISSING_PHOTOS)
    missingPhotoKeys[missingPhotoCount++] = key;
}

bool photoCached(const String& path) {
  return littleFsReady &&
         files.exists(LittleFS, path) &&
         files.getFileSize(LittleFS, path) > 500;
}

// ============================================================
// SELECAO ATIVA
// ============================================================

RuntimeSelection getRuntimeSelection() {
  RuntimeSelection s;

  xSemaphoreTake(selectionMutex, portMAX_DELAY);
  s.round = activeRound;
  s.ufIndex = activeUf;
  s.revision = selectionRevision;
  xSemaphoreGive(selectionMutex);

  getElectionProfile(APP_ELECTION_YEAR, s.profile);
  return s;
}

void clearRaceData(bool notifyUi = false) {
  xSemaphoreTake(raceMutex, portMAX_DELAY);

  for (uint8_t i = 0; i < RACE_SLOTS; i++)
    races[i] = RaceData();

  if (notifyUi) {
    uiRaceDirtyMask = (1U << RACE_SLOTS) - 1U;
    uiStatusDirty = true;
  }

  xSemaphoreGive(raceMutex);
  resultPage = 0;
}

void activateSelection(uint8_t round, uint8_t ufIndex) {
  if (ufIndex >= UF_COUNT) ufIndex = 24;

  ElectionProfile p;
  getElectionProfile(APP_ELECTION_YEAR, p);

  if (round == 2 && !secondRoundAvailableForUf(p, ufIndex))
    round = 1;

  xSemaphoreTake(selectionMutex, portMAX_DELAY);
  activeYear = APP_ELECTION_YEAR;
  activeRound = round;
  activeUf = ufIndex;
  selectionRevision++;
  xSemaphoreGive(selectionMutex);

  configureClockForUf(activeUf);
  saveProfile();
  clearRaceData();
  pollTimer.resetMillis();
  refreshRequested = true;
}

// ============================================================
// RESULTADOS: DEFINICOES DE CARGO
// ============================================================

bool activeSecondRoundHasPresident() {
  ElectionProfile p;

  if (!getElectionProfile(APP_ELECTION_YEAR, p))
    return false;

  return p.federal2Available && p.federal2 > 0;
}

bool activeSecondRoundHasGovernor() {
  ElectionProfile p;

  if (!getElectionProfile(APP_ELECTION_YEAR, p))
    return false;

  return stateSecondRoundAvailableForUf(p, activeUf);
}

uint8_t resultPageCount() {
  if (activeRound != 2)
    return 5;

  uint8_t count = 1; // Status sempre existe.

  if (activeSecondRoundHasPresident())
    count++;

  if (activeSecondRoundHasGovernor())
    count++;

  return count;
}

bool resultPageIsStatus(uint8_t page) {
  return page == (resultPageCount() - 1);
}

uint8_t raceSlotForPage(uint8_t page) {
  if (activeRound != 2)
    return page; // 0 Pres, 1 Gov, 2 Sen, 3 Dep, 4 Status.

  uint8_t logicalPage = 0;

  if (activeSecondRoundHasPresident()) {
    if (page == logicalPage)
      return 0;

    logicalPage++;
  }

  if (activeSecondRoundHasGovernor()) {
    if (page == logicalPage)
      return 1;

    logicalPage++;
  }

  return 255; // Status.
}

uint8_t officeForSlot(uint8_t slot, uint8_t ufIndex) {
  if (slot == 0) return 1;
  if (slot == 1) return 3;
  if (slot == 2) return 5;
  if (slot == 3) return String(UFS[ufIndex].code) == "DF" ? 8 : 7;
  return 0;
}

String titleForSlot(uint8_t slot, uint8_t ufIndex) {
  String uf = UFS[ufIndex].code;
  if (slot == 0) return "PRESIDENTE";
  if (slot == 1) return "GOVERNADOR " + uf;
  if (slot == 2) return "SENADOR " + uf;
  if (slot == 3) return (uf == "DF") ? "DEP DISTRITAL DF" : "DEP ESTADUAL " + uf;
  return "";
}

bool photosForSlot(uint8_t slot) {
  return slot <= 1;
}

uint8_t round2RefreshSlot(
  const RuntimeSelection& s,
  uint8_t index
) {
  uint8_t logical = 0;

  if (s.profile.federal2Available && s.profile.federal2 > 0) {
    if (index == logical)
      return 0;

    logical++;
  }

  if (stateSecondRoundAvailableForUf(s.profile, s.ufIndex)) {
    if (index == logical)
      return 1;

    logical++;
  }

  return 255;
}

uint8_t round2RefreshCount(
  const RuntimeSelection& s
) {
  uint8_t count = 0;

  if (s.profile.federal2Available && s.profile.federal2 > 0)
    count++;

  if (stateSecondRoundAvailableForUf(s.profile, s.ufIndex))
    count++;

  return count;
}

int electionCodeForOffice(const RuntimeSelection& s, uint8_t office) {
  bool federal = office == 1;

  if (s.round == 1)
    return federal ? s.profile.federal1 : s.profile.state1;

  if (federal) {
    return (s.profile.federal2Available && s.profile.federal2 > 0)
      ? s.profile.federal2
      : 0;
  }

  return stateSecondRoundAvailableForUf(s.profile, s.ufIndex)
    ? s.profile.state2
    : 0;
}

String resultUrl(const RuntimeSelection& s, uint8_t office) {
  int election = electionCodeForOffice(s, office);
  if (election <= 0) return "";

  String abr = office == 1 ? "br" : String(UFS[s.ufIndex].code);
  abr.toLowerCase();

  return String(TSE_BASE) + "/" + TSE_ENV + "/" + s.profile.cycle + "/" +
         String(election) + "/dados/" + abr + "/" + abr +
         "-c" + padNumber(office, 4) + "-e" + padNumber(election, 6) + "-u.json";
}

String photoUrl(const RuntimeSelection& s, uint8_t office, const Candidate& c) {
  int election = electionCodeForOffice(s, office);
  String abr = office == 1 ? "br" : String(UFS[s.ufIndex].code);
  abr.toLowerCase();

  return String(TSE_BASE) + "/" + TSE_ENV + "/" + s.profile.cycle + "/" +
         String(election) + "/fotos/" + abr + "/" + c.sqcand + ".jpeg";
}

String photoPath(const RuntimeSelection& s, uint8_t office, const Candidate& c) {
  String abr = office == 1 ? "br" : String(UFS[s.ufIndex].code);
  abr.toLowerCase();

  return "/photos/" + String(s.profile.year) + "_" + String(s.round) + "_" +
         abr + "_" + c.sqcand + ".jpg";
}

// ============================================================
// CANDIDATOS / ORDENACAO
// ============================================================

bool candidateBefore(const Candidate& a, const Candidate& b) {
  if (a.votes != b.votes) return a.votes > b.votes;
  return a.sourceOrder < b.sourceOrder;
}

void insertTopCandidate(
  Candidate list[],
  uint8_t& count,
  const Candidate& candidate
) {
  uint8_t pos = count;

  for (uint8_t i = 0; i < count; i++) {
    if (candidateBefore(candidate, list[i])) {
      pos = i;
      break;
    }
  }

  if (count < MAX_CANDIDATES) {
    for (int i = count; i > pos; i--) list[i] = list[i - 1];
    list[pos] = candidate;
    count++;
    return;
  }

  if (pos >= MAX_CANDIDATES) return;

  for (int i = MAX_CANDIDATES - 1; i > pos; i--)
    list[i] = list[i - 1];

  list[pos] = candidate;
}

uint32_t jsonUInt(JsonVariantConst v) {
  if (v.isNull()) return 0;
  if (v.is<const char*>()) return strtoul(v.as<const char*>(), nullptr, 10);
  return v.as<uint32_t>();
}

String jsonString(JsonVariantConst v, const char* fallback = "") {
  if (v.isNull()) return String(fallback);
  if (v.is<const char*>()) return String(v.as<const char*>());
  if (v.is<uint64_t>()) {
    char b[32];
    snprintf(b, sizeof(b), "%llu", (unsigned long long)v.as<uint64_t>());
    return String(b);
  }
  if (v.is<int64_t>()) {
    char b[32];
    snprintf(b, sizeof(b), "%lld", (long long)v.as<int64_t>());
    return String(b);
  }
  if (v.is<double>()) {
    String s(v.as<double>(), 2);
    s.replace(".", ",");
    return s;
  }
  return String(fallback);
}

// ============================================================
// LEITOR HTTP STREAMING
// ============================================================

class HttpReader {
public:
  HttpReader(HTTPClient& http, WiFiClient* stream, int length)
    : _http(http), _stream(stream), _remaining(length) {
    _stall.resetMillis();
  }

  int read() {
    while (true) {
      int available = _stream->available();

      if (available > 0) {
        int c = _stream->read();
        if (c >= 0) {
          if (_remaining > 0) _remaining--;
          _stall.resetMillis();
          return c;
        }
      }

      if (_remaining == 0) return -1;

      if (!_http.connected() && _stream->available() == 0)
        return -1;

      if (_stall.intervalMillis(12000UL))
        return -1;

      vTaskDelay(pdMS_TO_TICKS(1));
    }
  }

private:
  HTTPClient& _http;
  WiFiClient* _stream;
  int _remaining;
  ES_TimeInterval _stall;
};

String readJsonString(HttpReader& r) {
  String out;
  bool escape = false;

  while (true) {
    int ci = r.read();
    if (ci < 0) break;
    char c = (char)ci;

    if (escape) {
      // Para chaves/valores simples basta manter o caractere.
      // O objeto completo do candidato sera novamente interpretado
      // pelo ArduinoJson, preservando escapes no capturador abaixo.
      switch (c) {
        case 'n': out += '\n'; break;
        case 'r': out += '\r'; break;
        case 't': out += '\t'; break;
        default: out += c; break;
      }
      escape = false;
    } else if (c == '\\') {
      escape = true;
    } else if (c == '"') {
      break;
    } else {
      out += c;
    }
  }

  return out;
}

int nextNonSpace(HttpReader& r) {
  while (true) {
    int c = r.read();
    if (c < 0) return -1;
    if (!isspace((char)c)) return c;
  }
}

bool readCandidateObject(HttpReader& r, String& objectJson) {
  objectJson = "{";
  int depth = 1;
  bool inString = false;
  bool escape = false;

  while (depth > 0) {
    int ci = r.read();
    if (ci < 0) return false;

    char c = (char)ci;
    objectJson += c;

    if (inString) {
      if (escape) {
        escape = false;
      } else if (c == '\\') {
        escape = true;
      } else if (c == '"') {
        inString = false;
      }
    } else {
      if (c == '"') inString = true;
      else if (c == '{') depth++;
      else if (c == '}') depth--;
    }
  }

  return true;
}

void parseCandidateArray(
  HttpReader& r,
  const String& parentParty,
  RaceData& out,
  uint16_t& sourceOrder
) {
  while (true) {
    int c = nextNonSpace(r);
    if (c < 0 || c == ']') return;
    if (c == ',') continue;
    if (c != '{') continue;

    String objectJson;
    objectJson.reserve(768);

    if (!readCandidateObject(r, objectJson))
      return;

    JsonDocument doc;
    DeserializationError err =
      deserializeJson(doc, objectJson, DeserializationOption::NestingLimit(10));

    if (err) continue;

    Candidate cand;
    cand.number = jsonString(doc["n"]);
    cand.sqcand = jsonString(doc["sqcand"]);
    cand.name = jsonString(doc["nmu"]);
    if (cand.name.length() == 0) cand.name = jsonString(doc["nm"]);
    cand.votes = jsonUInt(doc["vap"]);
    cand.percentage = jsonString(doc["pvap"], "0,00");
    cand.sourceOrder = sourceOrder++;

    String ownParty = jsonString(doc["sg"]);
    if (ownParty.length() == 0) ownParty = jsonString(doc["cc"]);
    cand.party = ownParty.length() ? ownParty : parentParty;

    if (cand.name.length() == 0) continue;

    out.totalCandidateVotes += cand.votes;
    insertTopCandidate(out.candidates, out.candidateCount, cand);
  }
}

// ============================================================
// PARSER STREAMING EA20
// ============================================================

bool parseResultHttp(
  HTTPClient& http,
  RaceData& out
) {
  WiFiClient* stream = http.getStreamPtr();
  if (!stream) return false;

  HttpReader reader(http, stream, http.getSize());
  String currentParty;
  uint16_t sourceOrder = 0;

  while (true) {
    int ci = reader.read();
    if (ci < 0) break;
    if ((char)ci != '"') continue;

    String key = readJsonString(reader);

    int colon = nextNonSpace(reader);
    if (colon != ':') continue;

    int valueStart = nextNonSpace(reader);
    if (valueStart < 0) break;

    if (key == "cand" && valueStart == '[') {
      parseCandidateArray(reader, currentParty, out, sourceOrder);
      continue;
    }

    if (valueStart == '"') {
      String value = readJsonString(reader);

      if (key == "sg") currentParty = value;
      else if (key == "dt" && out.date.length() == 0) out.date = value;
      else if (key == "ht" && out.time.length() == 0) out.time = value;
      else if (key == "dv" && out.divulgacao.length() == 0) out.divulgacao = value;
      else if (key == "and" && out.andamento.length() == 0) out.andamento = value;
      else if (key == "pst" && out.sectionPercent == "0,00") out.sectionPercent = value;
    }
  }

  out.countStarted = out.totalCandidateVotes > 0;
  out.hasData = true;
  out.stale = false;

  return true;
}

// ============================================================
// FETCH RESULTADO
// ============================================================

RaceData raceCopy(uint8_t slot) {
  RaceData out;
  xSemaphoreTake(raceMutex, portMAX_DELAY);
  if (slot < RACE_SLOTS) out = races[slot];
  xSemaphoreGive(raceMutex);
  return out;
}

bool candidateUiEquals(
  const Candidate& a,
  const Candidate& b
) {
  return
    a.number == b.number &&
    a.name == b.name &&
    a.party == b.party &&
    a.percentage == b.percentage &&
    a.sqcand == b.sqcand &&
    a.votes == b.votes &&
    a.sourceOrder == b.sourceOrder;
}

bool raceUiEquals(
  const RaceData& a,
  const RaceData& b
) {
  if (
    a.title != b.title ||
    a.shortTitle != b.shortTitle ||
    a.office != b.office ||
    a.usePhotos != b.usePhotos ||
    a.candidateCount != b.candidateCount ||
    a.selected != b.selected ||
    a.scroll != b.scroll ||
    a.hasData != b.hasData ||
    a.stale != b.stale ||
    a.countStarted != b.countStarted ||
    a.httpCode != b.httpCode ||
    a.sectionPercent != b.sectionPercent ||
    a.andamento != b.andamento ||
    a.divulgacao != b.divulgacao ||
    a.date != b.date ||
    a.time != b.time ||
    a.error != b.error ||
    a.totalCandidateVotes != b.totalCandidateVotes
  ) {
    return false;
  }

  for (uint8_t i = 0; i < a.candidateCount; i++) {
    if (!candidateUiEquals(a.candidates[i], b.candidates[i]))
      return false;
  }

  return true;
}

void markRaceUiDirty(uint8_t slot) {
  if (slot >= RACE_SLOTS)
    return;

  xSemaphoreTake(raceMutex, portMAX_DELAY);
  uiRaceDirtyMask |= (1U << slot);
  xSemaphoreGive(raceMutex);
}

void markStatusUiDirty() {
  xSemaphoreTake(raceMutex, portMAX_DELAY);
  uiStatusDirty = true;
  xSemaphoreGive(raceMutex);
}

uint8_t takeRaceUiDirtyMask() {
  xSemaphoreTake(raceMutex, portMAX_DELAY);
  uint8_t mask = uiRaceDirtyMask;
  uiRaceDirtyMask = 0;
  xSemaphoreGive(raceMutex);
  return mask;
}

bool takeStatusUiDirty() {
  xSemaphoreTake(raceMutex, portMAX_DELAY);
  bool dirty = uiStatusDirty;
  uiStatusDirty = false;
  xSemaphoreGive(raceMutex);
  return dirty;
}

void setRace(uint8_t slot, const RaceData& value) {
  xSemaphoreTake(raceMutex, portMAX_DELAY);

  if (slot < RACE_SLOTS) {
    bool changed = !raceUiEquals(races[slot], value);
    races[slot] = value;

    if (changed)
      uiRaceDirtyMask |= (1U << slot);
  }

  xSemaphoreGive(raceMutex);
}

bool fetchRace(
  const RuntimeSelection& s,
  uint8_t slot
) {
  uint8_t office = officeForSlot(slot, s.ufIndex);

  RaceData previous = raceCopy(slot);
  RaceData out;
  out.office = office;
  out.selected = previous.selected;
  out.scroll = previous.scroll;
  out.shortTitle = titleForSlot(slot, s.ufIndex);
  out.title = out.shortTitle;
  out.usePhotos = photosForSlot(slot);

  auto publish = [&](const RaceData& value) {
    RuntimeSelection now = getRuntimeSelection();
    if (now.revision == s.revision) setRace(slot, value);
  };

  String url = resultUrl(s, office);

  if (url.length() == 0) {
    if (previous.hasData) {
      previous.stale = true;
      previous.error = "Cargo indisponivel";
      publish(previous);
    } else {
      out.error = "Cargo indisponivel";
      publish(out);
    }
    return false;
  }

  Serial.println();
  Serial.println(out.shortTitle);
  Serial.println(url);

  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient http;
  http.setConnectTimeout(10000);
  http.setTimeout(45000);
  http.setReuse(false);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);

  if (!http.begin(client, url)) {
    if (previous.hasData) {
      previous.stale = true;
      previous.error = "Falha HTTPS";
      publish(previous);
    } else {
      out.error = "Falha HTTPS";
      publish(out);
    }
    return false;
  }

  http.addHeader("Accept", "application/json");
  http.addHeader("Accept-Encoding", "identity");

  int code = http.GET();
  out.httpCode = code;

  Serial.print("HTTP ");
  Serial.println(code);

  if (code != HTTP_CODE_OK) {
    String errText = (s.round == 2 && code == HTTP_CODE_NOT_FOUND)
      ? String("TURNO 2 INDISPONIVEL")
      : String("HTTP ") + String(code);

    http.end();

    if (previous.hasData) {
      previous.stale = true;
      previous.error = errText;
      previous.httpCode = code;
      publish(previous);
    } else {
      out.error = errText;
      publish(out);
    }
    return false;
  }

  bool ok = parseResultHttp(http, out);
  http.end();

  if (!ok) {
    if (previous.hasData) {
      previous.stale = true;
      previous.error = "Falha no parser";
      publish(previous);
    } else {
      out.error = "Falha no parser";
      publish(out);
    }
    return false;
  }

  if (out.candidateCount == 0) {
    out.error = "0 candidatos no JSON";
  }

  if (out.candidateCount == 0) {
    out.selected = 0;
    out.scroll = 0;
  } else {
    if (out.selected >= out.candidateCount) out.selected = 0;
    uint8_t visible = out.countStarted ? 5 : 4;
    uint8_t maxScroll = out.candidateCount > visible
      ? out.candidateCount - visible
      : 0;
    if (out.scroll > maxScroll) out.scroll = maxScroll;
  }

  Serial.print("Top candidatos mantidos: ");
  Serial.println(out.candidateCount);

  publish(out);
  return true;
}

void fetchAllActiveRaces() {
  RuntimeSelection s = getRuntimeSelection();

  if (s.round == 1) {
    for (uint8_t slot = 0; slot < 4; slot++) {
      RuntimeSelection now = getRuntimeSelection();

      if (now.revision != s.revision)
        return;

      fetchRace(s, slot);
    }

    return;
  }

  uint8_t count = round2RefreshCount(s);

  for (uint8_t i = 0; i < count; i++) {
    RuntimeSelection now = getRuntimeSelection();

    if (now.revision != s.revision)
      return;

    uint8_t slot = round2RefreshSlot(s, i);

    if (slot < RACE_SLOTS)
      fetchRace(s, slot);
  }

  // Senado e deputados nunca existem no 2o turno.
  setRace(2, RaceData());
  setRace(3, RaceData());

  // Tambem limpa Presidente/Governador quando a respectiva disputa
  // nao existe no 2o turno.
  if (!(s.profile.federal2Available && s.profile.federal2 > 0))
    setRace(0, RaceData());

  if (!stateSecondRoundAvailableForUf(s.profile, s.ufIndex))
    setRace(1, RaceData());
}

// ============================================================
// CATALOGO ONLINE: ele-c.json
// ============================================================

void refreshElectionCatalogOnline() {
  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient http;
  http.setConnectTimeout(10000);
  http.setTimeout(25000);
  http.setReuse(false);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);

  if (!http.begin(client, TSE_CONFIG_URL))
    return;

  http.addHeader("Accept", "application/json");
  http.addHeader("Accept-Encoding", "identity");

  int code = http.GET();

  if (code != HTTP_CODE_OK) {
    http.end();
    return;
  }

  JsonDocument filter;

  filter["pl"][0]["cd"] = true;
  filter["pl"][0]["c"] = true;
  filter["pl"][0]["dt"] = true;

  filter["pl"][0]["e"][0]["cd"] = true;
  filter["pl"][0]["e"][0]["cdt2"] = true;
  filter["pl"][0]["e"][0]["t"] = true;
  filter["pl"][0]["e"][0]["tp"] = true;

  // Necessario para descobrir em quais UFs ha 2o turno estadual.
  filter["pl"][0]["e"][0]["abr"][0]["cd"] = true;

  JsonDocument doc;

  DeserializationError err =
    deserializeJson(
      doc,
      *http.getStreamPtr(),
      DeserializationOption::Filter(filter),
      DeserializationOption::NestingLimit(32)
    );

  http.end();

  if (err) {
    Serial.print("ele-c.json JSON: ");
    Serial.println(err.c_str());
    return;
  }

  for (JsonObject pleito : doc["pl"].as<JsonArray>()) {
    String cycle = jsonString(pleito["c"]);

    if (!cycle.startsWith("ele") || cycle.length() < 7)
      continue;

    int year = cycle.substring(3, 7).toInt();

    // v2.1.0: aceita somente o ciclo eleitoral configurado.
    if (year != APP_ELECTION_YEAR || cycle != APP_ELECTION_CYCLE)
      continue;

    for (JsonObject e : pleito["e"].as<JsonArray>()) {
      int type = jsonString(e["tp"]).toInt();
      int round = jsonString(e["t"]).toInt();

      if (type != 8 && type != 1)
        continue;

      if (round != 1 && round != 2)
        continue;

      ElectionProfile p;

      p.year = year;
      p.pleito = jsonString(pleito["cd"]).toInt();
      copyText(p.cycle, sizeof(p.cycle), cycle);

      int code1 = jsonString(e["cd"]).toInt();
      int planned2 = jsonString(e["cdt2"]).toInt();

      if (round == 1) {
        copyText(
          p.firstDate,
          sizeof(p.firstDate),
          jsonString(pleito["dt"])
        );

        if (type == 8) {
          p.federal1 = code1;

          // Guardamos o codigo previsto para uso futuro, mas NAO
          // marcamos como disponivel apenas por existir cdt2.
          if (planned2 > 0)
            p.federal2 = planned2;
        }
        else {
          p.state1 = code1;

          if (planned2 > 0)
            p.state2 = planned2;
        }
      }
      else {
        copyText(
          p.secondDate,
          sizeof(p.secondDate),
          jsonString(pleito["dt"])
        );

        // Somente uma entrada explicitamente t=2 torna o segundo
        // turno realmente disponivel.
        if (type == 8) {
          p.federal2 = code1;
          p.federal2Available = code1 > 0;
        }
        else {
          p.state2 = code1;
          p.state2Available = code1 > 0;

          uint32_t mask = 0;
          bool allBrazil = false;

          for (JsonObject abr : e["abr"].as<JsonArray>()) {
            String uf = jsonString(abr["cd"]);
            uf.toUpperCase();

            if (uf == "BR") {
              allBrazil = true;
              break;
            }

            uint8_t idx = findUfIndex(uf);

            if (idx < UF_COUNT)
              mask |= (1UL << idx);
          }

          if (allBrazil) {
            mask = 0;

            for (uint8_t i = 0; i < UF_COUNT; i++)
              mask |= (1UL << i);
          }

          p.state2UfMask = mask;
        }
      }

      mergeElectionProfile(p);
    }
  }

  catalogUiDirty = true;

  Serial.println("Catalogo eleitoral TSE atualizado.");

  ElectionProfile dbg;

  if (getElectionProfile(APP_ELECTION_YEAR, dbg)) {
    Serial.print("Perfil ");
    Serial.print(APP_ELECTION_YEAR);
    Serial.print(" | T1 federal=");
    Serial.print(dbg.federal1);
    Serial.print(" estadual=");
    Serial.print(dbg.state1);
    Serial.print(" | T2 publicado=");
    Serial.println(
      secondRoundAvailableForUf(dbg, activeUf)
      ? "SIM"
      : "NAO"
    );
  }

  // Somente depois de recebermos com sucesso o catalogo oficial
  // decidimos se um turno 2 salvo por uma versao anterior e valido.
  ElectionProfile checked;

  if (getElectionProfile(APP_ELECTION_YEAR, checked)) {
    bool mustFallback = false;

    xSemaphoreTake(selectionMutex, portMAX_DELAY);

    if (activeRound == 2 &&
        !secondRoundAvailableForUf(checked, activeUf)) {
      activeRound = 1;
      selectionRevision++;
      mustFallback = true;
    }

    xSemaphoreGive(selectionMutex);

    if (mustFallback) {
      prefs.putUChar("round", 1);
      clearRaceData(true);
      refreshRequested = true;

      Serial.println("Turno 2 ainda nao publicado para este perfil; usando turno 1.");
    }
  }
}

// ============================================================
// FOTO BACKGROUND
// ============================================================

void requestPhotoFor(
  const RuntimeSelection& s,
  uint8_t slot,
  uint8_t office,
  const Candidate& c,
  bool notifyUi
) {
  if (!littleFsReady || c.sqcand.length() == 0) return;

  String path = photoPath(s, office, c);
  String key = String(s.profile.year) + ":" + String(s.round) + ":" +
               String(UFS[s.ufIndex].code) + ":" + c.sqcand;

  if (photoCached(path) || photoMarkedMissing(key)) return;

  xSemaphoreTake(photoMutex, portMAX_DELAY);
  if (!photoRequest.pending) {
    photoRequest.notifyUi = notifyUi;
    photoRequest.slot = slot;
    photoRequest.url = photoUrl(s, office, c);
    photoRequest.path = path;
    photoRequest.key = key;
    photoRequest.revision = s.revision;
    photoRequest.pending = true;
  }
  xSemaphoreGive(photoMutex);
}

void processPhotoRequest() {
  PhotoRequest req;

  xSemaphoreTake(photoMutex, portMAX_DELAY);
  if (!photoRequest.pending) {
    xSemaphoreGive(photoMutex);
    return;
  }
  req = photoRequest;
  photoRequest.pending = false;
  xSemaphoreGive(photoMutex);

  RuntimeSelection s = getRuntimeSelection();
  if (s.revision != req.revision) return;
  if (photoCached(req.path)) return;

  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient http;
  http.setConnectTimeout(10000);
  http.setTimeout(30000);
  http.setReuse(false);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);

  if (!http.begin(client, req.url)) return;

  http.addHeader("Accept", "image/jpeg");
  http.addHeader("Accept-Encoding", "identity");

  int code = http.GET();

  if (code == HTTP_CODE_NOT_FOUND) {
    markPhotoMissing(req.key);
    http.end();
    return;
  }

  if (code != HTTP_CODE_OK) {
    http.end();
    return;
  }

  int len = http.getSize();
  size_t required = len > 0 ? (size_t)len : 200000UL;

  if (!ensurePhotoSpace(required)) {
    http.end();
    return;
  }

  String tmp = "/photo.tmp";
  files.remove(LittleFS, tmp);

  File out = LittleFS.open(tmp, FILE_WRITE);
  if (!out) {
    http.end();
    return;
  }

  int written = http.writeToStream(&out);
  out.close();
  http.end();

  if (written <= 0 || files.getFileSize(LittleFS, tmp) < 500) {
    files.remove(LittleFS, tmp);
    return;
  }

  files.remove(LittleFS, req.path);
  if (!files.move(LittleFS, tmp, req.path)) {
    files.remove(LittleFS, tmp);
  } else if (req.notifyUi) {
    // Somente a foto solicitada pela pagina visivel pede redesenho.
    // O pre-cache da proxima foto permanece totalmente silencioso.
    markRaceUiDirty(req.slot);
  }
}

// ============================================================
// SCAN WIFI BACKGROUND
// ============================================================

void performWifiScan() {
  String json = esWifi.scanWifi(true);

  JsonDocument doc;
  if (deserializeJson(doc, json)) {
    scanReady = true;
    return;
  }

  ScannedNetwork temp[MAX_SCAN_NETWORKS];
  uint8_t count = 0;

  for (JsonObject n : doc["scanwifi"].as<JsonArray>()) {
    String ssid = jsonString(n["ssid"]);
    if (ssid.length() == 0) continue;

    bool duplicate = false;
    for (uint8_t i = 0; i < count; i++)
      if (temp[i].ssid == ssid) duplicate = true;
    if (duplicate) continue;

    temp[count].ssid = ssid;
    temp[count].rssi = n["rssi"] | 0;
    temp[count].secure = n["secure"] | false;

    count++;
    if (count >= MAX_SCAN_NETWORKS) break;
  }

  xSemaphoreTake(scanMutex, portMAX_DELAY);
  scanCount = count;
  for (uint8_t i = 0; i < count; i++) scanList[i] = temp[i];
  xSemaphoreGive(scanMutex);

  scanReady = true;
}

// ============================================================
// NETWORK WORKER
// ============================================================

void networkTask(void* parameter) {
  bool refreshCycleActive = false;
  uint8_t refreshSlot = 0;
  RuntimeSelection refreshSelection;

  for (;;) {
    // Durante verificacao/instalacao OTA, o worker de rede fica em repouso
    // para nao disputar HTTPS, RAM ou banda com o atualizador.
    if (otaModeActive) {
      networkWorkerState = NET_IDLE;
      vTaskDelay(pdMS_TO_TICKS(30));
      continue;
    }

    // Scan Wi-Fi e uma acao solicitada explicitamente pelo usuario.
    if (scanRequested) {
      scanRequested = false;
      scanReady = false;
      networkWorkerState = NET_SCAN;

      performWifiScan();

      networkWorkerState = NET_IDLE;
    }
    else if (WiFi.status() == WL_CONNECTED) {
      // Foto da tela atual tem prioridade sobre o proximo JSON.
      bool photoPending = false;

      xSemaphoreTake(photoMutex, portMAX_DELAY);
      photoPending = photoRequest.pending;
      xSemaphoreGive(photoMutex);

      if (photoPending) {
        networkWorkerState = NET_PHOTO;

        processPhotoRequest();

        networkWorkerState = NET_IDLE;
      }
      else if (catalogRequested) {
        catalogRequested = false;
        networkWorkerState = NET_CONFIG;

        refreshElectionCatalogOnline();

        networkWorkerState = NET_IDLE;
      }
      else {
        // Comeca um ciclo de atualizacao. A diferenca para a v2.0.0
        // e que cada cargo e consultado separadamente. Entre dois
        // cargos o worker volta ao inicio do loop e pode atender
        // imediatamente a foto que a interface acabou de solicitar.
        if (refreshRequested && !refreshCycleActive) {
          refreshRequested = false;
          refreshRunning = true;
          refreshSelection = getRuntimeSelection();
          refreshSlot = 0;
          refreshCycleActive = true;

          // A pagina STATUS pode refletir que o ciclo iniciou.
          // As paginas de cargos permanecem com os ultimos dados validos
          // ate que o respectivo cargo receba uma resposta nova.
          markStatusUiDirty();
        }

        if (refreshCycleActive) {
          RuntimeSelection now = getRuntimeSelection();

          if (now.revision != refreshSelection.revision) {
            refreshCycleActive = false;
            refreshRunning = false;
            networkWorkerState = NET_IDLE;
            markStatusUiDirty();
          }
          else {
            uint8_t raceCount =
              refreshSelection.round == 2
              ? round2RefreshCount(refreshSelection)
              : 4;

            if (refreshSlot < raceCount) {
              networkWorkerState = NET_RESULTS;

              uint8_t slot =
                refreshSelection.round == 2
                ? round2RefreshSlot(refreshSelection, refreshSlot)
                : refreshSlot;

              if (slot < RACE_SLOTS) {
                fetchRace(
                  refreshSelection,
                  slot
                );
              }

              refreshSlot++;

              networkWorkerState = NET_IDLE;
            }
            else {
              refreshCycleActive = false;
              refreshRunning = false;
              networkWorkerState = NET_IDLE;
              markStatusUiDirty();
            }
          }
        }
      }
    }

    vTaskDelay(pdMS_TO_TICKS(20));
  }
}

// ============================================================
// ABERTURA / QR CODE ES DEVELOPER
// ============================================================

void renderStartupSplash() {
  display.fillScreen(TFT_WHITE);

  const int16_t x = (160 - ESDEVELOPER_QR_WIDTH) / 2;
  const int16_t y = (128 - ESDEVELOPER_QR_HEIGHT) / 2;

  // A matriz do ESDeveloper_QR.h esta em RGB565.
  // TFT_eSPI/ES_TFT usa swap de bytes para esse tipo de bitmap.
  display.setSwapBytes(true);
  display.pushImage(
    x,
    y,
    ESDEVELOPER_QR_WIDTH,
    ESDEVELOPER_QR_HEIGHT,
    ESDEVELOPER_QR_RGB565
  );
  display.setSwapBytes(false);
}

void renderPresentationSplash() {
  display.setSwapBytes(true);
  display.pushImage(
    0,
    0,
    ES32LAB_ELEICOES_SPLASH_WIDTH,
    ES32LAB_ELEICOES_SPLASH_HEIGHT,
    ES32LAB_ELEICOES_SPLASH_RGB565
  );
  display.setSwapBytes(false);
}

void drawUiBackground() {
  display.setSwapBytes(true);
  display.pushImage(
    0,
    0,
    ES32LAB_UI_BG_WIDTH,
    ES32LAB_UI_BG_HEIGHT,
    ES32LAB_UI_BG_RGB565
  );
  display.setSwapBytes(false);
}


// ============================================================
// UI BASE
// ============================================================

void drawClockOnly() {
  display.fillRect(126, 0, 34, 18, C_HEADER);
  display.setTextColor(TFT_YELLOW, C_HEADER);
  display.drawRightScreenString(clockText(), 5, 1);
}

void drawHeader(const String& title) {
  display.fillRect(0, 0, 160, 18, C_HEADER);
  display.drawFastHLine(0, 18, 160, TFT_CYAN);

  display.setTextColor(TFT_WHITE, C_HEADER);
  display.drawString(fitText(title, 17), 3, 5, 1);

  drawClockOnly();
}

void drawFooterArrow(int x, int centerY, char direction, uint16_t color) {
  switch (direction) {
    case '^':
      display.fillTriangle(
        x + 3, centerY - 3,
        x,     centerY + 1,
        x + 6, centerY + 1,
        color
      );
      break;

    case 'v':
      display.fillTriangle(
        x,     centerY - 1,
        x + 6, centerY - 1,
        x + 3, centerY + 3,
        color
      );
      break;

    case '<':
      display.fillTriangle(
        x,     centerY,
        x + 4, centerY - 4,
        x + 4, centerY + 4,
        color
      );
      break;

    case '>':
      display.fillTriangle(
        x + 5, centerY,
        x + 1, centerY - 4,
        x + 1, centerY + 4,
        color
      );
      break;
  }
}

int footerVisualWidth(const String& text) {
  int width = 0;

  for (size_t i = 0; i < text.length(); i++) {
    char c = text[i];

    // Pares usados historicamente na interface:
    // ^v = cima/baixo e <> = esquerda/direita.
    if (i + 1 < text.length()) {
      char n = text[i + 1];

      if (c == '^' && n == 'v') {
        width += 14;
        i++;
        continue;
      }

      if (c == '<' && n == '>') {
        width += 14;
        i++;
        continue;
      }
    }

    if (c == '^' || c == 'v' || c == '<' || c == '>') {
      width += 7;
    } else {
      width += display.textWidth(String(c), 1);
    }
  }

  return width;
}

void drawFooter(const String& text) {
  display.fillRect(0, 115, 160, 13, C_PANEL);
  display.drawFastHLine(0, 114, 160, TFT_CYAN);

  // Preserva o 'v' minusculo do marcador ^v. Assim a letra V real
  // de palavras como VOLTA/VAZIO nunca e confundida com uma seta.
  String shown = asciiText(text);
  if (shown.length() > 26)
    shown = shown.substring(0, 25) + ".";
  int totalWidth = footerVisualWidth(shown);
  int x = max(2, (160 - totalWidth) / 2);
  const int textY = 118;
  const int arrowY = 122;

  display.setTextColor(TFT_CYAN, C_PANEL);

  for (size_t i = 0; i < shown.length(); i++) {
    char c = shown[i];

    if (i + 1 < shown.length()) {
      char n = shown[i + 1];

      if (c == '^' && n == 'v') {
        drawFooterArrow(x,     arrowY, '^', TFT_YELLOW);
        drawFooterArrow(x + 7, arrowY, 'v', TFT_YELLOW);
        x += 14;
        i++;
        continue;
      }

      if (c == '<' && n == '>') {
        drawFooterArrow(x,     arrowY, '<', TFT_YELLOW);
        drawFooterArrow(x + 7, arrowY, '>', TFT_YELLOW);
        x += 14;
        i++;
        continue;
      }
    }

    if (c == '^' || c == 'v' || c == '<' || c == '>') {
      drawFooterArrow(x, arrowY, c, TFT_YELLOW);
      x += 7;
      continue;
    }

    String one(c);
    display.drawString(one, x, textY, 1);
    x += display.textWidth(one, 1);
  }
}

void drawContentPanel(int x = 2, int y = 22, int w = 156, int h = 89) {
  // Painel solido proposital: o background continua aparente nas bordas,
  // mas a informacao dinamica ganha contraste na pequena tela 160x128.
  display.fillRoundRect(x, y, w, h, 4, C_BG);
  display.drawRoundRect(x, y, w, h, 4, C_LINE);
}

void drawRow(
  int y,
  const String& left,
  const String& right,
  bool selected
) {
  const int x = 2;
  const int w = 156;
  const int h = 16;
  const int rightPadding = 6;

  uint16_t bg = selected ? C_SELECT : C_PANEL;
  uint16_t fg = selected ? TFT_YELLOW : TFT_WHITE;
  uint16_t border = selected ? TFT_CYAN : C_LINE;

  display.fillRoundRect(x, y, w, h, 3, bg);
  display.drawRoundRect(x, y, w, h, 3, border);

  display.setTextColor(fg, bg);
  display.drawString(fitText(left, 18), x + 3, y + 4, 1);

  if (right.length()) {
    String shown = fitText(right, 9);
    int rw = display.textWidth(shown, 1);
    int rx = x + w - rightPadding - rw;
    if (rx < x + 70) rx = x + 70;
    display.drawString(shown, rx, y + 4, 1);
  }
}

void drawRowRaw(
  int y,
  const String& left,
  const String& right,
  bool selected
) {
  const int x = 2;
  const int w = 156;
  const int h = 16;
  const int rightPadding = 6;

  uint16_t bg = selected ? C_SELECT : C_PANEL;
  uint16_t fg = selected ? TFT_YELLOW : TFT_WHITE;
  uint16_t border = selected ? TFT_CYAN : C_LINE;

  display.fillRoundRect(x, y, w, h, 3, bg);
  display.drawRoundRect(x, y, w, h, 3, border);

  display.setTextColor(fg, bg);
  display.drawString(fitRawText(left, 18), x + 3, y + 4, 1);

  if (right.length()) {
    String shown = fitRawText(right, 9);
    int rw = display.textWidth(shown, 1);
    int rx = x + w - rightPadding - rw;
    if (rx < x + 70) rx = x + 70;
    display.drawString(shown, rx, y + 4, 1);
  }
}

void drawElectionField(
  int y,
  const String& label,
  const String& value,
  bool selected
) {
  const int x = 8;
  const int w = 144;
  const int h = 17;
  const int pad = 6;

  uint16_t bg = selected ? C_SELECT : C_PANEL;
  uint16_t fg = selected ? TFT_YELLOW : TFT_WHITE;
  uint16_t border = selected ? TFT_CYAN : C_LINE;

  display.fillRoundRect(x, y, w, h, 4, bg);
  display.drawRoundRect(x, y, w, h, 4, border);

  display.setTextColor(fg, bg);
  display.drawString(fitText(label, 13), x + pad, y + 4, 1);

  String shown = fitText(value, 5);
  int rw = display.textWidth(shown, 1);
  display.drawString(shown, x + w - pad - rw, y + 4, 1);
}

void drawElectionAction(
  int y,
  const String& label,
  bool selected
) {
  const int x = 18;
  const int w = 124;
  const int h = 17;

  uint16_t bg = selected ? C_SELECT : C_PANEL;
  uint16_t fg = selected ? TFT_YELLOW : TFT_WHITE;
  uint16_t border = selected ? TFT_CYAN : C_LINE;

  display.fillRoundRect(x, y, w, h, 4, bg);
  display.drawRoundRect(x, y, w, h, 4, border);
  display.setTextColor(fg, bg);
  display.drawCentreScreenString(fitText(label, 18), y + 4, 1);
}

// ============================================================
// MENU PRINCIPAL
// ============================================================

void renderMain() {
  drawUiBackground();
  drawHeader("MENU PRINCIPAL");

  const char* items[] = {
    "INICIAR RESULTADOS",
    "ELEICAO",
    "CONFIGURACOES"
  };

  for (uint8_t i = 0; i < 3; i++)
    drawRow(28 + i * 22, items[i], "", i == menuIndex);

  display.setTextColor(TFT_LIGHTGREY, C_BG);
  display.drawCentreScreenString(
    String(APP_ELECTION_YEAR) + " | TURNO " + String(activeRound) + " | " +
    UFS[activeUf].code,
    98,
    1
  );

  drawFooter("^v SELECIONA  OK ENTRA");
}

// ============================================================
// EDITOR ELEICAO
// ============================================================

void beginElectionEditor() {
  editUf = profileWasSaved ? activeUf : findUfIndex("SP");
  editRound = profileWasSaved ? activeRound : 1;

  ElectionProfile p;

  if (getElectionProfile(APP_ELECTION_YEAR, p) &&
      editRound == 2 &&
      !secondRoundAvailableForUf(p, editUf)) {
    editRound = 1;
  }

  electionEditRow = 0;
}

void renderElectionEditor() {
  drawUiBackground();
  drawHeader("ELEICAO " + String(APP_ELECTION_YEAR));
  drawContentPanel(5, 23, 150, 88);

  drawElectionField(28, "TURNO", String(editRound), electionEditRow == 0);
  drawElectionField(49, "UF", UFS[editUf].code, electionEditRow == 1);

  ElectionProfile p;
  bool round2Available =
    getElectionProfile(APP_ELECTION_YEAR, p) &&
    secondRoundAvailableForUf(p, editUf);

  uint16_t msgColor = round2Available ? TFT_GREEN : TFT_YELLOW;
  display.setTextColor(msgColor, C_BG);
  display.drawCentreScreenString(
    round2Available ? "TURNO 2 DISPONIVEL" : "TURNO 2 INDISPONIVEL",
    73,
    1
  );

  drawElectionAction(91, "SALVAR E INICIAR", electionEditRow == 2);
  drawFooter("^v CAMPO  <> ALTERA  OK");
}

// ============================================================
// CONFIGURACOES
// ============================================================

void drawSettingsRow(
  int y,
  const String& label,
  bool selected
) {
  const int x = 7;
  const int w = 146;
  const int h = 14;

  uint16_t bg = selected ? C_SELECT : C_PANEL;
  uint16_t fg = selected ? TFT_YELLOW : TFT_WHITE;
  uint16_t border = selected ? TFT_CYAN : C_LINE;

  display.fillRoundRect(x, y, w, h, 3, bg);
  display.drawRoundRect(x, y, w, h, 3, border);
  display.setTextColor(fg, bg);
  display.drawCentreScreenString(fitText(label, 22), y + 3, 1);
}

void renderSettings() {
  drawUiBackground();
  drawHeader("CONFIGURACOES");
  drawContentPanel(2, 20, 156, 94);

  const char* items[] = {
    "WI-FI",
    "INTERVALO TSE",
    "ATUALIZAR SISTEMA",
    "LIMPAR CACHE",
    "SOBRE",
    "VOLTAR"
  };

  for (uint8_t i = 0; i < 6; i++)
    drawSettingsRow(22 + i * 15, items[i], i == settingsIndex);

  drawFooter("^v SELECIONA  OK ENTRA");
}

// ============================================================
// MENU WIFI
// ============================================================

void renderWifiConnectionSuccess() {
  drawUiBackground();
  drawHeader("WI-FI");
  drawContentPanel(5, 26, 150, 80);

  display.setTextColor(TFT_GREEN, C_BG);
  display.drawCentreScreenString("CONEXAO BEM-SUCEDIDA", 38, 1);

  display.setTextColor(TFT_WHITE, C_BG);
  display.drawCentreScreenString(
    fitRawText(wifiSuccessSsid, 23),
    58,
    1
  );

  if (wifiSuccessIp.length()) {
    display.setTextColor(TFT_LIGHTGREY, C_BG);
    display.drawCentreScreenString(
      "IP " + wifiSuccessIp,
      74,
      1
    );
  }

  display.setTextColor(TFT_CYAN, C_BG);
  display.drawCentreScreenString("RETORNANDO AO MENU...", 92, 1);
  drawFooter("WI-FI CONECTADO");
}

uint8_t wifiMenuItemCount() {
  return wifiCount + 2; // buscar + voltar
}

void renderWifiMenu() {
  drawUiBackground();
  drawHeader("WI-FI");
  drawContentPanel(2, 21, 156, 91);

  display.setTextColor(
    WiFi.status() == WL_CONNECTED ? TFT_GREEN : TFT_YELLOW,
    C_BG
  );

  String status = WiFi.status() == WL_CONNECTED
    ? "OK: " + WiFi.SSID()
    : (wifiConnecting ? "CONECTANDO..." : "DESCONECTADO");

  display.drawString(fitRawText(status, 25), 4, 24, 1);

  uint8_t total = wifiMenuItemCount();
  uint8_t first = 0;
  if (wifiMenuIndex >= 4) first = wifiMenuIndex - 3;

  for (uint8_t row = 0; row < 4; row++) {
    uint8_t idx = first + row;
    if (idx >= total) break;

    String itemText;
    String right;

    if (idx < wifiCount) {
      itemText = wifiList[idx].ssid;
      right =
        (WiFi.status() == WL_CONNECTED && WiFi.SSID() == itemText) ? "*" : "";
    } else if (idx == wifiCount) {
      itemText = "BUSCAR NOVA REDE";
    } else {
      itemText = "VOLTAR";
    }

    drawRowRaw(37 + row * 18, itemText, right, idx == wifiMenuIndex);
  }

  drawFooter("^v MOVE OK CONECTA > APAGA");
}

// ============================================================
// WIFI SCAN
// ============================================================

void renderWifiScan() {
  drawUiBackground();
  drawHeader("BUSCAR WI-FI");
  drawContentPanel(2, 22, 156, 89);

  if (!scanReady) {
    display.setTextColor(TFT_YELLOW, C_BG);
    display.drawCentreScreenString("BUSCANDO REDES...", 52, 1);
    display.setTextColor(TFT_LIGHTGREY, C_BG);
    display.drawCentreScreenString("AGUARDE", 69, 1);
    drawFooter("< VOLTAR");
    return;
  }

  uint8_t localCount;
  ScannedNetwork local[MAX_SCAN_NETWORKS];

  xSemaphoreTake(scanMutex, portMAX_DELAY);
  localCount = scanCount;
  for (uint8_t i = 0; i < localCount; i++) local[i] = scanList[i];
  xSemaphoreGive(scanMutex);

  if (localCount == 0) {
    display.setTextColor(TFT_YELLOW, C_BG);
    display.drawCentreScreenString("NENHUMA REDE ENCONTRADA", 52, 1);
    display.setTextColor(TFT_LIGHTGREY, C_BG);
    display.drawCentreScreenString("TENTE NOVAMENTE", 69, 1);
    drawFooter("< VOLTAR");
    return;
  }

  if (scanIndex >= localCount) scanIndex = 0;

  // Quatro linhas deixam espaco visual entre a lista e o rodape.
  uint8_t first = scanIndex >= 4 ? scanIndex - 3 : 0;

  for (uint8_t row = 0; row < 4; row++) {
    uint8_t idx = first + row;
    if (idx >= localCount) break;

    String right = String(local[idx].rssi) + "%";
    if (local[idx].secure) right += "*";

    drawRowRaw(27 + row * 20, local[idx].ssid, right, idx == scanIndex);
  }

  drawFooter("^v MOVE  OK SELEC  < VOLTA");
}

// ============================================================
// EDITOR DE SENHA
// ============================================================

const char* PASSWORD_GROUPS[] = {
  "abcdefghijklmnopqrstuvwxyz",
  "ABCDEFGHIJKLMNOPQRSTUVWXYZ",
  "0123456789",
  "!@#$%&*()-_=+.,;:?/\\[]{}"
};
constexpr uint8_t PASSWORD_GROUP_COUNT = 4;

char selectedPasswordChar() {
  const char* group = PASSWORD_GROUPS[passwordGroup];
  size_t len = strlen(group);
  if (len == 0) return 'a';
  if (passwordCharIndex >= len) passwordCharIndex = 0;
  return group[passwordCharIndex];
}

String passwordGroupName() {
  if (passwordGroup == 0) return "abc";
  if (passwordGroup == 1) return "ABC";
  if (passwordGroup == 2) return "123";
  return "#!?";
}

void drawPasswordEntryLine() {
  // passwordValue contem somente caracteres ja confirmados.
  // passwordHasPendingChar indica se existe um caractere em edicao.
  // Quando nao existe caractere pendente, aparece apenas o cursor:
  // a proxima posicao esta realmente vazia.
  constexpr int fieldX = 43;
  constexpr int fieldY = 39;
  constexpr int fieldW = 113;
  constexpr int charWidth = 6;
  constexpr uint8_t maxCommittedVisible = 18;

  display.fillRoundRect(fieldX - 2, fieldY, fieldW + 2, 18, 2, C_PANEL);
  display.drawRoundRect(fieldX - 2, fieldY, fieldW + 2, 18, 2, C_LINE);

  String committed;

  if (passwordValue.length() <= maxCommittedVisible) {
    committed = passwordValue;
  } else {
    committed =
      "<" +
      passwordValue.substring(
        passwordValue.length() - (maxCommittedVisible - 1)
      );
  }

  display.setTextColor(TFT_WHITE, C_PANEL);
  display.drawString(committed, fieldX, 42, 1);

  int cursorX = fieldX + committed.length() * charWidth;

  if (cursorX > fieldX + fieldW - charWidth)
    cursorX = fieldX + fieldW - charWidth;

  // O caractere so e desenhado quando o usuario realmente iniciou
  // a selecao dessa nova posicao usando UP/DOWN.
  if (passwordHasPendingChar) {
    display.setTextColor(TFT_YELLOW, C_PANEL);
    display.drawString(
      String(selectedPasswordChar()),
      cursorX,
      42,
      1
    );
  }

  // O sublinhado pisca tanto com caractere pendente quanto na posicao vazia.
  if (passwordCursorVisible) {
    display.drawFastHLine(
      cursorX,
      52,
      5,
      TFT_YELLOW
    );
  }
}

void resetPasswordCursor() {
  passwordCursorVisible = true;
  passwordCursorTimer.resetMillis();
}

void renderPassword() {
  drawUiBackground();
  drawHeader("SENHA WI-FI");
  drawContentPanel(2, 21, 156, 91);

  display.setTextColor(TFT_WHITE, C_BG);
  display.drawString("SSID:", 4, 25, 1);
  display.drawString(fitRawText(passwordSsid, 19), 38, 25, 1);

  display.drawString("SENHA:", 4, 43, 1);

  resetPasswordCursor();
  drawPasswordEntryLine();

  display.setTextColor(TFT_YELLOW, C_BG);

  String pendingText = passwordHasPendingChar
    ? String(selectedPasswordChar())
    : " ";

  display.drawCentreScreenString(
    "GRUPO " + passwordGroupName() +
    "  [" + pendingText + "]",
    65,
    1
  );

  display.setTextColor(TFT_LIGHTGREY, C_BG);
  display.drawCentreScreenString("CIMA/BAIXO MUDA", 83, 1);
  display.drawCentreScreenString("DIR CONFIRMA | ESQ APAGA", 98, 1);

  drawFooter("OK GRUPO | SEG OK SALVA");
}

void changePasswordChar(int delta) {
  const char* group = PASSWORD_GROUPS[passwordGroup];
  int len = strlen(group);

  if (len <= 0)
    return;

  // Primeira acao numa posicao vazia cria o caractere pendente.
  // UP comeca pelo primeiro caractere; DOWN pelo ultimo.
  if (!passwordHasPendingChar) {
    passwordCharIndex = delta < 0 ? len - 1 : 0;
    passwordHasPendingChar = true;
    return;
  }

  int pos = passwordCharIndex + delta;

  if (pos < 0) pos = len - 1;
  if (pos >= len) pos = 0;

  passwordCharIndex = pos;
}

// ============================================================
// UPDATE INTERVAL
// ============================================================

void renderUpdateInterval() {
  drawUiBackground();
  drawHeader("INTERVALO TSE");
  drawContentPanel(2, 23, 156, 88);

  display.setTextColor(TFT_WHITE, C_BG);
  display.drawCentreScreenString("INTERVALO TSE", 38, 1);

  display.setTextColor(TFT_YELLOW, C_BG);
  display.drawCentreScreenString(
    "<  " + String(POLL_OPTIONS_MS[pollOptionIndex] / 1000UL) + " s  >",
    58,
    2
  );

  display.setTextColor(TFT_LIGHTGREY, C_BG);
  display.drawCentreScreenString("30 / 60 / 90 / 120", 84, 1);

  drawFooter("<> ALTERA  OK SALVA");
}


// ============================================================
// ATUALIZACAO DO SISTEMA / OTA
// ============================================================

int versionPart(const String& version, uint8_t part) {
  int values[3] = {0, 0, 0};
  uint8_t current = 0;
  String number;

  for (size_t i = 0; i <= version.length() && current < 3; i++) {
    char c = i < version.length() ? version[i] : '.';

    if (c >= '0' && c <= '9') {
      number += c;
    }
    else if (c == '.' || i == version.length()) {
      values[current] = number.length() ? number.toInt() : 0;
      number = "";
      current++;
    }
    else {
      // Sufixos como -beta sao ignorados para a comparacao numerica.
      if (number.length() && current < 3) {
        values[current] = number.toInt();
      }
      break;
    }
  }

  return part < 3 ? values[part] : 0;
}

int compareVersions(const String& a, const String& b) {
  for (uint8_t i = 0; i < 3; i++) {
    int av = versionPart(a, i);
    int bv = versionPart(b, i);

    if (av < bv) return -1;
    if (av > bv) return 1;
  }

  return 0;
}

bool waitNetworkWorkerForOta(uint32_t timeoutMs = 10000UL) {
  otaModeActive = true;

  // Da ao worker de rede tempo para observar o bloqueio antes de
  // verificarmos NET_IDLE. Se ele ja estiver em uma requisicao,
  // aguardamos sua conclusao abaixo.
  delay(60);

  ES_TimeInterval waitTimer;
  waitTimer.resetMillis();

  while (networkWorkerState != NET_IDLE) {
    if (waitTimer.intervalMillis(timeoutMs)) {
      otaModeActive = false;
      return false;
    }

    delay(10);
  }

  return true;
}

void renderSystemUpdate() {
  drawUiBackground();
  drawHeader("ATUALIZACAO");
  drawContentPanel(4, 23, 152, 88);

  display.setTextColor(TFT_WHITE, C_BG);
  display.drawString("INSTALADA:", 9, 31, 1);
  display.drawString(String(APP_VERSION), 85, 31, 1);

  display.drawString("DISPONIVEL:", 9, 47, 1);

  String available = otaAvailableVersion.length()
    ? otaAvailableVersion
    : "--";

  display.drawString(fitRawText(available, 11), 85, 47, 1);

  if (otaUiState == OTA_UI_CHECKING) {
    display.setTextColor(TFT_CYAN, C_BG);
    display.drawCentreScreenString("CONSULTANDO GITHUB...", 68, 1);
    drawFooter("AGUARDE");
    return;
  }

  if (otaUiState == OTA_UI_AVAILABLE) {
    display.setTextColor(TFT_YELLOW, C_BG);
    display.drawCentreScreenString("ATUALIZACAO DISPONIVEL", 66, 1);

    if (otaRemoteSize > 0) {
      display.setTextColor(TFT_LIGHTGREY, C_BG);
      display.drawCentreScreenString(
        String(otaRemoteSize / 1024UL) + " KB",
        82,
        1
      );
    }

    drawFooter("< VOLTA  OK ATUALIZA");
    return;
  }

  if (otaUiState == OTA_UI_CURRENT) {
    display.setTextColor(TFT_GREEN, C_BG);
    display.drawCentreScreenString("SISTEMA ATUALIZADO", 70, 1);
    drawFooter("< VOLTA  OK VERIFICA");
    return;
  }

  if (otaUiState == OTA_UI_ERROR) {
    display.setTextColor(TFT_RED, C_BG);
    display.drawCentreScreenString(
      fitText(otaMessage.length() ? otaMessage : "ERRO AO CONSULTAR", 24),
      66,
      1
    );
    drawFooter("< VOLTA  OK TENTA NOVO");
    return;
  }

  display.setTextColor(TFT_CYAN, C_BG);
  display.drawCentreScreenString("VERIFICAR NOVA VERSAO", 69, 1);
  drawFooter("< VOLTA  OK VERIFICA");
}

void renderOtaProgress(int percent) {
  percent = constrain(percent, 0, 100);

  // Atualiza somente a regiao dinamica.
  display.fillRect(8, 52, 144, 54, C_PANEL);

  display.setTextColor(TFT_WHITE, C_PANEL);
  display.drawCentreScreenString("INSTALANDO FIRMWARE", 57, 1);

  display.drawRoundRect(13, 75, 134, 14, 2, TFT_CYAN);

  int fill = ((134 - 4) * percent) / 100;

  if (fill > 0)
    display.fillRect(15, 77, fill, 10, TFT_GREEN);

  display.setTextColor(TFT_YELLOW, C_PANEL);
  display.drawCentreScreenString(String(percent) + "%", 94, 1);
}

void otaFirmwareProgress(int current, int total) {
  if (total <= 0)
    return;

  int percent = (current * 100) / total;

  if (percent != 100 && percent < otaLastProgress + 5)
    return;

  otaLastProgress = percent;
  renderOtaProgress(percent);

  Serial.printf("OTA: %d%%\n", percent);
}

void checkFirmwareUpdate(bool renderUi = true, uint32_t timeoutMs = 15000UL) {
  otaUiState = OTA_UI_CHECKING;
  otaAvailableVersion = "";
  otaFirmwareUrl = OTA_FIRMWARE_FALLBACK_URL;
  otaExpectedSha256 = "";
  otaRemoteSize = 0;
  otaMessage = "";
  if (renderUi) renderSystemUpdate();

  if (WiFi.status() != WL_CONNECTED) {
    otaUiState = OTA_UI_ERROR;
    otaMessage = "SEM CONEXAO WI-FI";
    if (renderUi) renderSystemUpdate();
    return;
  }

  if (!waitNetworkWorkerForOta(timeoutMs)) {
    otaUiState = OTA_UI_ERROR;
    otaMessage = "REDE OCUPADA";
    if (renderUi) renderSystemUpdate();
    return;
  }

  WiFiClientSecure client;
  client.setInsecure();
  client.setTimeout(timeoutMs);

  HTTPClient http;
  http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);

  bool begun = http.begin(client, OTA_MANIFEST_URL);

  if (!begun) {
    otaModeActive = false;
    otaUiState = OTA_UI_ERROR;
    otaMessage = "FALHA HTTPS";
    if (renderUi) renderSystemUpdate();
    return;
  }

  int httpCode = http.GET();

  if (httpCode != HTTP_CODE_OK) {
    http.end();
    otaModeActive = false;

    otaUiState = OTA_UI_ERROR;
    otaMessage = "HTTP " + String(httpCode);
    if (renderUi) renderSystemUpdate();
    return;
  }

  String payload = http.getString();
  http.end();

  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, payload);

  if (error) {
    otaModeActive = false;
    otaUiState = OTA_UI_ERROR;
    otaMessage = "MANIFESTO INVALIDO";
    if (renderUi) renderSystemUpdate();
    return;
  }

  otaAvailableVersion = doc["version"] | "";

  const char* remoteFirmwareUrl =
    doc["firmware_url"] | OTA_FIRMWARE_FALLBACK_URL;

  otaFirmwareUrl = remoteFirmwareUrl;
  otaExpectedSha256 = doc["sha256"] | "";
  otaRemoteSize = doc["size"] | 0UL;

  otaModeActive = false;

  if (otaAvailableVersion.length() == 0) {
    otaUiState = OTA_UI_ERROR;
    otaMessage = "VERSAO AUSENTE";
    if (renderUi) renderSystemUpdate();
    return;
  }

  otaUiState =
    compareVersions(String(APP_VERSION), otaAvailableVersion) < 0
      ? OTA_UI_AVAILABLE
      : OTA_UI_CURRENT;

  if (renderUi) renderSystemUpdate();
}

void installFirmwareUpdate() {
  if (WiFi.status() != WL_CONNECTED) {
    otaUiState = OTA_UI_ERROR;
    otaMessage = "SEM CONEXAO WI-FI";
    renderSystemUpdate();
    return;
  }

  if (!waitNetworkWorkerForOta()) {
    otaUiState = OTA_UI_ERROR;
    otaMessage = "REDE OCUPADA";
    renderSystemUpdate();
    return;
  }

  otaUiState = OTA_UI_INSTALLING;

  drawUiBackground();
  drawHeader("ATUALIZACAO");
  drawContentPanel(4, 23, 152, 88);

  display.setTextColor(TFT_CYAN, C_BG);
  display.drawCentreScreenString("PREPARANDO...", 37, 1);
  renderOtaProgress(0);
  drawFooter("NAO DESLIGUE A PLACA");

  Serial.println();
  Serial.println("========================================");
  Serial.println("ATUALIZACAO OTA");
  Serial.print("Instalada: ");
  Serial.println(APP_VERSION);
  Serial.print("Disponivel: ");
  Serial.println(otaAvailableVersion);
  Serial.print("URL: ");
  Serial.println(otaFirmwareUrl);
  Serial.println("========================================");

  WiFiClientSecure client;
  client.setInsecure();
  client.setTimeout(15000);

  otaLastProgress = -5;

  httpUpdate.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
  httpUpdate.rebootOnUpdate(false);
  httpUpdate.onProgress(otaFirmwareProgress);

  t_httpUpdate_return result =
    httpUpdate.update(client, otaFirmwareUrl);

  if (result == HTTP_UPDATE_OK) {
    renderOtaProgress(100);

    display.fillRect(8, 31, 144, 75, C_PANEL);
    display.setTextColor(TFT_GREEN, C_PANEL);
    display.drawCentreScreenString("ATUALIZACAO CONCLUIDA", 48, 1);
    display.setTextColor(TFT_WHITE, C_PANEL);
    display.drawCentreScreenString("REINICIANDO...", 69, 1);
    drawFooter("ES32Lab ELEICOES");

    Serial.println("OTA concluida. Reiniciando...");

    delay(2500);
    ESP.restart();
    return;
  }

  otaModeActive = false;
  otaUiState = OTA_UI_ERROR;

  if (result == HTTP_UPDATE_NO_UPDATES) {
    otaMessage = "SEM ALTERACOES";
  } else {
    otaMessage =
      "OTA " +
      String(httpUpdate.getLastError());
  }

  Serial.print("Falha OTA: ");
  Serial.println(httpUpdate.getLastErrorString());

  renderSystemUpdate();
}

// ============================================================
// CONFIRMACOES / ABOUT
// ============================================================

void renderCacheConfirm() {
  drawUiBackground();
  drawHeader("LIMPAR CACHE");
  drawContentPanel(2, 23, 156, 88);

  display.setTextColor(TFT_YELLOW, C_BG);
  display.drawCentreScreenString("APAGAR FOTOS EM CACHE?", 47, 1);

  display.setTextColor(TFT_WHITE, C_BG);
  display.drawCentreScreenString("OK = SIM", 69, 1);
  display.drawCentreScreenString("< = CANCELAR", 84, 1);

  drawFooter("SOMENTE FOTOS");
}

void renderWifiDelete() {
  drawUiBackground();
  drawHeader("EXCLUIR WI-FI");
  drawContentPanel(2, 23, 156, 88);

  String ssid = wifiDeleteIndex < wifiCount ? wifiList[wifiDeleteIndex].ssid : "";

  display.setTextColor(TFT_YELLOW, C_BG);
  display.drawCentreScreenString("EXCLUIR REDE?", 43, 1);

  display.setTextColor(TFT_WHITE, C_BG);
  display.drawCentreScreenString(fitRawText(ssid, 24), 61, 1);
  display.drawCentreScreenString("OK = SIM   < = NAO", 84, 1);

  drawFooter("CONFIRMACAO");
}

void renderAbout() {
  drawUiBackground();
  drawHeader("SOBRE");
  drawContentPanel(2, 22, 156, 89);

  display.setTextColor(TFT_WHITE, C_BG);
  display.drawString("ES32Lab Eleicoes", 5, 27, 1);
  display.drawString("Versao: " + String(APP_VERSION), 5, 41, 1);
  display.drawString("Build: " + String(APP_BUILD_DATE), 5, 55, 1);
  display.drawString("LIB: " + String(ES32LAB_VERSION), 5, 69, 1);
  display.drawString("WiFi: " + String(ES_WIFI_VERSION), 5, 83, 1);

  display.setTextColor(TFT_CYAN, C_BG);
  display.drawString("Dados: TSE", 5, 98, 1);

  drawFooter("ESDEVELOPER.COM.BR");
}

// ============================================================
// RESULTADOS UI
// ============================================================

uint16_t rankColor(uint8_t rank) {
  if (rank == 0) return TFT_GREEN;
  if (rank == 1) return TFT_BLUE;
  if (rank == 2) return TFT_ORANGE;
  return TFT_DARKGREY;
}

void drawPercentBar(
  int x, int y, int w, int h,
  const String& pct,
  uint16_t color
) {
  float p = percentFloat(pct);
  if (p < 0) p = 0;
  if (p > 100) p = 100;

  display.drawRoundRect(x, y, w, h, 2, C_LINE);

  int fill = (int)((w - 2) * p / 100.0f);
  if (fill > 0)
    display.fillRect(x + 1, y + 1, fill, h - 2, color);
}

void drawPreCountNotice() {
  display.setTextColor(TFT_YELLOW, C_BG);
  display.drawCentreScreenString("APURACAO NAO INICIOU", 20, 1);
  display.setTextColor(TFT_WHITE, C_BG);
  display.drawCentreScreenString("ORDEM RECEBIDA DO TSE", 29, 1);
  display.setTextColor(TFT_LIGHTGREY, C_BG);
  display.drawCentreScreenString("NAO INDICA LIDERANCA", 38, 1);
}

void schedulePhotoIfNeeded(
  uint8_t slot,
  const RaceData& race,
  uint8_t candidateIndex,
  bool notifyUi = true
) {
  if (!race.usePhotos || candidateIndex >= race.candidateCount) return;

  RuntimeSelection s = getRuntimeSelection();
  const Candidate& c = race.candidates[candidateIndex];
  requestPhotoFor(
    s,
    slot,
    race.office,
    c,
    notifyUi
  );
}

void drawCandidatePhoto(
  const RaceData& race,
  const Candidate& c,
  int x, int y, int w, int h
) {
  RuntimeSelection s = getRuntimeSelection();
  String path = photoPath(s, race.office, c);
  bool shown = false;

  if (photoCached(path)) {
    display.setViewport(x, y, w, h, true);
    display.fillScreen(C_PANEL);
    shown = display.renderJPEG(LittleFS, path, 0, 0, true);
    display.resetViewport();
  }

  if (!shown) {
    display.fillRoundRect(x, y, w, h, 4, C_PANEL);
    display.drawRoundRect(x, y, w, h, 4, TFT_CYAN);
    display.setTextColor(TFT_LIGHTGREY, C_PANEL);
    display.drawCentreString("FOTO TSE", x + w / 2, y + h / 2 - 7, 1);
    display.drawCentreString(
      WiFi.status() == WL_CONNECTED ? "CARREGANDO" : "SEM REDE",
      x + w / 2, y + h / 2 + 4, 1
    );
  }
}

void renderPhotoRace(uint8_t slot) {
  RaceData race = raceCopy(slot);

  drawUiBackground();
  drawHeader(race.shortTitle.length() ? race.shortTitle : titleForSlot(slot, activeUf));

  if (!race.hasData || race.candidateCount == 0) {
    display.setTextColor(TFT_YELLOW, C_BG);
    display.drawCentreScreenString(
      refreshRunning ? "ATUALIZANDO TSE..." : "AGUARDANDO TSE",
      50, 1
    );

    display.setTextColor(TFT_WHITE, C_BG);
    display.drawCentreScreenString(fitText(race.error, 24), 68, 1);

    display.setTextColor(TFT_LIGHTGREY, C_BG);
    display.drawCentreScreenString("HTTP " + String(race.httpCode), 82, 1);

    drawFooter("<> TELAS  SEG OK MENU");
    return;
  }

  int contentY = 38;
  int photoHeight = 74;

  if (!race.countStarted) {
    drawPreCountNotice();
    contentY = 48;
    photoHeight = 64;
  } else {
    display.setTextColor(TFT_WHITE, C_BG);
    display.drawString(
      "APUR " + race.sectionPercent + "% " + race.time,
      3, 21, 1
    );
    drawPercentBar(3, 30, 154, 5, race.sectionPercent, TFT_YELLOW);
  }

  uint8_t index = race.selected;
  if (index >= race.candidateCount) index = 0;

  Candidate& c = race.candidates[index];

  drawCandidatePhoto(race, c, 2, contentY, 67, photoHeight);

  display.fillRect(72, contentY, 88, photoHeight, C_BG);
  display.drawFastVLine(71, contentY, photoHeight, C_LINE);

  uint16_t color = race.countStarted ? rankColor(index) : TFT_DARKGREY;

  display.fillCircle(82, contentY + 9, 8, color);
  display.setTextColor(TFT_WHITE, color);
  display.drawCentreString(String(index + 1), 82, contentY + 6, 1);

  display.setTextColor(TFT_WHITE, C_BG);
  display.drawString(fitText(c.name, 11), 93, contentY + 4, 1);

  display.setTextColor(TFT_LIGHTGREY, C_BG);
  display.drawString(
    fitText(c.number + " " + c.party, 14),
    76, contentY + 18, 1
  );

  display.setTextColor(race.countStarted ? color : TFT_LIGHTGREY, C_BG);
  display.drawString(c.percentage + "%", 76, contentY + 29, 2);

  display.setTextColor(TFT_WHITE, C_BG);
  display.drawString(
    fitText(formatVotes(c.votes) + " votos", 14),
    76, contentY + 47, 1
  );

  drawPercentBar(
    76, contentY + 58, 81, 6,
    c.percentage,
    race.countStarted ? color : TFT_DARKGREY
  );

  schedulePhotoIfNeeded(slot, race, index, true);
  drawFooter("<> TELAS ^v CAND SEG OK");
}

void renderListRace(uint8_t slot) {
  RaceData race = raceCopy(slot);

  drawUiBackground();
  drawHeader(race.shortTitle.length() ? race.shortTitle : titleForSlot(slot, activeUf));

  if (!race.hasData || race.candidateCount == 0) {
    display.setTextColor(TFT_YELLOW, C_BG);
    display.drawCentreScreenString(
      refreshRunning ? "ATUALIZANDO TSE..." : "AGUARDANDO TSE",
      50, 1
    );
    display.setTextColor(TFT_WHITE, C_BG);
    display.drawCentreScreenString(fitText(race.error, 24), 68, 1);
    display.setTextColor(TFT_LIGHTGREY, C_BG);
    display.drawCentreScreenString("HTTP " + String(race.httpCode), 82, 1);
    drawFooter("<> TELAS  SEG OK MENU");
    return;
  }

  int startY = 39;

  if (!race.countStarted) {
    drawPreCountNotice();
    startY = 48;
  } else {
    display.setTextColor(TFT_WHITE, C_BG);
    display.drawString("APURADO " + race.sectionPercent + "%", 3, 21, 1);
    drawPercentBar(3, 30, 154, 5, race.sectionPercent, TFT_YELLOW);
  }

  uint8_t visibleRows = race.countStarted ? 5 : 4;

  for (uint8_t row = 0; row < visibleRows; row++) {
    uint8_t index = race.scroll + row;
    if (index >= race.candidateCount) break;

    Candidate& c = race.candidates[index];
    int y = startY + row * 13;
    uint16_t color = race.countStarted ? rankColor(index) : TFT_DARKGREY;

    if (index <= 2) {
      display.fillCircle(9, y + 4, 7, color);
      display.setTextColor(TFT_WHITE, color);
      display.drawCentreString(String(index + 1), 9, y + 1, 1);
    } else {
      display.setTextColor(TFT_LIGHTGREY, C_BG);
      display.drawNumber(index + 1, 4, y, 1);
    }

    display.setTextColor(
      (race.countStarted && index == 0) ? TFT_GREEN : TFT_WHITE,
      C_BG
    );
    display.drawString(fitText(c.name, 15), 20, y, 1);
    display.drawRightScreenString(c.percentage + "%", y, 1);
    display.drawFastHLine(20, y + 10, 137, C_LINE);
  }

  drawFooter("<> TELAS ^v LISTA SEG OK");
}

String workerText() {
  switch (networkWorkerState) {
    case NET_CONFIG: return "CONFIG TSE";
    case NET_RESULTS: return "RESULTADOS";
    case NET_PHOTO: return "FOTO";
    case NET_SCAN: return "SCAN WIFI";
    default: return "IDLE";
  }
}

void renderResultStatus() {
  drawUiBackground();
  drawHeader("STATUS");
  drawContentPanel(2, 22, 156, 89);

  int y = 26;

  display.setTextColor(
    WiFi.status() == WL_CONNECTED ? TFT_GREEN : TFT_RED,
    C_BG
  );

  String wifiLine =
    WiFi.status() == WL_CONNECTED
      ? String("WiFi: ") + fitRawText(WiFi.SSID(), 18)
      : String("WiFi: OFFLINE");
  display.drawString(fitRawText(wifiLine, 25), 5, y, 1);

  y += 14;
  display.setTextColor(TFT_WHITE, C_BG);
  display.drawString(
    String(APP_ELECTION_YEAR) +
    "  T" + String(activeRound) +
    "  " + UFS[activeUf].code +
    "  UTC" + String((int)UFS[activeUf].utcOffset),
    5, y, 1
  );

  y += 14;
  display.drawString(
    dateText() + "  " + clockText(),
    5, y, 1
  );

  y += 14;
  display.setTextColor(refreshRunning ? TFT_YELLOW : TFT_CYAN, C_BG);
  display.drawString(
    "TSE " + String(POLL_OPTIONS_MS[pollOptionIndex] / 1000UL) +
    "s | " + workerText(),
    5, y, 1
  );

  y += 14;
  display.setTextColor(littleFsReady ? TFT_GREEN : TFT_RED, C_BG);
  String storageLine =
    String("FS ") + (littleFsReady ? "OK" : "FALHA") +
    " | RAM " + String(ESP.getFreeHeap() / 1024UL) + " KB";
  display.drawString(storageLine, 5, y, 1);

  y += 14;
  display.setTextColor(TFT_CYAN, C_BG);
  display.drawString("Versao " + String(APP_VERSION), 5, y, 1);

  drawFooter("<> TELAS  OK ATUALIZA");
}

void renderResults() {
  uint8_t pages = resultPageCount();
  if (resultPage >= pages) resultPage = 0;

  if (resultPageIsStatus(resultPage)) {
    renderResultStatus();
    return;
  }

  uint8_t slot = raceSlotForPage(resultPage);
  if (photosForSlot(slot))
    renderPhotoRace(slot);
  else
    renderListRace(slot);
}

// ============================================================
// RENDER GERAL
// ============================================================

void renderCurrentView() {
  switch (view) {
    case VIEW_MAIN: renderMain(); break;
    case VIEW_ELECTION: renderElectionEditor(); break;
    case VIEW_RESULTS: renderResults(); break;
    case VIEW_SETTINGS: renderSettings(); break;
    case VIEW_WIFI: renderWifiMenu(); break;
    case VIEW_WIFI_SCAN: renderWifiScan(); break;
    case VIEW_WIFI_DELETE: renderWifiDelete(); break;
    case VIEW_PASSWORD: renderPassword(); break;
    case VIEW_UPDATE_INTERVAL: renderUpdateInterval(); break;
    case VIEW_SYSTEM_UPDATE: renderSystemUpdate(); break;
    case VIEW_CACHE_CONFIRM: renderCacheConfirm(); break;
    case VIEW_ABOUT: renderAbout(); break;
  }
}

// ============================================================
// GESTO DO BOTAO CENTRAL
// ============================================================

enum CenterEvent {
  CENTER_NONE,
  CENTER_SHORT,
  CENTER_LONG
};

CenterEvent readCenterEvent() {
  if (keyboard.press(KEY_CENTER)) {
    centerTracking = true;
    centerLongFired = false;
    centerHoldTimer.resetMillis();
  }

  if (
    centerTracking &&
    !centerLongFired &&
    keyboard.hold(KEY_CENTER) &&
    centerHoldTimer.intervalMillis(1400UL)
  ) {
    centerLongFired = true;
    return CENTER_LONG;
  }

  if (centerTracking && keyboard.release(KEY_CENTER)) {
    centerTracking = false;
    if (!centerLongFired) return CENTER_SHORT;
  }

  return CENTER_NONE;
}

// ============================================================
// PASSWORD HOLD / ACELERACAO
// ============================================================

void servicePasswordCharacterInput() {
  bool upPress = keyboard.press(KEY_UP);
  bool downPress = keyboard.press(KEY_DOWN);

  if (upPress) {
    changePasswordChar(+1);
    resetPasswordCursor();
    passwordHoldDelay.resetMillis();
    passwordRepeatTimer.resetMillis();
    passwordRepeatActive = false;
    passwordRepeatCount = 0;
    renderPassword();
  }

  if (downPress) {
    changePasswordChar(-1);
    resetPasswordCursor();
    passwordHoldDelay.resetMillis();
    passwordRepeatTimer.resetMillis();
    passwordRepeatActive = false;
    passwordRepeatCount = 0;
    renderPassword();
  }

  int direction = 0;
  if (keyboard.hold(KEY_UP)) direction = +1;
  else if (keyboard.hold(KEY_DOWN)) direction = -1;

  if (direction == 0) {
    passwordRepeatActive = false;
    passwordRepeatCount = 0;
    return;
  }

  if (!passwordRepeatActive) {
    if (passwordHoldDelay.intervalMillis(500UL)) {
      passwordRepeatActive = true;
      passwordRepeatTimer.resetMillis();
    }
    return;
  }

  uint32_t repeatMs = 180UL;
  if (passwordRepeatCount >= 8) repeatMs = 90UL;
  if (passwordRepeatCount >= 20) repeatMs = 45UL;

  if (passwordRepeatTimer.intervalMillis(repeatMs)) {
    changePasswordChar(direction);
    resetPasswordCursor();
    passwordRepeatCount++;
    drawPasswordEntryLine();
  }
}

// ============================================================
// INPUT POR VIEW
// ============================================================

void handleMainInput(CenterEvent center) {
  if (keyboard.press(KEY_UP)) {
    menuIndex = menuIndex == 0 ? 2 : menuIndex - 1;
    renderMain();
  }
  if (keyboard.press(KEY_DOWN)) {
    menuIndex = (menuIndex + 1) % 3;
    renderMain();
  }

  if (center == CENTER_SHORT) {
    if (menuIndex == 0) {
      view = VIEW_RESULTS;
      resultPage = 0;
      refreshRequested = true;
    } else if (menuIndex == 1) {
      beginElectionEditor();
      view = VIEW_ELECTION;
    } else {
      settingsIndex = 0;
      view = VIEW_SETTINGS;
    }
    renderCurrentView();
  }
}

void handleElectionInput(CenterEvent center) {
  if (keyboard.press(KEY_UP)) {
    electionEditRow = electionEditRow == 0 ? 2 : electionEditRow - 1;
    renderElectionEditor();
  }

  if (keyboard.press(KEY_DOWN)) {
    electionEditRow = (electionEditRow + 1) % 3;
    renderElectionEditor();
  }

  int delta = 0;

  if (keyboard.press(KEY_LEFT))
    delta = -1;

  if (keyboard.press(KEY_RIGHT))
    delta = +1;

  if (delta != 0) {
    if (electionEditRow == 0) {
      ElectionProfile p;

      if (getElectionProfile(APP_ELECTION_YEAR, p) &&
          secondRoundAvailableForUf(p, editUf)) {
        editRound = editRound == 1 ? 2 : 1;
      } else {
        editRound = 1;
      }
    }
    else if (electionEditRow == 1) {
      int v = editUf + delta;

      if (v < 0)
        v = UF_COUNT - 1;

      if (v >= UF_COUNT)
        v = 0;

      editUf = v;

      ElectionProfile p;

      if (getElectionProfile(APP_ELECTION_YEAR, p) &&
          editRound == 2 &&
          !secondRoundAvailableForUf(p, editUf)) {
        editRound = 1;
      }
    }

    renderElectionEditor();
  }

  if (center == CENTER_SHORT) {
    if (electionEditRow < 2) {
      electionEditRow++;
      renderElectionEditor();
    }
    else {
      activateSelection(
        editRound,
        editUf
      );

      view = VIEW_RESULTS;
      resultPage = 0;
      renderResults();
    }
  }

  if (center == CENTER_LONG) {
    view = profileWasSaved ? VIEW_MAIN : VIEW_ELECTION;
    renderCurrentView();
  }
}

void handleSettingsInput(CenterEvent center) {
  constexpr uint8_t SETTINGS_COUNT = 6;

  if (keyboard.press(KEY_UP)) {
    settingsIndex =
      settingsIndex == 0
      ? SETTINGS_COUNT - 1
      : settingsIndex - 1;
    renderSettings();
  }

  if (keyboard.press(KEY_DOWN)) {
    settingsIndex = (settingsIndex + 1) % SETTINGS_COUNT;
    renderSettings();
  }

  if (keyboard.press(KEY_LEFT)) {
    view = VIEW_MAIN;
    renderMain();
    return;
  }

  if (center == CENTER_SHORT) {
    if (settingsIndex == 0) {
      wifiMenuIndex = 0;
      view = VIEW_WIFI;
      renderWifiMenu();
      return;
    }

    if (settingsIndex == 1) {
      view = VIEW_UPDATE_INTERVAL;
      renderUpdateInterval();
      return;
    }

    if (settingsIndex == 2) {
      otaEnteredFromStartup = false;
      view = VIEW_SYSTEM_UPDATE;
      otaUiState = OTA_UI_IDLE;
      renderSystemUpdate();
      checkFirmwareUpdate();
      return;
    }

    if (settingsIndex == 3) {
      view = VIEW_CACHE_CONFIRM;
      renderCacheConfirm();
      return;
    }

    if (settingsIndex == 4) {
      view = VIEW_ABOUT;
      renderAbout();
      return;
    }

    view = VIEW_MAIN;
    renderMain();
  }
}

void handleWifiMenuInput(CenterEvent center) {
  uint8_t total = wifiMenuItemCount();

  if (keyboard.press(KEY_UP)) {
    wifiMenuIndex = wifiMenuIndex == 0 ? total - 1 : wifiMenuIndex - 1;
    renderWifiMenu();
  }
  if (keyboard.press(KEY_DOWN)) {
    wifiMenuIndex = (wifiMenuIndex + 1) % total;
    renderWifiMenu();
  }

  if (keyboard.press(KEY_LEFT)) {
    view = VIEW_SETTINGS;
    renderSettings();
    return;
  }

  // Direita numa rede salva = excluir com confirmacao.
  if (keyboard.press(KEY_RIGHT) && wifiMenuIndex < wifiCount) {
    wifiDeleteIndex = wifiMenuIndex;
    view = VIEW_WIFI_DELETE;
    renderWifiDelete();
    return;
  }

  if (center == CENTER_SHORT) {
    if (wifiMenuIndex < wifiCount) {
      requestManualWifi(
        wifiList[wifiMenuIndex].ssid,
        wifiList[wifiMenuIndex].password
      );
      renderWifiMenu();
    }
    else if (wifiMenuIndex == wifiCount) {
      scanIndex = 0;
      scanReady = false;
      scanRequested = true;
      view = VIEW_WIFI_SCAN;
      renderWifiScan();
    }
    else {
      view = VIEW_SETTINGS;
      renderSettings();
    }
  }
}

void handleWifiScanInput(CenterEvent center) {
  if (keyboard.press(KEY_LEFT)) {
    view = VIEW_WIFI;
    renderWifiMenu();
    return;
  }

  if (!scanReady) return;

  uint8_t localCount;
  ScannedNetwork local[MAX_SCAN_NETWORKS];

  xSemaphoreTake(scanMutex, portMAX_DELAY);
  localCount = scanCount;
  for (uint8_t i = 0; i < localCount; i++) local[i] = scanList[i];
  xSemaphoreGive(scanMutex);

  if (localCount == 0) return;

  if (keyboard.press(KEY_UP)) {
    scanIndex = scanIndex == 0 ? localCount - 1 : scanIndex - 1;
    renderWifiScan();
  }
  if (keyboard.press(KEY_DOWN)) {
    scanIndex = (scanIndex + 1) % localCount;
    renderWifiScan();
  }

  if (center == CENTER_SHORT && scanIndex < localCount) {
    passwordSsid = local[scanIndex].ssid;
    passwordSecure = local[scanIndex].secure;
    passwordValue = "";
    passwordGroup = 0;
    passwordCharIndex = 0;
    passwordHasPendingChar = false;

    if (!passwordSecure) {
      addOrUpdateWifi(passwordSsid, "");
      requestManualWifi(passwordSsid, "");
      view = VIEW_WIFI;
      wifiMenuIndex = 0;
      renderWifiMenu();
    } else {
      view = VIEW_PASSWORD;
      renderPassword();
    }
  }
}

void handlePasswordInput(CenterEvent center) {
  servicePasswordCharacterInput();

  if (keyboard.press(KEY_RIGHT)) {
    // RIGHT confirma somente se existe caractere pendente.
    // A proxima posicao volta a ficar realmente vazia.
    if (passwordHasPendingChar && passwordValue.length() < 63) {
      passwordValue += selectedPasswordChar();
      passwordHasPendingChar = false;
      passwordCharIndex = 0;
      resetPasswordCursor();
      renderPassword();
    }
  }

  if (keyboard.press(KEY_LEFT)) {
    // Se existe um caractere ainda nao confirmado, LEFT apenas cancela
    // essa selecao. Caso contrario apaga o ultimo caractere confirmado.
    if (passwordHasPendingChar) {
      passwordHasPendingChar = false;
      passwordCharIndex = 0;
      resetPasswordCursor();
      renderPassword();
      return;
    }

    if (passwordValue.length() > 0) {
      passwordValue.remove(passwordValue.length() - 1);
      renderPassword();
    }
    else {
      view = VIEW_WIFI_SCAN;
      renderWifiScan();
      return;
    }
  }

  if (center == CENTER_SHORT) {
    passwordGroup = (passwordGroup + 1) % PASSWORD_GROUP_COUNT;
    passwordCharIndex = 0;

    // Se ja havia caractere em edicao, ele passa a representar o
    // primeiro caractere do novo grupo. Se a posicao estava vazia,
    // continua vazia.
    resetPasswordCursor();
    renderPassword();
  }

  if (center == CENTER_LONG) {
    String finalPassword = passwordValue;

    // O caractere que o usuario esta vendo e automaticamente valido
    // ao salvar. Nao e mais necessario pressionar RIGHT antes.
    if (passwordHasPendingChar && finalPassword.length() < 63) {
      finalPassword += selectedPasswordChar();
    }

    passwordValue = finalPassword;
    passwordHasPendingChar = false;

    addOrUpdateWifi(passwordSsid, finalPassword);
    requestManualWifi(passwordSsid, finalPassword);

    wifiMenuIndex = 0;
    view = VIEW_WIFI;
    renderWifiMenu();
  }
}

void handleWifiDeleteInput(CenterEvent center) {
  if (keyboard.press(KEY_LEFT)) {
    view = VIEW_WIFI;
    renderWifiMenu();
    return;
  }

  if (center == CENTER_SHORT) {
    deleteWifi(wifiDeleteIndex);
    if (wifiMenuIndex >= wifiMenuItemCount()) wifiMenuIndex = 0;
    view = VIEW_WIFI;
    renderWifiMenu();
  }
}

void handleUpdateInput(CenterEvent center) {
  if (keyboard.press(KEY_LEFT)) {
    pollOptionIndex = pollOptionIndex == 0
      ? POLL_OPTION_COUNT - 1
      : pollOptionIndex - 1;
    renderUpdateInterval();
  }

  if (keyboard.press(KEY_RIGHT)) {
    pollOptionIndex = (pollOptionIndex + 1) % POLL_OPTION_COUNT;
    renderUpdateInterval();
  }

  if (center == CENTER_SHORT) {
    savePollSetting();
    pollTimer.resetMillis();
    view = VIEW_SETTINGS;
    renderSettings();
  }
}

void handleSystemUpdateInput(CenterEvent center) {
  if (keyboard.press(KEY_LEFT)) {
    otaModeActive = false;

    // Quando a atualizacao foi oferecida automaticamente na abertura,
    // VOLTAR significa continuar para a aplicacao, e nao cair no meio
    // do submenu de configuracoes.
    if (otaEnteredFromStartup) {
      otaEnteredFromStartup = false;

      if (profileWasSaved) {
        view = VIEW_MAIN;
        menuIndex = 0;
      } else {
        beginElectionEditor();
        view = VIEW_ELECTION;
      }

      renderCurrentView();

      if (WiFi.status() == WL_CONNECTED) {
        catalogRequested = true;
        refreshRequested = true;
      }

      pollTimer.resetMillis();
      return;
    }

    view = VIEW_SETTINGS;
    renderSettings();
    return;
  }

  if (center == CENTER_SHORT) {
    if (otaUiState == OTA_UI_AVAILABLE) {
      installFirmwareUpdate();
    }
    else if (otaUiState != OTA_UI_INSTALLING) {
      checkFirmwareUpdate();
    }
  }
}

void handleCacheInput(CenterEvent center) {
  if (keyboard.press(KEY_LEFT)) {
    view = VIEW_SETTINGS;
    renderSettings();
    return;
  }

  if (center == CENTER_SHORT) {
    clearPhotoCache();
    view = VIEW_SETTINGS;
    renderSettings();
  }
}

void handleAboutInput(CenterEvent center) {
  if (keyboard.press(KEY_LEFT) || center == CENTER_SHORT || center == CENTER_LONG) {
    view = VIEW_SETTINGS;
    renderSettings();
  }
}

void changeRaceSelection(uint8_t slot, int delta) {
  xSemaphoreTake(raceMutex, portMAX_DELAY);

  RaceData& race = races[slot];

  if (photosForSlot(slot)) {
    if (race.candidateCount > 0) {
      int n = race.selected + delta;
      if (n < 0) n = 0;
      if (n >= race.candidateCount) n = race.candidateCount - 1;
      race.selected = n;
    }
  } else {
    uint8_t visible = race.countStarted ? 5 : 4;
    int maxScroll = race.candidateCount > visible
      ? race.candidateCount - visible
      : 0;

    int n = race.scroll + delta;
    if (n < 0) n = 0;
    if (n > maxScroll) n = maxScroll;
    race.scroll = n;
  }

  xSemaphoreGive(raceMutex);
}

void handleResultsInput(CenterEvent center) {
  uint8_t pages = resultPageCount();

  if (keyboard.press(KEY_LEFT)) {
    resultPage = resultPage == 0 ? pages - 1 : resultPage - 1;
    renderResults();
  }

  if (keyboard.press(KEY_RIGHT)) {
    resultPage = (resultPage + 1) % pages;
    renderResults();
  }

  if (!resultPageIsStatus(resultPage)) {
    uint8_t slot = raceSlotForPage(resultPage);

    if (keyboard.press(KEY_UP)) {
      changeRaceSelection(slot, -1);
      renderResults();
    }

    if (keyboard.press(KEY_DOWN)) {
      changeRaceSelection(slot, +1);
      renderResults();
    }
  }

  if (center == CENTER_SHORT) {
    refreshRequested = true;
    renderResults();
  }

  if (center == CENTER_LONG) {
    view = VIEW_MAIN;
    menuIndex = 0;
    renderMain();
  }
}

void handleInput() {
  CenterEvent center = readCenterEvent();

  switch (view) {
    case VIEW_MAIN: handleMainInput(center); break;
    case VIEW_ELECTION: handleElectionInput(center); break;
    case VIEW_RESULTS: handleResultsInput(center); break;
    case VIEW_SETTINGS: handleSettingsInput(center); break;
    case VIEW_WIFI: handleWifiMenuInput(center); break;
    case VIEW_WIFI_SCAN: handleWifiScanInput(center); break;
    case VIEW_WIFI_DELETE: handleWifiDeleteInput(center); break;
    case VIEW_PASSWORD: handlePasswordInput(center); break;
    case VIEW_UPDATE_INTERVAL: handleUpdateInput(center); break;
    case VIEW_SYSTEM_UPDATE: handleSystemUpdateInput(center); break;
    case VIEW_CACHE_CONFIRM: handleCacheInput(center); break;
    case VIEW_ABOUT: handleAboutInput(center); break;
  }
}

// ============================================================
// PRE-CACHE OPORTUNISTA
// ============================================================

void serviceIdlePhotoPreload() {
  if (otaModeActive)
    return;

  if (!startupAppEntered || view != VIEW_RESULTS)
    return;

  if (WiFi.status() != WL_CONNECTED)
    return;

  if (networkWorkerState != NET_IDLE ||
      refreshRunning ||
      refreshRequested ||
      catalogRequested ||
      scanRequested)
    return;

  if (!photoPreloadTimer.intervalMillis(1800UL))
    return;

  if (resultPageIsStatus(resultPage))
    return;

  uint8_t slot = raceSlotForPage(resultPage);

  if (!photosForSlot(slot))
    return;

  RaceData race = raceCopy(slot);

  if (!race.hasData || race.candidateCount == 0)
    return;

  uint8_t nextIndex = race.selected + 1;

  if (nextIndex >= race.candidateCount)
    return;

  // Somente chega aqui quando toda atividade mais importante terminou.
  // requestPhotoFor() nao duplica arquivos que ja estejam no cache.
  schedulePhotoIfNeeded(
    slot,
    race,
    nextIndex,
    false
  );
}

// ============================================================
// FLUXO INICIAL
// ============================================================

void enterApplicationAfterStartup() {
  startupSplashActive = false;
  startupPresentationActive = false;
  startupWifiProvisioning = false;
  startupAppEntered = true;

  if (profileWasSaved) {
    view = VIEW_MAIN;
    menuIndex = 0;
  } else {
    beginElectionEditor();
    view = VIEW_ELECTION;
  }

  renderCurrentView();

  // Agora que a tela principal entrou, libera as consultas eleitorais.
  if (WiFi.status() == WL_CONNECTED) {
    catalogRequested = true;
    refreshRequested = true;
  }

  pollTimer.resetMillis();
}

void serviceWifiSuccessFlow() {
  if (wifiSuccessPending && !wifiSuccessActive) {
    wifiSuccessPending = false;
    wifiSuccessActive = true;

    // A confirmacao de Wi-Fi assume temporariamente a tela inteira.
    startupSplashActive = false;
    startupPresentationActive = false;

    renderWifiConnectionSuccess();
    wifiSuccessTimer.resetMillis();
  }

  if (!wifiSuccessActive)
    return;

  if (!wifiSuccessTimer.intervalMillis(1800UL))
    return;

  wifiSuccessActive = false;

  // No primeiro provisionamento, continua exatamente para o ponto
  // normal de entrada da aplicacao. Nas demais conexoes, volta ao menu.
  if (!startupAppEntered) {
    enterApplicationAfterStartup();
  } else {
    view = VIEW_MAIN;
    menuIndex = 0;
    renderMain();
  }
}

void enterStartupWifiScan() {
  startupSplashActive = false;
  startupPresentationActive = false;
  startupWifiProvisioning = true;

  scanIndex = 0;
  scanReady = false;
  scanRequested = true;

  view = VIEW_WIFI_SCAN;
  renderWifiScan();
}

void serviceStartupFlow() {
  // Depois de entrar no aplicativo, este servico nao interfere mais
  // na navegacao normal.
  if (startupAppEntered)
    return;

  // Durante a confirmacao visual de uma nova rede, o fluxo inicial
  // aguarda a mensagem terminar antes de trocar de tela.
  if (wifiSuccessPending || wifiSuccessActive)
    return;

  // Etapa 1: QR Code ES Developer.
  if (startupSplashActive) {
    if (!splashMinimumElapsed &&
        splashTimer.intervalMillis(QR_MIN_MS)) {
      splashMinimumElapsed = true;
    }

    if (!splashMinimumElapsed)
      return;

    startupSplashActive = false;
    startupPresentationActive = true;
    splashMinimumElapsed = false;

    renderPresentationSplash();
    splashTimer.resetMillis();
    return;
  }

  // Etapa 2: apresentacao Eleicoes 2026.
  if (startupPresentationActive) {
    // Assim que houver internet, aproveita a propria tela de abertura para
    // consultar silenciosamente a Release mais recente. A consulta tem
    // timeout reduzido para nunca prender a inicializacao por muito tempo.
    if (WiFi.status() == WL_CONNECTED && !startupUpdateChecked) {
      startupUpdateChecked = true;
      checkFirmwareUpdate(false, 5000UL);
    }

    if (!splashMinimumElapsed &&
        splashTimer.intervalMillis(PRESENTATION_MIN_MS)) {
      splashMinimumElapsed = true;
    }

    if (!splashMinimumElapsed)
      return;

    // Se a consulta silenciosa encontrou versao nova, a primeira tela
    // interativa do aplicativo ja sera a tela de atualizacao.
    if (WiFi.status() == WL_CONNECTED) {
      if (otaUiState == OTA_UI_AVAILABLE) {
        startupSplashActive = false;
        startupPresentationActive = false;
        startupWifiProvisioning = false;
        startupAppEntered = true;
        otaEnteredFromStartup = true;

        view = VIEW_SYSTEM_UPDATE;
        renderSystemUpdate();
        return;
      }

      enterApplicationAfterStartup();
      return;
    }

    // Sem rede salva ou apos testar todas sem sucesso, abre o scan.
    if (wifiCount == 0 || startupWifiCycleFinished) {
      enterStartupWifiScan();
      return;
    }

    // Ainda ha tentativa Wi-Fi em andamento: mantem a tela de
    // apresentacao ate a conexao ou o fim do ciclo inicial.
    return;
  }

  // Quando o scan inicial levou ao cadastro de uma rede, assim que a
  // conexao for confirmada seguimos para o aplicativo sem exigir que
  // o usuario navegue de volta pelos menus.
  if (startupWifiProvisioning &&
      WiFi.status() == WL_CONNECTED) {
    enterApplicationAfterStartup();
  }
}

// ============================================================
// SETUP
// ============================================================

void setup() {
  // PRIMEIRA ACAO DO FIRMWARE:
  // inicializa o expansor I2C onboard e desliga os LEDs ligados a
  // EX0 (branco) e EX1 (laranja) o mais cedo possivel.
  expanderReady = expander.begin(true, false);
  expander.digitalWrite(EX0, LOW);
  expander.digitalWrite(EX1, LOW);

  Serial.begin(115200);

  Serial.println();
  Serial.println("========================================");
  Serial.println(APP_NAME);
  Serial.print("Versao: ");
  Serial.println(APP_VERSION);
  Serial.print("Build: ");
  Serial.println(APP_BUILD_DATE);
  Serial.print("LIB ES32Lab: ");
  Serial.println(ES32LAB_VERSION);
  Serial.print("ES_WiFi: ");
  Serial.println(ES_WIFI_VERSION);
  Serial.print("PCF8574 onboard: ");
  Serial.println(expanderReady ? "OK" : "FALHA");
  Serial.println("LED EX0/EX1: OFF");
  Serial.println("========================================");

  raceMutex = xSemaphoreCreateMutex();
  catalogMutex = xSemaphoreCreateMutex();
  selectionMutex = xSemaphoreCreateMutex();
  scanMutex = xSemaphoreCreateMutex();
  photoMutex = xSemaphoreCreateMutex();

  prefs.begin("es32-eleicao", false);
  wifiPrefs.begin(WIFI_PREFS_NAMESPACE, false);

  seedElectionCatalog();
  loadPollSetting();
  loadProfile();
  loadWifiList();

  display.init();
  display.setRotation(3);
  display.setTextSize(1);

  // Etapa 1 da abertura: QR Code ES Developer.
  // Rede, armazenamento e relogio continuam sendo preparados em paralelo.
  renderStartupSplash();
  splashTimer.resetMillis();

  initLittleFs();
  configureClockForUf(activeUf);

  xTaskCreatePinnedToCore(
    networkTask,
    "TSE-Network",
    12288,
    nullptr,
    1,
    &networkTaskHandle,
    0
  );

  WiFi.persistent(false);
  WiFi.setAutoReconnect(false);

  if (wifiCount > 0) {
    startWifiAutoCycle();
  } else {
    startupWifiCycleFinished = true;
  }
}

// ============================================================
// LOOP
// ============================================================

void loop() {
  serviceWifiConnection();
  serviceWifiSuccessFlow();
  serviceStartupFlow();

  // Durante QR Code, apresentacao e confirmacao de Wi-Fi nao ha navegacao.
  // O teclado volta a atuar ao entrar no scanner ou no aplicativo.
  if (!startupSplashActive &&
      !startupPresentationActive &&
      !wifiSuccessActive) {
    handleInput();
  }

  if (
    startupAppEntered &&
    WiFi.status() == WL_CONNECTED &&
    pollTimer.intervalMillis(POLL_OPTIONS_MS[pollOptionIndex])
  ) {
    refreshRequested = true;
  }

  // Nao desenha o relogio sobre QR Code ou splash de apresentacao.
  if (!startupSplashActive &&
      !startupPresentationActive &&
      !wifiSuccessActive &&
      clockUiTimer.intervalMillis(5000UL)) {
    drawClockOnly();
  }

  // Cursor do editor de senha: somente o sublinhado pisca.
  if (view == VIEW_PASSWORD &&
      passwordCursorTimer.intervalMillis(380UL)) {
    passwordCursorVisible = !passwordCursorVisible;
    drawPasswordEntryLine();
  }

  // Atualiza somente a pagina de resultado que realmente mudou.
  // Atualizacoes de outros cargos sao consumidas silenciosamente; ao
  // navegar para eles, renderResults() ja usara os dados mais recentes.
  uint8_t dirtyRaceMask = takeRaceUiDirtyMask();
  bool statusDirty = takeStatusUiDirty();

  if (view == VIEW_RESULTS) {
    if (resultPageIsStatus(resultPage)) {
      if (statusDirty)
        renderResultStatus();
    }
    else {
      uint8_t visibleSlot = raceSlotForPage(resultPage);

      if (
        visibleSlot < RACE_SLOTS &&
        (dirtyRaceMask & (1U << visibleSlot))
      ) {
        renderResults();
      }
    }
  }

  // O ele-c.json serve apenas para atualizar a disponibilidade
  // do 2o turno da eleicao geral configurada.
  if (catalogUiDirty) {
    catalogUiDirty = false;

    if (!profileWasSaved && view == VIEW_ELECTION) {
      editUf = findUfIndex("SP");
      editRound = suggestedRoundForYear(APP_ELECTION_YEAR, editUf);
      renderElectionEditor();
    }
    else if (view == VIEW_ELECTION) {
      ElectionProfile p;

      if (getElectionProfile(APP_ELECTION_YEAR, p) &&
          editRound == 2 &&
          !secondRoundAvailableForUf(p, editUf)) {
        editRound = 1;
      }

      renderElectionEditor();
    }
  }

  // Quando nao ha JSON, scan ou foto atual para tratar,
  // podemos antecipar apenas a proxima foto da tela.
  serviceIdlePhotoPreload();

  // Se terminou um scan enquanto o usuario esta nesta tela.
  static bool lastScanReady = false;
  if (view == VIEW_WIFI_SCAN && scanReady != lastScanReady) {
    lastScanReady = scanReady;
    renderWifiScan();
  }
}
