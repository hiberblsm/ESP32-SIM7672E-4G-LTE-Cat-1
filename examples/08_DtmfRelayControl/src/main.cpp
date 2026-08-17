/*
 * main.cpp — ESP32-S3 + SIM7672E DTMF Röle Kontrolü (PlatformIO)
 *
 * Bağımsız kütüphane: 08_DtmfRelayControl.h / 08_DtmfRelayControl.cpp
 *
 * SIM kartın numarasını arayın → DTMF tuşlarıyla röleleri kontrol edin:
 *   1 → Röle 1 AÇ          2 → Röle 1 KAPAT
 *   3 → Röle 2 AÇ          4 → Röle 2 KAPAT
 *   * → HEPSİ AÇ           0 → HEPSİ KAPAT
 *   # → Aramayı Kapat      9 → Durum (Serial'e yaz)
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
#include "08_DtmfRelayControl.h"

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
// Boş bırakırsan → herkesten gelen arama yanıtlanır
// Dolu olursa  → sadece bu numaradan gelen arama yanıtlanır
#define YETKILI  "+905468422222"

// Maksimum çağrı süresi (ms) — dolunca otomatik kapat
#define MAKS_CAGRI_MS   120000UL    // 120 saniye

// RING gelip CLIP gelmezse kaç ms sonra yine de yanıtla (YETKILI boşsa)
#define CLIP_TIMEOUT_MS   4000UL

// ============================================================

#ifdef RELAY_ACTIVE_HIGH
  #define ROL_AC    HIGH
  #define ROL_KAPAT LOW
#else
  #define ROL_AC    LOW
  #define ROL_KAPAT HIGH
#endif

DtmfRelay gsm(17, 16, 4, 5);

const uint8_t ROLE_PINLERI[2] = {R1_PIN, R2_PIN};
bool rolAcik[2] = {false, false};

// Çağrı durum makinesi
enum CallState : uint8_t { IDLE, RINGING, IN_CALL };
CallState callState     = IDLE;
unsigned long callTimer = 0;
bool clipGeldi          = false;

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

void durumYazdir() {
    Serial.println("  --- DURUM ---");
    for (uint8_t i = 0; i < 2; i++) {
        Serial.print("  R");
        Serial.print(i + 1);
        Serial.print(": ");
        Serial.println(rolAcik[i] ? "ACIK" : "KAPALI");
    }
}

// ============================================================
//  DTMF TUŞUNU İŞLE
// ============================================================

void dtmfIsle(char ton) {
    Serial.print("[DTMF] Tus: ");
    Serial.println(ton);

    switch (ton) {
        case '1': roleSet(0, true);  break;  // R1 AÇ
        case '2': roleSet(0, false); break;  // R1 KAPAT
        case '3': roleSet(1, true);  break;  // R2 AÇ
        case '4': roleSet(1, false); break;  // R2 KAPAT
        case '*': for (uint8_t i = 0; i < 2; i++) roleSet(i, true);  break; // HEPSI AÇ
        case '0': for (uint8_t i = 0; i < 2; i++) roleSet(i, false); break; // HEPSI KAPAT
        case '9': durumYazdir(); break;
        case '#':
            Serial.println("  [DTMF] # → Arama kapatiliyor");
            gsm.callHangup();
            callState = IDLE;
            break;
        default:
            Serial.println("  [DTMF] Tanimsiz tus (1-4, *, 0, 9, #)");
            break;
    }
}

// ============================================================
//  SETUP
// ============================================================

void setup() {
    roleInit();   // Önce röleleri kapat — güç açılışında tetiklenmesin
    Serial.begin(115200);
    while (!Serial);
    delay(500);

    // Onboard LED'i kapat (GPIO48, aktif-dusuk)
    pinMode(48, OUTPUT);
    digitalWrite(48, HIGH);

    Serial.println("\n==============================");
    Serial.println("  08 - DTMF Role Kontrol");
    Serial.println("  Hiber Bilisim - hiber.com.tr");
    Serial.println("==============================");
    Serial.println("  1/2=R1  3/4=R2");
    Serial.println("  * =HepsiAc  0=HepsiKapat  #=Kapat  9=Durum");

    // Modem başlat
    Serial.println("\nModem baslatiliyor...");
    gsm.hardReset();

    // SIM kart
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

    // DTMF ve arayan ID açık
    gsm.callSetCLIP(true);
    gsm.callSetDTMF(true);

    Serial.println("\n[HAZIR] Arama bekleniyor...");
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
    char event[8];
    char detail[20];

    if (gsm.callPoll(event, sizeof(event), detail, sizeof(detail))) {

        // ---- RING — gelen çağrı ----
        if (!strcmp(event, "RING")) {
            if (callState == IDLE) {
                callState = RINGING;
                callTimer = millis();
                clipGeldi = false;
                Serial.println("\n[RING] Gelen arama...");
            }
        }

        // ---- CLIP — arayan numara ----
        else if (!strcmp(event, "CLIP")) {
            if (callState == RINGING) {
                clipGeldi = true;
                Serial.print("  Arayan: ");
                Serial.println(detail);

                bool yetkili = (strlen(YETKILI) == 0 || !strcmp(detail, YETKILI));
                if (yetkili) {
                    gsm.callAnswer();
                    callState = IN_CALL;
                    callTimer = millis();
                    Serial.println("[CALL] Yanitlandi — DTMF bekleniyor");
                } else {
                    Serial.println("[RED] Yetkisiz numara, yanitlanmadi");
                    callState = IDLE;
                }
            }
        }

        // ---- DTMF — tuş basıldı ----
        else if (!strcmp(event, "DTMF")) {
            if (callState == IN_CALL) {
                dtmfIsle(detail[0]);
            }
        }

        // ---- HANGUP — karşı taraf kapattı ----
        else if (!strcmp(event, "HANGUP")) {
            if (callState != IDLE) {
                Serial.println("[HANG] Arama kapandi");
                callState = IDLE;
            }
        }
    }

    // RINGING: CLIP gelmeden timeout
    if (callState == RINGING && !clipGeldi &&
        (millis() - callTimer > CLIP_TIMEOUT_MS)) {
        if (strlen(YETKILI) == 0) {
            Serial.println("  (CLIP gelmedi, yine de yanitlaniyor)");
            gsm.callAnswer();
            callState = IN_CALL;
            callTimer = millis();
        } else {
            Serial.println("  (CLIP gelmedi, yetkili mod → yoksayildi)");
            callState = IDLE;
        }
    }

    // IN_CALL: maksimum süre doldu → otomatik kapat
    if (callState == IN_CALL &&
        (millis() - callTimer > MAKS_CAGRI_MS)) {
        Serial.println("[AUTO] Maks sure doldu, arama kapatiliyor");
        gsm.callHangup();
        callState = IDLE;
    }
}
