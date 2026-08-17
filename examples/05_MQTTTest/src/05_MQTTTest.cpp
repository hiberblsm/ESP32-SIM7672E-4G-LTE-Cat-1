/*
 * 05_MQTTTest.cpp — ESP32-S3 + SIM7672E MQTT Testi
 *
 * Yalnızca MQTT testi için gerekli fonksiyonları içerir.
 * Token HTTP üzerinden alınır, veri MQTT (TCP) üzerinden publish edilir.
 * ESP32-S3 üzerinde UART2 (Serial2) kullanılır.
 */

#include "05_MQTTTest.h"

// ==================== KURUCU & BAŞLATMA ====================

MqttTest::MqttTest(uint8_t txPin, uint8_t rxPin, uint8_t pwrKeyPin, uint8_t resetPin)
    : _txPin(txPin), _rxPin(rxPin), _pwrKeyPin(pwrKeyPin), _resetPin(resetPin),
      _debugEnabled(true), _netOpen(false),
      _mqttConnected(false), _mqttBroker(""), _mqttPort(0)
{
    _serial = &Serial2;
}

bool MqttTest::begin(long baud) {
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

void MqttTest::powerOn() {
    if (_pwrKeyPin == 255) { debugPrint("powerOn: PWRKEY pin yok, atlaniyor."); return; }
    digitalWrite(_pwrKeyPin, LOW);
    delay(1500);
    digitalWrite(_pwrKeyPin, HIGH);
    delay(5000);
}

void MqttTest::powerOff() {
    sendAT("AT+CPOF", 3000);
    if (_pwrKeyPin != 255) {
        delay(1000);
        digitalWrite(_pwrKeyPin, LOW);
        delay(2500);
        digitalWrite(_pwrKeyPin, HIGH);
    }
}

void MqttTest::hardReset() {
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

String MqttTest::sendAT(const String &cmd, uint32_t timeoutMs) {
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
            // AT+CIPSEND veri girişi istemi: satır sonu "> "
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

bool MqttTest::sendATExpect(const String &cmd, const String &expected,
                            uint32_t timeoutMs) {
    return sendAT(cmd, timeoutMs).indexOf(expected) >= 0;
}

bool MqttTest::waitForResponse(const String &expected, uint32_t timeoutMs) {
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

void MqttTest::clearBuffer() {
    unsigned long idle = millis();
    while (millis() - idle < 180) {
        if (_serial->available()) {
            _serial->read();
            idle = millis();
        }
        yield();
    }
}

void MqttTest::_exitDataModeAndDrain() {
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

int MqttTest::getSignalQuality() {
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

bool MqttTest::isRegistered() {
    String resp = sendAT("AT+CEREG?");
    if (resp.indexOf(",1") >= 0 || resp.indexOf(",5") >= 0) return true;
    resp = sendAT("AT+CREG?");
    return (resp.indexOf(",1") >= 0 || resp.indexOf(",5") >= 0);
}

// ==================== LTE VERİ BAĞLANTISI ====================

bool MqttTest::initGPRS(const String &apn,
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

bool MqttTest::closeGPRS() {
    sendAT("AT+CIPCLOSE=" + String(CIP_IDX), 3000);
    delay(300);
    sendAT("AT+NETCLOSE", 5000);
    _netOpen = false;
    sendAT("AT+CGACT=0,1", 5000);
    debugPrint("LTE baglanti kapatildi");
    return true;
}

String MqttTest::getLocalIP() {
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

bool MqttTest::httpInit() {
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

void MqttTest::httpTerm() {
    sendAT("AT+HTTPTERM", 2000);
}

bool MqttTest::httpSetUrl(const String &url) {
    String cmd = "AT+HTTPPARA=\"URL\",\"" + url + "\"";
    return sendATExpect(cmd, "OK");
}

bool MqttTest::httpSetContentType(const String &ct) {
    String cmd = "AT+HTTPPARA=\"CONTENT\",\"" + ct + "\"";
    return sendATExpect(cmd, "OK");
}

bool MqttTest::httpData(const String &body) {
    String cmd = "AT+HTTPDATA=" + String(body.length()) + ",30000";
    String resp = sendAT(cmd, 5000);
    if (resp.indexOf("DOWNLOAD") < 0) return false;
    _serial->print(body);
    return waitForResponse("OK", 5000);
}

int MqttTest::httpAction(int method) {
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

String MqttTest::httpRead(int maxLen) {
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

bool MqttTest::getToken(String &token, String &clientId) {
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

bool MqttTest::_openNet() {
    if (_netOpen) return true;
    String resp = sendAT("AT+NETOPEN", 10000);
    _netOpen = (resp.indexOf("+NETOPEN: 0")  >= 0 ||
                resp.indexOf("+NETOPEN: 23") >= 0 ||
                resp.indexOf("Network is opened") >= 0 ||
                resp.indexOf("OK") >= 0);
    return _netOpen;
}

bool MqttTest::_cipConnect(const char *type,
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

// Ham tamponda ASCII imza arama (binary null-byte'a takılmaz).
static bool _contains(const char *hay, size_t hayLen, const char *needle) {
    size_t n = strlen(needle);
    if (n == 0 || hayLen < n) return false;
    for (size_t i = 0; i + n <= hayLen; i++) {
        if (memcmp(hay + i, needle, n) == 0) return true;
    }
    return false;
}

bool MqttTest::_cipSend(const uint8_t *buf, size_t len) {
    String cmd = "AT+CIPSEND=" + String(CIP_IDX) + "," + String(len);
    String resp = sendAT(cmd, 5000);
    if (resp.indexOf('>') < 0) return false;

    // Modemin "veri alma moduna" geçmesi için bekleme.
    // Eski kütüphane sendAT "OK"/"ERROR" beklerken bu süreyi dolaylı sağlıyordu;
    // `>` anında yakalanınca veri çok erken gidiyor ve SEND OK dönmüyordu.
    delay(300);

    for (size_t i = 0; i < len; i++) _serial->write(buf[i]);
    _serial->flush();      // TX tamponunun tamamen gönderildiğinden emin ol
    delay(100);

    // Ham tamponla oku: echo + CONNACK 0x00 içerir; String indexOf yerine
    // bayt bayt memcmp ile ASCII imzası ararız (null byte'a takılmaz).
    char ack[512];
    size_t ackLen = 0;
    unsigned long t = millis();
    while (millis() - t < 10000) {
        while (_serial->available() && ackLen < sizeof(ack) - 1) {
            ack[ackLen++] = (char)_serial->read();
        }
        ack[ackLen] = '\0';
        if (_contains(ack, ackLen, "+CIPSEND:")) {
            _lastCipResp = String(ack, ackLen);   // CONNACK burada kalabilir
            return true;
        }
        if (_contains(ack, ackLen, "SEND FAIL") ||
            _contains(ack, ackLen, "+IPCLOSE")) {
            _lastCipResp = String(ack, ackLen);
            return false;
        }
        yield();
    }
    _lastCipResp = String(ack, ackLen);
    if (_debugEnabled) {
        debugPrint("CIPSEND ack (" + String(ackLen) + " byte) alindi ama taninamadi");
        Serial.print("[HEX] ");
        for (size_t i = 0; i < ackLen; i++) {
            uint8_t c = (uint8_t)ack[i];
            if (c < 0x10) Serial.print('0');
            Serial.print(c, HEX);
            Serial.print(' ');
        }
        Serial.println();
    }
    return false;
}

String MqttTest::_cipReceive(uint32_t timeoutMs) {
    // _cipSend'in "SEND OK" ile aynı anda okuyup bıraktığı tamponu önce kullan;
    // CONNACK burada kalmış olabilir, kaybetme.
    String combined = _lastCipResp;
    _lastCipResp = "";

    char   rawBuf[1024];
    size_t rawLen = 0;
    unsigned long start = millis();
    while (millis() - start < timeoutMs) {
        while (_serial->available() && rawLen < sizeof(rawBuf) - 1) {
            rawBuf[rawLen++] = (char)_serial->read();
        }
        if (rawLen > 0 && !_serial->available()) {
            delay(50);
            if (!_serial->available()) break;
        }
        yield();
    }
    combined += String(rawBuf, rawLen);

    // SIM7672E TCP veri formatı: "+IPD<uzunluk>\r\n<data>"
    // (SIM7600'ün "+IPD<conn>,<len>:<data>" formatından FARKLIDIR; hex dump'ta
    //  "+IPD4\r\n 20 02 00 00" olarak gözlemlendi.)
    int ipdIdx = combined.indexOf("+IPD");
    if (ipdIdx >= 0) {
        int p = ipdIdx + 4;
        int dataLen = 0;
        while (p < combined.length() && combined[p] >= '0' && combined[p] <= '9') {
            dataLen = dataLen * 10 + (combined[p] - '0');
            p++;
        }
        // "\r\n" (ve olası boşluk) atla
        while (p < combined.length() &&
               (combined[p] == '\r' || combined[p] == '\n' || combined[p] == ' ')) {
            p++;
        }
        if (dataLen > 0 && combined.length() - p >= dataLen) {
            return combined.substring(p, p + dataLen);
        }
    }
    return combined;
}

bool MqttTest::tcpConnect(const String &host, uint16_t port) {
    debugPrint("TCP baglanti: " + host + ":" + String(port));
    if (_cipConnect("TCP", host, port)) {
        debugPrint("TCP baglanildi!");
        return true;
    }
    debugPrint("HATA: TCP baglanti basarisiz!");
    return false;
}

bool MqttTest::tcpSend(const String &data) {
    if (_cipSend((const uint8_t *)data.c_str(), data.length())) {
        return true;
    }
    debugPrint("HATA: TCP gonderim basarisiz!");
    return false;
}

String MqttTest::tcpReceive(uint32_t timeoutMs) {
    return _cipReceive(timeoutMs);
}

bool MqttTest::tcpClose() {
    bool ok = sendATExpect("AT+CIPCLOSE=" + String(CIP_IDX),
                            "+CIPCLOSE:", 5000);
    debugPrint("TCP kapatildi");
    return ok;
}

bool MqttTest::isTCPConnected() {
    String resp = sendAT("AT+CIPSTATE=" + String(CIP_IDX));
    return resp.indexOf("+CIPSTATE:") >= 0;
}

// ==================== MQTT FONKSİYONLAR ====================

// MQTT Remaining Length: değişken uzunluklu (1-4 byte) kodlama.
// len <= 127 → 1 byte, len > 127 → devam biti (0x80) ile çok byte.
// Dönen değer yazılan byte sayısıdır.
static size_t _encodeRemLen(uint32_t len, uint8_t *out) {
    size_t n = 0;
    do {
        uint8_t digit = len % 128;
        len /= 128;
        if (len > 0) digit |= 0x80;
        out[n++] = digit;
    } while (len > 0 && n < 4);
    return n;
}

bool MqttTest::mqttConnect(const String &broker, uint16_t port,
                            const String &clientId,
                            const String &user, const String &pass) {
    debugPrint("MQTT baglanti: " + broker + ":" + String(port));
    _mqttBroker = broker;
    _mqttPort   = port;

    if (!tcpConnect(broker, port)) return false;
    delay(1000);

    // MQTT CONNECT paketi
    uint8_t packet[128];
    size_t idx = 0;

    packet[idx++] = MQTT_CONNECT;
    size_t lenIdx = idx++;      // remaining length placeholder

    // Protocol Name "MQTT"
    packet[idx++] = 0x00; packet[idx++] = 0x04;
    packet[idx++] = 'M';  packet[idx++] = 'Q';
    packet[idx++] = 'T';  packet[idx++] = 'T';

    packet[idx++] = 0x04;   // Protocol Level MQTT 3.1.1

    uint8_t flags = 0x02;   // Clean Session
    if (user.length() > 0) flags |= 0x80;
    if (pass.length() > 0) flags |= 0x40;
    packet[idx++] = flags;

    packet[idx++] = 0x00; packet[idx++] = 0x3C;  // Keep-alive 60s

    // Client ID
    packet[idx++] = (clientId.length() >> 8) & 0xFF;
    packet[idx++] =  clientId.length()       & 0xFF;
    memcpy(&packet[idx], clientId.c_str(), clientId.length());
    idx += clientId.length();

    if (user.length() > 0) {
        packet[idx++] = (user.length() >> 8) & 0xFF;
        packet[idx++] =  user.length()       & 0xFF;
        memcpy(&packet[idx], user.c_str(), user.length());
        idx += user.length();
    }
    if (pass.length() > 0) {
        packet[idx++] = (pass.length() >> 8) & 0xFF;
        packet[idx++] =  pass.length()       & 0xFF;
        memcpy(&packet[idx], pass.c_str(), pass.length());
        idx += pass.length();
    }
    packet[lenIdx] = idx - 2;  // remaining length

    if (_cipSend(packet, idx)) {
        String connResp = _cipReceive(5000);
        if (_debugEnabled) {
            debugPrint("Gelen (" + String(connResp.length()) + " byte):");
            Serial.print("[HEX] ");
            for (size_t i = 0; i < connResp.length(); i++) {
                uint8_t c = (uint8_t)connResp[i];
                if (c < 0x10) Serial.print('0');
                Serial.print(c, HEX);
                Serial.print(' ');
            }
            Serial.println();
        }
        // CONNACK = 0x20 0x02 <len> <code>. Yanıtın başında "SEND OK",
        // "+IPD0,4:" gibi metinler ve binary null-byte olabilir; bu yüzden
        // String uzunluğu üzerinden bayt bayt ararız (strstr/ilk-byte kullanılmaz).
        int cIdx = -1;
        for (size_t i = 0; i + 1 < connResp.length(); i++) {
            if ((uint8_t)connResp[i] == MQTT_CONNACK &&
                (uint8_t)connResp[i + 1] == 0x02) {
                cIdx = (int)i;
                break;
            }
        }
        if (cIdx >= 0 && (int)connResp.length() >= cIdx + 4) {
            uint8_t code = (uint8_t)connResp[cIdx + 3];
            if (code == 0x00) {
                _mqttConnected = true;
                debugPrint("MQTT baglanildi (CONNACK OK)");
                return true;
            }
            debugPrint("MQTT CONNACK RED: return code=" + String(code));
        } else {
            debugPrint("MQTT CONNACK bulunamadi");
        }
    }
    debugPrint("HATA: MQTT baglanti basarisiz!");
    return false;
}

bool MqttTest::mqttPublish(const String &topic, const String &payload) {
    if (!_mqttConnected) return false;
    debugPrint("MQTT Publish: " + topic + " = " + payload);

    uint8_t packet[256];
    size_t idx = 0;

    packet[idx++] = MQTT_PUBLISH;
    uint32_t remainLen = 2 + topic.length() + payload.length();
    idx += _encodeRemLen(remainLen, &packet[idx]);

    packet[idx++] = (topic.length() >> 8) & 0xFF;
    packet[idx++] =  topic.length()       & 0xFF;
    memcpy(&packet[idx], topic.c_str(), topic.length());
    idx += topic.length();
    memcpy(&packet[idx], payload.c_str(), payload.length());
    idx += payload.length();

    if (_cipSend(packet, idx)) {
        debugPrint("MQTT publish OK");
        return true;
    }
    debugPrint("HATA: MQTT publish basarisiz!");
    return false;
}

bool MqttTest::mqttSubscribe(const String &topic) {
    if (!_mqttConnected) return false;
    debugPrint("MQTT Subscribe: " + topic);

    uint8_t packet[128];
    size_t idx = 0;

    packet[idx++] = MQTT_SUBSCRIBE;
    uint32_t remainLen = 2 + 2 + topic.length() + 1;
    idx += _encodeRemLen(remainLen, &packet[idx]);

    packet[idx++] = 0x00; packet[idx++] = 0x01;  // Packet ID

    packet[idx++] = (topic.length() >> 8) & 0xFF;
    packet[idx++] =  topic.length()       & 0xFF;
    memcpy(&packet[idx], topic.c_str(), topic.length());
    idx += topic.length();
    packet[idx++] = 0x00;  // QoS 0

    if (_cipSend(packet, idx)) {
        debugPrint("MQTT subscribe OK");
        return true;
    }
    debugPrint("HATA: MQTT subscribe basarisiz!");
    return false;
}

String MqttTest::mqttLoop(uint32_t timeoutMs) {
    String raw = _cipReceive(timeoutMs);
    if (raw.length() < 4) return "";

    // Ham MQTT PUBLISH paketini ayrıştır: fixed header + değişken remaining
    // length + topic + (QoS>0 ise packet id) + payload.
    for (size_t i = 0; i < raw.length(); i++) {
        uint8_t b = (uint8_t)raw[i];
        if ((b & 0xF0) != MQTT_PUBLISH) continue;   // 0x30/0x31/0x32/0x33

        size_t pos = i + 1;

        // Remaining Length: değişken uzunluklu (1-4 byte) decode
        uint32_t remLen = 0;
        uint32_t mult   = 1;
        for (int r = 0; r < 4 && pos < raw.length(); r++) {
            uint8_t d = (uint8_t)raw[pos++];
            remLen   += (d & 0x7F) * mult;
            if (!(d & 0x80)) break;
            mult *= 128;
        }

        // Topic uzunluğu (2 byte)
        if (pos + 2 > raw.length()) break;
        uint16_t topicLen = ((uint8_t)raw[pos] << 8) | (uint8_t)raw[pos + 1];
        pos += 2;
        if (pos + topicLen > raw.length()) break;
        String topic = raw.substring(pos, pos + topicLen);
        pos += topicLen;

        // QoS > 0 ise Packet ID (2 byte) payload'dan önce gelir
        uint8_t qos = (b >> 1) & 0x03;
        if (qos > 0) pos += 2;

        // Payload uzunluğu = remLen - topic(2) - topicLen - (packetId varsa 2)
        uint32_t plen = remLen - 2 - topicLen - (qos > 0 ? 2 : 0);
        String payload = raw.substring(pos, pos + plen);
        return topic + ": " + payload;
    }
    return "";
}

bool MqttTest::mqttPing() {
    if (!_mqttConnected) return false;
    uint8_t packet[2] = {MQTT_PINGREQ, 0x00};
    return _cipSend(packet, 2);
}

bool MqttTest::mqttDisconnect() {
    if (!_mqttConnected) return true;
    uint8_t packet[2] = {MQTT_DISCONNECT, 0x00};
    _cipSend(packet, 2);
    _mqttConnected = false;
    tcpClose();
    debugPrint("MQTT baglanti kapatildi");
    return true;
}

// ==================== YARDIMCILAR ====================

void MqttTest::debugPrint(const String &msg) {
    if (_debugEnabled) {
        Serial.print("[SIM7672E] ");
        Serial.println(msg);
    }
}

void MqttTest::setDebug(bool enabled) {
    _debugEnabled = enabled;
}
