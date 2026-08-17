/*
 * 05_MQTTTest.h — ESP32-S3 + SIM7672E MQTT Testi
 *
 * Bu sınıf YALNIZCA 05_MQTTTest için gereken fonksiyonları içerir:
 *   - Başlatma & güç yönetimi (begin/powerOn/powerOff/hardReset)
 *   - Temel AT komutları (sendAT/sendATExpect/waitForResponse)
 *   - LTE veri bağlantısı (initGPRS/closeGPRS/getLocalIP)
 *   - HTTP istemcisi (yalnız token almak için: POST /token)
 *   - TCP soket (tcpConnect/tcpSend/tcpReceive/tcpClose)
 *   - MQTT (mqttConnect/mqttPublish/mqttSubscribe/mqttLoop/mqttPing/mqttDisconnect)
 *
 * MQTT: TCP üzerinden ham MQTT 3.1.1 paketleri.
 * Auth: publish payload JSON'ında "token" + "deviceId" alanları.
 *
 * Pin Bağlantıları (ESP32-S3):
 *   GPIO 17 (TX) -> SIM7672E RX
 *   GPIO 16 (RX) <- SIM7672E TX
 *   GPIO  4      -> SIM7672E PWRKEY
 *   GPIO  5      -> SIM7672E RESET (LOW aktif)
 */

#ifndef MQTTTEST_H
#define MQTTTEST_H

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
#define GPRS_TIMEOUT     15000
#define HTTP_TIMEOUT     28000
#define MQTT_TIMEOUT     10000

// MQTT paket sabitleri (MQTT 3.1.1)
#define MQTT_CONNECT      0x10
#define MQTT_CONNACK      0x20
#define MQTT_PUBLISH      0x30
#define MQTT_SUBSCRIBE    0x82
#define MQTT_SUBACK       0x90
#define MQTT_PINGREQ      0xC0
#define MQTT_PINGRESP     0xD0
#define MQTT_DISCONNECT   0xE0

// TCP/UDP bağlantı indeksi (AT+CIPOPEN=<n>,...)
#define CIP_IDX  0

class MqttTest {
public:
    // Kurucu: TX pini, RX pini, PWRKEY pini, RESET pini
    // Dahili olarak ESP32 Hardware Serial 2 (UART2) kullanılır.
    MqttTest(uint8_t txPin      = SIM7672_TX_PIN,
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
    int    getSignalQuality();   // CSQ (0-31, 99=bilinmiyor)
    bool   isRegistered();       // AT+CEREG? (LTE) önce, AT+CREG? (2G) yedek

    // LTE veri bağlantısı
    bool   initGPRS(const String &apn,
                    const String &user = "", const String &pass = "");
    bool   closeGPRS();
    String getLocalIP();         // AT+CGPADDR

    // HTTP istemcisi (yalnız token almak için)
    bool   httpInit();
    void   httpTerm();
    bool   httpSetUrl(const String &url);
    bool   httpSetContentType(const String &ct);
    bool   httpData(const String &body);
    int    httpAction(int method);

    // MQTT (TCP üzerinden ham paketler)
    bool   mqttConnect(const String &broker, uint16_t port,
                       const String &clientId,
                       const String &user = "", const String &pass = "");
    bool   mqttPublish(const String &topic, const String &payload);
    bool   mqttSubscribe(const String &topic);
    String mqttLoop(uint32_t timeoutMs = 1000);
    bool   mqttPing();
    bool   mqttDisconnect();

    String httpRead(int maxLen = 1024);
    bool   getToken(String &token, String &clientId);      // POST /token

    // TCP soket
    bool   tcpConnect(const String &host, uint16_t port);
    bool   tcpSend(const String &data);
    String tcpReceive(uint32_t timeoutMs = 5000);
    bool   tcpClose();
    bool   isTCPConnected();

    // Yardımcı
    void debugPrint(const String &msg);
    void setDebug(bool enabled);
// MQTT dahili durum
    bool     _mqttConnected;
    String   _mqttBroker;
    uint16_t _mqttPort;

    
private:
    HardwareSerial *_serial;
    uint8_t  _txPin;
    uint8_t  _rxPin;
    uint8_t  _pwrKeyPin;
    uint8_t  _resetPin;
    bool     _debugEnabled;
    bool     _netOpen;
    String   _lastCipResp;   // _cipSend sonrası okunmuş tampon (CONNACK burada kalabilir)

    void _exitDataModeAndDrain();
    bool _openNet();
    bool _cipConnect(const char *type, const String &host, uint16_t port, int localPort = -1);
    bool _cipSend(const uint8_t *buf, size_t len);
    String _cipReceive(uint32_t timeoutMs);
};

#endif // MQTTTEST_H
