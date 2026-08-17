/*
 * main.cpp — ESP32-S3 + SIM7672E Serial / AT Komut Testi
 *
 * Bu sketch SIM7672E modülünün temel fonksiyonlarını test eder:
 *   - AT komut yanıtı (begin)
 *   - IMEI / IMSI bilgileri
 *   - Sinyal gücü (CSQ)
 *   - SIM kart durumu
 *   - Operatör bilgisi
 *   - LTE Network kayıt durumu
 *
 * Bağlantılar (ESP32-S3):
 *   GPIO 17 (TX) -> SIM7672E RX
 *   GPIO 16 (RX) <- SIM7672E TX
 *   GPIO  4      -> SIM7672E PWRKEY
 *   GPIO  5      -> SIM7672E RESET
 *
 * Serial Monitor: 115200 baud
 *
 * Kütüphane: src/01_SerialTest.h + src/01_SerialTest.cpp
 */

#include <Arduino.h>
#include "01_SerialTest.h"

SerialTest gsm(17, 16, 4, 5);  // txPin, rxPin, pwrKeyPin, resetPin

void printSeparator() {
    Serial.println("========================================");
}

void setup() {
    Serial.begin(115200);
    while (!Serial);

    printSeparator();
    Serial.println("  ESP32-S3 + SIM7672E Serial Test");
    Serial.println("  HiberSoft LTE Shield Library");
    printSeparator();
    Serial.println();

    // Modemi başlat
    Serial.println("[TEST] Modem baslatiliyor...");
    if (!gsm.begin()) {
        Serial.println("[FAIL] Modem baslatma hatasi!");
        Serial.println("  -> Kablo baglantilarini kontrol edin");
        Serial.println("  -> SIM7672E guc kaynagini kontrol edin");
        while (1) delay(1000);
    }
    Serial.println("[PASS] Modem hazir");
    Serial.println();

    // ===== TEST 1: IMEI =====
    Serial.print("[TEST] IMEI: ");
    String imei = gsm.getIMEI();
    if (imei.length() > 0) {
        Serial.println(imei);
        Serial.println("[PASS] IMEI okundu");
    } else {
        Serial.println("[FAIL] IMEI okunamadi");
    }
    Serial.println();

    // ===== TEST 2: IMSI =====
    Serial.print("[TEST] IMSI: ");
    String imsi = gsm.getIMSI();
    if (imsi.length() > 0) {
        Serial.println(imsi);
        Serial.println("[PASS] IMSI okundu");
    } else {
        Serial.println("[FAIL] IMSI okunamadi (SIM kart var mi?)");
    }
    Serial.println();

    // ===== TEST 3: SIM Durumu =====
    Serial.print("[TEST] SIM Durumu: ");
    String simStatus = gsm.getSIMStatus();
    Serial.println(simStatus);
    if (simStatus == "READY") {
        Serial.println("[PASS] SIM kart hazir");
    } else {
        Serial.print("[WARN] SIM kart durumu: ");
        Serial.println(simStatus);
    }
    Serial.println();

    // ===== TEST 4: Sinyal Gücü =====
    Serial.print("[TEST] Sinyal Gucu (CSQ): ");
    int csq = gsm.getSignalQuality();
    Serial.println(csq);
    if (csq > 0 && csq < 99) {
        int rssi = -113 + (csq * 2);
        Serial.print("  RSSI: ");
        Serial.print(rssi);
        Serial.println(" dBm");

        if      (csq > 20) Serial.println("  Kalite: MUKEMMEL");
        else if (csq > 15) Serial.println("  Kalite: IYI");
        else if (csq > 10) Serial.println("  Kalite: ORTA");
        else               Serial.println("  Kalite: ZAYIF");

        Serial.println("[PASS] Sinyal olculdu");
    } else {
        Serial.println("[FAIL] Sinyal alinamiyor");
    }
    Serial.println();

    // ===== TEST 5: Operatör =====
    Serial.print("[TEST] Operator: ");
    String op = gsm.getOperator();
    if (op.length() > 0) {
        Serial.println(op);
        Serial.println("[PASS] Operator bilgisi alindi");
    } else {
        Serial.println("[FAIL] Operator bilgisi alinamadi");
    }
    Serial.println();

    // ===== TEST 6: LTE Network Kayıt =====
    Serial.print("[TEST] LTE Network Kayit: ");
    if (gsm.isRegistered()) {
        Serial.println("KAYITLI");
        Serial.println("[PASS] Network kaydi basarili");
    } else {
        Serial.println("KAYITSIZ");
        Serial.println("[FAIL] Network kaydi yok");
    }
    Serial.println();

    // ===== SONUÇ =====
    printSeparator();
    Serial.println("  Tum testler tamamlandi!");
    Serial.println("  Interaktif AT modu aktif:");
    Serial.println("  Monitore AT komutu yazip Enter'a basin.");
    printSeparator();
}

void loop() {
    // Serial Monitor'dan AT komutu gönderme (interaktif mod)
    if (Serial.available()) {
        String cmd = Serial.readStringUntil('\n');
        cmd.trim();
        if (cmd.length() > 0) {
            Serial.print("> ");
            Serial.println(cmd);
            String resp = gsm.sendAT(cmd, 5000);
            Serial.println(resp);
        }
    }
}
