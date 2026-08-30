/*
 * main.cpp — ESP32-S3 + SIM7672E Hücresel Konum / LBS Testi (PlatformIO)
 *
 * Bağımsız kütüphane: 10_LbsLocation.h / 10_LbsLocation.cpp
 *
 * NE YAPAR?
 *   GPS OLMADAN, yalnızca baz istasyonu bilgisiyle cihazın konumunu bulur.
 *   Modem bağlı olduğu hücrenin kimliğini (MCC/MNC/LAC/CID) verir; bu kimlik
 *   bir hücre veritabanında aranarak enlem/boylam elde edilir.
 *
 * Akış:
 *   1. Modemi başlat (hard reset) + SIM durumu
 *   2. Şebeke kaydı + operatör + sinyal
 *   3. Servis hücresi bilgisi (AT+CPSI?)
 *   4. LTE veri bağlantısı (PDP context) — LBS için gerekli
 *   5. Konum sorgusu: önce modem içi LBS (AT+CLBS), olmazsa online API
 *   6. Google Maps bağlantısı
 *   Sonra loop() içinde LOOP_PERIOD_MS'de bir konumu tazeler.
 *
 * DOĞRULUK: Şehir içi ~150–1000 m, kırsalda birkaç km. GPS değildir.
 *
 * Bağlantılar (ESP32-S3):
 *   GPIO 17 (TX) -> SIM7672E RX
 *   GPIO 16 (RX) <- SIM7672E TX
 *   GPIO  7      -> SIM7672E PWRKEY
 *   GPIO  5      -> SIM7672E RESET
 *
 * Serial Monitor: 115200 baud
 */

#include <Arduino.h>
#include "10_LbsLocation.h"

LbsLocation gsm(17, 16, 7, 5);  // txPin, rxPin, pwrKeyPin, resetPin

// ====== AYARLAR ======
// APN: Turkcell/Vodafone: "internet", Turk Telekom: "tt"
const char APN_STR[] = "internet";

// Online hücre veritabanı API anahtarı — ÇOĞU KULLANICI İÇİN GEREKSİZ.
// Modem içi LBS (AT+CLBS) anahtarsız çalışıyorsa buraya hiçbir şey yazmayın.
// Yalnızca AT+CLBS desteklenmeyen bir firmware'de yedek olarak gerekir.
// Ücretsiz anahtar: https://unwiredlabs.com  veya  https://my.opencellid.org
const char LBS_API_KEY[] = "";

// Sağlayıcı: LBS_UNWIRED (POST) veya LBS_OPENCELLID (GET)
const LbsProvider LBS_PROVIDER = LBS_UNWIRED;

// false → önce AT+CLBS (önerilen). Başarılı olursa online API HİÇ çağrılmaz,
//          bu yüzden sağlayıcı panelinizde istek görünmez — normaldir.
// true  → önce online API. Sağlayıcıyı test etmek/karşılaştırmak için.
const bool PREFER_ONLINE = false;

// Konum tazeleme periyodu
const uint32_t LOOP_PERIOD_MS = 60000;
// =====================

void printSeparator() {
    Serial.println("========================================");
}

void printCellInfo(const CellInfo &c) {
    Serial.print("  Teknoloji : "); Serial.println(c.rat.length() ? c.rat : "?");
    Serial.print("  MCC / MNC : "); Serial.print(c.mcc);
    Serial.print(" / ");            Serial.println(c.mnc);
    Serial.print("  LAC / TAC : "); Serial.print(c.lac);
    Serial.print("  (0x");          Serial.print(c.lac, HEX); Serial.println(")");
    Serial.print("  Cell ID   : "); Serial.print(c.cid);
    Serial.print("  (0x");          Serial.print(c.cid, HEX); Serial.println(")");
    if (c.pcid >= 0)     { Serial.print("  PCI       : "); Serial.println(c.pcid); }
    if (c.band.length()) { Serial.print("  Band      : "); Serial.println(c.band); }
    Serial.print("  RSRP/RSSI : "); Serial.print(c.rsrp);
    Serial.print(" / ");            Serial.print(c.rssi); Serial.println(" dBm");
}

void printFix(const GeoFix &f) {
    if (!f.valid) {
        Serial.print("[FAIL] Konum alinamadi: ");
        Serial.println(f.error.length() ? f.error : "bilinmeyen sebep");
        return;
    }
    Serial.println("[PASS] Konum bulundu!");
    Serial.print("  Kaynak    : "); Serial.println(f.source);
    Serial.print("  Enlem     : "); Serial.println(f.lat, 6);
    Serial.print("  Boylam    : "); Serial.println(f.lon, 6);
    Serial.print("  Dogruluk  : ~"); Serial.print(f.accuracy); Serial.println(" m");
    Serial.print("  Harita    : "); Serial.println(gsm.mapsUrl(f));
}

void setup() {
    Serial.begin(115200);
    while (!Serial);
    delay(1000);

    // Onboard LED'i kapat (GPIO48, aktif-dusuk)
    pinMode(48, OUTPUT);
    digitalWrite(48, HIGH);

    printSeparator();
    Serial.println("  10 - Hucresel Konum (LBS) Testi");
    printSeparator();

    // ----------------------------------------------------------
    // [1/6] Modem başlat
    // ----------------------------------------------------------
    Serial.println("\n[1/6] Modem baslatiliyor...");
    gsm.hardReset();

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
    Serial.println("[PASS] SIM hazir");

    // ----------------------------------------------------------
    // [2/6] Şebeke kaydı
    // ----------------------------------------------------------
    Serial.println("\n[2/6] Sebekeye kayit bekleniyor...");
    bool reg = false;
    for (int i = 0; i < 45; i++) {
        if (gsm.isRegistered()) { reg = true; break; }
        delay(1000);
    }
    if (!reg) {
        Serial.println("[FAIL] Sebekeye kayit yok! SIM kart veya anten kontrol edin.");
        while (1) delay(1000);
    }

    int rssi = gsm.getSignalQuality();
    Serial.print("  RSSI (CSQ): ");
    Serial.print(rssi);
    Serial.println(rssi == 99 ? "  (sinyal yok!)" : "  OK");
    Serial.print("  Operator  : ");
    Serial.println(gsm.getOperator());
    Serial.println("[PASS] Sebeke kayitli");

    // ----------------------------------------------------------
    // [3/6] Servis hücresi bilgisi
    // ----------------------------------------------------------
    Serial.println("\n[3/6] Servis hucresi okunuyor (AT+CPSI?)...");
    CellInfo cell;
    if (gsm.getCellInfo(cell)) {
        Serial.println("[PASS] Hucre bilgisi:");
        printCellInfo(cell);
    } else {
        Serial.println("[WARN] Hucre bilgisi eksik — ham cikti:");
        Serial.println(gsm.getRawCellReport());
    }

    // ----------------------------------------------------------
    // [4/6] LTE veri bağlantısı
    // ----------------------------------------------------------
    Serial.println("\n[4/6] LTE veri baglantisi kuruluyor...");
    Serial.print("  APN: ");
    Serial.println(APN_STR);
    if (!gsm.initGPRS(APN_STR)) {
        Serial.println("[FAIL] PDP context acilamadi! APN degerini kontrol edin.");
        while (1) delay(1000);
    }
    Serial.print("[PASS] IP: ");
    Serial.println(gsm.getLocalIP());

    // Online sağlayıcıyı tanıt (anahtar boşsa yalnız AT+CLBS denenir)
    if (strlen(LBS_API_KEY) > 0) {
        gsm.setApiKey(LBS_API_KEY, LBS_PROVIDER);
        gsm.setPreferOnline(PREFER_ONLINE);
        Serial.print("  Online hucre veritabani: AKTIF");
        Serial.println(PREFER_ONLINE ? "  (once online)" : "  (once AT+CLBS)");
    } else {
        Serial.println("  Online hucre veritabani: KAPALI (LBS_API_KEY bos)");
    }

    // ----------------------------------------------------------
    // [5/6] + [6/6] Konum sorgusu
    // ----------------------------------------------------------
    Serial.println("\n[5/6] Hucresel konum sorgulaniyor...");
    GeoFix fix;
    gsm.getLocation(fix, &cell);

    Serial.println("\n[6/6] Sonuc:");
    printFix(fix);

    printSeparator();
    Serial.print("  Konum her ");
    Serial.print(LOOP_PERIOD_MS / 1000);
    Serial.println(" sn'de tazelenecek");
    printSeparator();
}

void loop() {
    static uint32_t lastRun = 0;

    if (millis() - lastRun < LOOP_PERIOD_MS) {
        delay(100);
        return;
    }
    lastRun = millis();

    Serial.println("\n--- Konum tazeleniyor ---");

    CellInfo cell;
    GeoFix   fix;
    gsm.getLocation(fix, &cell);

    if (cell.valid) printCellInfo(cell);
    printFix(fix);
}
