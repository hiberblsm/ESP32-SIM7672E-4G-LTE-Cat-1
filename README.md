# ESP32-S3 + SIM7672E LTE Cat-1 Geliştirme Kiti

> **SIM7672E 4G LTE Cat-1** modülü için bağımsız, üretime hazır test ve uygulama projeleri.
> HTTP · TCP · UDP · MQTT · SMS · DTMF · Röle Kontrolü · Sıcaklık Ölçümü

[![License: MIT](https://img.shields.io/badge/License-MIT-green.svg)](LICENSE)
[![Platform: ESP32-S3](https://img.shields.io/badge/Platform-ESP32--S3-blue.svg)](https://www.espressif.com/)
[![Framework: Arduino](https://img.shields.io/badge/Framework-Arduino-orange.svg)](https://www.arduino.cc/)

**Geliştirici:** [Hiber Bilişim](https://www.hiber.com.tr) — [hibersoft.com.tr](https://www.hibersoft.com.tr)
**Lisans:** MIT © 2026

---

## 📖 İçindekiler

1. [Bu Repo Nedir?](#-bu-repo-nedir)
2. [Donanım](#-donanım)
3. [Başlarken](#-başlarken)
4. [Proje Mimarisi](#-proje-mimarisi)
5. [Modül Modül Kullanım Rehberi](#-modül-modül-kullanım-rehberi)
6. [Test Sunucusu ve Kimlik Doğrulama](#-test-sunucusu-ve-kimlik-doğrulama)
7. [SIM7672E Notları ve Farkları](#-sim7672e-notları-ve-farkları)
8. [Sahada Öğrenilenler (Tecrübeler)](#-sahada-öğrenilenler-tecrübeler)
9. [İyileştirmeler ve Üretim İpuçları](#-iyileştirmeler-ve-üretim-i̇puçları)
10. [Örnek Cihaz Kullanım Senaryoları](#-örnek-cihaz-kullanım-senaryoları)
11. [Sık Sorulan Sorular](#-sık-sorulan-sorular)
12. [Lisans](#-lisans)

---

## 🚀 Bu Repo Nedir?

Bu repo, **ESP32-S3 + SIM7672E LTE Cat-1** modülüyle hızlıca ürün geliştirebilmeniz için hazırlanmış **bağımsız, üretime hazır projeler** içerir. Her örnek kendi klasöründe, kendi kütüphanesiyle birlikte gelir.

> ⚠️ **Önemli Tasarım Kararı:** Her örnek **kendi bağımsız kütüphanesini** içerir. Ortak tek bir `SIM7672E` kütüphanesi yoktur. Böylece:
> - Bir örnekte yaptığınız değişiklik diğerini asla bozmaz.
> - Her proje yalnızca ihtiyaç duyduğu AT komutlarını içerir → daha küçük binary, daha az RAM.
> - Üretime aldığınız projeyi olduğu gibi kopyalayıp kendi ürününüze dönüştürebilirsiniz.

---

## 🔌 Donanım

### Pin Bağlantıları (Sabit)

Tüm projelerde modem pinleri **aynıdır** ve değişmez:

| ESP32-S3 GPIO | SIM7672E | Açıklama |
|:-------------:|:--------:|----------|
| **17** | RX | ESP32 TX → Modül RX |
| **16** | TX | ESP32 RX ← Modül TX |
| **4**  | PWRKEY | Güç açma/kapama |
| **5**  | RESET | Donanım sıfırlama (LOW aktif) |
| GND | GND | Ortak toprak |

Kodda kurucu çağrısı her zaman şu şekildedir:

```cpp
gsm(17, 16, 4, 5);   // TX, RX, PWRKEY, RESET
```

### ESP32-S3 Pin Kısıtları (ÇOK ÖNEMLİ)

ESP32-S3'te **GPIO 22–25 yoktur**, **GPIO 26–37** ise Octal PSRAM/Flash tarafından kullanılır. Bu yüzden röle ve sensörler için **yalnızca şu serbest pinleri** kullanın:

| Kullanım | Pin |
|----------|-----|
| Röle 1 (IN1) | **GPIO 6** |
| Röle 2 (IN2) | **GPIO 7** |
| DS18B20 Data | **GPIO 13** |
| Diğer serbest | 8, 9, 10, 11, 12, 14, 15 |

> ❗ Eski `25/26/27/32` pinleri ESP32-S3'te `Invalid pin selected` hatasına ve **watchdog reset'e** yol açar.

### Hedef Kart

Tüm projeler şu kart için yapılandırılmıştır:

```
board = esp32-s3-devkitc-1  (N16R8)
- 16 MB Flash
- 8 MB Octal PSRAM (OPI)
```

---

## 🏁 Başlarken

### Gereksinimler

- [PlatformIO](https://platformio.org/) (VS Code eklentisi önerilir) veya Arduino IDE
- ESP32-S3 geliştirme kartı
- SIM7672E modülü + aktif SIM kart (Nano SIM)

### Derleme ve Yükleme

Her proje **bağımsız** olduğu için ilgili klasöre girip çalıştırın:

```bash
# Örnek: MQTT testi
cd examples/05_MQTTTest
pio run -t upload
pio device monitor -p /dev/ttyACM0 -b 115200
```

> **Varsayılan port:** `/dev/ttyACM0` (CH343 USB-UART). Farklıysa `platformio.ini` içindeki `upload_port` ve `monitor_port` değerlerini değiştirin.

### Ortak `platformio.ini` Ayarları

Her projede aynı N16R8 konfigürasyonu kullanılır:

```ini
[env:esp32s3]
platform = espressif32
board = esp32-s3-devkitc-1
framework = arduino

board_build.flash_size = 16MB
board_build.psram_type = opi
board_build.arduino.memory_type = qio_opi
board_upload.flash_size = 16MB

build_src_filter = +<main.cpp> +<XX_Modul.cpp>

monitor_speed = 115200
upload_speed = 921600
upload_port = /dev/ttyACM0
monitor_port = /dev/ttyACM0
```

---

## 🏗️ Proje Mimarisi

Her modül aynı klasör düzenini izler:

```
examples/05_MQTTTest/
├── platformio.ini          ← Kart + N16R8 + derleme filtresi
└── src/
    ├── main.cpp            ← Uygulama mantığı (akış, menü, komutlar)
    ├── 05_MQTTTest.h       ← O modüle özel sınıf bildirimi
    └── 05_MQTTTest.cpp     ← O modüle özel AT komut implementasyonu
```

Sınıf adları modül bazlıdır: `SerialTest`, `HttpTest`, `TcpTest`, `UdpTest`, `MqttTest`, `SmsTest`, `SmsRelay`, `DtmfRelay`, `SmsTemperature`.

Her sınıfta ortak olarak bulunan **çekirdek fonksiyonlar**:

| Fonksiyon | Açıklama |
|-----------|----------|
| `begin(baud)` | UART2'yi başlatır, modemi uyandırır |
| `hardReset()` | RESET piniyle donanım sıfırlama (önerilen başlangıç) |
| `powerOn()` / `powerOff()` | PWRKEY ile açma/kapama |
| `sendAT(cmd, timeout)` | AT komutu gönder, yanıtı `String` döndür |
| `sendATExpect(cmd, beklenen)` | Yanıt bekler, `true/false` döndür |
| `waitForResponse(beklenen)` | Beklenen metni bekler |
| `clearBuffer()` | Seri tamponunu temizler |
| `getSignalQuality()` | Sinyal gücü (CSQ: 0–31, 99=ölçülemedi) |
| `isRegistered()` | LTE şebekeye kayıtlı mı |
| `getSIMStatus()` | SIM durumu (`"READY"` vb.) |
| `getOperator()` | Operatör adı |
| `setDebug(bool)` | AT trafiğini Serial'e yansıt |
| `debugPrint(msg)` | `[SIM7672E]` etiketiyle log |

---

## 📚 Modül Modül Kullanım Rehberi

### 01 — SerialTest (Modem Tanılama)

**Amaç:** Modemin temel sağlık durumunu doğrular — ilk kurulumda mutlaka çalıştırın.

**Ne yapar:**
- Modem uyanma testi (`begin`)
- IMEI / IMSI okuma
- SIM kart durumu
- Sinyal gücü (CSQ → RSSI dönüşümü)
- Operatör adı
- LTE şebeke kayıt durumu

**Ne zaman kullanılır:** Yeni bir kart/modül taktığınızda, bağlantı sorunlarını ayıklarken ilk adım.

```cpp
SerialTest gsm(17, 16, 4, 5);

gsm.begin();
Serial.println("IMEI: " + gsm.getIMEI());
Serial.println("IMSI: " + gsm.getIMSI());
Serial.println("SIM : " + gsm.getSIMStatus());
Serial.println("CSQ : " + String(gsm.getSignalQuality()));
```

---

### 02 — HTTPTest (Token + Veri Gönderimi)

**Amaç:** LTE veri bağlantısı kurup sunucuya HTTP GET/POST yapmak.

**Akış:**
1. Modem başlat + LTE kayıt
2. PDP context aç (`initGPRS("internet")`)
3. Menü: kayıtlı token / otomatik token (`POST /token`) / manuel token
4. 1 dakika boyunca 5 sn'de bir `POST /ingest` ile JSON veri gönderimi

**Öne çıkan fonksiyonlar:**

```cpp
gsm.initGPRS("internet");               // LTE veri bağlantısı
gsm.httpPOST("http://test.hibersoft.com.tr:2884/ingest",
             "application/json",
             jsonBody, cevap, httpKod);
```

> SIM7672E'de HTTP işlemi sırasında TCP/UDP soketi **otomatik kapanır** — kütüphane bunu kendisi yönetir.

---

### 03 — TCPTest (Raw Soket)

**Amaç:** Sunucuya kalıcı TCP soket açıp JSON veri akışı yapmak.

**Akış:**
1. Modem + LTE + PDP context
2. Token menüsü
3. `tcpConnect` ile `test.hibersoft.com.tr:2885`'e bağlan
4. Periyodik `tcpSend` + `tcpReceive`

```cpp
gsm.tcpConnect("test.hibersoft.com.tr", 2885);
gsm.tcpSend(json + "\n");           // satır sonu \n ile
String veri = gsm.tcpReceive(3000);
```

> SIM7672E TCP veri formatı `+IPD<uzunluk>\r\n<veri>` şeklindedir (SIM7600'den farklıdır).

---

### 04 — UDPTest (Paket Gönderimi)

**Amaç:** Bağlantısız UDP paketleri göndermek.

```cpp
gsm.udpConnect("test.hibersoft.com.tr", 2886);
gsm.udpSend(json);
String veri = gsm.udpReceive(3000);
```

> ⚠️ **Bilinen Durum:** UDP bağlantı açılışında SIM7672E **yerel port parametresi zorunludur** (`AT+CIPOPEN`'da `localPort=0`). Bu modülde bağlantı kurulumunda halen doğrulanması gereken bir nokta vardır — sahada test edilmektedir.

---

### 05 — MQTTTest (Yayın + Abonelik) ✅ Doğrulandı

**Amaç:** MQTT 3.1.1 ile broker'a bağlanıp veri yayınlamak ve komut almak.

**Özellikler:**
- Harici kütüphane **gerekmez** (PubSubClient yok) — raw TCP üzerinden MQTT implementasyonu
- Publish + Subscribe + gelen mesaj okuma
- Değişken uzunluklu Remaining Length kodlama desteği

```cpp
gsm.mqttConnect("test.hibersoft.com.tr", 2887, deviceId, "testuser", "PUBLIC_MQTT_2026_PASS");
gsm.mqttSubscribe("test/esp32/cmd");
gsm.mqttPublish("test/esp32/sensor", json);

String gelen = gsm.mqttLoop(100);
if (gelen.length() > 0) Serial.println("Gelen: " + gelen);
```

| Parametre | Değer |
|-----------|-------|
| Broker | `test.hibersoft.com.tr` |
| Port | `2887` |
| Kullanıcı | `testuser` |
| Şifre | `PUBLIC_MQTT_2026_PASS` |
| Publish Topic | `test/esp32/sensor` |
| Subscribe Topic | `test/esp32/cmd` |

---

### 06 — SmsTest (SMS İşlemleri)

**Amaç:** SMS gönderme, alma, listeleme, okuma ve silme.

**Akış:** 6 adımlı test — gönder → listele → oku → gelen bildirimi aç → bekle → tümünü sil.

```cpp
gsm.smsSend("+905468422222", "Merhaba!");
String liste = gsm.smsList("ALL");
String msj   = gsm.smsRead(1);
gsm.smsDeleteAll();
```

---

### 07 — SmsRelayControl (SMS ile Röle)

**Amaç:** Cep telefonundan SMS atarak röleleri aç/kapat. Uzaktan kontrol cihazlarının temeli.

**Röle pinleri:** GPIO 6 ve GPIO 7 (2 kanal).

**Desteklenen SMS Komutları** (büyük/küçük harf ve boşluk duyarsız):

| Komut | İşlem |
|-------|-------|
| `R1 AC` / `ROLE1 AC` | Röle 1 AÇ |
| `R1 KAPAT` | Röle 1 KAPAT |
| `R2 AC` / `ROLE2 AC` | Röle 2 AÇ |
| `R2 KAPAT` | Röle 2 KAPAT |
| `HEPSI AC` | İki röleyi de AÇ |
| `HEPSI KAPAT` | İki röleyi de KAPAT |
| `DURUM` | Röle durumlarını SMS ile bildir |

**Özellikler:**
- Yetkili numara kontrolü (`YETKILI`)
- Her komuta otomatik **onay SMS'i** geri gönderilir
- `smsPoll` non-blocking (loop içinde, `delay`'siz dinler)

```cpp
#define R1_PIN  6
#define R2_PIN  7
#define YETKILI "+905468422222"
```

> **Röle aktiflik modu:** `RELAY_ACTIVE_HIGH` tanımlıysa pin HIGH = röle AÇIK. Röle modülünüz active-LOW ise bu satırı yorumlayın.

---

### 08 — DtmfRelayControl (Arama + Tuş ile Röle)

**Amaç:** SIM numarasını **arayıp** tuşlara basarak röle kontrol etmek. İnternet gerektirmez.

**Röle pinleri:** GPIO 6 ve GPIO 7 (2 kanal).

**DTMF Tuş Haritası:**

| Tuş | İşlem |
|-----|-------|
| 1 / 2 | Röle 1 AÇ / KAPAT |
| 3 / 4 | Röle 2 AÇ / KAPAT |
| `*` / `0` | HEPSİ AÇ / KAPAT |
| `9` | Durum (Serial'e yaz) |
| `#` | Aramayı kapat |

**Çalışma mantığı (durum makinesi):**
```
IDLE → RING (çağrı gelir) → CLIP (arayan kontrol) → IN_CALL (DTMF dinlenir) → HANGUP
```

- Yetkili numara kontrolü: `YETKILI` doluysa sadece o numara yanıtlanır.
- Maksimum çağrı süresi: `MAKS_CAGRI_MS = 120 sn` → dolunca otomatik kapanır.

```cpp
gsm.callSetCLIP(true);   // Arayan numara gösterimi
gsm.callSetDTMF(true);   // (no-op — SIM7672E otomatik algılar)
gsm.callAnswer();        // ATA
gsm.callHangup();        // ATH

char olay[8], detay[20];
if (gsm.callPoll(olay, sizeof(olay), detay, sizeof(detay))) {
    // olay: "RING" | "CLIP" | "DTMF" | "HANGUP"
}
```

> SIM7672E DTMF tonlarını `+RXDTMF: X` URC olarak otomatik gönderir; `AT+DDET` **desteklenmez**.

---

### 09 — SmsTemperature (SMS ile Sıcaklık)

**Amaç:** DS18B20 sıcaklık sensöründen değer okuyup SMS ile bildirmek.

**Sensör pini:** GPIO 13 (4.7 kΩ pull-up direnci gerekli).

**SMS Komutları** (büyük/küçük harf duyarsız):

| Komut | İşlem |
|-------|-------|
| `SICAKLIK` (veya herhangi bir şey) | Anlık sıcaklığı SMS ile bildir |
| `DURUM` | Sıcaklık + sinyal + sensör durumu |

**Gerekli kütüphaneler** (`platformio.ini` içinde otomatik indirilir):
```ini
lib_deps =
    paulstoffregen/OneWire
    milesburton/DallasTemperature
```

**Bağlantı:**
```
DS18B20 VCC  → 3.3V
DS18B20 GND  → GND
DS18B20 Data → GPIO 13 + 4.7kΩ → VCC
```

---

## 🌐 Test Sunucusu ve Kimlik Doğrulama

Tüm ağ projeleri (02–05) `test.hibersoft.com.tr` genel test sunucusunu kullanır.

| Protokol | Port | Endpoint / Açıklama |
|----------|:----:|---------------------|
| HTTP | 2884 | `POST /token` (token al) · `POST /ingest` (veri gönder) |
| TCP | 2885 | JSON mesajlaşma |
| UDP | 2886 | UDP paket alıcı |
| MQTT | 2887 | Broker (`testuser`) |

### Token Alma (Kimlik Doğrulama)

Sunucuya veri göndermeden önce **token** gerekir. `POST /token`'a boş `{}` gövdesi gönderilir:

```
İstek:  POST http://test.hibersoft.com.tr:2884/token
Gövde:  {}

Yanıt:  {"ok":true,"token":"406b...","clientId":"c_...","expiresAt":"...","ttlSec":86400}
```

Her projede `SAVED_TOKEN` / `SAVED_CLIENT_ID` sabitleri kayıtlı token'ı saklar; boş bırakılırsa otomatik/manuel token alma menüsü devreye girer.

---

## 📌 SIM7672E Notları ve Farkları

SIM7672E, eski SIM800C/SIM800L modüllerinden önemli farklar içerir. Bu repo tüm bu farkları **otomatik yönetir**:

| Konu | SIM800C/SIM800L | SIM7672E |
|------|:---------------:|:--------:|
| LTE şebeke kaydı | `AT+CREG?` | `AT+CEREG?` (LTE) |
| HTTP PDP context | `AT+HTTPPARA="CID",1` | **Desteklenmez** |
| HTTP veri okuma | `AT+HTTPREAD` | `AT+HTTPREAD=0,N` |
| HTTP + TCP eş zamanlı | Destekler | **Desteklenmez** (otomatik NETCLOSE/NETOPEN) |
| DTMF etkinleştirme | `AT+DDET=1,0,0,0` | **Gerekmez** (otomatik) |
| DTMF URC | `+DTMF: X` | `+RXDTMF: X` |
| UDP CIPOPEN | sadece port | **Yerel port zorunlu** (`,0`) |
| UDP CIPSEND | sadece veri | **Remote host/port gerekli** |
| TCP veri formatı | `+IPD<conn>,<len>:<data>` | `+IPD<len>\r\n<data>` |

---

## 🧠 Sahada Öğrenilenler (Tecrübeler)

Bu bölüm, geliştirme ve saha testleri sırasında karşılaşılan gerçek sorunlar ve çözümleridir.

### 1. ESP32-S3 Pin Tuzağı ⚠️
- **Sorun:** `Invalid pin selected` + sonsuz watchdog reset.
- **Neden:** ESP32-S3'te GPIO 22–25 **yok**, 26–37 Octal PSRAM/Flash tarafından kullanılıyor.
- **Çözüm:** Röleler GPIO 6/7, sensörler GPIO 13 gibi **serbest** pinlere taşındı.

### 2. MQTT Remaining Length Kodlaması
- **Sorun:** Payload 127 baytı aştığında broker bağlantıyı kapatıyordu.
- **Neden:** MQTT Remaining Length 127'den büyükse **çok baytlı** kodlanmalı (146 → `0x92 0x01`).
- **Çözüm:** `_encodeRemLen()` ile değişken uzunluklu kodlama eklendi.

### 3. Binary Null-Byte Sorunu
- **Sorun:** `strstr()` ile yanıt arama, veri içindeki `\0` baytlarında takılıyordu.
- **Çözüm:** `String` + `indexOf` + `memcmp` kullanıldı (binary-safe).

### 4. SMS Gönderiminde Zamanlama
- **Sorun:** Gelen `+CMT:` URC'sinden hemen sonra SMS gönderimi başarısız oluyordu.
- **Çözüm:** `AT+CMGS` öncesi `delay(300)`, `>` isteminden sonra `delay(200)` eklendi.

### 5. CIPSEND Ack Tanıma
- **Sorun:** `AT+CIPSEND` sonrası `>` istemi geç geliyordu.
- **Çözüm:** Veri göndermeden önce ~300 ms bekleme eklendi.

### 6. SIM7672E TCP Veri Formatı
- **Sorun:** SIM7600'ün `+IPD<conn>,<len>:<data>` formatına göre parse ediyorduk.
- **Çözüm:** SIM7672E'nin gerçek formatı `+IPD<len>\r\n<data>` olarak düzeltildi.

### 7. Şebeke Kaydı Çift Yedek
- LTE ağlarında `AT+CREG?` yanıtı gecikebilir; `AT+CEREG?` (LTE) önce, `AT+CREG?` (2G) yedek olarak sorgulanır.

---

## ⚡ İyileştirmeler ve Üretim İpuçları

### Bağımsız Mimarinin Faydaları
- Her modül yalnızca kendi ihtiyaç duyduğu kodu taşır → **daha küçük binary** (örn. SMS modülü ~%8 flash).
- Hata ayıklama izole: bir modülü bozmak diğerini etkilemez.
- Üretime alırken ilgili klasörü kopyalayıp kendi mantığınızı ekleyin.

### Zaman Aşımı Değerleri (Ayarlanabilir)

```cpp
#define AT_TIMEOUT     3000     // Genel AT komutları
#define GPRS_TIMEOUT  15000     // PDP context açılışı
#define HTTP_TIMEOUT  28000     // HTTP istekleri
#define MQTT_TIMEOUT  10000     // MQTT işlemleri
#define SMS_SEND_TIMEOUT 60000  // SMS gönderimi
```

### Üretim Önerileri

1. **Başlangıç sırası:** `roleInit()` (röleleri kapat) → `hardReset()` → SIM → şebeke → uygulama. Röleler güç açılışında tetiklenmesin diye önce kapatılır.
2. **Onboard LED:** ESP32-S3 GPIO48 aktif-düşüktür; `pinMode(48, OUTPUT); digitalWrite(48, HIGH);` ile kapatılır.
3. **Debug:** Geliştirme sırasında `gsm.setDebug(true)` ile tüm AT trafiğini izleyin; üretimde `false` yapın (binary küçülür, RAM rahatlar).
4. **Yetkilendirme:** SMS/DTMF projelerinde `YETKILI` numarasını mutlaka doldurun.
5. **Güç:** SIM7672E anlık **2 A'e kadar** akım çekebilir — besleme kaynağı yeterli olmalı.

---

## 🛠️ Örnek Cihaz Kullanım Senaryoları

### Senaryo 1: Uzaktan Kapı/Pompa Kontrolü (SMS)
1. `07_SmsRelayControl` projesini yükleyin.
2. Röle çıkışını kontak kapısına/pompa rölesine bağlayın.
3. Telefondan `R1 AC` SMS'i atın → röle çekilir, onay SMS'i gelir.
4. `DURUM` yazın → anlık durum raporu.

### Senaryo 2: İnternetsiz Sahada Komut (DTMF)
1. `08_DtmfRelayControl` projesini yükleyin.
2. SIM numarasını arayın → cihaz otomatik yanıtlar.
3. `1` tuşuna basın → Röle 1 açılır. `#` ile kapatırsınız.
> İnternet/LTE veri gerektirmez — yalnız ses araması yeterli.

### Senaryo 3: Sıcaklık İzleme (SMS)
1. `09_SmsTemperature` projesini yükleyin, DS18B20'yi GPIO 13'e bağlayın.
2. `SICAKLIK` SMS'i atın → anlık derece gelir.
3. Soğuk hava deposu, sera, kazan dairesi izleme için idealdir.

### Senaryo 4: Bulut Veri Gönderimi (MQTT)
1. `05_MQTTTest` projesini yükleyin.
2. Cihaz token alır, broker'a bağlanır, `test/esp32/sensor` topic'ine JSON yayınlar.
3. Sunucudan `test/esp32/cmd` topic'ine komut göndererek cihazı yönetebilirsiniz.

---

## ❓ Sık Sorulan Sorular

**S: Hangi SIM kartları çalışır?**
C: SIM7672E **Cat-1** modülüdür; LTE Cat-1 destekleyen herhangi bir operatör SIM'i çalışır. APN değeri operatöre göre değişir (Turkcell: `mglobalinternet`, Vodafone/Türk Telekom: `internet`).

**S: ESP32 (S3 değil) ile çalışır mı?**
C: Evet. Pin kısıtları yalnızca S3'e özgüdür; klasik ESP32'de röle pinlerini serbest GPIO'lara taşıyabilirsiniz.

**S: 4 röleli sürüm var mı?**
C: ESP32-S3'te serbest GPIO az olduğu için örnekler 2 röleli yapıldı. Serbest pinlerden (8–15) ek röle bağlayıp `roleSet` fonksiyonunu genişletebilirsiniz.

**S: Debug modu nasıl açılır?**
C: `setup()` içinde `gsm.setDebug(true)`. Tüm AT komutları `[TX]`/`[RX]` etiketiyle görünür.

**S: HTTP sırasında TCP kopuyor, normal mi?**
C: Evet. SIM7672E'de HTTP ve NETOPEN aynı anda aktif olamaz; kütüphane bunu otomatik yönetir.

**S: DS18B20 bulunamadı hatası?**
C: GPIO 13 + 4.7 kΩ pull-up direnci bağlı mı kontrol edin. Kabloları kısa tutun.

---

## 📁 Proje Yapısı

```
esp32-7072e/
├── examples/
│   ├── 01_SerialTest/        ← Modem tanılama
│   ├── 02_HTTPTest/          ← HTTP GET/POST + token
│   ├── 03_TCPTest/           ← Raw TCP soket
│   ├── 04_UDPTest/           ← UDP paket
│   ├── 05_MQTTTest/          ← MQTT publish/subscribe (✅ doğrulandı)
│   ├── 06_SmsTest/           ← SMS işlemleri
│   ├── 07_SmsRelayControl/   ← SMS ile röle
│   ├── 08_DtmfRelayControl/  ← DTMF ile röle
│   └── 09_SmsTemperature/    ← DS18B20 + SMS
├── library.properties
├── keywords.txt
└── LICENSE
```

> Her `examples/XX_*/` klasörü kendi `platformio.ini` + `src/` dosyalarıyla **bağımsız bir projedir**.

---

## 📄 Lisans

MIT © 2026 [Hiber Bilişim](https://www.hiber.com.tr)

> **GitHub:** [github.com/hiberblsm/esp32-7072e](https://github.com/hiberblsm/esp32-7072e)
