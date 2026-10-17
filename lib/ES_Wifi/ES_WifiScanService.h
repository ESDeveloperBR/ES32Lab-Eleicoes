#pragma once

// ES_WifiScanService v0.3.0 - prototipo de manutencao na inicializacao ES32Lab.
// JSON Lines (protocolo v1) via HardwareSerial; sem ArduinoJson nem callbacks.
// A janela de entrada bloqueia brevemente o setup(); apos o prazo sem comando
// reconhecido, o servico NAO monitora nem consome mais dados da serial.
// Em modo de manutencao, o begin() aguarda ate receber "exit" ou "reset".

#include <Arduino.h>
#include <ES32Lab.h>
#include <HardwareSerial.h>
#include <WiFi.h>
#include <atomic>
#include <cstring>
#include <cstdio>
#include <cstdlib>

class ES_WifiScanService {
public:
    static constexpr uint16_t PROTOCOL_VERSION = 1;
    static constexpr size_t RX_CAPACITY = 256;
    static constexpr size_t TX_LINE_LIMIT = 16384;
    static constexpr uint16_t MAX_NETWORKS = 128;

    ES_WifiScanService() = default;
    ES_WifiScanService(const ES_WifiScanService&) = delete;
    ES_WifiScanService& operator=(const ES_WifiScanService&) = delete;

    // A UART deve estar inicializada (ex.: Serial.begin(115200)).
    // Abre uma janela de ate windowMs milissegundos, padrao 500.
    // Apenas comandos reconhecidos (hello, wifi_scan, reset, exit) assumem
    // o atendimento. Uma vez ativado, nao ha timeout de inatividade.
    // "exit" libera o setup(); "reset" reinicia o ESP32.
    // Caso nao receba comando reconhecido, retorna sem monitoramento em segundo plano.
    void begin(HardwareSerial& serial, uint32_t windowMs = 500) {
        if (windowMs == 0) return;

        char rx[RX_CAPACITY] = {};
        size_t rxSize = 0;
        bool dropping = false;
        bool maintenance = false;
        ES_TimeInterval window;
        window.resetMillis();

        while (maintenance || !window.intervalMillis(windowMs)) {
            if (serial.available() <= 0) {
                delay(1);  // cede CPU; nenhuma tarefa/callback extra
                continue;
            }

            const int ch = serial.read();
            if (ch < 0) continue;

            if (ch == '\n') {
                if (!dropping && rxSize > 0) {
                    rx[rxSize] = '\0';
                    const Command cmd = identify(rx);
                    if (cmd != Command::None) {
                        maintenance = true;
                        // Retorno true significa que "exit" liberou o setup.
                        if (execute(serial, cmd)) return;
                    }
                }
                rxSize = 0;
                dropping = false;
            } else if (!dropping) {
                if (rxSize + 1 < RX_CAPACITY)
                    rx[rxSize++] = static_cast<char>(ch);
                else
                    dropping = true;  // linha >255 bytes: descarta ate LF
            }
        }
        // Sem handshake: a aplicacao volta a ser a unica leitora da serial.
    }

    // Mantido por compatibilidade. A v0.3.0 nao registra callbacks;
    // portanto end() nao tem trabalho a realizar.
    void end() {}

    // Uso interno opcional: escreve {"networks":[...]} + LF no Print indicado.
    // Nao conecta Wi-Fi, nao salva senhas. Retorna false em falha/ocupado.
    bool scan(Print& destination) {
        if (_busy.exchange(true)) return false;
        const bool ok = scanAndWrite(destination, false);
        _busy.store(false);
        return ok;
    }

private:
    std::atomic<bool> _busy{false};

    enum class Command : uint8_t { None, Hello, WifiScan, Reset, Exit };

    static void space(const char*& p) {
        while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') ++p;
    }

    static bool readString(const char*& p, const char*& start, size_t& length) {
        if (*p != '"') return false;
        ++p;
        start = p;
        while (*p) {
            const unsigned char c = static_cast<unsigned char>(*p++);
            if (c == '"') {
                length = static_cast<size_t>((p - 1) - start);
                return true;
            }
            if (c < 0x20) return false;
            if (c != '\\') continue;
            const char escape = *p++;
            if (!escape) return false;
            if (escape == 'u') {
                for (unsigned i = 0; i < 4; ++i) {
                    if (!*p) return false;
                    const char h = *p++;
                    if (!((h >= '0' && h <= '9') || (h >= 'A' && h <= 'F') ||
                          (h >= 'a' && h <= 'f'))) return false;
                }
            } else if (escape != '"' && escape != '\\' && escape != '/' &&
                       escape != 'b' && escape != 'f' && escape != 'n' &&
                       escape != 'r' && escape != 't') {
                return false;
            }
        }
        return false;
    }

    static bool skipValue(const char*& p, uint8_t depth) {
        if (depth > 6) return false;
        space(p);
        if (*p == '"') {
            const char* s = nullptr;
            size_t n = 0;
            return readString(p, s, n);
        }
        if (*p == '{' || *p == '[') {
            const bool object = (*p++ == '{');
            space(p);
            if (*p == (object ? '}' : ']')) { ++p; return true; }
            for (;;) {
                if (object) {
                    const char* key = nullptr;
                    size_t length = 0;
                    if (!readString(p, key, length)) return false;
                    space(p);
                    if (*p != ':') return false;
                    ++p;
                }
                if (!skipValue(p, depth + 1)) return false;
                space(p);
                if (*p == (object ? '}' : ']')) { ++p; return true; }
                if (*p != ',') return false;
                ++p;
                space(p);
            }
        }
        if (!strncmp(p, "true", 4))  { p += 4; return true; }
        if (!strncmp(p, "false", 5)) { p += 5; return true; }
        if (!strncmp(p, "null", 4))  { p += 4; return true; }

        // Numero JSON: [-] inteiro [.] [expoente].
        const char* q = p;
        if (*p == '-') ++p;
        if (*p == '0') ++p;
        else {
            if (*p < '1' || *p > '9') return false;
            while (*p >= '0' && *p <= '9') ++p;
        }
        if (*p == '.') {
            ++p;
            if (*p < '0' || *p > '9') return false;
            while (*p >= '0' && *p <= '9') ++p;
        }
        if (*p == 'e' || *p == 'E') {
            ++p;
            if (*p == '+' || *p == '-') ++p;
            if (*p < '0' || *p > '9') return false;
            while (*p >= '0' && *p <= '9') ++p;
        }
        return p != q;
    }

    // Valida o objeto de topo: "cmd" precisa ser uma string direta;
    // os demais campos JSON sao aceitos e ignorados.
    static bool parseCommand(const char* text, const char*& cmd, size_t& size) {
        const char* p = text;
        space(p);
        if (*p != '{') return false;
        ++p;
        space(p);
        bool found = false;
        if (*p != '}') {
            for (;;) {
                const char* key = nullptr;
                size_t keySize = 0;
                if (!readString(p, key, keySize)) return false;
                space(p);
                if (*p != ':') return false;
                ++p;
                space(p);
                if (keySize == 3 && !memcmp(key, "cmd", 3)) {
                    if (found || !readString(p, cmd, size)) return false;
                    found = true;
                } else if (!skipValue(p, 0)) {
                    return false;
                }
                space(p);
                if (*p == '}') break;
                if (*p != ',') return false;
                ++p;
                space(p);
            }
        }
        ++p; // '}'
        space(p);
        return found && *p == '\0';
    }

    static bool isCommand(const char* cmd, size_t n, const char* expected) {
        const size_t len = strlen(expected);
        return n == len && memcmp(cmd, expected, len) == 0;
    }

    // Cada resposta curta tambem utiliza uma unica operacao write().
    // O LF inicial impede que um print() anterior deixe o JSON na mesma linha.
    static void error(Print& out, const char* cmd, const char* code, const char* message) {
        char line[240];
        const int n = snprintf(line, sizeof(line),
            "\n{\"cmd\":\"%s\",\"status\":\"error\",\"data\":{\"code\":\"%s\",\"message\":\"%s\"}}\n",
            cmd, code, message);
        if (n > 0 && static_cast<size_t>(n) < sizeof(line))
            out.write(reinterpret_cast<const uint8_t*>(line), static_cast<size_t>(n));
    }

    // Comandos fora da lista fechada sao ignorados, inclusive na manutencao.
    // Nao ha interpretacao generica nem extensoes externas.
    static Command identify(const char* line) {
        const char* cmd = nullptr;
        size_t size = 0;
        if (!parseCommand(line, cmd, size)) return Command::None;

        if (isCommand(cmd, size, "hello"))     return Command::Hello;
        if (isCommand(cmd, size, "wifi_scan")) return Command::WifiScan;
        if (isCommand(cmd, size, "reset"))     return Command::Reset;
        if (isCommand(cmd, size, "exit"))      return Command::Exit;
        return Command::None;
    }

    // Executa somente comandos internos permitidos. true = sair do begin().
    bool execute(HardwareSerial& serial, Command cmd) {
        switch (cmd) {
            case Command::Hello: {
                static const char hello[] =
                    "\n{\"cmd\":\"hello\",\"status\":\"done\",\"data\":{"
                    "\"device\":\"es32lab-app\",\"protocol_version\":1,"
                    "\"firmware_version\":\"unspecified\","
                    "\"capabilities\":[\"wifi_scan\",\"reset\",\"exit\"]}}\n";
                serial.write(reinterpret_cast<const uint8_t*>(hello), sizeof(hello) - 1);
                return false;
            }
            case Command::WifiScan: {
                if (_busy.exchange(true)) {
                    error(serial, "wifi_scan", "BUSY", "Scanner ocupado.");
                    return false;
                }
                const bool ok = scanAndWrite(serial, true);
                _busy.store(false);
                if (!ok) error(serial, "wifi_scan", "SCAN_FAILED", "Falha no escaneamento.");
                return false;
            }
            case Command::Reset: {
                static const char response[] =
                    "\n{\"cmd\":\"reset\",\"status\":\"done\",\"data\":{}}\n";
                serial.write(reinterpret_cast<const uint8_t*>(response), sizeof(response) - 1);
                serial.flush();  // tenta transmitir a resposta antes do reset
                ESP.restart();
                return true;     // protecao caso restart retorne inesperadamente
            }
            case Command::Exit: {
                static const char response[] =
                    "\n{\"cmd\":\"exit\",\"status\":\"done\",\"data\":{}}\n";
                serial.write(reinterpret_cast<const uint8_t*>(response), sizeof(response) - 1);
                serial.flush();
                return true;
            }
            case Command::None:
            default: return false;
        }
    }

    // Buffer curto, reservado somente durante a construcao da resposta.
    struct Buffer {
        char* data;
        size_t capacity;
        size_t length;
        bool valid;

        // Construtor explicito para compatibilidade com C++11 (PlatformIO).
        Buffer(char* storage, size_t storageSize)
            : data(storage), capacity(storageSize), length(0), valid(true) {}

        void raw(const char* text, size_t n) {
            if (!valid || n > capacity - length) {
                valid = false;
                return;
            }
            memcpy(data + length, text, n);
            length += n;
        }
        void text(const char* text) { raw(text, strlen(text)); }
        void byte(char value) { raw(&value, 1); }
    };

    // Preserva modo anterior sempre que necessario.
    bool scanAndWrite(Print& out, bool protocolEnvelope) {
        const wifi_mode_t previousMode = WiFi.getMode();
        bool changed = false;

        if (previousMode == WIFI_OFF || previousMode == WIFI_AP) {
            const wifi_mode_t scanMode =
                previousMode == WIFI_AP ? WIFI_AP_STA : WIFI_STA;
            if (!WiFi.mode(scanMode)) return false;
            changed = true;
        }

        const int16_t count = WiFi.scanNetworks(false, false);
        if (count < 0) {
            WiFi.scanDelete();
            if (changed) WiFi.mode(previousMode);
            return false;
        }

        // Duas passagens sobre os resultados: mede, aloca o necessario e monta.
        // A memoria e liberada apos uma unica chamada write().
        char* frame = nullptr;
        size_t length = 0;
        const bool built = buildNetworks(count, protocolEnvelope, frame, length);

        WiFi.scanDelete();
        if (changed) WiFi.mode(previousMode);

        if (!built) return false;

        const size_t sent =
            out.write(reinterpret_cast<const uint8_t*>(frame), length);
        free(frame);
        return sent == length;
    }

    // Numero de bytes de um caracter UTF-8 valido; 0 para byte invalido.
    static uint8_t validUtf8(const String& s, size_t i) {
        const size_t n = s.length();
        const uint8_t a = static_cast<uint8_t>(s[i]);
        if (a < 0x80) return 1;
        if (a < 0xC2 || a > 0xF4) return 0;
        const uint8_t len = (a < 0xE0) ? 2 : ((a < 0xF0) ? 3 : 4);
        if (i + len > n) return 0;
        const uint8_t b = static_cast<uint8_t>(s[i + 1]);
        if (b < 0x80 || b > 0xBF) return 0;
        if (len == 2) return 2;
        if ((a == 0xE0 && b < 0xA0) || (a == 0xED && b > 0x9F) ||
            (a == 0xF0 && b < 0x90) || (a == 0xF4 && b > 0x8F)) return 0;
        for (uint8_t j = 2; j < len; ++j) {
            const uint8_t c = static_cast<uint8_t>(s[i + j]);
            if (c < 0x80 || c > 0xBF) return 0;
        }
        return len;
    }

    // Calcula tamanho escapado (out == nullptr) ou insere no buffer.
    static size_t escapeSSID(Buffer* out, const String& ssid) {
        size_t bytes = 0;
        const char hex[] = "0123456789abcdef";
        for (size_t i = 0; i < ssid.length();) {
            const uint8_t c = static_cast<uint8_t>(ssid[i]);
            if (c == '"' || c == '\\') {
                if (out) { out->byte('\\'); out->byte(static_cast<char>(c)); }
                bytes += 2;
                ++i;
            } else if (c < 0x20) {
                if (out) {
                    const char seq[6] = {'\\', 'u', '0', '0', hex[c >> 4], hex[c & 15]};
                    out->raw(seq, sizeof(seq));
                }
                bytes += 6;
                ++i;
            } else {
                const uint8_t len = validUtf8(ssid, i);
                if (!len) {
                    if (out) out->byte('?');
                    ++bytes;
                    ++i;
                } else {
                    if (out) out->raw(ssid.c_str() + i, len);
                    bytes += len;
                    i += len;
                }
            }
        }
        return bytes;
    }

    // Aloca somente o espaco necessario para uma mensagem JSON-Lines.
    // O LF inicial separa a resposta de qualquer linha de log incompleta.
    static bool buildNetworks(int16_t count, bool envelope,
                              char*& frame, size_t& length) {
        const char* head = envelope
            ? "\n{\"cmd\":\"wifi_scan\",\"status\":\"done\",\"data\":{\"networks\":["
            : "\n{\"networks\":[";
        const char* tail = envelope ? "}}\n" : "}\n";
        const char* extra = ",\"truncated\":true";
        const size_t extraLen = strlen(extra);
        const size_t maxBytes = TX_LINE_LIMIT + 1; // + LF inicial, fora da linha JSON

        size_t needed = strlen(head) + 1 + strlen(tail);
        if (needed + extraLen > maxBytes) return false;

        bool truncated = count > MAX_NETWORKS;
        bool first = true;
        int16_t lastIncluded = 0;

        for (int16_t i = 0; i < count && i < MAX_NETWORKS; ++i) {
            String ssid;
            uint8_t auth = 0;
            int32_t rssi = 0;
            uint8_t* bssid = nullptr;
            int32_t channel = 0;
            if (!WiFi.getNetworkInfo(static_cast<uint8_t>(i), ssid, auth, rssi, bssid, channel))
                continue;
            if (ssid.length() == 0) continue;

            char rssiText[16];
            snprintf(rssiText, sizeof(rssiText), "%ld", static_cast<long>(rssi));
            const size_t entry = (first ? 0 : 1) +
                strlen("{\"ssid\":\"") + escapeSSID(nullptr, ssid) +
                strlen("\",\"rssi\":") + strlen(rssiText) +
                strlen(",\"secure\":") + ((auth != WIFI_AUTH_OPEN) ? 4 : 5) + 1;
            // Reserva margem para o marcador de truncamento se necessario.
            if (needed + entry + extraLen > maxBytes) {
                truncated = true;
                break;
            }

            needed += entry;
            first = false;
            lastIncluded = i + 1;
        }

        if (truncated) needed += extraLen;
        frame = static_cast<char*>(malloc(needed));
        if (!frame) return false;

        Buffer out{frame, needed};
        out.text(head);
        first = true;
        for (int16_t i = 0; i < lastIncluded; ++i) {
            String ssid;
            uint8_t auth = 0;
            int32_t rssi = 0;
            uint8_t* bssid = nullptr;
            int32_t channel = 0;
            if (!WiFi.getNetworkInfo(static_cast<uint8_t>(i), ssid, auth, rssi, bssid, channel))
                continue;
            if (ssid.length() == 0) continue;

            if (!first) out.byte(',');
            out.text("{\"ssid\":\"");
            escapeSSID(&out, ssid);
            out.text("\",\"rssi\":");

            char rssiText[16];
            snprintf(rssiText, sizeof(rssiText), "%ld", static_cast<long>(rssi));
            out.text(rssiText);
            out.text(",\"secure\":");
            out.text(auth != WIFI_AUTH_OPEN ? "true}" : "false}");
            first = false;
        }
        out.byte(']');
        if (truncated) out.text(extra);
        out.text(tail);

        if (!out.valid) {
            free(frame);
            frame = nullptr;
            return false;
        }
        length = out.length;
        return true;
    }
};
