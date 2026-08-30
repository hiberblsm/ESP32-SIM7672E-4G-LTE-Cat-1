/*
 * 08_DtmfRelayControl.h — ESP32-S3 + SIM7672E DTMF Röle Kontrolü
 *
 * Bu sınıf YALNIZCA 08_DtmfRelayControl için gereken fonksiyonları içerir:
 *   - Başlatma & güç yönetimi (begin/powerOn/powerOff/hardReset)
 *   - Temel AT komutları (sendAT/sendATExpect/waitForResponse/clearBuffer)
 *   - Modem/SIM bilgileri (getSignalQuality/isRegistered/getSIMStatus/getOperator)
 *   - Çağrı & DTMF (callSetCLIP/callSetDTMF/callAnswer/callHangup/callPoll)
 *
 * callPoll, gelen URC'leri "olay + detay" olarak ayrıştırır:
 *   RING  → gelen çağrı
 *   CLIP  → arayan numara (detail = numara)
 *   DTMF  → tuş basımı (detail = tek karakter)
 *   HANGUP→ çağrı kapandı (NO CARRIER / BUSY)
 *
 * Röle kontrolü (pin ataması ve roleSet) main.cpp'de tutulur.
 *
 * Pin Bağlantıları (ESP32-S3):
 *   GPIO 17 (TX) -> SIM7672E RX
 *   GPIO 16 (RX) <- SIM7672E TX
 *   GPIO  7      -> SIM7672E PWRKEY
 *   GPIO  5      -> SIM7672E RESET (LOW aktif)
 */

#ifndef DTMFRELAYCONTROL_H
#define DTMFRELAYCONTROL_H

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
#define CALL_TIMEOUT      5000

class DtmfRelay {
public:
    // Kurucu: TX pini, RX pini, PWRKEY pini, RESET pini
    DtmfRelay(uint8_t txPin      = SIM7672_TX_PIN,
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

    // Modem & SIM bilgileri
    int    getSignalQuality();
    bool   isRegistered();
    String getSIMStatus();
    String getOperator();

    // Çağrı & DTMF
    bool callSetCLIP(bool enable);
    bool callSetDTMF(bool enable);
    bool callAnswer();
    bool callHangup();

    // Gelen URC'yi ayrıştır: event = RING/CLIP/DTMF/HANGUP, detail = numara/tuş
    bool callPoll(char *event,  uint8_t eventLen,
                  char *detail, uint8_t detailLen);

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

#endif // DTMFRELAYCONTROL_H
