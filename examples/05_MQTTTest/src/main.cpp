/*
 * main.cpp — ESP32-S3 + SIM7672E MQTT Testi (PlatformIO)
 *
 * Bağımsız kütüphane: 05_MQTTTest.h / 05_MQTTTest.cpp
 * MQTT Broker: test.hibersoft.com.tr:2887
 *
 * Akış:
 *   1. Modemi başlat + LTE şebekeye kayıt ol
 *   2. LTE veri bağlantısı kur (PDP context)
 *   3. MENÜ: kayıtlı/otomatik/manuel token
 *   4. MQTT broker'a bağlan + subscribe
 *   5. 1 dakika boyunca MQTT publish (JSON: token + deviceId + data)
 *
 * Auth: publish payload JSON'ında "token" + "deviceId" alanları.
 * deviceId = clientId (sunucudan/manuel alınan; boşsa DEVICE_ID).
 *
 * Bağlantılar (ESP32-S3):
 *   GPIO 17 (TX) -> SIM7672E RX
 *   GPIO 16 (RX) <- SIM7672E TX
 *   GPIO  4      -> SIM7672E PWRKEY
 *   GPIO  5      -> SIM7672E RESET
 *
 * Serial Monitor: 115200 baud
 */

#include <Arduino.h>
#include "05_MQTTTest.h"

MqttTest gsm(17, 16, 4, 5);  // txPin, rxPin, pwrKeyPin, resetPin

// ====== AYARLAR ======
const char APN_STR[]     = "internet";
const char MQTT_BROKER[] = "test.hibersoft.com.tr";
const char MQTT_USER[]   = "testuser";
const char MQTT_PASS[]   = "PUBLIC_MQTT_2026_PASS";
const char PUB_TOPIC[]   = "test/esp32/sensor";
const char SUB_TOPIC[]   = "test/esp32/cmd";
const char DEVICE_ID[]   = "esp32-mqtt";
static const int MQTT_PORT = 2887;
// =====================

// ====== KAYITLI (SABİT) TOKEN ======
// Boş bırakılırsa "kayıtlı token" seçeneği devre dışı kalır.
const char SAVED_TOKEN[]     = "406b1d2fdfa35758a25cf1c47d4482bc5d4dd235a722fab5";   // örn: "4c86d683587f67a96ea48a8d45ab744d41720bd457454353"
const char SAVED_CLIENT_ID[] = "c_1a2c80499d400f6c";   // örn: "c_14c8cc4d950dbbf3"
// ===============================

char authToken[64] = "";
char clientId[32]   = "";

void printSeparator() {
    Serial.println("========================================");
}

// timeoutMs = 0 → sonsuza kadar bekle (bloklama)
String readLine(uint32_t timeoutMs) {
    String line = "";
    unsigned long start = millis();
    while (true) {
        if (timeoutMs > 0 && millis() - start >= timeoutMs) break;
        while (Serial.available()) {
            char c = (char)Serial.read();
            if (c == '\n' || c == '\r') {
                if (line.length() > 0) { line.trim(); return line; }
            } else {
                line += c;
            }
        }
        yield();
    }
    line.trim();
    return line;
}

void setup() {
    Serial.begin(115200);
    while (!Serial);

    // Onboard LED'i kapat (GPIO48, aktif-dusuk)
    pinMode(48, OUTPUT);
    digitalWrite(48, HIGH);

    printSeparator();
    Serial.println("  ESP32-S3 + SIM7672E MQTT Test");
    Serial.println("  HiberSoft MQTT Broker");
    printSeparator();
    Serial.println();

    // [1] Modem
    Serial.println("[1/5] Modem baslatiliyor...");
    if (!gsm.begin()) {
        Serial.println("[FAIL] Modem hatasi!");
        while (1) delay(1000);
    }
    Serial.println("[PASS] Modem hazir");
    gsm.setDebug(false);

    // Önceki oturumu temizle
    gsm.sendAT("AT+HTTPTERM", 2000);
    gsm.sendAT("AT+NETCLOSE", 5000);
    gsm.sendAT("AT+CGACT=0,1", 5000);
    delay(1000);

    // [2] LTE Network
    Serial.println("[2/5] LTE network bekleniyor...");
    for (int i = 0; i < 30 && !gsm.isRegistered(); i++) {
        delay(1000);
        Serial.print('.');
    }
    Serial.println();
    if (!gsm.isRegistered()) {
        Serial.println("[FAIL] Network yok!");
        while (1) delay(1000);
    }
    Serial.print("[PASS] Network OK | CSQ: ");
    Serial.println(gsm.getSignalQuality());

    // [3] LTE Veri Bağlantısı
    Serial.println("[3/5] LTE veri baglaniliyor...");
    if (!gsm.initGPRS(APN_STR)) {
        Serial.println("[FAIL] LTE veri hatasi!");
        while (1) delay(1000);
    }
    Serial.print("[PASS] IP: ");
    Serial.println(gsm.getLocalIP());
    Serial.println();

    // [4] Token — MENÜ
    Serial.println("[4/5] Token secimi");
    printSeparator();
    Serial.println("  [1] Kayitli token kullan");
    Serial.println("  [2] Otomatik token al (POST /token)");
    Serial.println("  [3] Manuel token gir");
    printSeparator();
    Serial.print("Secim (1, 2 veya 3 girin): ");

    String choice = readLine(0);
    Serial.println(choice.length() ? choice : "(bos)");

    if (choice == "1") {
        if (SAVED_TOKEN[0] == '\0') {
            Serial.println("[FAIL] Kayitli token bos! Otomatik aliniyor...");
            choice = "2";
        } else {
            strncpy(authToken, SAVED_TOKEN, sizeof(authToken) - 1);
            strncpy(clientId, SAVED_CLIENT_ID, sizeof(clientId) - 1);
            Serial.print("[OK] Kayitli token: ");
            Serial.println(authToken);
            Serial.print("[OK] Kayitli ClientId: ");
            Serial.println(clientId);
        }
    }

    if (choice == "3") {
        Serial.print("Token girin (48 karakter): ");
        String manual = readLine(60000);
        manual.trim();
        manual.toCharArray(authToken, sizeof(authToken));
        if (authToken[0] == '\0') {
            Serial.println("[FAIL] Token bos, otomatik aliniyor...");
            choice = "2";
        } else {
            Serial.print("[OK] Manuel token: ");
            Serial.println(authToken);

            Serial.print("ClientId girin (c_ ile baslar): ");
            String cid = readLine(60000);
            cid.trim();
            cid.toCharArray(clientId, sizeof(clientId));
            Serial.print("[OK] Manuel ClientId: ");
            Serial.println(clientId);
        }
    }

    if (choice == "2" || authToken[0] == '\0') {
        Serial.println("[TOKEN] Otomatik token aliniyor...");
        String token, cid;
        if (gsm.getToken(token, cid)) {
            token.toCharArray(authToken, sizeof(authToken));
            cid.toCharArray(clientId, sizeof(clientId));
            Serial.print("[PASS] Token: ");
            Serial.println(authToken);
            Serial.print("[PASS] ClientId: ");
            Serial.println(clientId);
        } else {
            Serial.println("[FAIL] Token alinamadi!");
        }
    }

    Serial.println();
    printSeparator();
    if (authToken[0] == '\0') {
        Serial.println("  Token yok — MQTT publish yapilamaz!");
        printSeparator();
        while (1) delay(1000);
    }

    // deviceId = clientId (sunucudan/manuel alınan; boşsa DEVICE_ID)
    const char *devId = (clientId[0] != '\0') ? clientId : DEVICE_ID;

    // [5] MQTT bağlan + subscribe
    Serial.println("[5/5] MQTT broker'a baglaniliyor...");
    Serial.print("  Broker: "); Serial.print(MQTT_BROKER);
    Serial.print(':'); Serial.println(MQTT_PORT);
    gsm.setDebug(true);   // MQTT teşhisi için AT trafiğini aç
    if (!gsm.mqttConnect(MQTT_BROKER, MQTT_PORT, devId, MQTT_USER, MQTT_PASS)) {
        Serial.println("[FAIL] MQTT baglanti hatasi!");
        gsm.closeGPRS();
        while (1) delay(1000);
    }
    Serial.println("[PASS] MQTT baglanildi!");
    gsm.setDebug(false);

    if (gsm.mqttSubscribe(SUB_TOPIC)) Serial.println("[PASS] Subscribe OK");
    else Serial.println("[WARN] Subscribe hatasi, devam...");

    Serial.println("  1 dakika boyunca MQTT publish basliyor");
    Serial.println("  (10 sn'de bir publish gonderilecek)");
    printSeparator();
    Serial.println();
}

void loop() {
    static unsigned long startMs  = millis();
    static unsigned long lastSend = 0;
    static int count = 0;

    // 60 saniye boyunca her 10 saniyede bir publish
    if (millis() - startMs > 60000) {
        Serial.println();
        printSeparator();
        Serial.print("  Test bitti! Toplam publish: ");
        Serial.println(count);
        printSeparator();
        gsm.mqttDisconnect();
        gsm.closeGPRS();
        while (1) delay(1000);
    }

    // Gelen mesajları dinle (kısa, non-blocking)
    String incoming = gsm.mqttLoop(100);
    if (incoming.length() > 0) {
        Serial.print("  [SUB] Gelen: ");
        Serial.println(incoming);
    }

    if (millis() - lastSend >= 10000) {
        lastSend = millis();
        count++;

        float temp = 24.0 + (count % 10) * 0.3;
        float humi = 55.0 + (count % 20) * 0.8;

        // deviceId = clientId (sunucudan/manuel alınan; boşsa DEVICE_ID)
        const char *devId = (clientId[0] != '\0') ? clientId : DEVICE_ID;

        // MQTT publish payload'ı: token + deviceId + data
        String json = String("{\"token\":\"") + authToken +
                      "\",\"deviceId\":\"" + devId +
                      "\",\"data\":{\"temp\":" + String(temp, 1) +
                      ",\"humi\":" + String(humi, 1) + "}}";

        Serial.print("[PUB ");
        Serial.print(count);
        Serial.print("/6] ");
        Serial.println(json);

        if (!gsm.mqttPublish(PUB_TOPIC, json)) {
            Serial.println("[FAIL] Publish basarisiz!");
            gsm.mqttDisconnect();
            gsm.closeGPRS();
            while (1) delay(1000);
        }
        Serial.println();
    }

    delay(100);
}
