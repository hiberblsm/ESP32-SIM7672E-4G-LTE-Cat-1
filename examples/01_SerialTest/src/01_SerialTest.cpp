/*
 * 01_SerialTest.cpp — ESP32-S3 + SIM7672E Serial / AT Komut Testi
 *
 * Yalnızca seri/AT testi için gerekli fonksiyonları içerir.
 * ESP32-S3 üzerinde UART2 (Serial2) kullanılır.
 */

#include "01_SerialTest.h"

// ==================== KURUCU & BAŞLATMA ====================

SerialTest::SerialTest(uint8_t txPin, uint8_t rxPin, uint8_t pwrKeyPin, uint8_t resetPin)
    : _txPin(txPin), _rxPin(rxPin), _pwrKeyPin(pwrKeyPin), _resetPin(resetPin),
      _debugEnabled(true)
{
    _serial = &Serial2;
}

bool SerialTest::begin(long baud) {
    if (_serial) {
        _serial->end();
        delay(30);
    }

    // TX pini önce manuel HIGH yap — logic level translator reset sırasında
    // TX'i LOW görebilir, modem bunu veri olarak algılar.
    pinMode(_txPin, OUTPUT);
    digitalWrite(_txPin, HIGH);
    delay(50);

    // ESP32 HardwareSerial: begin(baud, config, rxPin, txPin)
    _serial->begin(baud, SERIAL_8N1, _rxPin, _txPin);
    if (_pwrKeyPin != 255) {
        pinMode(_pwrKeyPin, OUTPUT);
        _pwrKeySet(false);          // boşta: tuş bırakılmış
    }
    if (_resetPin != 255) {
        pinMode(_resetPin, OUTPUT);
        _resetSet(false);           // boşta: reset serbest
    }

    delay(1000);
    debugPrint("SIM7672E baslatiliyor...");
    _exitDataModeAndDrain();

    _serial->println("ATE0");
    delay(300);
    clearBuffer();

    // --- İlk 3 deneme: modem zaten açık mı? ---
    bool alive = false;
    for (int i = 0; i < 3; i++) {
        if (sendATExpect("AT", "OK", 2000)) { alive = true; break; }
        _serial->println("ATE0");
        delay(500);
        clearBuffer();
    }

    // Yanıt yok → PWRKEY ile güç ver, boot bekle
    if (!alive) {
        if (_pwrKeyPin != 255) {
            debugPrint("Modem yanit yok, PWRKEY ile guc veriliyor...");
            _pwrKeySet(true);
            delay(PWRKEY_ON_MS);
            _pwrKeySet(false);
            debugPrint("PWRKEY birakildi, boot bekleniyor...");
            delay(PWRKEY_BOOT_MS);
        } else {
            debugPrint("Modem yanit yok, PWRKEY yok — boot bekleniyor...");
            delay(PWRKEY_BOOT_MS);
        }
        clearBuffer();
    }

    // --- Son 8 deneme (hem ilk açılış hem de güç sonrası) ---
    for (int i = 0; i < 8; i++) {
        if (sendATExpect("AT", "OK", 2000)) {
            debugPrint("Modem hazir!");
            _serial->write(0x1B);
            delay(200);
            clearBuffer();
            sendAT("ATE0");
            sendAT("AT+CMGF=1");
            return true;
        }
        _serial->println("ATE0");
        delay(800);
        clearBuffer();
    }
    debugPrint("HATA: Modem cevap vermiyor!");
    return false;
}

void SerialTest::powerOn() {
    if (_pwrKeyPin == 255) { debugPrint("powerOn: PWRKEY pin yok, atlaniyor."); return; }
    debugPrint("Power ON...");
    _pwrKeySet(true);
    delay(PWRKEY_ON_MS);
    _pwrKeySet(false);
    delay(5000);
    debugPrint("Power ON");
}

void SerialTest::powerOff() {
    debugPrint("Power OFF...");
    sendAT("AT+CPOF", 3000);
    if (_pwrKeyPin != 255) {
        delay(1000);
        _pwrKeySet(true);
        delay(PWRKEY_OFF_MS);
        _pwrKeySet(false);
    }
    debugPrint("Power OFF");
}

void SerialTest::hardReset() {
    debugPrint("Hard reset...");
    if (_resetPin != 255) {
        if (_serial) _serial->end();
        pinMode(_txPin, OUTPUT);
        digitalWrite(_txPin, HIGH);
        delay(50);
        _resetSet(true);
        delay(300);
        _resetSet(false);
        delay(6000);
        begin();
        debugPrint("Hard reset tamam (HW RESET pin)");
        return;
    }
    debugPrint("hardReset: RESET yok, AT+CRESET kullaniliyor.");
    sendAT("AT+CRESET", 3000);
    delay(8000);
    clearBuffer();
}

// ==================== TEMEL AT KOMUTLARI ====================

#define AT_BUF_SIZE 768

String SerialTest::sendAT(const String &cmd, uint32_t timeoutMs) {
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
            if (c == '\n') {
                if (strstr(buf, "\r\nOK\r\n")    ||
                    strstr(buf, "\nOK\r")         ||
                    strstr(buf, "ERROR")           ||
                    strstr(buf, "NO CARRIER")      ||
                    strstr(buf, "DOWNLOAD")        ||
                    (pos > 2 && buf[pos-3] == '>' && buf[pos-2] == ' ')) {
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

bool SerialTest::sendATExpect(const String &cmd, const String &expected,
                               uint32_t timeoutMs) {
    return sendAT(cmd, timeoutMs).indexOf(expected) >= 0;
}

bool SerialTest::waitForResponse(const String &expected, uint32_t timeoutMs) {
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

void SerialTest::clearBuffer() {
    unsigned long idle = millis();
    while (millis() - idle < 180) {
        if (_serial->available()) {
            _serial->read();
            idle = millis();
        }
        yield();
    }
}

// PWRKEY "tuşa bas / bırak". Modülde pin aktif-düşüktür; araya NPN girdiğinde
// GPIO seviyesi tersine döner (PWRKEY_ACTIVE_HIGH).
void SerialTest::_pwrKeySet(bool pressed) {
    if (_pwrKeyPin == 255) return;
#if PWRKEY_ACTIVE_HIGH
    digitalWrite(_pwrKeyPin, pressed ? HIGH : LOW);
#else
    digitalWrite(_pwrKeyPin, pressed ? LOW : HIGH);
#endif
}

// RESET pini de aktif-düşüktür; NPN varsa aynı şekilde terslenir.
void SerialTest::_resetSet(bool asserted) {
    if (_resetPin == 255) return;
#if RESET_ACTIVE_HIGH
    digitalWrite(_resetPin, asserted ? HIGH : LOW);
#else
    digitalWrite(_resetPin, asserted ? LOW : HIGH);
#endif
}

void SerialTest::powerKeyPulse(uint16_t ms) {
    if (_pwrKeyPin == 255) { debugPrint("powerKeyPulse: PWRKEY pin yok"); return; }
    pinMode(_pwrKeyPin, OUTPUT);
    debugPrint("PWRKEY darbesi: " + String(ms) + " ms");
    _pwrKeySet(true);
    delay(ms);
    _pwrKeySet(false);
}

void SerialTest::_exitDataModeAndDrain() {
    clearBuffer();
    _serial->write(0x1B);
    delay(250);
    clearBuffer();
    delay(1050);
    _serial->print("+++");
    delay(1150);
    clearBuffer();
}

// ==================== MODEM BİLGİLERİ ====================

String SerialTest::getIMEI() {
    String resp = sendAT("AT+GSN");
    int s = resp.indexOf('\n');
    int e = resp.indexOf('\r', s + 1);
    if (s >= 0 && e > s) return resp.substring(s + 1, e);
    return "";
}

String SerialTest::getIMSI() {
    String resp = sendAT("AT+CIMI");
    int s = resp.indexOf('\n');
    int e = resp.indexOf('\r', s + 1);
    if (s >= 0 && e > s) return resp.substring(s + 1, e);
    return "";
}

String SerialTest::getOperator() {
    String resp = sendAT("AT+COPS?");
    int s = resp.indexOf('"');
    int e = resp.indexOf('"', s + 1);
    if (s >= 0 && e > s) return resp.substring(s + 1, e);
    return "";
}

int SerialTest::getSignalQuality() {
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

String SerialTest::getSIMStatus() {
    String resp = sendAT("AT+CPIN?");
    if (resp.indexOf("READY")        >= 0) return "READY";
    if (resp.indexOf("SIM PIN")      >= 0) return "PIN_REQUIRED";
    if (resp.indexOf("SIM PUK")      >= 0) return "PUK_REQUIRED";
    if (resp.indexOf("NOT INSERTED") >= 0) return "NOT_INSERTED";
    return "UNKNOWN";
}

bool SerialTest::isRegistered() {
    String resp = sendAT("AT+CEREG?");
    if (resp.indexOf(",1") >= 0 || resp.indexOf(",5") >= 0) return true;
    resp = sendAT("AT+CREG?");
    return (resp.indexOf(",1") >= 0 || resp.indexOf(",5") >= 0);
}

// ==================== YARDIMCILAR ====================

void SerialTest::debugPrint(const String &msg) {
    if (_debugEnabled) {
        Serial.print("[SIM7672E] ");
        Serial.println(msg);
    }
}

void SerialTest::setDebug(bool enabled) {
    _debugEnabled = enabled;
}
