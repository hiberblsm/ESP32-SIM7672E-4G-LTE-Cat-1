/*
 * 01_SerialTest.h — ESP32-S3 + SIM7672E Serial / AT Komut Testi
 *
 * Bu sınıf YALNIZCA 01_SerialTest için gereken fonksiyonları içerir:
 *   - Başlatma & güç yönetimi (begin/powerOn/powerOff/hardReset)
 *   - Temel AT komutları (sendAT/sendATExpect/waitForResponse)
 *   - Modem bilgileri (IMEI/IMSI/Operatör/CSQ/SIM/LTE kayıt)
 *
 * HTTP/TCP/UDP/MQTT/SMS/DTMF fonksiyonları BURADA YOKTUR.
 * Her örnek kendi bağımsız sınıfını kullanır (ortak kütüphane yoktur).
 *
 * Pin Bağlantıları (ESP32-S3):
 *   GPIO 17 (TX) -> SIM7672E RX
 *   GPIO 16 (RX) <- SIM7672E TX
 *   GPIO  4      -> SIM7672E PWRKEY
 *   GPIO  5      -> SIM7672E RESET (LOW aktif)
 */

#ifndef SERIALTEST_H
#define SERIALTEST_H

#include <Arduino.h>
#include <HardwareSerial.h>

// Varsayılan pin tanımları
#define SIM7672_TX_PIN      17
#define SIM7672_RX_PIN      16
#define SIM7672_PWRKEY_PIN   4
#define SIM7672_RESET_PIN    5

// Varsayılan baud rate (SIM7672E: 115200)
#define SIM7672_BAUD      115200

// Zaman aşımı değerleri (ms)
#define AT_TIMEOUT        3000
#define BOOT_TIMEOUT     15000

class SerialTest {
public:
    // Kurucu: TX pini, RX pini, PWRKEY pini, RESET pini
    // Dahili olarak ESP32 Hardware Serial 2 (UART2) kullanılır.
    SerialTest(uint8_t txPin      = SIM7672_TX_PIN,
               uint8_t rxPin      = SIM7672_RX_PIN,
               uint8_t pwrKeyPin  = SIM7672_PWRKEY_PIN,
               uint8_t resetPin   = SIM7672_RESET_PIN);

    // Başlatma & güç
    bool begin(long baud = SIM7672_BAUD);
    void powerOn();
    void powerOff();
    void hardReset();

    // Temel AT komutları
    String sendAT(const String &cmd, uint32_t timeoutMs = AT_TIMEOUT);
    bool   sendATExpect(const String &cmd, const String &expected,
                        uint32_t timeoutMs = AT_TIMEOUT);
    bool   waitForResponse(const String &expected, uint32_t timeoutMs = AT_TIMEOUT);
    void   clearBuffer();

    // Modem bilgileri
    String getIMEI();
    String getIMSI();
    String getOperator();
    int    getSignalQuality();   // CSQ (0-31, 99=bilinmiyor)
    String getSIMStatus();
    bool   isRegistered();       // AT+CEREG? (LTE) önce, AT+CREG? (2G) yedek

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
};

#endif // SERIALTEST_H
