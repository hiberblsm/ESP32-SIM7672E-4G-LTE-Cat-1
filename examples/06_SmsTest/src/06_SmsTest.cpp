/*
 * 06_SmsTest.cpp — ESP32-S3 + SIM7672E SMS Testi
 *
 * Yalnızca SMS testi için gerekli fonksiyonları içerir.
 * GPRS/LTE veri bağlantısı gerektirmez (yalnız şebeke kaydı yeterli).
 * ESP32-S3 üzerinde UART2 (Serial2) kullanılır.
 */

#include "06_SmsTest.h"

// ==================== KURUCU & BAŞLATMA ====================

SmsTest::SmsTest(uint8_t txPin, uint8_t rxPin, uint8_t pwrKeyPin, uint8_t resetPin)
    : _txPin(txPin), _rxPin(rxPin), _pwrKeyPin(pwrKeyPin), _resetPin(resetPin),
      _debugEnabled(true)
{
    _serial = &Serial2;
}

bool SmsTest::begin(long baud) {
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

void SmsTest::powerOn() {
    if (_pwrKeyPin == 255) { debugPrint("powerOn: PWRKEY pin yok, atlaniyor."); return; }
    digitalWrite(_pwrKeyPin, LOW);
    delay(1500);
    digitalWrite(_pwrKeyPin, HIGH);
    delay(5000);
}

void SmsTest::powerOff() {
    sendAT("AT+CPOF", 3000);
    if (_pwrKeyPin != 255) {
        delay(1000);
        digitalWrite(_pwrKeyPin, LOW);
        delay(2500);
        digitalWrite(_pwrKeyPin, HIGH);
    }
}

void SmsTest::hardReset() {
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

String SmsTest::sendAT(const String &cmd, uint32_t timeoutMs) {
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

bool SmsTest::sendATExpect(const String &cmd, const String &expected,
                           uint32_t timeoutMs) {
    return sendAT(cmd, timeoutMs).indexOf(expected) >= 0;
}

bool SmsTest::waitForResponse(const String &expected, uint32_t timeoutMs) {
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

void SmsTest::clearBuffer() {
    unsigned long idle = millis();
    while (millis() - idle < 180) {
        if (_serial->available()) {
            _serial->read();
            idle = millis();
        }
        yield();
    }
}

void SmsTest::_exitDataModeAndDrain() {
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

int SmsTest::getSignalQuality() {
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

bool SmsTest::isRegistered() {
    String resp = sendAT("AT+CEREG?");
    if (resp.indexOf(",1") >= 0 || resp.indexOf(",5") >= 0) return true;
    resp = sendAT("AT+CREG?");
    return (resp.indexOf(",1") >= 0 || resp.indexOf(",5") >= 0);
}

String SmsTest::getSIMStatus() {
    String resp = sendAT("AT+CPIN?", 3000);
    if (resp.indexOf("READY") >= 0)       return "READY";
    if (resp.indexOf("SIM PIN") >= 0)     return "SIM PIN";
    if (resp.indexOf("SIM PUK") >= 0)     return "SIM PUK";
    if (resp.indexOf("SIM PIN2") >= 0)    return "SIM PIN2";
    if (resp.indexOf("SIM PUK2") >= 0)    return "SIM PUK2";
    if (resp.indexOf("PH-NET PIN") >= 0)  return "PH-NET PIN";
    if (resp.indexOf("NOT READY") >= 0)   return "NOT READY";
    if (resp.indexOf("NOT INSERTED") >= 0) return "NOT INSERTED";
    return "UNKNOWN";
}

String SmsTest::getOperator() {
    String resp = sendAT("AT+COPS?", 3000);
    int idx = resp.indexOf("+COPS:");
    if (idx >= 0) {
        // +COPS: <mode>,<format>,<oper>[,<act>]
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

// ==================== SMS ====================

bool SmsTest::smsSend(const String &number, const String &text) {
    // Text mode
    sendAT("AT+CMGF=1", 3000);

    String resp = sendAT("AT+CMGS=\"" + number + "\"", 5000);
    if (resp.indexOf('>') < 0) {
        debugPrint("CMGS '>' istemi gelmedi");
        return false;
    }

    delay(100);
    _serial->print(text);
    _serial->write(0x1A);   // Ctrl-Z — gönder
    _serial->flush();

    // +CMGS: <mr> gelirse gönderim başarılı
    return waitForResponse("+CMGS:", SMS_SEND_TIMEOUT);
}

String SmsTest::smsList(const String &type) {
    sendAT("AT+CMGF=1", 3000);
    return sendAT("AT+CMGL=\"" + type + "\"", SMS_LIST_TIMEOUT);
}

String SmsTest::smsRead(int index) {
    sendAT("AT+CMGF=1", 3000);
    return sendAT("AT+CMGR=" + String(index), 5000);
}

void SmsTest::smsSetIncoming(bool enabled) {
    if (enabled) {
        // +CMT: URC ile gelen SMS'i doğrudan bildir
        sendAT("AT+CMGF=1", 3000);
        sendAT("AT+CNMI=2,2,0,0,0", 3000);
    } else {
        sendAT("AT+CNMI=0,0,0,0,0", 3000);
    }
}

String SmsTest::smsWaitIncoming(uint32_t timeoutMs) {
    String buf;
    unsigned long start = millis();
    while (millis() - start < timeoutMs) {
        while (_serial->available()) buf += (char)_serial->read();
        int idx = buf.indexOf("+CMT:");
        if (idx >= 0) {
            // Mesaj gövdesinin tamamının gelmesi için kısa bekle
            delay(400);
            while (_serial->available()) buf += (char)_serial->read();
            return buf.substring(idx);
        }
        yield();
    }
    return "";
}

bool SmsTest::smsDeleteAll() {
    return sendATExpect("AT+CMGD=1,4", "OK", 10000);
}

// ==================== YARDIMCILAR ====================

void SmsTest::debugPrint(const String &msg) {
    if (_debugEnabled) {
        Serial.print("[SIM7672E] ");
        Serial.println(msg);
    }
}

void SmsTest::setDebug(bool enabled) {
    _debugEnabled = enabled;
}
