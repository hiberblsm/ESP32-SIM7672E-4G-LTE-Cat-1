/*
 * 02_HTTPTest.h — ESP32-S3 + SIM7672E HTTP GET / POST Testi
 *
 * Bu sınıf YALNIZCA 02_HTTPTest için gereken fonksiyonları içerir:
 *   - Başlatma & güç yönetimi (begin/powerOn/powerOff/hardReset)
 *   - Temel AT komutları (sendAT/sendATExpect/waitForResponse)
 *   - LTE veri bağlantısı (initGPRS/closeGPRS/getLocalIP)
 *   - HTTP istemcisi (httpInit/httpSetUrl/httpSetApiKey/httpData/httpAction/httpRead)
 *   - Yüksek seviye yardımcılar (getToken/postIngest)
 *
 * Diğer örneklerin (TCP/UDP/MQTT/SMS/DTMF) fonksiyonları BURADA YOKTUR.
 * Her örnek kendi bağımsız sınıfını kullanır (ortak kütüphane yoktur).
 *
 * Pin Bağlantıları (ESP32-S3):
 *   GPIO 17 (TX) -> SIM7672E RX
 *   GPIO 16 (RX) <- SIM7672E TX
 *   GPIO  7      -> SIM7672E PWRKEY
 *   GPIO  5      -> SIM7672E RESET (LOW aktif)
 */

#ifndef HTTPTEST_H
#define HTTPTEST_H

#include <Arduino.h>
#include <HardwareSerial.h>

// Varsayılan pin tanımları
#define SIM7672_TX_PIN      17
#define SIM7672_RX_PIN      16
#define SIM7672_PWRKEY_PIN   7
#define SIM7672_RESET_PIN    5

// Varsayılan baud rate (SIM7672E: 115200)
#define SIM7672_BAUD      115200

// ---- PWRKEY / RESET SÜRÜCÜ KUTUPLULUĞU ----
// Modülde her iki pin de AKTİF-DÜŞÜK'tür (pin GND'ye çekilince tetiklenir).
//
//   0 = GPIO doğrudan pine bağlı        → GPIO LOW  = tetiklendi
//   1 = GPIO bir NPN transistör bazında → GPIO HIGH = tetiklendi (mantık TERS!)
//
// NPN sürücü (base--R-->GPIO, emitter-->GND, collector-->pin) kullanıldığında
// yanlış değer, pini boşta sürekli tetiklenmiş tutar: PWRKEY'de modül açılıp
// hemen kapanır, RESET'te modül reset'ten hiç çıkamaz.
#define PWRKEY_ACTIVE_HIGH  1
#define RESET_ACTIVE_HIGH   1

// PWRKEY darbe süreleri (ms)
#define PWRKEY_ON_MS      1500    // Açma darbesi  (~1.0-1.5 s)
#define PWRKEY_OFF_MS     3000    // Kapatma darbesi (>= 2.5 s)
#define PWRKEY_BOOT_MS    8000    // Darbe sonrası AT'ye hazır olma süresi


// Zaman aşımı değerleri (ms)
#define AT_TIMEOUT        3000
#define GPRS_TIMEOUT     15000
#define HTTP_TIMEOUT     28000

class HttpTest {
public:
    // Kurucu: TX pini, RX pini, PWRKEY pini, RESET pini
    // Dahili olarak ESP32 Hardware Serial 2 (UART2) kullanılır.
    HttpTest(uint8_t txPin      = SIM7672_TX_PIN,
             uint8_t rxPin      = SIM7672_RX_PIN,
             uint8_t pwrKeyPin  = SIM7672_PWRKEY_PIN,
             uint8_t resetPin   = SIM7672_RESET_PIN);

    // Başlatma & güç
    bool begin(long baud = SIM7672_BAUD);
    void powerOn();
    void powerOff();
    void hardReset();

    // Ham PWRKEY darbesi — donanım/kutupluluk tanılaması için.
    void powerKeyPulse(uint16_t ms);

    // Temel AT komutları
    String sendAT(const String &cmd, uint32_t timeoutMs = AT_TIMEOUT);
    bool   sendATExpect(const String &cmd, const String &expected,
                        uint32_t timeoutMs = AT_TIMEOUT);
    bool   waitForResponse(const String &expected, uint32_t timeoutMs = AT_TIMEOUT);
    void   clearBuffer();

    // Modem bilgileri
    int    getSignalQuality();   // CSQ (0-31, 99=bilinmiyor)
    bool   isRegistered();       // AT+CEREG? (LTE) önce, AT+CREG? (2G) yedek

    // LTE veri bağlantısı
    bool   initGPRS(const String &apn,
                    const String &user = "", const String &pass = "");
    bool   closeGPRS();
    String getLocalIP();         // AT+CGPADDR

    // HTTP istemcisi (SIM7672E AT+HTTP* komutları)
    bool   httpInit();
    void   httpTerm();
    bool   httpSetUrl(const String &url);
    bool   httpSetContentType(const String &ct);
    bool   httpSetApiKey(const String &token);   // x-api-key header
    bool   httpData(const String &body);         // AT+HTTPDATA
    int    httpAction(int method);               // 0=GET, 1=POST — HTTP kodu döner
    String httpRead(int maxLen = 1024);          // yanıt gövdesini döndürür

    // Yüksek seviye yardımcılar
    bool getToken(String &token, String &clientId);      // POST /token
    bool postIngest(const String &token, const String &deviceId,
                    const String &jsonData, String &resp, int &code); // POST /ingest

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

    void _exitDataModeAndDrain();
    void _pwrKeySet(bool pressed);   // kutupluluğu kendi içinde çözer
    void _resetSet(bool asserted);
};

#endif // HTTPTEST_H
