/*
 * 08_DtmfRelayControl.cpp — ESP32-S3 + SIM7672E DTMF Röle Kontrolü
 *
 * Yalnızca DTMF/çağrı röle kontrolü için gerekli fonksiyonları içerir.
 * GPRS/LTE veri bağlantısı gerektirmez (yalnız şebeke kaydı yeterli).
 * ESP32-S3 üzerinde UART2 (Serial2) kullanılır.
 */

#include "08_DtmfRelayControl.h"

// ==================== KURUCU & BAŞLATMA ====================

DtmfRelay::DtmfRelay(uint8_t txPin, uint8_t rxPin, uint8_t pwrKeyPin, uint8_t resetPin)
    : _txPin(txPin), _rxPin(rxPin), _pwrKeyPin(pwrKeyPin), _resetPin(resetPin),
      _debugEnabled(true)
{
    _serial = &Serial2;
}

bool DtmfRelay::begin(long baud) {
    if (_serial) {
        _serial->end();
        delay(30);
    }

    pinMode(_txPin, OUTPUT);
    digitalWrite(_txPin, HIGH);
    delay(50);

    _serial->begin(baud, SERIAL_8N1, _rxPin, _txPin);
    if (_pwrKeyPin != 255) {
        pinMode(_pwrKeyPin, OUTPUT);
        digitalWrite(_pwrKeyPin, HIGH);
    }
    if (_resetPin != 255) {
        pinMode(_resetPin, OUTPUT);
        digitalWrite(_resetPin, HIGH);
    }

    delay(1000);
    debugPrint("SIM7672E baslatiliyor...");
    _exitDataModeAndDrain();

    _serial->println("ATE0");
    delay(300);
    clearBuffer();

    bool alive = false;
    for (int i = 0; i < 3; i++) {
        if (sendATExpect("AT", "OK", 2000)) { alive = true; break; }
        _serial->println("ATE0");
        delay(500);
        clearBuffer();
    }

    if (!alive) {
        if (_pwrKeyPin != 255) {
            debugPrint("Modem yanit yok, PWRKEY ile guc veriliyor...");
            digitalWrite(_pwrKeyPin, LOW);
            delay(1600);
            digitalWrite(_pwrKeyPin, HIGH);
            debugPrint("PWRKEY HIGH, boot bekleniyor (8s)...");
            delay(8000);
        } else {
            debugPrint("Modem yanit yok, PWRKEY yok — boot icin 8s bekleniyor...");
            delay(8000);
        }
        clearBuffer();
    }

    for (int i = 0; i < 8; i++) {
        if (sendATExpect("AT", "OK", 2000)) {
            debugPrint("Modem hazir!");
            _serial->write(0x1B);
            delay(200);
            clearBuffer();
            sendAT("ATE0");
            return true;
        }
        _serial->println("ATE0");
        delay(800);
        clearBuffer();
    }
    debugPrint("HATA: Modem cevap vermiyor!");
    return false;
}

void DtmfRelay::powerOn() {
    if (_pwrKeyPin == 255) { debugPrint("powerOn: PWRKEY pin yok, atlaniyor."); return; }
    digitalWrite(_pwrKeyPin, LOW);
    delay(1500);
    digitalWrite(_pwrKeyPin, HIGH);
    delay(5000);
}

void DtmfRelay::powerOff() {
    sendAT("AT+CPOF", 3000);
    if (_pwrKeyPin != 255) {
        delay(1000);
        digitalWrite(_pwrKeyPin, LOW);
        delay(2500);
        digitalWrite(_pwrKeyPin, HIGH);
    }
}

void DtmfRelay::hardReset() {
    debugPrint("Hard reset...");
    if (_resetPin != 255) {
        if (_serial) _serial->end();
        pinMode(_txPin, OUTPUT);
        digitalWrite(_txPin, HIGH);
        delay(50);
        digitalWrite(_resetPin, LOW);
        delay(300);
        digitalWrite(_resetPin, HIGH);
        delay(6000);
        begin();
        debugPrint("Hard reset tamam (HW RESET pin)");
        return;
    }
    sendAT("AT+CRESET", 3000);
    delay(8000);
    clearBuffer();
}

// ==================== TEMEL AT KOMUTLARI ====================

#define AT_BUF_SIZE 768

String DtmfRelay::sendAT(const String &cmd, uint32_t timeoutMs) {
    clearBuffer();
    _serial->println(cmd);

    char   buf[AT_BUF_SIZE];
    size_t pos   = 0;
    bool   done  = false;
    unsigned long start = millis();

    while (!done && (millis() - start < timeoutMs)) {
        while (_serial->available()) {
            char c = (char)_serial->read();
            if (pos < AT_BUF_SIZE - 1) {
                buf[pos++] = c;
                buf[pos]   = '\0';
            }
            // AT+CMGS veri girişi istemi: satır sonu "> "
            if (pos >= 2 && buf[pos-2] == '>' && buf[pos-1] == ' ') {
                delay(5);
                done = true;
                break;
            }
            if (c == '\n') {
                if (strstr(buf, "\r\nOK\r\n")    ||
                    strstr(buf, "\nOK\r")         ||
                    strstr(buf, "ERROR")           ||
                    strstr(buf, "NO CARRIER")      ||
                    strstr(buf, "DOWNLOAD")) {
                    delay(5);
                    while (_serial->available() && pos < AT_BUF_SIZE - 1) {
                        buf[pos++] = (char)_serial->read();
                        buf[pos]   = '\0';
                    }
                    done = true;
                    break;
                }
            }
        }
        yield();
    }

    String response(buf);
    if (_debugEnabled) {
        Serial.print("[TX] "); Serial.println(cmd);
        Serial.print("[RX] "); Serial.println(response);
    }
    return response;
}

bool DtmfRelay::sendATExpect(const String &cmd, const String &expected,
                             uint32_t timeoutMs) {
    return sendAT(cmd, timeoutMs).indexOf(expected) >= 0;
}

bool DtmfRelay::waitForResponse(const String &expected, uint32_t timeoutMs) {
    char   buf[AT_BUF_SIZE];
    size_t pos = 0;
    unsigned long start = millis();
    while (millis() - start < timeoutMs) {
        while (_serial->available() && pos < AT_BUF_SIZE - 1) {
            buf[pos++] = (char)_serial->read();
            buf[pos]   = '\0';
        }
        if (pos > 0 && strstr(buf, expected.c_str())) return true;
        yield();
    }
    return false;
}

void DtmfRelay::clearBuffer() {
    unsigned long idle = millis();
    while (millis() - idle < 180) {
        if (_serial->available()) {
            _serial->read();
            idle = millis();
        }
        yield();
    }
}

void DtmfRelay::_exitDataModeAndDrain() {
    clearBuffer();
    _serial->write(0x1B);
    delay(250);
    clearBuffer();
    delay(1050);
    _serial->print("+++");
    delay(1150);
    clearBuffer();
}

// ==================== MODEM & SIM BİLGİLERİ ====================

int DtmfRelay::getSignalQuality() {
    String resp = sendAT("AT+CSQ");
    int idx = resp.indexOf("+CSQ:");
    if (idx >= 0) {
        int comma = resp.indexOf(',', idx);
        String csqStr = resp.substring(idx + 6, comma);
        csqStr.trim();
        return csqStr.toInt();
    }
    return 99;
}

bool DtmfRelay::isRegistered() {
    String resp = sendAT("AT+CEREG?");
    if (resp.indexOf(",1") >= 0 || resp.indexOf(",5") >= 0) return true;
    resp = sendAT("AT+CREG?");
    return (resp.indexOf(",1") >= 0 || resp.indexOf(",5") >= 0);
}

String DtmfRelay::getSIMStatus() {
    String resp = sendAT("AT+CPIN?", 3000);
    if (resp.indexOf("READY") >= 0)        return "READY";
    if (resp.indexOf("SIM PIN") >= 0)      return "SIM PIN";
    if (resp.indexOf("SIM PUK") >= 0)      return "SIM PUK";
    if (resp.indexOf("SIM PIN2") >= 0)     return "SIM PIN2";
    if (resp.indexOf("SIM PUK2") >= 0)     return "SIM PUK2";
    if (resp.indexOf("PH-NET PIN") >= 0)   return "PH-NET PIN";
    if (resp.indexOf("NOT READY") >= 0)    return "NOT READY";
    if (resp.indexOf("NOT INSERTED") >= 0) return "NOT INSERTED";
    return "UNKNOWN";
}

String DtmfRelay::getOperator() {
    String resp = sendAT("AT+COPS?", 3000);
    int idx = resp.indexOf("+COPS:");
    if (idx >= 0) {
        int c1 = resp.indexOf(',', idx);
        int c2 = resp.indexOf(',', c1 + 1);
        if (c1 >= 0 && c2 >= 0) {
            String oper = resp.substring(c2 + 1);
            oper.replace("\"", "");
            oper.trim();
            int comma = oper.indexOf(',');
            if (comma >= 0) oper = oper.substring(0, comma);
            oper.trim();
            if (oper.length() > 0) return oper;
        }
    }
    return "";
}

// ==================== ÇAĞRI & DTMF ====================

bool DtmfRelay::callSetCLIP(bool enable) {
    return sendATExpect(enable ? "AT+CLIP=1" : "AT+CLIP=0", "OK", 3000);
}

bool DtmfRelay::callSetDTMF(bool enable) {
    // SIM7672E: AT+DDET / AT+QTONEDET desteklenmez.
    // DTMF tonları aktif çağrı sırasında +DTMF: veya +RXDTMF: URC olarak
    // otomatik gelir; ek komut gerekmez.
    (void)enable;
    return true;
}

bool DtmfRelay::callAnswer() {
    delay(200);  // CLIP URC'nin tamamen gelmesini bekle
    clearBuffer();
    bool ok = sendATExpect("ATA", "OK", CALL_TIMEOUT);
    if (ok) debugPrint("Arama yanitlandi");
    else    debugPrint("ATA komutu basarisiz!");
    return ok;
}

bool DtmfRelay::callHangup() {
    bool ok = sendATExpect("ATH", "OK", 3000);
    debugPrint("Arama kapatildi");
    return ok;
}

bool DtmfRelay::callPoll(char *event, uint8_t eventLen,
                         char *detail, uint8_t detailLen) {
    static char buf[128];
    static uint8_t pos = 0;
    static bool init = false;
    if (!init) { memset(buf, 0, sizeof(buf)); init = true; }

    // Seri arabellekten gelen her şeyi topla (non-blocking)
    while (_serial->available()) {
        char c = (char)_serial->read();
        if (_debugEnabled) Serial.write(c);
        if (pos >= (uint8_t)(sizeof(buf) - 1)) {
            memmove(buf, buf + 64, sizeof(buf) - 64);
            memset(buf + sizeof(buf) - 64, 0, 64);
            pos = (uint8_t)(sizeof(buf) - 64);
        }
        buf[pos++] = c;
        buf[pos]   = '\0';
    }

    if (event  && eventLen  > 0) event[0]  = '\0';
    if (detail && detailLen > 0) detail[0] = '\0';

    char *p;

    // ---- DTMF tuşu ----
    p = strstr(buf, "+DTMF:");
    if (!p) p = strstr(buf, "+RXDTMF:");   // SIM7672E URC
    if (p && strchr(p, '\n')) {
        int skip = (strncmp(p, "+RXDTMF:", 8) == 0) ? 8 : 6;
        if (event && eventLen > 1) { strncpy(event, "DTMF", eventLen - 1); event[eventLen - 1] = '\0'; }
        if (detail && detailLen > 1) {
            char *s = p + skip; while (*s == ' ') s++;
            if (*s && *s != '\r' && *s != '\n') { detail[0] = *s; detail[1] = '\0'; }
        }
        pos = 0; memset(buf, 0, sizeof(buf)); return true;
    }

    // ---- Arayan numara ----
    p = strstr(buf, "+CLIP:");
    if (p && strchr(p, '\n')) {
        if (event && eventLen > 1) { strncpy(event, "CLIP", eventLen - 1); event[eventLen - 1] = '\0'; }
        if (detail && detailLen > 1) {
            char *q1 = strchr(p, '"');
            if (q1) {
                char *q2 = strchr(q1 + 1, '"');
                if (q2) {
                    uint8_t len = (uint8_t)(q2 - q1 - 1);
                    if (len >= detailLen) len = detailLen - 1;
                    memcpy(detail, q1 + 1, len); detail[len] = '\0';
                }
            }
        }
        pos = 0; memset(buf, 0, sizeof(buf)); return true;
    }

    // ---- Gelen çağrı ----
    p = strstr(buf, "RING");
    if (p) {
        if (event && eventLen > 1) { strncpy(event, "RING", eventLen - 1); event[eventLen - 1] = '\0'; }
        memmove(p, p + 4, strlen(p + 4) + 1);
        if (pos >= 4) pos -= 4; else pos = 0;
        return true;
    }

    // ---- Çağrı kapandı ----
    if (strstr(buf, "NO CARRIER") || strstr(buf, "BUSY")) {
        if (event && eventLen > 1) { strncpy(event, "HANGUP", eventLen - 1); event[eventLen - 1] = '\0'; }
        pos = 0; memset(buf, 0, sizeof(buf)); return true;
    }

    return false;
}

// ==================== YARDIMCILAR ====================

void DtmfRelay::debugPrint(const String &msg) {
    if (_debugEnabled) {
        Serial.print("[SIM7672E] ");
        Serial.println(msg);
    }
}

void DtmfRelay::setDebug(bool enabled) {
    _debugEnabled = enabled;
}
