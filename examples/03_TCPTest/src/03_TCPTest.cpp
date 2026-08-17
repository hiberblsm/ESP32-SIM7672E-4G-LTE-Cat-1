/*
 * 03_TCPTest.cpp — ESP32-S3 + SIM7672E TCP Soket Testi
 *
 * Yalnızca TCP testi için gerekli fonksiyonları içerir.
 * Token HTTP üzerinden alınır, veri TCP üzerinden gönderilir.
 * ESP32-S3 üzerinde UART2 (Serial2) kullanılır.
 */

#include "03_TCPTest.h"

// ==================== KURUCU & BAŞLATMA ====================

TcpTest::TcpTest(uint8_t txPin, uint8_t rxPin, uint8_t pwrKeyPin, uint8_t resetPin)
    : _txPin(txPin), _rxPin(rxPin), _pwrKeyPin(pwrKeyPin), _resetPin(resetPin),
      _debugEnabled(true), _netOpen(false)
{
    _serial = &Serial2;
}

bool TcpTest::begin(long baud) {
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

void TcpTest::powerOn() {
    if (_pwrKeyPin == 255) { debugPrint("powerOn: PWRKEY pin yok, atlaniyor."); return; }
    digitalWrite(_pwrKeyPin, LOW);
    delay(1500);
    digitalWrite(_pwrKeyPin, HIGH);
    delay(5000);
}

void TcpTest::powerOff() {
    sendAT("AT+CPOF", 3000);
    if (_pwrKeyPin != 255) {
        delay(1000);
        digitalWrite(_pwrKeyPin, LOW);
        delay(2500);
        digitalWrite(_pwrKeyPin, HIGH);
    }
}

void TcpTest::hardReset() {
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

String TcpTest::sendAT(const String &cmd, uint32_t timeoutMs) {
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

bool TcpTest::sendATExpect(const String &cmd, const String &expected,
                            uint32_t timeoutMs) {
    return sendAT(cmd, timeoutMs).indexOf(expected) >= 0;
}

bool TcpTest::waitForResponse(const String &expected, uint32_t timeoutMs) {
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

void TcpTest::clearBuffer() {
    unsigned long idle = millis();
    while (millis() - idle < 180) {
        if (_serial->available()) {
            _serial->read();
            idle = millis();
        }
        yield();
    }
}

void TcpTest::_exitDataModeAndDrain() {
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

int TcpTest::getSignalQuality() {
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

bool TcpTest::isRegistered() {
    String resp = sendAT("AT+CEREG?");
    if (resp.indexOf(",1") >= 0 || resp.indexOf(",5") >= 0) return true;
    resp = sendAT("AT+CREG?");
    return (resp.indexOf(",1") >= 0 || resp.indexOf(",5") >= 0);
}

// ==================== LTE VERİ BAĞLANTISI ====================

bool TcpTest::initGPRS(const String &apn,
                       const String &user, const String &pass) {
    debugPrint("LTE veri baglantisi baslatiliyor...");

    sendAT("AT+CFUN=1", 5000);
    sendAT("AT+CEREG=2", 3000);
    sendAT("AT+CGATT=1", 10000);

    sendAT("AT+NETCLOSE", 5000);
    delay(500);
    sendAT("AT+CGACT=0,1", 3000);
    _netOpen = false;
    delay(1000);

    String pdpCmd = "AT+CGDCONT=1,\"IP\",\"" + apn + "\"";
    sendATExpect(pdpCmd, "OK");

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

    // TCP yığını için NETOPEN
    String netResp = sendAT("AT+NETOPEN", 10000);
    _netOpen = (netResp.indexOf("+NETOPEN: 0")  >= 0 ||
                netResp.indexOf("+NETOPEN: 23") >= 0 ||
                netResp.indexOf("Network is opened") >= 0 ||
                netResp.indexOf("OK") >= 0);
    return true;
}

bool TcpTest::closeGPRS() {
    sendAT("AT+CIPCLOSE=" + String(CIP_IDX), 3000);
    delay(300);
    sendAT("AT+NETCLOSE", 5000);
    _netOpen = false;
    sendAT("AT+CGACT=0,1", 5000);
    debugPrint("LTE baglanti kapatildi");
    return true;
}

String TcpTest::getLocalIP() {
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

// ==================== HTTP İSTEMCİSİ (TOKEN) ====================

bool TcpTest::httpInit() {
    // SIM7672E: NETOPEN ve HTTPINIT aynı anda çalışamaz
    sendAT("AT+NETCLOSE", 8000);
    _netOpen = false;
    delay(1000);
    sendAT("AT+CGACT=1,1", 10000);
    delay(500);
    sendAT("AT+HTTPTERM", 3000);
    delay(500);
    return sendATExpect("AT+HTTPINIT", "OK", 5000);
}

void TcpTest::httpTerm() {
    sendAT("AT+HTTPTERM", 2000);
}

bool TcpTest::httpSetUrl(const String &url) {
    String cmd = "AT+HTTPPARA=\"URL\",\"" + url + "\"";
    return sendATExpect(cmd, "OK");
}

bool TcpTest::httpSetContentType(const String &ct) {
    String cmd = "AT+HTTPPARA=\"CONTENT\",\"" + ct + "\"";
    return sendATExpect(cmd, "OK");
}

bool TcpTest::httpData(const String &body) {
    String cmd = "AT+HTTPDATA=" + String(body.length()) + ",30000";
    String resp = sendAT(cmd, 5000);
    if (resp.indexOf("DOWNLOAD") < 0) return false;
    _serial->print(body);
    return waitForResponse("OK", 5000);
}

int TcpTest::httpAction(int method) {
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
    return 0;
}

String TcpTest::httpRead(int maxLen) {
    // AT+HTTPREAD yanıtı: +HTTPREAD: <len>\r\n<gövde>\r\nOK\r\n
    // <len> değerine göre gövdeyi BYTE-SAYISI bazında tam toplarız;
    // "OK" tabanlı erken durma JSON'un kesilmesine yol açmaz.
    clearBuffer();
    _serial->println("AT+HTTPREAD=0," + String(maxLen));

    unsigned long start = millis();
    String resp;

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

                while (millis() - start < HTTP_TIMEOUT) {
                    while (_serial->available()) resp += (char)_serial->read();
                    if (resp.length() - bodyStart >= len) {
                        String body = resp.substring(bodyStart, bodyStart + len);
                        int s = body.indexOf('{');
                        int e = body.lastIndexOf('}');
                        if (s >= 0 && e > s) body = body.substring(s, e + 1);
                        clearBuffer();
                        return body;
                    }
                    yield();
                }
                break;
            }
        }
        yield();
    }

    int s = resp.indexOf('{');
    int e = resp.lastIndexOf('}');
    if (s >= 0 && e > s) return resp.substring(s, e + 1);
    return resp;
}

bool TcpTest::getToken(String &token, String &clientId) {
    if (!httpInit()) return false;
    if (!httpSetUrl("http://test.hibersoft.com.tr:2884/token")) { httpTerm(); return false; }
    if (!httpSetContentType("application/json"))                  { httpTerm(); return false; }

    if (!httpData("{}")) { httpTerm(); return false; }

    int code = httpAction(1);
    if (_debugEnabled) { Serial.print("  HTTP "); Serial.println(code); }

    if (code == 200) {
        String body = httpRead(512);
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

    // HTTP bittikten sonra TCP yığınını yeniden aç
    sendAT("AT+NETOPEN", 10000);
    _netOpen = true;

    return token.length() > 0;
}

// ==================== TCP SOKET ====================

bool TcpTest::_openNet() {
    if (_netOpen) return true;
    String resp = sendAT("AT+NETOPEN", 10000);
    _netOpen = (resp.indexOf("+NETOPEN: 0")  >= 0 ||
                resp.indexOf("+NETOPEN: 23") >= 0 ||
                resp.indexOf("Network is opened") >= 0 ||
                resp.indexOf("OK") >= 0);
    return _netOpen;
}

bool TcpTest::_cipConnect(const char *type,
                          const String &host, uint16_t port, int localPort) {
    if (!_openNet()) return false;

    sendAT("AT+CIPCLOSE=" + String(CIP_IDX), 3000);
    delay(300);

    String cmd = "AT+CIPOPEN=" + String(CIP_IDX) + ",\"" + String(type) + "\",\""
                 + host + "\"," + String(port);
    if (localPort >= 0) cmd += "," + String(localPort);
    String resp = sendAT(cmd, GPRS_TIMEOUT);

    if (resp.indexOf("+CIPOPEN: " + String(CIP_IDX) + ",0") >= 0) return true;
    return waitForResponse("+CIPOPEN: " + String(CIP_IDX) + ",0", 15000);
}

bool TcpTest::_cipSend(const uint8_t *buf, size_t len) {
    String cmd = "AT+CIPSEND=" + String(CIP_IDX) + "," + String(len);
    String resp = sendAT(cmd, 5000);
    if (resp.indexOf('>') < 0) return false;

    for (size_t i = 0; i < len; i++) _serial->write(buf[i]);
    delay(100);

    char     ack[256];
    uint16_t ackLen = 0;
    unsigned long t = millis();
    while (millis() - t < 10000) {
        while (_serial->available() && ackLen < sizeof(ack) - 1) {
            ack[ackLen++] = (char)_serial->read();
        }
        ack[ackLen] = '\0';
        if (strstr(ack, "+CIPSEND:") || strstr(ack, "SEND OK")) return true;
        if (strstr(ack, "ERROR") || strstr(ack, "+IPCLOSE") ||
            strstr(ack, "CLOSED"))                           return false;
        yield();
    }
    return false;
}

String TcpTest::_cipReceive(uint32_t timeoutMs) {
    char   rawBuf[512];
    size_t rawLen = 0;
    unsigned long start = millis();
    while (millis() - start < timeoutMs) {
        while (_serial->available() && rawLen < sizeof(rawBuf) - 1) {
            rawBuf[rawLen++] = (char)_serial->read();
        }
        if (rawLen > 0 && !_serial->available()) {
            delay(100);
            if (!_serial->available()) break;
        }
        yield();
    }
    rawBuf[rawLen] = '\0';

    char marker[12];
    snprintf(marker, sizeof(marker), "+IPD%d,", CIP_IDX);
    char *ipdPtr = strstr(rawBuf, marker);
    if (ipdPtr) {
        char *colon = strchr(ipdPtr, ':');
        if (colon) return String(colon + 1);
    }
    return String(rawBuf);
}

bool TcpTest::tcpConnect(const String &host, uint16_t port) {
    debugPrint("TCP baglanti: " + host + ":" + String(port));
    if (_cipConnect("TCP", host, port)) {
        debugPrint("TCP baglanildi!");
        return true;
    }
    debugPrint("HATA: TCP baglanti basarisiz!");
    return false;
}

bool TcpTest::tcpSend(const String &data) {
    if (_cipSend((const uint8_t *)data.c_str(), data.length())) {
        return true;
    }
    debugPrint("HATA: TCP gonderim basarisiz!");
    return false;
}

String TcpTest::tcpReceive(uint32_t timeoutMs) {
    return _cipReceive(timeoutMs);
}

bool TcpTest::tcpClose() {
    bool ok = sendATExpect("AT+CIPCLOSE=" + String(CIP_IDX),
                            "+CIPCLOSE:", 5000);
    debugPrint("TCP kapatildi");
    return ok;
}

bool TcpTest::isTCPConnected() {
    String resp = sendAT("AT+CIPSTATE=" + String(CIP_IDX));
    return resp.indexOf("+CIPSTATE:") >= 0;
}

// ==================== YARDIMCILAR ====================

void TcpTest::debugPrint(const String &msg) {
    if (_debugEnabled) {
        Serial.print("[SIM7672E] ");
        Serial.println(msg);
    }
}

void TcpTest::setDebug(bool enabled) {
    _debugEnabled = enabled;
}
