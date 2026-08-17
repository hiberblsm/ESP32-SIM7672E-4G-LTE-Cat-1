/*
 * main.cpp — ESP32-S3 + SIM7672E SMS Röle Kontrolü (PlatformIO)
 *
 * Bağımsız kütüphane: 07_SmsRelayControl.h / 07_SmsRelayControl.cpp
 *
 * SMS Komutları:
 *   R1 AC / R1 KAPAT      → Röle 1 aç / kapat
 *   R2 AC / R2 KAPAT      → Röle 2 aç / kapat
 *   HEPSI AC / HEPSI KAPAT → 2 röleyi birden aç / kapat
 *   DURUM                 → Tüm röle durumunu SMS ile bildir
 *
 * Donanım - Pin Bağlantıları (ESP32-S3):
 *   GPIO 17 (TX) -> SIM7672E RX
 *   GPIO 16 (RX) <- SIM7672E TX
 *   GPIO  4      -> SIM7672E PWRKEY
 *   GPIO  5      -> SIM7672E RESET
 *   GPIO  6      -> Röle 1 (IN1)
 *   GPIO  7      -> Röle 2 (IN2)
 *
 * Serial Monitor: 115200 baud
 */

#include <Arduino.h>
#include "07_SmsRelayControl.h"

// ============================================================
//  YAPILANDIRMA - sadece burası değiştirilir
// ============================================================

// Röle pin numaraları (ESP32-S3 GPIO)
// NOT: ESP32-S3'te GPIO 22-25 YOKTUR; GPIO 26-37 flash/PSRAM (OPI) tarafından
// kullanılır. Serbest pinler: GPIO 6 ve 7.
#define R1_PIN  6
#define R2_PIN  7

// Röle shield tipi:
//   RELAY_ACTIVE_HIGH (varsayılan) → pin HIGH olduğunda röle AÇILIR
//   Bu satırı yorum yap → Active-LOW moda geçer (pin LOW = röle AÇIK)
#define RELAY_ACTIVE_HIGH

// Yetkili telefon numarası (güvenlik)
// Boş bırakırsan → herkesten komut kabul edilir
// Dolu olursa → sadece bu numaradan gelen SMS işleme alınır
#define YETKILI  "+905468422222"

// ============================================================

#ifdef RELAY_ACTIVE_HIGH
  #define ROL_AC    HIGH
  #define ROL_KAPAT LOW
#else
  #define ROL_AC    LOW
  #define ROL_KAPAT HIGH
#endif

SmsRelay gsm(17, 16, 4, 5);

const uint8_t ROLE_PINLERI[2] = {R1_PIN, R2_PIN};
bool rolAcik[2] = {false, false};

// ============================================================
//  RÖLE FONKSİYONLARI
// ============================================================

void roleInit() {
    for (uint8_t i = 0; i < 2; i++) {
        pinMode(ROLE_PINLERI[i], OUTPUT);
        digitalWrite(ROLE_PINLERI[i], ROL_KAPAT);
        rolAcik[i] = false;
    }
}

void roleSet(uint8_t idx, bool ac) {
    if (idx >= 2) return;
    digitalWrite(ROLE_PINLERI[idx], ac ? ROL_AC : ROL_KAPAT);
    rolAcik[idx] = ac;
    Serial.print("  [ROLE] ");
    Serial.print(idx + 1);
    Serial.println(ac ? " ACILDI" : " KAPATILDI");
}

// ============================================================
//  SMS FONKSİYONLARI
// ============================================================

void durumSmsSend(const char *numara) {
    char msg[64];
    snprintf(msg, sizeof(msg),
        "Durum:\nR1:%s R2:%s",
        rolAcik[0] ? "AC " : "KPL",
        rolAcik[1] ? "AC " : "KPL"
    );
    gsm.smsSend(numara, msg);
    Serial.println("  [SMS] Durum gonderildi");
}

void komutIsle(const char *gonderen, const char *komut) {
    Serial.print("[SMS ALINDI] ");
    Serial.print(gonderen);
    Serial.print(" -> \"");
    Serial.print(komut);
    Serial.println('"');

    // Yetki kontrolü
    if (strlen(YETKILI) > 3 && strcmp(gonderen, YETKILI) != 0) {
        Serial.println("  [RED] Yetkisiz numara, komut yoksayildi");
        return;
    }

    const char *yanit = NULL;

    // Komutu normalize et: büyük harf + fazladan boşlukları tek boşluğa indir.
    // "r1 ac", "R1  AC", "R1 Ac", "R1 AC" hepsi "R1 AC" olur.
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

    // Komut eşleştirme (hem kısa R1/R2 hem uzun ROLE1/ROLE2 destekli)
    if      (norm == "R1 AC"      || norm == "ROLE1 AC")    { roleSet(0, true);  yanit = "Role 1 ACILDI"; }
    else if (norm == "R1 KAPAT"   || norm == "ROLE1 KAPAT") { roleSet(0, false); yanit = "Role 1 KAPATILDI"; }
    else if (norm == "R2 AC"      || norm == "ROLE2 AC")    { roleSet(1, true);  yanit = "Role 2 ACILDI"; }
    else if (norm == "R2 KAPAT"   || norm == "ROLE2 KAPAT") { roleSet(1, false); yanit = "Role 2 KAPATILDI"; }
    else if (norm == "HEPSI AC")    { for (uint8_t i = 0; i < 2; i++) roleSet(i, true);  yanit = "Tum roller ACILDI"; }
    else if (norm == "HEPSI KAPAT") { for (uint8_t i = 0; i < 2; i++) roleSet(i, false); yanit = "Tum roller KAPATILDI"; }
    else if (norm == "DURUM")       { durumSmsSend(gonderen); return; }
    else                            { yanit = "Bilinmeyen komut";
                                      Serial.print("  [WARN] "); Serial.println(komut); }

    // Onay SMS'i gönder
    if (yanit && strlen(gonderen) > 3) {
        bool ok = gsm.smsSend(gonderen, yanit);
        Serial.print("  [SMS] Yanit ");
        Serial.print(yanit);
        Serial.println(ok ? " -> GONDERILDI" : " -> GONDERILEMEDI!");
    }
}

// ============================================================
//  SETUP
// ============================================================

void setup() {
    roleInit();   // Önce röleleri kapat! (güç açılışında röle tetiklenmesin)
    Serial.begin(115200);
    while (!Serial);
    delay(500);

    // Onboard LED'i kapat (GPIO48, aktif-dusuk)
    pinMode(48, OUTPUT);
    digitalWrite(48, HIGH);

    Serial.println("\n==============================");
    Serial.println("  07 - SMS Role Kontrol");
    Serial.println("  Hiber Bilisim - hiber.com.tr");
    Serial.println("==============================");
    Serial.println("Komutlar:  R1 AC / R1 KAPAT");
    Serial.println("           R2 AC / R2 KAPAT");
    Serial.println("           HEPSI AC / HEPSI KAPAT");
    Serial.println("           DURUM");

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

    // Şebeke kaydı
    Serial.print("  Sebeke: ");
    for (uint8_t i = 0; i < 30; i++) {
        if (gsm.isRegistered()) break;
        delay(1000);
    }
    Serial.println(gsm.getOperator());

    // Sinyal
    int rssi = gsm.getSignalQuality();
    Serial.print("  RSSI : ");
    Serial.print(rssi);
    Serial.println(rssi == 99 ? "  (sinyal yok!)" : "  OK");

    // Eski SMS'leri temizle
    gsm.smsDeleteAll();

    // Gelen SMS bildirimi (+CMT: URC) aç
    gsm.smsSetIncoming(true);

    Serial.println("\n[HAZIR] SMS komut bekleniyor...");
    if (strlen(YETKILI) > 3) {
        Serial.print("  Yetkili: ");
        Serial.println(YETKILI);
    } else {
        Serial.println("  (Yetki yok - herkese acik)");
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
