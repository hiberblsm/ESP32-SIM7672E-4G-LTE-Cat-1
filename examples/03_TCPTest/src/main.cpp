/*
 * main.cpp — ESP32-S3 + SIM7672E TCP Soket Testi (PlatformIO)
 *
 * Bağımsız kütüphane: 03_TCPTest.h / 03_TCPTest.cpp
 * Test Sunucusu: test.hibersoft.com.tr:2885 (TCP)
 *
 * Akış:
 *   1. Modemi başlat + LTE şebekeye kayıt ol
 *   2. LTE veri bağlantısı kur (PDP context)
 *   3. MENÜ: otomatik token al veya manuel token + clientId gir
 *   4. TCP bağlan (port 2885)
 *   5. 1 dakika boyunca TCP ile JSON satırı gönder
 *
 * TCP formatı: her mesaj tek satır JSON + '\n'
 * Auth: JSON body içinde "token" alanı (header değil!)
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
#include "03_TCPTest.h"

TcpTest gsm(17, 16, 4, 5);  // txPin, rxPin, pwrKeyPin, resetPin

// ====== AYARLAR ======
const char APN_STR[]   = "internet";
const char TCP_HOST[]  = "test.hibersoft.com.tr";
const char DEVICE_ID[] = "esp32-tcp";
static const int TCP_PORT = 2885;
// =====================

// ====== KAYITLI (SABİT) TOKEN ======
// Boş bırakılırsa "kayıtlı token" seçeneği devre dışı kalır.
// Token reset'te kaybolmaz; buraya yazılan değer kalıcıdır.
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
    Serial.println("  ESP32-S3 + SIM7672E TCP Test");
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
        Serial.println("  Token yok — TCP veri gonderilemez!");
        printSeparator();
        while (1) delay(1000);
    }

    // TCP bağlan
    Serial.print("TCP baglaniyor: ");
    Serial.print(TCP_HOST);
    Serial.print(':');
    Serial.println(TCP_PORT);
    if (!gsm.tcpConnect(TCP_HOST, TCP_PORT)) {
        Serial.println("[FAIL] TCP baglanti kurulamadi!");
        gsm.closeGPRS();
        while (1) delay(1000);
    }
    Serial.println("[PASS] TCP baglanildi!");

    Serial.println("  1 dakika boyunca TCP veri gonderimi basliyor");
    Serial.println("  (5 sn'de bir JSON satiri gonderilecek)");
    printSeparator();
    Serial.println();
}

void loop() {
    static unsigned long startMs  = millis();
    static unsigned long lastSend = 0;
    static int count = 0;

    // 60 saniye boyunca her 5 saniyede bir gönder
    if (millis() - startMs > 60000) {
        Serial.println();
        printSeparator();
        Serial.print("  Test bitti! Toplam gonderim: ");
        Serial.println(count);
        printSeparator();
        gsm.tcpClose();
        gsm.closeGPRS();
        while (1) delay(1000);
    }

    if (millis() - lastSend >= 5000) {
        lastSend = millis();
        count++;

        float temp = 24.0 + (count % 10) * 0.3;
        float humi = 55.0 + (count % 20) * 0.8;

        // deviceId = clientId (manuel girilen / sunucudan alınan değer).
        // clientId boşsa sabit DEVICE_ID yedek olarak kullanılır.
        const char *devId = (clientId[0] != '\0') ? clientId : DEVICE_ID;

        // TCP: token JSON body içinde gider, mesaj '\n' ile biter
        String json = String("{\"token\":\"") + authToken +
                      "\",\"deviceId\":\"" + devId +
                      "\",\"data\":{\"temp\":" + String(temp, 1) +
                      ",\"humi\":" + String(humi, 1) + "}}\n";

        Serial.print("[SEND ");
        Serial.print(count);
        Serial.print("/12] ");
        Serial.println(json);

        if (!gsm.tcpSend(json)) {
            Serial.println("[FAIL] Gonderilemedi — TCP kesildi!");
            gsm.closeGPRS();
            while (1) delay(1000);
        }

        String rx = gsm.tcpReceive(3000);
        if (rx.length() > 0) {
            rx.trim();
            Serial.print("  -> Yanit: ");
            Serial.println(rx);
        }
        Serial.println();
    }

    delay(100);
}
