/*
 * main.cpp — ESP32-S3 + SIM7672E HTTP Testi (PlatformIO)
 *
 * Bağımsız kütüphane: 02_HTTPTest.h / 02_HTTPTest.cpp
 * Test Sunucusu: test.hibersoft.com.tr:2884
 *
 * Akış:
 *   1. Modemi başlat + LTE şebekeye kayıt ol
 *   2. LTE veri bağlantısı kur (PDP context)
 *   3. MENÜ: kayıtlı/otomatik/manuel token
 *   4. 1 dakika boyunca HTTP POST ile /ingest veri gönder
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
#include "02_HTTPTest.h"

HttpTest gsm(17, 16, 4, 5);  // txPin, rxPin, pwrKeyPin, resetPin

// ====== AYARLAR ======
// APN: Turkcell/Vodafone: "internet", Turk Telekom: "tt"
const char APN_STR[]   = "internet";
const char DEVICE_ID[] = "esp32-sim7672e";
// =====================

// ====== KAYITLI (SABİT) TOKEN ======
// Boş bırakılırsa "kayıtlı token" seçeneği devre dışı kalır.
// Token reset'te kaybolmaz; buraya yazılan değer kalıcıdır.
const char SAVED_TOKEN[]     = "c7ce763c827382f5a8d8a68e56c68682d74e8d73996ae881";   // örn: "4c86d683587f67a96ea48a8d45ab744d41720bd457454353"
const char SAVED_CLIENT_ID[] = "c_8a5c209e4539f868";   // örn: "c_14c8cc4d950dbbf3"
// ===============================

char authToken[64] = "";
char clientId[32]   = "";

void printSeparator() {
    Serial.println("========================================");
}

// Serial'dan satır oku (timeoutMs ms bekler, aşarsa boş döner)
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
    Serial.println("  ESP32-S3 + SIM7672E HTTP Test");
    Serial.println("  HiberSoft IoT Test Sunucusu");
    printSeparator();
    Serial.println();

    // [1] Modem
    Serial.println("[1/4] Modem baslatiliyor...");
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
    Serial.println("[2/4] LTE network bekleniyor...");
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
    Serial.println("[3/4] LTE veri baglaniliyor...");
    if (!gsm.initGPRS(APN_STR)) {
        Serial.println("[FAIL] LTE veri hatasi!");
        while (1) delay(1000);
    }
    Serial.print("[PASS] IP: ");
    Serial.println(gsm.getLocalIP());
    Serial.println();

    // [4] Token — MENÜ
    Serial.println("[4/4] Token secimi");
    printSeparator();
    Serial.println("  [1] Kayitli token kullan");
    Serial.println("  [2] Otomatik token al (POST /token)");
    Serial.println("  [3] Manuel token gir");
    printSeparator();
    Serial.print("Secim (1, 2 veya 3 girin): ");

    String choice = readLine(0);  // kullanıcı seçim yapana kadar bekle
    Serial.println(choice.length() ? choice : "(bos)");

    if (choice == "1") {
        // Kayıtlı (sabit) token
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
        // Manuel token + clientId
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
        Serial.println("  Token yok — HTTP POST yapilamaz!");
        printSeparator();
        while (1) delay(1000);
    }
    Serial.println("  1 dakika boyunca HTTP POST basliyor");
    Serial.println("  (5 sn'de bir /ingest veri gonderilecek)");
    printSeparator();
    Serial.println();
}

void loop() {
    static unsigned long startMs  = millis();
    static unsigned long lastPost = 0;
    static int count = 0;

    // 60 saniye boyunca her 5 saniyede bir POST
    if (millis() - startMs > 60000) {
        Serial.println();
        printSeparator();
        Serial.print("  Test bitti! Toplam POST: ");
        Serial.println(count);
        printSeparator();
        gsm.closeGPRS();
        while (1) delay(1000);
    }

    if (millis() - lastPost >= 5000) {
        lastPost = millis();
        count++;

        // Örnek veri (her seferinde hafifçe değişir)
        float temp = 24.0 + (count % 10) * 0.3;
        float humi = 55.0 + (count % 20) * 0.8;

        // deviceId = clientId (manuel girilen / sunucudan alınan değer).
        // clientId boşsa sabit DEVICE_ID yedek olarak kullanılır.
        const char *devId = (clientId[0] != '\0') ? clientId : DEVICE_ID;

        String json = String("{\"deviceId\":\"") + devId +
                      "\",\"data\":{\"temp\":" + String(temp, 1) +
                      ",\"humi\":" + String(humi, 1) + "}}";

        Serial.print("[POST ");
        Serial.print(count);
        Serial.print("/12] ");
        Serial.println(json);

        String resp;
        int code = 0;
        if (gsm.postIngest(authToken, devId, json, resp, code)) {
            Serial.print("  -> HTTP ");
            Serial.print(code);
            Serial.print(" | Yanit: ");
            Serial.println(resp);
        } else {
            Serial.print("  -> [FAIL] HTTP ");
            Serial.println(code);
        }
        Serial.println();
    }

    delay(100);
}
