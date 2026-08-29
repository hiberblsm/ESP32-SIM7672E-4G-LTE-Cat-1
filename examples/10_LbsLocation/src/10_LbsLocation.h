/*
 * 10_LbsLocation.h — ESP32-S3 + SIM7672E Hücresel Konum (LBS) Testi
 *
 * Bu sınıf YALNIZCA 10_LbsLocation için gereken fonksiyonları içerir:
 *   - Başlatma & güç yönetimi (begin/powerOn/powerOff/hardReset)
 *   - Temel AT komutları (sendAT/sendATExpect/waitForResponse/clearBuffer)
 *   - Modem/SIM bilgileri (getSignalQuality/isRegistered/getSIMStatus/getOperator)
 *   - Hücre bilgisi (getCellInfo — AT+CPSI? / AT+CEREG? / AT+COPS?)
 *   - Hücresel konum (lbsQueryModem — AT+CLBS, lbsQueryOnline — HTTP API)
 *   - LTE veri bağlantısı + HTTP istemcisi (online sorgulama için)
 *
 * LBS (Location Based Service) NEDİR?
 *   GPS yoktur. Modem bağlı olduğu baz istasyonunun kimliğini (MCC/MNC/LAC/CID)
 *   bildirir; bu kimlik bir hücre veritabanında aranarak enlem/boylam bulunur.
 *   Doğruluk şehir içinde ~150–1000 m, kırsalda birkaç km olabilir.
 *
 * İKİ YÖNTEM DESTEKLENİR:
 *   1) Modem içi LBS  → AT+CLBS (SIMCom konum sunucusu; her firmware desteklemez)
 *   2) Online API     → Unwired Labs / OpenCelliD (ücretsiz API anahtarı ile)
 *   getLocation() önce 1'i dener, başarısız olursa 2'ye düşer.
 *
 * Her iki yöntem de aktif PDP context (LTE veri) gerektirir.
 *
 * Pin Bağlantıları (ESP32-S3):
 *   GPIO 17 (TX) -> SIM7672E RX
 *   GPIO 16 (RX) <- SIM7672E TX
 *   GPIO  4      -> SIM7672E PWRKEY
 *   GPIO  5      -> SIM7672E RESET (LOW aktif)
 */

#ifndef LBSLOCATION_H
#define LBSLOCATION_H

#include <Arduino.h>
#include <HardwareSerial.h>

// Varsayılan pin tanımları
#define SIM7672_TX_PIN      17
#define SIM7672_RX_PIN      16
#define SIM7672_PWRKEY_PIN   4
#define SIM7672_RESET_PIN    5

// Varsayılan baud rate (SIM7672E: 115200)
#define SIM7672_BAUD      115200

// ---- PWRKEY / RESET SÜRÜCÜ KUTUPLULUĞU ----
// PWRKEY ve RESET, modül tarafında AKTİF-DÜŞÜK'tür (pin GND'ye çekilince tetiklenir).
//
//   0 = GPIO doğrudan pine bağlı        → GPIO LOW  = tetiklendi
//   1 = GPIO bir NPN transistör bazında → GPIO HIGH = tetiklendi (mantık TERS!)
//
// NPN sürücü (base--R-->GPIO, emitter-->GND, collector-->PWRKEY) kullanıyorsanız
// aşağıdaki değeri 1 yapın; aksi halde PWRKEY sürekli basılı kalır ve modül açılmaz.
#define PWRKEY_ACTIVE_HIGH  0
#define RESET_ACTIVE_HIGH   0

// PWRKEY darbe süreleri (ms) — SIMCom A76xx ailesi için pratik değerler
#define PWRKEY_ON_MS      1500    // Açma darbesi  (~1.0-1.5 s)
#define PWRKEY_OFF_MS     3000    // Kapatma darbesi (>= 2.5 s)
#define PWRKEY_BOOT_MS    8000    // Darbe sonrası AT'ye hazır olma süresi

// Zaman aşımı değerleri (ms)
#define AT_TIMEOUT        3000
#define GPRS_TIMEOUT     15000
#define HTTP_TIMEOUT     28000
#define LBS_TIMEOUT      45000

// Online hücre veritabanı sağlayıcıları
enum LbsProvider {
    LBS_UNWIRED    = 0,   // https://unwiredlabs.com  (POST /v2/process.php)
    LBS_OPENCELLID = 1    // https://opencellid.org   (GET  /cell/get)
};

// Servis hücresi bilgisi (AT+CPSI? çıktısından)
struct CellInfo {
    bool     valid;    // Bilgi okunabildi mi
    String   rat;      // "LTE" | "GSM" | "WCDMA" | "NO SERVICE"
    int      mcc;      // Ülke kodu (Türkiye: 286)
    int      mnc;      // Operatör kodu (Turkcell:1, Vodafone:2, TT:3)
    uint32_t lac;      // LTE'de TAC, GSM/WCDMA'da LAC
    uint32_t cid;      // LTE'de ECI (28 bit), GSM'de Cell ID
    int      pcid;     // LTE Physical Cell ID (yoksa -1)
    String   band;     // "EUTRAN-BAND3" / "GSM 900" vb.
    int      rsrp;     // dBm (LTE); GSM'de RxLev
    int      rsrq;     // dB   (LTE); yoksa 0
    int      rssi;     // dBm
};

// Çözümlenmiş konum
struct GeoFix {
    bool   valid;
    double lat;        // Enlem
    double lon;        // Boylam
    int    accuracy;   // Tahmini yarıçap (metre)
    String source;     // "CLBS" | "UNWIRED" | "OPENCELLID"
    String error;      // Başarısızsa sebep
};

class LbsLocation {
public:
    // Kurucu: TX pini, RX pini, PWRKEY pini, RESET pini
    // Dahili olarak ESP32 Hardware Serial 2 (UART2) kullanılır.
    LbsLocation(uint8_t txPin      = SIM7672_TX_PIN,
                uint8_t rxPin      = SIM7672_RX_PIN,
                uint8_t pwrKeyPin  = SIM7672_PWRKEY_PIN,
                uint8_t resetPin   = SIM7672_RESET_PIN);

    // Başlatma & güç
    bool begin(long baud = SIM7672_BAUD);
    void powerOn();
    void powerOff();
    void hardReset();

    // Ham PWRKEY darbesi — donanım/kutupluluk tanılaması için.
    // Örn. powerKeyPulse(1500) → 1.5 sn "tuşa bas, bırak".
    void powerKeyPulse(uint16_t ms);

    // Temel AT komutları
    String sendAT(const String &cmd, uint32_t timeoutMs = AT_TIMEOUT);
    bool   sendATExpect(const String &cmd, const String &expected,
                        uint32_t timeoutMs = AT_TIMEOUT);
    bool   waitForResponse(const String &expected, uint32_t timeoutMs = AT_TIMEOUT);
    void   clearBuffer();

    // Modem & SIM bilgileri
    int    getSignalQuality();      // CSQ (0-31, 99=bilinmiyor)
    bool   isRegistered();          // AT+CEREG? (LTE) önce, AT+CREG? (2G) yedek
    String getSIMStatus();          // AT+CPIN? -> READY / SIM PIN / ...
    String getOperator();           // AT+COPS? -> operatör adı

    // LTE veri bağlantısı
    bool   initGPRS(const String &apn,
                    const String &user = "", const String &pass = "");
    bool   closeGPRS();
    String getLocalIP();            // AT+CGPADDR

    // ---------- HÜCRE BİLGİSİ ----------
    // AT+CPSI? ile servis hücresini okur; eksik alanları AT+CEREG? / AT+COPS?
    // ile tamamlar. LBS sorgusunun girdisidir.
    bool   getCellInfo(CellInfo &info);
    String getRawCellReport();      // CPSI + CEREG + COPS ham çıktısı (tanılama)

    // ---------- HÜCRESEL KONUM ----------
    // 1) Modem içi LBS — AT+CLBS (SIMCom sunucusu). PDP aktif olmalı.
    bool   lbsQueryModem(GeoFix &fix);
    // 2) Online hücre veritabanı — HTTP ile sorgular. API anahtarı gerekir.
    bool   lbsQueryOnline(const CellInfo &info, GeoFix &fix);
    // 3) Otomatik: önce modem LBS, olmazsa online API.
    bool   getLocation(GeoFix &fix, CellInfo *outInfo = nullptr);

    // Online sağlayıcı ayarı
    void   setApiKey(const String &key, LbsProvider provider = LBS_UNWIRED);
    void   setUnwiredEndpoint(const String &url);   // us1/eu1/ap1 bölge seçimi

    // Google Maps bağlantısı üretir
    String mapsUrl(const GeoFix &fix);

    // HTTP istemcisi (online sorgu için; doğrudan da kullanılabilir)
    bool   httpInit();
    void   httpTerm();
    bool   httpSetUrl(const String &url);
    bool   httpSetContentType(const String &ct);
    bool   httpData(const String &body);         // AT+HTTPDATA
    int    httpAction(int method);               // 0=GET, 1=POST — HTTP kodu döner
    String httpRead(int maxLen = 1024);          // yanıt gövdesini döndürür

    // Yardımcı
    void debugPrint(const String &msg);
    void setDebug(bool enabled);

private:
    HardwareSerial *_serial;
    uint8_t  _txPin;
    uint8_t  _rxPin;
    uint8_t  _pwrKeyPin;
    uint8_t  _resetPin;
    bool     _debugEnabled;

    String      _apiKey;
    LbsProvider _provider;
    String      _unwiredUrl;

    void   _exitDataModeAndDrain();
    void   _pwrKeySet(bool pressed);   // kutupluluğu kendi içinde çözer
    void   _resetSet(bool asserted);
    void   _sslRelax();                          // HTTPS için sertifika doğrulamayı kapat
    bool   _parseCpsi(const String &resp, CellInfo &info);
    bool   _fillFromCereg(CellInfo &info);       // LAC/CID yedeği
    bool   _fillFromCops(CellInfo &info);        // MCC/MNC yedeği
    bool   _queryUnwired(const CellInfo &info, GeoFix &fix);
    bool   _queryOpenCellId(const CellInfo &info, GeoFix &fix);

    static String   _field(const String &csv, int index);
    static uint32_t _parseNum(String s);         // "0x1A2B" veya "12345"
    static double   _jsonNumber(const String &json, const String &key, bool *ok = nullptr);
    static String   _jsonString(const String &json, const String &key);
};

#endif // LBSLOCATION_H
