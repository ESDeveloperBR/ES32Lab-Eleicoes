#include "ES_Wifi.h"

ES_WiFi::ES_WiFi(const char* ssid, const char* password) {
    _ssid = ssid;
    _password = password;
}

/* Esse método ainda esta em desenvolvimento e só funciona com a lista WiFi */
void ES_WiFi::_swapJsonArray(JsonArray array, int idA, int idB) {
    if (idA == idB || idA >= array.size() || idB >= array.size()) return;
    String tempSSID = array[idA]["ssid"].as<String>();
    String tempPass = array[idA]["password"].as<String>();

    array[idA]["ssid"]     = array[idB]["ssid"];
    array[idA]["password"] = array[idB]["password"];

    array[idB]["ssid"]     = tempSSID;
    array[idB]["password"] = tempPass;
}




// <<< Método Begin >>>
bool ES_WiFi::begin(JsonVariant jsonNetwork) {
    Serial.println("=============== setupWiFi ===============");
    
    // Verifica se o módulo de rede está habilitado
    if (!jsonNetwork["enable"].as<bool>()) {
        Serial.println("Módulo de rede desabilitado no JSON - WiFi não será iniciado");
        return false;
    }
    String hostName     = jsonNetwork["hostName"].as<String>();
    bool softAPEnabled  = jsonNetwork["softAP"]["enable"].as<bool>();
    bool wifiConnected  = false;

    if (softAPEnabled) {
        // --- Configuração do SoftAP | SoftAP Configuration ---
        String apSSID     = jsonNetwork["softAP"]["ssid"].as<String>();
        String apPassword = jsonNetwork["softAP"]["password"].as<String>();

        Serial.println("Iniciando modo SoftAP...");
        WiFi.mode(WIFI_AP);

        // Definir hostname para SoftAP (opcional, nem todos os sistemas usam)
        if (!hostName.isEmpty()) {
            WiFi.softAPsetHostname(hostName.c_str());
            Serial.print("Hostname do SoftAP definido como: ");
            Serial.println(hostName);
        }
        
        if (WiFi.softAP(apSSID.c_str(), apPassword.c_str())) {
            Serial.println("SoftAP iniciado com sucesso!");
            Serial.print("SSID: ");
            Serial.println(apSSID);
            Serial.print("IP do SoftAP: ");
            Serial.println(WiFi.softAPIP());
            wifiConnected = true;
        } else {
            Serial.println("Falha ao iniciar SoftAP");
            return false;
        }
        
    } else {

        String staticIP = jsonNetwork["ip"].as<String>();
        String gateway  = jsonNetwork["gateway"].as<String>();
        String mask     = jsonNetwork["mask"].as<String>();
        String dns1     = jsonNetwork["dns1"].as<String>();
        String dns2     = jsonNetwork["dns2"].as<String>();
        
        WiFi.mode(WIFI_STA);

        // Definir hostname para Station
        if (!hostName.isEmpty()) {
            WiFi.setHostname(hostName.c_str());
            Serial.print("Hostname definido como: ");
            Serial.println(hostName);
        }        
        
        // Configure static IP if provided (IP não vazio) | Configura IP estático se fornecido (IP não vazio)
        if (!staticIP.isEmpty()) {
            IPAddress ip, gw, subnet, dns_primary, dns_secondary;
            
            ip.fromString(staticIP);
            subnet.fromString(mask.isEmpty() ? "255.255.255.0" : mask);
            
            // Se gateway estiver vazio, usa o IP da rede + .1 como padrão
            if (gateway.isEmpty()) {
                // Extrai os 3 primeiros octetos do IP e adiciona .1
                int lastDot = staticIP.lastIndexOf('.');
                if (lastDot > 0) {
                    gateway = staticIP.substring(0, lastDot) + ".1";
                }
            }
            gw.fromString(gateway);
            
            dns_primary.fromString(dns1.isEmpty()   ? "8.8.8.8" : dns1);
            dns_secondary.fromString(dns2.isEmpty() ? "8.8.4.4" : dns2);
            
            Serial.println("Configurando IP estático:");
            Serial.println("IP: "      + staticIP);
            Serial.println("Gateway: " + gateway);
            Serial.println("Máscara: " + (mask.isEmpty() ? "255.255.255.0" : mask));
            Serial.println("DNS1: "    + (dns1.isEmpty() ? "8.8.8.8" : dns1));
            Serial.println("DNS2: "    + (dns2.isEmpty() ? "8.8.4.4" : dns2));

            if (!WiFi.config(ip, gw, subnet, dns_primary, dns_secondary)) {
                Serial.println("Falha ao configurar IP estático");
            }
        } else {
            Serial.println("Usando DHCP para obter IP automaticamente");
        }

        // --- Gerenciamento de lista de SSIDs | SSID List Management ---
        for (int i = 0; i < jsonNetwork["wifi"].size(); i++) {
            String ssid     = jsonNetwork["wifi"][i]["ssid"].as<String>();
            String password = jsonNetwork["wifi"][i]["password"].as<String>();

            if (ssid.isEmpty()) {
                Serial.println("SSID vazio no JSON, pulando...");
                continue;
            }

            Serial.printf("Tentando conectar ao WiFi %d: %s\n", i + 1, ssid.c_str());
            WiFi.begin(ssid.c_str(), password.c_str());

            int attempts = jsonNetwork["attempts"].as<int>();
            if (attempts <= 0) attempts = 10; // Valor padrão de tentativas
            int contAttempts = 0;
            while (WiFi.status() != WL_CONNECTED && contAttempts < attempts) {
                delay(500);
                Serial.print(".");
                contAttempts++; // Incrementa o contador de tentativas
            }


            int minRssi = jsonNetwork["rssi"]["min"].as<int>();
            int wifiStrength = getRSSI(jsonNetwork["rssi"]["percent"].as<bool>());

            if (wifiStrength > minRssi && WiFi.status() == WL_CONNECTED) {

                if (i > 0) {    // Se não for o primeiro SSID, ajusta o JSON para mover o SSID conectado para o início
                    _swapJsonArray(jsonNetwork["wifi"], 0, i);
                    Serial.println("Lista de SSIDs atualizada, movendo SSID conectado para o início.");
                }

                Serial.println("\nConectado com sucesso!");
                Serial.print("IP obtido: ");
                Serial.println(WiFi.localIP());
                wifiConnected = true;
                break; // Sai do loop se conectado
            } else {
                Serial.println("\nFalha ao conectar, tentando próximo SSID...");

            }
        }

        
        if (WiFi.status() != WL_CONNECTED) {
            Serial.println("\nFalha ao conectar ao WiFi");
            softAPEnabled = true; // Marca como AP ativo para evitar reinicialização do WiFi
            // >>> Ativa o modo AP temporário sem salvar no JSON <<<
            String fallbackSSID     = jsonNetwork["softAP"]["ssid"].as<String>();
            String fallbackPassword = jsonNetwork["softAP"]["password"].as<String>();

            
            Serial.println("Ativando modo AP temporário...");
            WiFi.mode(WIFI_AP);
            WiFi.softAP(fallbackSSID.c_str(), fallbackPassword.c_str());
            Serial.print("SSID: ");
            Serial.println(fallbackSSID);
            Serial.print("IP do AP: ");
            Serial.println(WiFi.softAPIP());
            wifiConnected = true; // Considera conectado para seguir o fluxo
        } else {
            Serial.println("\nWiFi conectado!");
            Serial.println("SSID: " + WiFi.SSID());
            Serial.print("IP obtido: ");
            Serial.println(WiFi.localIP());
            wifiConnected = true;
        }
    }
    
    if (wifiConnected) {
        Serial.println("✅ WiFi configurado com sucesso!");

        if(WiFi.getMode() & WIFI_AP) {  // Verifica se o modo AP está ativo
            Serial.printf("Modo: SoftAP - IP: %s\n", WiFi.softAPIP().toString().c_str());
        } else {
            Serial.printf("Modo: Station - IP: %s\n", WiFi.localIP().toString().c_str());
        }

        // --- Configuração do mDNS | mDNS Configuration ---
        String hostName = jsonNetwork["hostName"].as<String>();

        if (!hostName.isEmpty()) {
            if (MDNS.begin(hostName.c_str())) {
                Serial.println("✅ mDNS iniciado!");
                Serial.printf("🌐 Acesse por: http://%s/\n", hostName.c_str());
            } else {
                Serial.println("❌ Falha ao iniciar mDNS");
            }
        }

    }
    
    Serial.println("==========================================");
    return wifiConnected;

}


/**
 * @brief Obtém a força do sinal Wi-Fi em porcentagem ou dBm
 * @param asPercent Se true, retorna a força do sinal em porcentagem; se false, retorna em dBm
 * @return A força do sinal Wi-Fi em porcentagem ou dBm
 */
int ES_WiFi::getRSSI(bool asPercent) {
    if(asPercent){
        if (WiFi.status() != WL_CONNECTED) {
            return 0; // Não conectado, sem sinal
        }    
        long rssi = WiFi.RSSI();
        int quality;
        if (rssi <= -100) {
            quality = 0;
        } else if (rssi >= -50) {
            quality = 100;
        } else {
            quality = 2 * (rssi + 100);
        }
        return quality;
    }else{
        return WiFi.RSSI();
    }
}






String ES_WiFi::scanWifi(bool asPercent) {
    int n = WiFi.scanNetworks(/*async=*/false, /*hidden=*/true);
    String json = "{\"scanwifi\":[";
    for (int i = 0; i < n; i++) {
        if (i > 0) json += ",";
        json += "{";
        json += "\"ssid\":\"" + WiFi.SSID(i) + "\"";
        long rssi = WiFi.RSSI(i);
        if (asPercent) {
            int quality;
            if (rssi <= -100) {
                quality = 0;
            } else if (rssi >= -50) {
                quality = 100;
            } else {
                quality = 2 * (rssi + 100);
            }
            json += ",\"rssi\":" + String(quality);
            json += ",\"unit\":\"%\"";
        } else {
            json += ",\"rssi\":" + String(rssi);
            json += ",\"unit\":\"dBm\"";
        }
        json += ",\"secure\":" + String(WiFi.encryptionType(i) != WIFI_AUTH_OPEN ? "true" : "false");
        json += "}";
    }
    json += "]}";
    return json;
}


void ES_WiFi::disconnect() {
    WiFi.disconnect();
}

bool ES_WiFi::isConnected() {
    return WiFi.status() == WL_CONNECTED;
}
