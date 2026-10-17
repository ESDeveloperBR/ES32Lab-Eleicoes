#include <Arduino.h>
#include <ES32Lab.h>
#include "ES_WifiScanService.h"

// Opcional: altera a janela inicial com begin(Serial, 1000).
ES_WifiScanService wifiScanner;

void setup() {
    Serial.begin(115200);
    wifiScanner.begin(Serial);  // 500 ms padrao; segura o setup em manutencao

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
