/*
 * 10_LbsLocation.cpp — ESP32-S3 + SIM7672E Hücresel Konum (LBS)
 *
 * Yalnızca hücresel konum testi için gerekli fonksiyonları içerir.
 * ESP32-S3 üzerinde UART2 (Serial2) kullanılır.
 */

#include "10_LbsLocation.h"
#include <math.h>

// ==================== KURUCU & BAŞLATMA ====================

LbsLocation::LbsLocation(uint8_t txPin, uint8_t rxPin,
                         uint8_t pwrKeyPin, uint8_t resetPin)
    : _txPin(txPin), _rxPin(rxPin), _pwrKeyPin(pwrKeyPin), _resetPin(resetPin),
      _debugEnabled(true), _provider(LBS_UNWIRED),
      _unwiredUrl("https://us1.unwiredlabs.com/v2/process.php")
{
    _serial = &Serial2;
}

bool LbsLocation::begin(long baud) {
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

void LbsLocation::powerOn() {
    if (_pwrKeyPin == 255) { debugPrint("powerOn: PWRKEY pin yok, atlaniyor."); return; }
    _pwrKeySet(true);
    delay(PWRKEY_ON_MS);
    _pwrKeySet(false);
    delay(5000);
}

void LbsLocation::powerOff() {
    sendAT("AT+CPOF", 3000);
    if (_pwrKeyPin != 255) {
        delay(1000);
        _pwrKeySet(true);
        delay(PWRKEY_OFF_MS);
        _pwrKeySet(false);
    }
}

void LbsLocation::hardReset() {
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
    sendAT("AT+CRESET", 3000);
    delay(8000);
    clearBuffer();
}

// ==================== TEMEL AT KOMUTLARI ====================

#define AT_BUF_SIZE 768

String LbsLocation::sendAT(const String &cmd, uint32_t timeoutMs) {
    clearBuffer();
    _serial->println(cmd);

    char   buf[AT_BUF_SIZE];
    size_t pos   = 0;
    bool   done  = false;
    unsigned long start = millis();
    buf[0] = '\0';

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

bool LbsLocation::sendATExpect(const String &cmd, const String &expected,
                               uint32_t timeoutMs) {
    return sendAT(cmd, timeoutMs).indexOf(expected) >= 0;
}

bool LbsLocation::waitForResponse(const String &expected, uint32_t timeoutMs) {
    char   buf[AT_BUF_SIZE];
    size_t pos = 0;
    buf[0] = '\0';
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

void LbsLocation::clearBuffer() {
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
void LbsLocation::_pwrKeySet(bool pressed) {
    if (_pwrKeyPin == 255) return;
#if PWRKEY_ACTIVE_HIGH
    digitalWrite(_pwrKeyPin, pressed ? HIGH : LOW);
#else
    digitalWrite(_pwrKeyPin, pressed ? LOW : HIGH);
#endif
}

// RESET pini de aktif-düşüktür; NPN varsa aynı şekilde terslenir.
void LbsLocation::_resetSet(bool asserted) {
    if (_resetPin == 255) return;
#if RESET_ACTIVE_HIGH
    digitalWrite(_resetPin, asserted ? HIGH : LOW);
#else
    digitalWrite(_resetPin, asserted ? LOW : HIGH);
#endif
}

void LbsLocation::powerKeyPulse(uint16_t ms) {
    if (_pwrKeyPin == 255) { debugPrint("powerKeyPulse: PWRKEY pin yok"); return; }
    pinMode(_pwrKeyPin, OUTPUT);
    debugPrint("PWRKEY darbesi: " + String(ms) + " ms");
    _pwrKeySet(true);
    delay(ms);
    _pwrKeySet(false);
}

void LbsLocation::_exitDataModeAndDrain() {
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

int LbsLocation::getSignalQuality() {
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

bool LbsLocation::isRegistered() {
    String resp = sendAT("AT+CEREG?");
    if (resp.indexOf(",1") >= 0 || resp.indexOf(",5") >= 0) return true;
    resp = sendAT("AT+CREG?");
    return (resp.indexOf(",1") >= 0 || resp.indexOf(",5") >= 0);
}

String LbsLocation::getSIMStatus() {
    String resp = sendAT("AT+CPIN?", 3000);
    if (resp.indexOf("READY") >= 0)        return "READY";
    if (resp.indexOf("SIM PIN2") >= 0)     return "SIM PIN2";
    if (resp.indexOf("SIM PUK2") >= 0)     return "SIM PUK2";
    if (resp.indexOf("SIM PIN") >= 0)      return "SIM PIN";
    if (resp.indexOf("SIM PUK") >= 0)      return "SIM PUK";
    if (resp.indexOf("PH-NET PIN") >= 0)   return "PH-NET PIN";
    if (resp.indexOf("NOT INSERTED") >= 0) return "NOT INSERTED";
    if (resp.indexOf("NOT READY") >= 0)    return "NOT READY";
    return "UNKNOWN";
}

String LbsLocation::getOperator() {
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

// ==================== LTE VERİ BAĞLANTISI ====================

bool LbsLocation::initGPRS(const String &apn,
                           const String &user, const String &pass) {
    debugPrint("LTE veri baglantisi baslatiliyor...");

    sendAT("AT+CFUN=1", 5000);
    sendAT("AT+CEREG=2", 3000);     // <lac>/<ci> alanlarını aç (hücre bilgisi için)
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

bool LbsLocation::closeGPRS() {
    sendAT("AT+NETCLOSE", 5000);
    sendAT("AT+CGACT=0,1", 5000);
    debugPrint("LTE baglanti kapatildi");
    return true;
}

String LbsLocation::getLocalIP() {
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
    return "";
}

// ==================== HÜCRE BİLGİSİ ====================
//
// AT+CPSI? servis hücresini tek satırda döndürür:
//   LTE  : +CPSI: LTE,Online,286-01,0x1A2B,187214780,257,EUTRAN-BAND3,1825,5,5,-94,-850,-616,17
//          alanlar:      0    1      2      3(TAC)   4(ECI)  5(PCI)   6(band) ... 10 11  12
//   GSM  : +CPSI: GSM,Online,286-01,0x182d,0x00e2,45,GSM 900,-64,0,0-0
//          alanlar:      0    1      2      3(LAC)  4(CID)  5      6(band) 7(RxLev)
//   Kayıt yoksa: +CPSI: NO SERVICE,Online

String LbsLocation::_field(const String &csv, int index) {
    int start = 0;
    for (int i = 0; i < index; i++) {
        start = csv.indexOf(',', start);
        if (start < 0) return "";
        start++;
    }
    int end = csv.indexOf(',', start);
    if (end < 0) end = csv.length();
    String f = csv.substring(start, end);
    f.trim();
    return f;
}

uint32_t LbsLocation::_parseNum(String s) {
    s.trim();
    s.replace("\"", "");
    if (s.length() == 0) return 0;
    if (s.startsWith("0x") || s.startsWith("0X"))
        return (uint32_t)strtoul(s.c_str() + 2, nullptr, 16);
    return (uint32_t)strtoul(s.c_str(), nullptr, 10);
}

// CPSI'de sinyal değerleri bazı firmware'lerde onda bir dBm'dir (-850 = -85.0 dBm)
static int _scaleDbm(const String &raw) {
    int v = raw.toInt();
    if (v < -200 || v > 200) v /= 10;
    return v;
}

bool LbsLocation::_parseCpsi(const String &resp, CellInfo &info) {
    int idx = resp.indexOf("+CPSI:");
    if (idx < 0) return false;

    int eol = resp.indexOf('\r', idx);
    if (eol < 0) eol = resp.length();
    String line = resp.substring(idx + 6, eol);
    line.trim();

    info.rat = _field(line, 0);
    if (info.rat.length() == 0 || info.rat.startsWith("NO SERVICE")) {
        info.rat = "NO SERVICE";
        return false;
    }

    // MCC-MNC (örn. "286-01")
    String plmn = _field(line, 2);
    int dash = plmn.indexOf('-');
    if (dash > 0) {
        info.mcc = plmn.substring(0, dash).toInt();
        info.mnc = plmn.substring(dash + 1).toInt();
    }

    info.lac = _parseNum(_field(line, 3));
    info.cid = _parseNum(_field(line, 4));

    if (info.rat == "LTE") {
        info.pcid = _field(line, 5).toInt();
        info.band = _field(line, 6);
        info.rsrq = _scaleDbm(_field(line, 10));
        info.rsrp = _scaleDbm(_field(line, 11));
        info.rssi = _scaleDbm(_field(line, 12));
    } else if (info.rat == "GSM") {
        info.pcid = -1;
        info.band = _field(line, 6);
        info.rssi = _scaleDbm(_field(line, 7));
        info.rsrp = info.rssi;
        info.rsrq = 0;
    } else {
        // WCDMA ve diğerleri: band alanı 5. sırada
        info.pcid = -1;
        info.band = _field(line, 5);
        info.rssi = _scaleDbm(_field(line, 11));
        info.rsrp = _scaleDbm(_field(line, 10));
        info.rsrq = 0;
    }

    return (info.cid != 0);
}

bool LbsLocation::_fillFromCereg(CellInfo &info) {
    // +CEREG: 2,1,"1A2B","0B2A5C1",7   → TAC ve ECI (her ikisi de HEX)
    sendAT("AT+CEREG=2", 3000);
    String resp = sendAT("AT+CEREG?", 3000);
    int idx = resp.indexOf("+CEREG:");
    if (idx < 0) return false;

    int q1 = resp.indexOf('"', idx);
    if (q1 < 0) return false;
    int q2 = resp.indexOf('"', q1 + 1);
    int q3 = resp.indexOf('"', q2 + 1);
    int q4 = resp.indexOf('"', q3 + 1);
    if (q2 < 0 || q3 < 0 || q4 < 0) return false;

    uint32_t lac = _parseNum("0x" + resp.substring(q1 + 1, q2));
    uint32_t cid = _parseNum("0x" + resp.substring(q3 + 1, q4));
    if (cid == 0) return false;

    info.lac = lac;
    info.cid = cid;
    if (info.rat.length() == 0 || info.rat == "NO SERVICE") info.rat = "LTE";
    return true;
}

bool LbsLocation::_fillFromCops(CellInfo &info) {
    // Sayısal formatta operatör: +COPS: 0,2,"28601",7  → MCC=286, MNC=01
    sendAT("AT+COPS=3,2", 3000);
    String resp = sendAT("AT+COPS?", 5000);
    sendAT("AT+COPS=3,0", 3000);   // biçimi tekrar alfanümeriğe al

    int q1 = resp.indexOf('"');
    int q2 = (q1 >= 0) ? resp.indexOf('"', q1 + 1) : -1;
    if (q1 < 0 || q2 < 0) return false;

    String numeric = resp.substring(q1 + 1, q2);
    numeric.trim();
    if (numeric.length() < 5) return false;

    info.mcc = numeric.substring(0, 3).toInt();
    info.mnc = numeric.substring(3).toInt();
    return (info.mcc > 0);
}

bool LbsLocation::getCellInfo(CellInfo &info) {
    info.valid = false;
    info.rat   = "";
    info.mcc   = 0;
    info.mnc   = 0;
    info.lac   = 0;
    info.cid   = 0;
    info.pcid  = -1;
    info.band  = "";
    info.rsrp  = 0;
    info.rsrq  = 0;
    info.rssi  = 0;

    String resp = sendAT("AT+CPSI?", 5000);
    _parseCpsi(resp, info);

    if (info.cid == 0)              _fillFromCereg(info);
    if (info.mcc == 0)              _fillFromCops(info);
    if (info.rssi == 0) {
        int csq = getSignalQuality();
        if (csq >= 0 && csq <= 31) info.rssi = -113 + (csq * 2);
    }

    info.valid = (info.mcc > 0 && info.cid > 0);
    if (!info.valid) debugPrint("Hucre bilgisi alinamadi (sebeke kaydi yok?)");
    return info.valid;
}

String LbsLocation::getRawCellReport() {
    String r;
    r += sendAT("AT+CPSI?", 5000);
    r += sendAT("AT+CEREG?", 3000);
    r += sendAT("AT+COPS?", 3000);
    r += sendAT("AT+CSQ", 3000);
    return r;
}

// ==================== HTTP İSTEMCİSİ ====================

bool LbsLocation::httpInit() {
    // SIM7672E: NETOPEN ve HTTPINIT aynı anda çalışamaz
    sendAT("AT+NETCLOSE", 8000);
    delay(1000);
    sendAT("AT+CGACT=1,1", 10000);
    delay(500);
    sendAT("AT+HTTPTERM", 3000);
    delay(500);
    if (!sendATExpect("AT+HTTPINIT", "OK", 5000)) return false;
    _sslRelax();
    return true;
}

void LbsLocation::httpTerm() {
    sendAT("AT+HTTPTERM", 2000);
}

void LbsLocation::_sslRelax() {
    // Hücre veritabanı API'leri HTTPS kullanır. Cihazda CA sertifikası
    // tutmamak için doğrulama kapatılır (authmode=0). Üretimde kendi
    // CA'nızı yükleyip authmode=1 yapmanız önerilir.
    sendAT("AT+CSSLCFG=\"sslversion\",0,4", 3000);
    sendAT("AT+CSSLCFG=\"authmode\",0,0", 3000);
    sendAT("AT+HTTPPARA=\"SSLCFG\",0", 3000);
}

bool LbsLocation::httpSetUrl(const String &url) {
    return sendATExpect("AT+HTTPPARA=\"URL\",\"" + url + "\"", "OK");
}

bool LbsLocation::httpSetContentType(const String &ct) {
    return sendATExpect("AT+HTTPPARA=\"CONTENT\",\"" + ct + "\"", "OK");
}

bool LbsLocation::httpData(const String &body) {
    String cmd = "AT+HTTPDATA=" + String(body.length()) + ",30000";
    String resp = sendAT(cmd, 5000);
    if (resp.indexOf("DOWNLOAD") < 0) return false;
    _serial->print(body);
    return waitForResponse("OK", 5000);
}

int LbsLocation::httpAction(int method) {
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

String LbsLocation::httpRead(int maxLen) {
    // AT+HTTPREAD yanıtı: +HTTPREAD: <len>\r\n<gövde>\r\nOK\r\n
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
                    if ((int)resp.length() - bodyStart >= len) {
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

// ==================== JSON YARDIMCILARI ====================

double LbsLocation::_jsonNumber(const String &json, const String &key, bool *ok) {
    if (ok) *ok = false;
    int k = json.indexOf("\"" + key + "\"");
    if (k < 0) return 0.0;
    int c = json.indexOf(':', k);
    if (c < 0) return 0.0;
    int i = c + 1;
    while (i < (int)json.length() && (json[i] == ' ' || json[i] == '"')) i++;
    int j = i;
    while (j < (int)json.length() &&
           (isdigit((int)json[j]) || json[j] == '-' || json[j] == '+' ||
            json[j] == '.' || json[j] == 'e' || json[j] == 'E')) j++;
    if (j == i) return 0.0;
    if (ok) *ok = true;
    return atof(json.substring(i, j).c_str());
}

String LbsLocation::_jsonString(const String &json, const String &key) {
    int k = json.indexOf("\"" + key + "\"");
    if (k < 0) return "";
    int c = json.indexOf(':', k);
    if (c < 0) return "";
    int q1 = json.indexOf('"', c);
    if (q1 < 0) return "";
    int q2 = json.indexOf('"', q1 + 1);
    if (q2 < 0) return "";
    return json.substring(q1 + 1, q2);
}

// ==================== HÜCRESEL KONUM (LBS) ====================

void LbsLocation::setApiKey(const String &key, LbsProvider provider) {
    _apiKey   = key;
    _provider = provider;
}

void LbsLocation::setUnwiredEndpoint(const String &url) {
    _unwiredUrl = url;
}

String LbsLocation::mapsUrl(const GeoFix &fix) {
    if (!fix.valid) return "";
    return "https://maps.google.com/?q=" + String(fix.lat, 6) + "," + String(fix.lon, 6);
}

// --- 1) Modem içi LBS: AT+CLBS ---
// SIMCom modemleri kendi konum sunucularına bağlanıp koordinat döndürebilir.
// Yanıt: +CLBS: <locationcode>,<boylam>,<enlem>,<dogruluk>[,<tarih>,<saat>]
// locationcode 0 → başarılı. Bu komut TÜM firmware sürümlerinde bulunmaz;
// desteklenmiyorsa ERROR döner ve online API'ye düşülür.
bool LbsLocation::lbsQueryModem(GeoFix &fix) {
    fix.valid    = false;
    fix.lat      = 0;
    fix.lon      = 0;
    fix.accuracy = 0;
    fix.source   = "CLBS";
    fix.error    = "";

    debugPrint("Modem LBS sorgusu (AT+CLBS)...");
    clearBuffer();
    _serial->println("AT+CLBS=1,1");

    String buf;
    unsigned long start = millis();
    while (millis() - start < LBS_TIMEOUT) {
        while (_serial->available()) buf += (char)_serial->read();
        if (buf.indexOf("+CLBS:") >= 0) {
            delay(300);
            while (_serial->available()) buf += (char)_serial->read();
            break;
        }
        if (buf.indexOf("ERROR") >= 0) {
            fix.error = "AT+CLBS desteklenmiyor / hata";
            debugPrint(fix.error);
            return false;
        }
        yield();
    }

    int idx = buf.indexOf("+CLBS:");
    if (idx < 0) {
        fix.error = "AT+CLBS zaman asimi";
        debugPrint(fix.error);
        return false;
    }

    int eol = buf.indexOf('\r', idx);
    if (eol < 0) eol = buf.length();
    String line = buf.substring(idx + 6, eol);
    line.trim();

    int code = _field(line, 0).toInt();
    if (code != 0) {
        fix.error = "CLBS hata kodu: " + String(code);
        debugPrint(fix.error);
        return false;
    }

    // SIMCom belgelerinde sıra: boylam, enlem
    double a = atof(_field(line, 1).c_str());   // boylam
    double b = atof(_field(line, 2).c_str());   // enlem
    fix.accuracy = _field(line, 3).toInt();

    // Bazı firmware'ler ters sırada döndürür — enlem |90| sınırını aşıyorsa takas et
    if (fabs(b) > 90.0 && fabs(a) <= 90.0) { double t = a; a = b; b = t; }

    fix.lon = a;
    fix.lat = b;

    if (fix.lat == 0.0 && fix.lon == 0.0) {
        fix.error = "CLBS gecersiz koordinat (0,0)";
        return false;
    }
    if (fix.accuracy <= 0) fix.accuracy = 1000;

    fix.valid = true;
    return true;
}

// --- 2a) Unwired Labs (OpenCelliD ile aynı anahtar kullanılabilir) ---
bool LbsLocation::_queryUnwired(const CellInfo &info, GeoFix &fix) {
    String radio = "lte";
    if (info.rat == "GSM")        radio = "gsm";
    else if (info.rat == "WCDMA") radio = "umts";

    String body = "{\"token\":\"" + _apiKey + "\",";
    body += "\"radio\":\"" + radio + "\",";
    body += "\"mcc\":" + String(info.mcc) + ",";
    body += "\"mnc\":" + String(info.mnc) + ",";
    body += "\"cells\":[{\"lac\":" + String(info.lac) +
            ",\"cid\":" + String(info.cid) + "}],";
    body += "\"address\":0}";

    if (!httpInit())                              { fix.error = "HTTPINIT hatasi";  return false; }
    if (!httpSetUrl(_unwiredUrl))                 { fix.error = "URL ayarlanamadi"; httpTerm(); return false; }
    httpSetContentType("application/json");
    if (!httpData(body))                          { fix.error = "HTTPDATA hatasi";  httpTerm(); return false; }

    int code = httpAction(1);   // POST
    if (code != 200) {
        fix.error = "HTTP kodu: " + String(code);
        httpTerm();
        return false;
    }

    String resp = httpRead(512);
    httpTerm();

    if (_jsonString(resp, "status") != "ok") {
        String msg = _jsonString(resp, "message");
        fix.error = "API: " + (msg.length() ? msg : String("bilinmeyen hata"));
        return false;
    }

    bool okLat = false, okLon = false;
    fix.lat = _jsonNumber(resp, "lat", &okLat);
    fix.lon = _jsonNumber(resp, "lon", &okLon);
    if (!okLat || !okLon) { fix.error = "Yanit ayristirilamadi"; return false; }

    fix.accuracy = (int)_jsonNumber(resp, "accuracy");
    if (fix.accuracy <= 0) fix.accuracy = 1000;
    fix.source = "UNWIRED";
    fix.valid  = true;
    return true;
}

// --- 2b) OpenCelliD (GET) ---
bool LbsLocation::_queryOpenCellId(const CellInfo &info, GeoFix &fix) {
    String radio = "LTE";
    if (info.rat == "GSM")        radio = "GSM";
    else if (info.rat == "WCDMA") radio = "UMTS";

    String url = "https://opencellid.org/cell/get?key=" + _apiKey;
    url += "&mcc="    + String(info.mcc);
    url += "&mnc="    + String(info.mnc);
    url += "&lac="    + String(info.lac);
    url += "&cellid=" + String(info.cid);
    url += "&radio="  + radio;
    url += "&format=json";

    if (!httpInit())        { fix.error = "HTTPINIT hatasi";  return false; }
    if (!httpSetUrl(url))   { fix.error = "URL ayarlanamadi"; httpTerm(); return false; }

    int code = httpAction(0);   // GET
    if (code != 200) {
        fix.error = "HTTP kodu: " + String(code);
        httpTerm();
        return false;
    }

    String resp = httpRead(512);
    httpTerm();

    String err = _jsonString(resp, "error");
    if (err.length() > 0) { fix.error = "API: " + err; return false; }

    bool okLat = false, okLon = false;
    fix.lat = _jsonNumber(resp, "lat", &okLat);
    fix.lon = _jsonNumber(resp, "lon", &okLon);
    if (!okLat || !okLon) { fix.error = "Yanit ayristirilamadi"; return false; }

    fix.accuracy = (int)_jsonNumber(resp, "range");
    if (fix.accuracy <= 0) fix.accuracy = 1000;
    fix.source = "OPENCELLID";
    fix.valid  = true;
    return true;
}

bool LbsLocation::lbsQueryOnline(const CellInfo &info, GeoFix &fix) {
    fix.valid    = false;
    fix.lat      = 0;
    fix.lon      = 0;
    fix.accuracy = 0;
    fix.source   = "";
    fix.error    = "";

    if (_apiKey.length() == 0) {
        fix.error = "API anahtari tanimli degil (setApiKey)";
        debugPrint(fix.error);
        return false;
    }
    if (!info.valid) {
        fix.error = "Gecerli hucre bilgisi yok";
        debugPrint(fix.error);
        return false;
    }

    debugPrint("Online hucre veritabani sorgulaniyor...");
    bool ok = (_provider == LBS_OPENCELLID) ? _queryOpenCellId(info, fix)
                                            : _queryUnwired(info, fix);
    if (!ok) debugPrint("Online sorgu basarisiz: " + fix.error);
    return ok;
}

bool LbsLocation::getLocation(GeoFix &fix, CellInfo *outInfo) {
    CellInfo info;
    getCellInfo(info);
    if (outInfo) *outInfo = info;

    // 1) Modem içi LBS
    if (lbsQueryModem(fix)) return true;
    String firstErr = fix.error;

    // 2) Online hücre veritabanı
    if (lbsQueryOnline(info, fix)) return true;

    if (fix.error.length() == 0) fix.error = firstErr;
    else if (firstErr.length())  fix.error = firstErr + " | " + fix.error;
    return false;
}

// ==================== YARDIMCILAR ====================

void LbsLocation::debugPrint(const String &msg) {
    if (_debugEnabled) {
        Serial.print("[SIM7672E] ");
        Serial.println(msg);
    }
}

void LbsLocation::setDebug(bool enabled) {
    _debugEnabled = enabled;
}
