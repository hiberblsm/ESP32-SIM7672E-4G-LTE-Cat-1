/*
 * 02_HTTPTest.cpp — ESP32-S3 + SIM7672E HTTP Testi
 *
 * Yalnızca HTTP testi için gerekli fonksiyonları içerir.
 * ESP32-S3 üzerinde UART2 (Serial2) kullanılır.
 */

#include "02_HTTPTest.h"

// ==================== KURUCU & BAŞLATMA ====================

HttpTest::HttpTest(uint8_t txPin, uint8_t rxPin, uint8_t pwrKeyPin, uint8_t resetPin)
    : _txPin(txPin), _rxPin(rxPin), _pwrKeyPin(pwrKeyPin), _resetPin(resetPin),
      _debugEnabled(true)
{
    _serial = &Serial2;
}

bool HttpTest::begin(long baud) {
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

    // Modem zaten açık mı?
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

void HttpTest::powerOn() {
    if (_pwrKeyPin == 255) { debugPrint("powerOn: PWRKEY pin yok, atlaniyor."); return; }
    digitalWrite(_pwrKeyPin, LOW);
    delay(1500);
    digitalWrite(_pwrKeyPin, HIGH);
    delay(5000);
}

void HttpTest::powerOff() {
    sendAT("AT+CPOF", 3000);
    if (_pwrKeyPin != 255) {
        delay(1000);
        digitalWrite(_pwrKeyPin, LOW);
        delay(2500);
        digitalWrite(_pwrKeyPin, HIGH);
    }
}

void HttpTest::hardReset() {
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

String HttpTest::sendAT(const String &cmd, uint32_t timeoutMs) {
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

bool HttpTest::sendATExpect(const String &cmd, const String &expected,
                             uint32_t timeoutMs) {
    return sendAT(cmd, timeoutMs).indexOf(expected) >= 0;
}

bool HttpTest::waitForResponse(const String &expected, uint32_t timeoutMs) {
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

void HttpTest::clearBuffer() {
    unsigned long idle = millis();
    while (millis() - idle < 180) {
        if (_serial->available()) {
            _serial->read();
            idle = millis();
        }
        yield();
    }
}

void HttpTest::_exitDataModeAndDrain() {
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

int HttpTest::getSignalQuality() {
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

bool HttpTest::isRegistered() {
    String resp = sendAT("AT+CEREG?");
    if (resp.indexOf(",1") >= 0 || resp.indexOf(",5") >= 0) return true;
    resp = sendAT("AT+CREG?");
    return (resp.indexOf(",1") >= 0 || resp.indexOf(",5") >= 0);
}

// ==================== LTE VERİ BAĞLANTISI ====================

bool HttpTest::initGPRS(const String &apn,
                        const String &user, const String &pass) {
    debugPrint("LTE veri baglantisi baslatiliyor...");

    sendAT("AT+CFUN=1", 5000);
    sendAT("AT+CEREG=2", 3000);
    sendAT("AT+CGATT=1", 10000);

    // Mevcut bağlantıyı temizle
    sendAT("AT+NETCLOSE", 5000);
    delay(500);
    sendAT("AT+CGACT=0,1", 3000);
    delay(1000);

    // PDP context yapılandır
    String pdpCmd = "AT+CGDCONT=1,\"IP\",\"" + apn + "\"";
    sendATExpect(pdpCmd, "OK");

    // Context 1'i aktive et
    String actResp = sendAT("AT+CGACT=1,1", GPRS_TIMEOUT);
    if (actResp.indexOf("OK") < 0) {
        debugPrint("HATA: PDP context aktive edilemedi!");
        return false;
    }
    delay(2000);

    String ip = getLocalIP();
    if (ip.length() == 0) {
        debugPrint("HATA: IP alinamadi!");
        return false;
    }
    debugPrint("LTE OK! IP: " + ip);
    return true;
}

bool HttpTest::closeGPRS() {
    sendAT("AT+NETCLOSE", 5000);
    sendAT("AT+CGACT=0,1", 5000);
    debugPrint("LTE baglanti kapatildi");
    return true;
}

String HttpTest::getLocalIP() {
    String resp = sendAT("AT+CGPADDR=1", 3000);
    int idx = resp.indexOf("+CGPADDR:");
    if (idx >= 0) {
        int comma = resp.indexOf(',', idx);
        if (comma >= 0) {
            String ip = resp.substring(comma + 1);
            ip.trim();
            ip.replace("\"", "");
            int nl = ip.indexOf('\r');
            if (nl > 0) ip = ip.substring(0, nl);
            ip.trim();
            if (ip.indexOf('.') > 0 && ip.indexOf("ERROR") < 0) return ip;
        }
    }

    // Alternatif: tüm context'leri tara
    resp = sendAT("AT+CGPADDR", 3000);
    int searchPos = 0;
    while (true) {
        idx = resp.indexOf("+CGPADDR:", searchPos);
        if (idx < 0) break;

        int comma = resp.indexOf(',', idx);
        int lineEnd = resp.indexOf('\r', idx);
        if (lineEnd < 0) lineEnd = resp.length();
        if (comma > idx && comma < lineEnd) {
            String ip = resp.substring(comma + 1, lineEnd);
            ip.trim();
            ip.replace("\"", "");
            if (ip.indexOf('.') > 0 && ip.indexOf("ERROR") < 0) return ip;
        }
        searchPos = lineEnd + 1;
    }
    return "";
}

// ==================== HTTP İSTEMCİSİ ====================

bool HttpTest::httpInit() {
    // SIM7672E: NETOPEN ve HTTPINIT aynı anda çalışamaz
    sendAT("AT+NETCLOSE", 8000);
    delay(1000);
    // PDP context'i yeniden aktif et
    sendAT("AT+CGACT=1,1", 10000);
    delay(500);
    sendAT("AT+HTTPTERM", 3000);
    delay(500);
    return sendATExpect("AT+HTTPINIT", "OK", 5000);
}

void HttpTest::httpTerm() {
    sendAT("AT+HTTPTERM", 2000);
}

bool HttpTest::httpSetUrl(const String &url) {
    String cmd = "AT+HTTPPARA=\"URL\",\"" + url + "\"";
    return sendATExpect(cmd, "OK");
}

bool HttpTest::httpSetContentType(const String &ct) {
    String cmd = "AT+HTTPPARA=\"CONTENT\",\"" + ct + "\"";
    return sendATExpect(cmd, "OK");
}

bool HttpTest::httpSetApiKey(const String &token) {
    String cmd = "AT+HTTPPARA=\"USERDATA\",\"x-api-key: " + token + "\"";
    return sendATExpect(cmd, "OK");
}

bool HttpTest::httpData(const String &body) {
    String cmd = "AT+HTTPDATA=" + String(body.length()) + ",30000";
    String resp = sendAT(cmd, 5000);
    if (resp.indexOf("DOWNLOAD") < 0) return false;
    _serial->print(body);
    return waitForResponse("OK", 5000);
}

int HttpTest::httpAction(int method) {
    // +HTTPACTION: <method>,<code>,<len> URC'sini bekle → kodu döndür
    char buf[64] = {0};
    uint8_t pos = 0;
    unsigned long start = millis();

    sendAT("AT+HTTPACTION=" + String(method), 3000);

    while (millis() - start < HTTP_TIMEOUT) {
        while (_serial->available()) {
            char c = (char)_serial->read();
            if (pos >= 63) { memmove(buf, buf + 1, 62); pos = 62; }
            buf[pos++] = c;
            buf[pos]   = '\0';

            if (c == '\n' && pos > 15) {
                char *ha = strstr(buf, "+HTTPACTION:");
                if (ha) {
                    char *c1 = strchr(ha, ',');
                    if (c1) {
                        char *c2 = strchr(c1 + 1, ',');
                        if (c2) {
                            if (_debugEnabled) {
                                Serial.print("[HTTP] code=");
                                Serial.println(atoi(c1 + 1));
                            }
                            return atoi(c1 + 1);
                        }
                    }
                }
                if (!strstr(buf, "+HTTPACT")) { pos = 0; buf[0] = '\0'; }
            }
        }
        yield();
    }
    return 0; // timeout
}

String HttpTest::httpRead(int maxLen) {
    // AT+HTTPREAD yanıtı: +HTTPREAD: <len>\r\n<gövde>\r\nOK\r\n
    // <len> değerine göre gövdeyi BYTE-SAYISI bazında tam olarak toplarız;
    // "OK" tabanlı erken durma JSON'un kesilmesine yol açmaz.
    clearBuffer();
    _serial->println("AT+HTTPREAD=0," + String(maxLen));

    unsigned long start = millis();
    String resp;

    // 1) "+HTTPREAD: <len>" başlığını bekle
    while (millis() - start < HTTP_TIMEOUT) {
        while (_serial->available()) resp += (char)_serial->read();

        int h = resp.indexOf("+HTTPREAD:");
        if (h >= 0) {
            int lf = resp.indexOf('\n', h);
            if (lf >= 0) {
                String header = resp.substring(h, lf);
                int colon = header.indexOf(':');
                int len = (colon >= 0) ? header.substring(colon + 1).toInt() : maxLen;
                int bodyStart = lf + 1;

                // 2) gövdeyi tam <len> byte topla
                while (millis() - start < HTTP_TIMEOUT) {
                    while (_serial->available()) resp += (char)_serial->read();
                    if (resp.length() - bodyStart >= len) {
                        String body = resp.substring(bodyStart, bodyStart + len);
                        // JSON gövdesini { ... } arasından çıkar
                        int s = body.indexOf('{');
                        int e = body.lastIndexOf('}');
                        if (s >= 0 && e > s) body = body.substring(s, e + 1);
                        clearBuffer();  // kalan OK'u temizle
                        return body;
                    }
                    yield();
                }
                break;
            }
        }
        yield();
    }

    // Yedek: '{' ile '}' arasını al
    int s = resp.indexOf('{');
    int e = resp.lastIndexOf('}');
    if (s >= 0 && e > s) return resp.substring(s, e + 1);
    return resp;
}

// ==================== YÜKSEK SEVİYE YARDIMCILAR ====================

bool HttpTest::getToken(String &token, String &clientId) {
    if (!httpInit()) return false;
    if (!httpSetUrl("http://test.hibersoft.com.tr:2884/token")) { httpTerm(); return false; }
    if (!httpSetContentType("application/json"))                  { httpTerm(); return false; }

    // POST /token — boş JSON gövde
    if (!httpData("{}")) { httpTerm(); return false; }

    int code = httpAction(1);
    if (_debugEnabled) { Serial.print("  HTTP "); Serial.println(code); }

    if (code == 200) {
        String body = httpRead(512);
        Serial.print("  [RAW] ");
        Serial.println(body);

        int s = body.indexOf("\"token\":\"");
        if (s >= 0) {
            s += 9;
            int e = body.indexOf('"', s);
            if (e > s) token = body.substring(s, e);
        }
        s = body.indexOf("\"clientId\":\"");
        if (s >= 0) {
            s += 12;
            int e = body.indexOf('"', s);
            if (e > s) clientId = body.substring(s, e);
        }
    }
    httpTerm();
    return token.length() > 0;
}

bool HttpTest::postIngest(const String &token, const String &deviceId,
                          const String &jsonData, String &resp, int &code) {
    if (!httpInit()) return false;
    if (!httpSetUrl("http://test.hibersoft.com.tr:2884/ingest")) { httpTerm(); return false; }
    if (!httpSetContentType("application/json"))                  { httpTerm(); return false; }
    if (!httpSetApiKey(token))                                    { httpTerm(); return false; }

    if (!httpData(jsonData)) { httpTerm(); return false; }

    code = httpAction(1);
    resp = httpRead(512);
    httpTerm();
    return (code >= 200 && code < 300);
}

// ==================== YARDIMCILAR ====================

void HttpTest::debugPrint(const String &msg) {
    if (_debugEnabled) {
        Serial.print("[SIM7672E] ");
        Serial.println(msg);
    }
}

void HttpTest::setDebug(bool enabled) {
    _debugEnabled = enabled;
}
