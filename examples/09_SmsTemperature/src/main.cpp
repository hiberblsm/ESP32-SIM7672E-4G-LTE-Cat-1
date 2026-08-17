/*
 * main.cpp — ESP32-S3 + SIM7672E SMS Sıcaklık Ölçümü (PlatformIO)
 *
 * Bağımsız kütüphane: 09_SmsTemperature.h / 09_SmsTemperature.cpp
 *
 * DS18B20 sıcaklık sensörüne SMS gönderilince anlık sıcaklık
 * gönderenin numarasına SMS olarak geri iletilir.
 *
 * SMS Komutları (büyük/küçük harf fark etmez):
 *   SICAKLIK   → Anlık sıcaklık değerini SMS ile bildir
 *   DURUM      → Sıcaklık + sensör durumunu + sinyali SMS ile bildir
 *   (diğer)    → Yine sıcaklık gönderilir
 *
 * Donanım - Pin Bağlantıları (ESP32-S3):
 *   GPIO 17 (TX) -> SIM7672E RX
 *   GPIO 16 (RX) <- SIM7672E TX
 *   GPIO  4      -> SIM7672E PWRKEY
 *   GPIO  5      -> SIM7672E RESET
 *   GPIO 13      -> DS18B20 Data (sarı/beyaz tel)
 *   DS18B20 VCC  -> 3.3V veya 5V
 *   DS18B20 GND  -> GND
 *   DS18B20 Data -> 4.7kΩ dirençle VCC'ye pull-up
 *
 * Serial Monitor: 115200 baud
 */

#include <Arduino.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include "09_SmsTemperature.h"

// ============================================================
//  YAPILANDIRMA - sadece burası değiştirilir
// ============================================================

// DS18B20 data pini (ESP32-S3 GPIO 13 serbesttir)
#define DS18B20_PIN   13

// Yetkili telefon numarası (boş = herkese cevap ver)
#define YETKILI       "+905468422222"

// ============================================================

OneWire           oneWire(DS18B20_PIN);
DallasTemperature sensors(&oneWire);
SmsTemperature    gsm(17, 16, 4, 5);

// ============================================================
//  SICAKLIK OKU
// ============================================================

// Sıcaklığı °C olarak oku, hata durumunda -127 döndür
float sicaklikOku() {
    sensors.requestTemperatures();
    float t = sensors.getTempCByIndex(0);
    return t;  // DEVICE_DISCONNECTED_C = -127.0
}

// ============================================================
//  SMS YANITI OLUŞTUR VE GÖNDER
// ============================================================

void sicaklikSmsGonder(const char *numara) {
    float t = sicaklikOku();
    char cevap[80];

    if (t <= -127.0f) {
        strncpy(cevap, "HATA: DS18B20 sensoru bulunamadi!", sizeof(cevap) - 1);
        cevap[sizeof(cevap) - 1] = '\0';
        Serial.println("  [WARN] Sensor bulunamadi!");
    } else {
        // ESP32: snprintf %f destekler (dtostrf gerekmez)
        snprintf(cevap, sizeof(cevap),
            "Sicaklik: %.1f C\nSensor: DS18B20 (Pin %d)\nhiber.com.tr",
            t, DS18B20_PIN);
        Serial.print("  [SENSOR] ");
        Serial.print(t, 1);
        Serial.println(" C");
    }

    bool ok = gsm.smsSend(numara, cevap);
    Serial.print("  [SMS] Yanit ");
    Serial.println(ok ? "-> GONDERILDI" : "-> GONDERILEMEDI!");
}

void durumSmsGonder(const char *numara) {
    float t = sicaklikOku();
    char cevap[120];

    if (t <= -127.0f) {
        strncpy(cevap, "Durum: HATA\nDS18B20 sensoru bulunamadi!\nBaglanti kontrol edin.",
                sizeof(cevap) - 1);
    } else {
        int csq = gsm.getSignalQuality();
        snprintf(cevap, sizeof(cevap),
            "Durum: OK\nSicaklik: %.1f C\nSinyal: %d\nDS18B20 Pin%d",
            t, csq, DS18B20_PIN);
    }

    cevap[sizeof(cevap) - 1] = '\0';
    bool ok = gsm.smsSend(numara, cevap);
    Serial.println(ok ? "  [SMS] Durum gonderildi" : "  [SMS] Durum GONDERILEMEDI!");
}

// ============================================================
//  KOMUT İŞLE
// ============================================================

void komutIsle(const char *gonderen, const char *komut) {
    Serial.print("[SMS] ");
    Serial.print(gonderen);
    Serial.print(" -> \"");
    Serial.print(komut);
    Serial.println('"');

    // Yetki kontrolü
    if (strlen(YETKILI) > 3 && strcmp(gonderen, YETKILI) != 0) {
        Serial.println("  [RED] Yetkisiz numara");
        return;
    }

    // Komutu normalize et: büyük harf + fazladan boşlukları tek boşluğa indir.
    // "durum", "SICAKLIK", "  sicaklik  " hepsi "SICAKLIK"/"DURUM" olur.
    String norm = "";
    bool prevSpace = false;
    for (size_t i = 0; komut[i]; i++) {
        char c = komut[i];
        if (c >= 'a' && c <= 'z') c -= 32;   // küçük harfi büyüt
        if (c == ' ' || c == '\t') {
            if (!prevSpace) norm += ' ';
            prevSpace = true;
        } else {
            norm += c;
            prevSpace = false;
        }
    }
    norm.trim();

    if (norm == "DURUM") {
        durumSmsGonder(gonderen);
    } else {
        // SICAKLIK, herhangi bir komut veya boş → sıcaklık gönder
        sicaklikSmsGonder(gonderen);
    }
}

// ============================================================
//  SETUP
// ============================================================

void setup() {
    Serial.begin(115200);
    while (!Serial);
    delay(500);

    // Onboard LED'i kapat (GPIO48, aktif-dusuk)
    pinMode(48, OUTPUT);
    digitalWrite(48, HIGH);

    Serial.println("\n==============================");
    Serial.println("  09 - SMS Sicaklik Olcumu");
    Serial.println("  Hiber Bilisim - hiber.com.tr");
    Serial.println("==============================");

    // DS18B20 başlat
    sensors.begin();
    uint8_t adet = sensors.getDeviceCount();
    Serial.print("  DS18B20: ");
    Serial.print(adet);
    Serial.println(" sensor bulundu");

    if (adet == 0) {
        Serial.println("  [WARN] Sensor bulunamadi! Pull-up direnci ve kablo kontrol edin.");
    } else {
        float t = sicaklikOku();
        Serial.print("  Baslangic okumasi: ");
        Serial.print(t, 1);
        Serial.println(" C");
    }

    // Modem başlat
    Serial.println("\nModem baslatiliyor...");
    gsm.hardReset();

    // SIM kart hazır mı?
    Serial.print("  SIM  : ");
    {
        uint8_t i;
        for (i = 0; i < 20; i++) {
            if (gsm.getSIMStatus() == "READY") break;
            delay(1000);
        }
        Serial.println(i < 20 ? "READY" : "HATA!");
        if (i >= 20) { while (1) delay(1000); }
    }

    // Şebeke
    Serial.print("  Sebeke: ");
    for (uint8_t i = 0; i < 30; i++) {
        if (gsm.isRegistered()) break;
        delay(1000);
    }
    Serial.println(gsm.getOperator());

    // Sinyal
    Serial.print("  RSSI : ");
    Serial.println(gsm.getSignalQuality());

    // Eski SMS'leri temizle
    gsm.smsDeleteAll();

    // Gelen SMS bildirimi aç
    gsm.smsSetIncoming(true);

    Serial.println("\n[HAZIR] SMS bekleniyor...");
    Serial.println("  Komutlar: SICAKLIK | DURUM");
    if (strlen(YETKILI) > 3) {
        Serial.print("  Yetkili : ");
        Serial.println(YETKILI);
    } else {
        Serial.println("  (Herkese acik)");
    }
}

// ============================================================
//  LOOP
// ============================================================

void loop() {
    char gonderen[20];
    char komut[32];

    if (gsm.smsPoll(gonderen, sizeof(gonderen), komut, sizeof(komut))) {
        komutIsle(gonderen, komut);
        delay(500);
        gsm.smsDeleteAll();
    }
    delay(50);
}
