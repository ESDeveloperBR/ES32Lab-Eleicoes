#ifndef ES_WIFI_H
#define ES_WIFI_H

#include <Arduino.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <ArduinoJson.h>

#define ES_WIFI_VERSION "0.2.2" // 12/08/2025"   // mm/dd/yyyy

class ES_WiFi {
public:
    ES_WiFi(const char* ssid = "", const char* password = "");
    bool begin(JsonVariant jsonNetwork); // Método para iniciar o WiFi com base no JSON
    int getRSSI(bool asPercent = false); // Método para obter a força do sinal Wi-Fi em porcentagem ou dBm
    String scanWifi(bool asPercent); // Método para escanear redes Wi-Fi disponíveis e retornar um JSON
    //bool connect();
    void disconnect();
    bool isConnected();


private:
    void _swapJsonArray(JsonArray array, int idA, int idB);
    const char* _ssid;
    const char* _password;
};

#endif