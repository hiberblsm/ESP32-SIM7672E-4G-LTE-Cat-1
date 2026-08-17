/*
 * main.cpp — ESP32-S3 + SIM7672E SMS Testi (PlatformIO)
 *
 * Bağımsız kütüphane: 06_SmsTest.h / 06_SmsTest.cpp
 *
 * Akış:
 *   1. Modemi başlat (hard reset) + SIM durumu
 *   2. SMS gönder (AT+CMGS)
 *   3. Tüm SMS listele (AT+CMGL)
 *   4. SMS oku (AT+CMGR)
 *   5. Gelen SMS bekle (+CMT: URC)
 *   6. Tüm SMS sil (AT+CMGD=1,4)
 *
 * SMS için GPRS/LTE veri bağlantısı gerekmez.
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
#include "06_SmsTest.h"

// ====== AYARLAR ======
// Kendi numaranı yaz (+90XXXXXXXXXX gibi)
#define TEST_PHONE_NUMBER  "+905468422222"
// =====================

SmsTest gsm(17, 16, 4, 5);  // txPin, rxPin, pwrKeyPin, resetPin

void printSeparator() {
    Serial.println("==============================");
}

void setup() {
    Serial.begin(115200);
    while (!Serial);
    delay(1000);

    // Onboard LED'i kapat (GPIO48, aktif-dusuk)
    pinMode(48, OUTPUT);
    digitalWrite(48, HIGH);

    printSeparator();
    Serial.println("  06 - SMS Test");
    printSeparator();

    // ----------------------------------------------------------
    // [1/6] Modem başlat
    // ----------------------------------------------------------
    Serial.println("\n[1/6] Modem baslatiliyor...");
    gsm.hardReset();

    // CPIN kontrolü — SIM hazır mı?
    String simStatus = "UNKNOWN";
    for (int i = 0; i < 10; i++) {
        simStatus = gsm.getSIMStatus();
        if (simStatus == "READY") break;
        Serial.print("  SIM bekleniyor: ");
        Serial.println(simStatus);
        delay(1000);
    }
    if (simStatus != "READY") {
        Serial.print("[FAIL] SIM kart hatasi: ");
        Serial.println(simStatus);
        while (1) delay(1000);
    }

    // Sinyal kalitesi
    int rssi = gsm.getSignalQuality();
    Serial.print("  RSSI: ");
    Serial.print(rssi);
    Serial.println(rssi == 99 ? "  (sinyal yok!)" : "  OK");

    // Şebeke kaydı
    bool reg = false;
    for (int i = 0; i < 30; i++) {
        if (gsm.isRegistered()) { reg = true; break; }
        delay(1000);
    }
    if (!reg) {
        Serial.println("[FAIL] Sebekeye kayit yok! SIM kart veya anten kontrol edin.");
        while (1) delay(1000);
    }

    Serial.print("  Operator: ");
    Serial.println(gsm.getOperator());
    Serial.println("[PASS] Modem hazir, SIM OK, sebeke kayitli");

    delay(1000);

    // ----------------------------------------------------------
    // [2/6] SMS GÖNDER
    // ----------------------------------------------------------
    Serial.println("\n[2/6] SMS gonderme testi...");
    Serial.print("  Hedef: ");
    Serial.println(TEST_PHONE_NUMBER);

    const char smsText[] =
        "Merhaba! ESP32 + SIM7672E test mesaji. 06_SmsTest OK.";

    if (gsm.smsSend(TEST_PHONE_NUMBER, smsText)) {
        Serial.println("[PASS] SMS gonderildi!");
    } else {
        Serial.println("[FAIL] SMS gonderilemedi (kredi/yetki kontrol edin)");
    }

    delay(3000);

    // ----------------------------------------------------------
    // [3/6] TÜM SMS LİSTELE
    // ----------------------------------------------------------
    Serial.println("\n[3/6] SMS listeleme testi (AT+CMGL)...");
    String list = gsm.smsList("ALL");
    if (list.length() > 5 && list.indexOf("+CMGL:") >= 0) {
        Serial.println("[PASS] Listede mesajlar mevcut:");
        Serial.println(list);
    } else {
        Serial.println("  Kuyrukta mesaj yok (ya da hepsi silindi)");
        Serial.println(list);
    }

    delay(1000);

    // ----------------------------------------------------------
    // [4/6] SMS OKU (index 1)
    // ----------------------------------------------------------
    Serial.println("\n[4/6] SMS okuma testi (AT+CMGR=1)...");
    String readResult = gsm.smsRead(1);
    if (readResult.indexOf("+CMGR:") >= 0) {
        Serial.println("[PASS] Mesaj okundu:");
        Serial.println(readResult);
    } else {
        Serial.println("[WARN] Index 1'de mesaj bulunamadi");
    }

    delay(1000);

    // ----------------------------------------------------------
    // [5/6] GELEN SMS BEKLE (+CMT: URC)
    // ----------------------------------------------------------
    Serial.println("\n[5/6] Gelen SMS bekleniyor (30 sn)...");
    Serial.println("  Lutfen simdi telefona bir SMS gonderin!");

    gsm.smsSetIncoming(true);

    String incoming = gsm.smsWaitIncoming(30000);

    if (incoming.length() > 0) {
        Serial.println("[PASS] Gelen SMS yakalandi:");
        Serial.println(incoming);
    } else {
        Serial.println("[WARN] 30 sn icinde SMS gelmedi (test atlandi)");
    }

    gsm.smsSetIncoming(false);

    delay(1000);

    // ----------------------------------------------------------
    // [6/6] TÜM SMS SİL
    // ----------------------------------------------------------
    Serial.println("\n[6/6] Tum SMS silme testi (AT+CMGD=1,4)...");
    if (gsm.smsDeleteAll()) {
        Serial.println("[PASS] Tum SMS'ler silindi!");
    } else {
        Serial.println("[FAIL] SMS silme hatasi");
    }

    // ----------------------------------------------------------
    // ÖZET
    // ----------------------------------------------------------
    Serial.println("\n==============================");
    Serial.println("  SMS testleri tamamlandi!");
    Serial.println("==============================");
    Serial.println("  1. SMS Gonderme  - AT+CMGS");
    Serial.println("  2. SMS Listeleme - AT+CMGL");
    Serial.println("  3. SMS Okuma     - AT+CMGR");
    Serial.println("  4. SMS Alma      - AT+CNMI (+CMT:)");
    Serial.println("  5. SMS Silme     - AT+CMGD");
}

void loop() {
    // Tek seferlik test tamamlandı
    delay(1000);
}
