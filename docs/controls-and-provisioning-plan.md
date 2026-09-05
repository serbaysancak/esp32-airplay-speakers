---
status: active
owner: firmware-engineer
updated: 2026-08-31
tags: [controls, provisioning, firmware]
---

# Harman Kardom kontroller, LED ve Wi-Fi provisioning planı

Güncelleme: 2026-08-30

## Karar özeti

Her hoparlörde üç kullanıcı arayüzü öğesi bulunacak:

1. Bir adet çok-fonksiyonlu anlık buton: provisioning, ağ sıfırlama ve fabrika sıfırlama.
2. Bir adet RGB durum LED'i: açılış, provisioning, Wi-Fi, AirPlay, hata ve düşük batarya durumları.
3. Ayrı bir kilitlemeli mekanik güç anahtarı: yükleri fiziksel olarak bataryadan ayırır.

Wi-Fi kurulumu iki yöntemle sunulacak:

- Uygulamasız: SoftAP + captive portal.
- Bluetooth LE: ESP-IDF Unified Provisioning. Espressif uygulaması veya ileride hazırlanacak özel iOS/Android uygulaması kullanılacak.

> [!note] İkisi sırayla sunulur, aynı anda değil
> ESP-IDF'in provisioning yöneticisi tek bir statik bağlam tutar ve yapılandırmasında **tek bir scheme** alır. `scheme_ble` Wi-Fi'yi `WIFI_MODE_STA`'ya, `scheme_softap` ise `WIFI_MODE_APSTA`'ya alır; ikisi bir oturumda birlikte çalışamaz. Kaynak: `components/wifi_provisioning/src/manager.c` (tek `prov_ctx`), `scheme_ble.c:337` ve `scheme_softap.c:185`, ESP-IDF v5.5.1.
>
> [[07-decisions/ADR-0005-dual-provisioning|ADR-0005]] bunu sırayla sunmaya karar verdi. Hangisinin açılacağını provisioning'e nasıl girildiği belirler: **kayıtlı kimlik bilgisi yoksa SoftAP**, **yapılandırılmış cihazda butonla açılırsa BLE**. Uygulamasız yol her zaman erişilebilir kalır, çünkü 5 saniyelik basış Wi-Fi'yi silip cihazı ilk duruma döndürür.

ESP32-S3'te Bluetooth Classic/A2DP bulunmaması BLE provisioning'i engellemez. Provisioning tamamlanınca BLE servisi durdurulacak ve ayrılan bellek serbest bırakılacak; normal AirPlay çalışmasında BLE açık tutulmayacak.

## Otomatik tanıma için gerçekçi platform sınırı

### Uygulamasız deneyim

- Cihaz ilk açılışta veya provisioning tuşuna basılınca `HarmanKardom-Setup-XXXX` isimli 2,4 GHz SoftAP açar.
- Kullanıcı telefonun Wi-Fi listesinden bu ağı seçer veya cihaz altındaki Wi-Fi QR kodunu tarar.
- iOS/Android captive portal algılaması kurulum sayfasını otomatik açmayı dener.
- Portal otomatik açılmazsa sabit adres `192.168.4.1` kullanılır.

Bu yöntem özel uygulama istemez, ancak telefonun BLE yayınını görür görmez ana ekranda Apple/Android sistem kartı açması garanti edilemez.

### BLE ile uygulamalı deneyim

- Cihaz `HarmanKardom-XXXX` adı ve üretici servis UUID'si ile BLE yayını yapar.
- ESP-IDF Unified Provisioning, Security 2 / SRP6a ve cihaza özel proof-of-possession kullanır.
- QR kod cihaz adı, transport, güvenlik sürümü ve benzersiz PoP bilgisini taşır.
- İlk prototip Espressif Provisioning iOS/Android uygulamalarıyla kurulabilir.
- Nihai özel uygulama yapılırsa iOS'ta AccessorySetupKit, Android'de Companion Device Manager kullanılarak sistem aksesuar seçicisi gösterilebilir.

iOS AccessorySetupKit ve Android Companion Device Manager bir uygulama tarafından çağrılan API'lerdir. Uygulama olmadan özel ürün görseli ve sistem eşleştirme kartı açma kapsam dışıdır. Apple HomeKit/MFi veya Google Fast Pair kimliği taklit edilmeyecektir.

## Harman Kardom ürün kimliği

| Yüzey | Varsayılan ad |
|---|---|
| Proje/ürün ailesi | `Harman Kardom` |
| AirPlay görünen adı | `Harman Kardom XXXX` |
| BLE provisioning yayını | `HarmanKardom-XXXX` |
| SoftAP SSID | `HarmanKardom-Setup-XXXX` |
| mDNS/yerel ağ adı | `harman-kardom-xxxx.local` |
| Captive portal başlığı | `Harman Kardom Kurulum` |
| QR ürün etiketi | `Harman Kardom` |

`XXXX`, MAC adresinden türetilen kısa benzersiz cihaz kimliğidir. Kullanıcı AirPlay adını değiştirebilir; BLE ve SoftAP adlarında benzersiz son ek korunur. Dört hoparlör ilk açılışta birbirinden bu kimlikle ayrılır.

## Provisioning durum makinesi

```text
İlk açılış / kayıtlı Wi-Fi yok
              |
              v
BLE yayını + SoftAP captive portal
              |
              v
Güvenli kimlik doğrulama ve ağ seçimi
              |
              v
Wi-Fi bağlantı testi
       | başarılı       | başarısız
       v                v
BLE/AP kapat         provisioning açık kalır
mDNS + AirPlay       hata LED'i + yeniden dene
```

- Provisioning penceresi ilk açılışta kurulum tamamlanana kadar açık kalır.
- Kayıtlı bir cihazda butonla açılan provisioning 10 dakika sonra otomatik kapanır.
- Başarılı bağlantıdan sonra BLE ve SoftAP tamamen kapatılır.
- Art arda bağlantı hatasında cihaz kontrollü olarak tekrar provisioning moduna döner.
- Wi-Fi parolası hiçbir log, web sayfası geri cevabı veya seri telemetride gösterilmez.

## Çok-fonksiyonlu buton davranışı

Buton aktif-low çalışacak; seçilecek normal GPIO ile GND arasına bağlanacak ve dahili pull-up kullanılacak. GPIO numarası kesin kart ve I2S pinleri seçildikten sonra belirlenecek; boot/strapping pinleri kullanılmayacak.

| Hareket | İşlev | LED geri bildirimi |
|---|---|---|
| Kısa basış, 0,1-1,5 sn | 10 dakikalık BLE + SoftAP provisioning başlat | Mavi nefes |
| 5 sn basılı tut | Yalnız Wi-Fi kimlik bilgilerini sil ve provisioning'e yeniden başlat | Sarı geri sayım, sonra mavi |
| 12 sn basılı tut | Kullanıcı ayarlarını fabrika değerine döndür | Kırmızı hızlı yanıp sönme, bırakınca beyaz |

Fabrika sıfırlama sürücü koruma profili, maksimum güvenli limiter, crossover güvenlik sınırları veya donanım kalibrasyonunu silmeyecek. NVS alanları `factory_cal` ve `user_settings` olarak ayrılacak.

Buton yazılım gereksinimleri:

- 50 ms debounce.
- Basılı tutma eşiklerinde anlık LED geri bildirimi.
- 12 saniyelik işlem yalnız buton bırakıldığında onaylanır; eşik geçilirken veri silinmez.
- Açılış sırasında buton basılıysa kurtarma provisioning modu başlatılır.
- Yanlışlıkla kısa dokunmada kayıtlı Wi-Fi silinmez.

## RGB LED durumları

Tek gövdeli, ortak katot RGB LED ve her renk için ayrı seri direnç kullanılacak. Üç PWM GPIO gerekir. Parlaklık gece kullanımında rahatsız etmeyecek şekilde sınırlandırılacak.

| Renk/desen | Durum |
|---|---|
| Kapalı | Fiziksel güç kapalı veya uyku göstergesi |
| Beyaz nefes | Açılış ve donanım testi |
| Mavi nefes | BLE/SoftAP provisioning aktif |
| Sarı yavaş yanıp sönme | Wi-Fi ağına bağlanıyor |
| Yeşil 3 sn, sonra sönük | Wi-Fi bağlı ve AirPlay hazır |
| Mor nefes, düşük parlaklık | Aktif AirPlay oynatma |
| Camgöbeği yanıp sönme | OTA güncelleme; güç kesilmemeli |
| Kırmızı yavaş | Batarya düşük |
| Kırmızı hızlı | Wi-Fi, ses veya batarya hatası |

> **2026-09-05 — hangi satırlar gerçekten sürülüyor.** Tablo baştan beri eksiksizdi ve `hk_led` her satırı uygulamıştı, ama iki durumu hiçbir yer set etmiyordu: `playing` ve `battery_low`. Oynatma durumu bu tarihte bağlandı — AirPlay yığınının kendi RTSP olayları `hk_main` üzerinden `hk_ui`'ya taşınıyor, LED'in tek sahibi `hk_ui` kalmaya devam ediyor. Sahibi "mor nefes" istediği için desen `SOLID`'den `BREATHE`'e alındı ve satır buna göre güncellendi.
>
> Aynı turda nefes efektinin kendisi de düzeltildi ve bu tablodaki üç "nefes" satırının hepsini etkiliyor: zarf üçgendi, kosinüs oldu (üçgen iki uçta da anında döndüğü için göz onu nefes değil sıçrama olarak görüyor), ve artık sıfıra inmiyor — sıfıra inen bir nefes yavaş yanıp sönmedir, renk kaybolur ve göz ritmi değil kaybolmayı fark eder.
>
> `battery_low` hâlâ ölü ve yazılımla açılamaz: ADC sürücüsü yok ve eşikler `G3`/`G4` ölçümlerine bağlı. Uydurma bir eşikle yakmak, batarya göstergesini güvenilmez yapardı.

LED animasyonları audio task üzerinde çalışmayacak; düşük öncelikli ayrı görev/timer kullanılacak. PWM veya GPIO güncellemelerinin I2S zamanlamasına ve analog dip gürültüsüne etkisi ölçülecek.

## Fiziksel güç anahtarı

- Ayrı, kilitlemeli ve en az 24 V DC / 5 A değerli mekanik anahtar kullanılacak.
- Kullanıcının seçtiği panel parçası [KM103 / DC-132A 12 V beyaz nokta ışıklı 3P rocker](https://www.direnc.net/dc-132a-12v-yuvarlak-nokta-isikli-on-off-anahtar-3p-beyaz) modelidir; ancak bu seçim elektriksel onay değildir. Ürün sayfasında kontak DC akım değeri bulunmadığından 16,8 V / 5 A yeterliliği yazılı doğrulanacak ve G3'te test edilecek.
- Dahili ışık 12 V DC sınıfındadır. İlk prototipte LED pini bağlanmayacak; kullanılacaksa 12 V akımı ölçülüp 16,8 V tam dolu gerilime uygun harici seri direnç seçilecektir.
- Anahtar BMS ile sistem yükleri arasına yerleştirilecek; amfi ve 5 V buck hattını birlikte kesecek.
- Şarj jakı BMS tarafında kalacak; hoparlör kapalıyken batarya şarj edilebilecek.
- Anahtar şarj akımını veya BMS balans işlevini kesmeyecek.
- Açma/kapatmada amfi mute sıralaması ve pop sesi ayrıca test edilecek.

## Güvenlik ve gizlilik

- Her cihaz için benzersiz provisioning PoP üretilecek; tüm cihazlarda ortak parola kullanılmayacak.
- SoftAP mümkünse cihaza özel parola ile korunacak; QR kod bu bilgiyi taşıyacak.
- BLE provisioning yalnız ilk kurulumda veya fiziksel butonla zaman sınırlı olarak açılacak.
- Provisioning istekleri hız sınırlı olacak ve başarısız kimlik doğrulamalar loglarda parola içermeyecek.
- Saklanan Wi-Fi kimlik bilgileri için ESP-IDF NVS encryption seçeneği değerlendirilecek.
- OTA sırasında düşük batarya eşiği ve harici güç kontrolü uygulanacak.

## Teknik kaynaklar

- ESP-IDF Unified Provisioning: https://github.com/espressif/esp-idf/blob/master/docs/en/api-reference/provisioning/provisioning.rst
- Espressif iOS provisioning: https://github.com/espressif/esp-idf-provisioning-ios
- Espressif Android provisioning: https://github.com/espressif/esp-idf-provisioning-android
- Apple AccessorySetupKit: https://developer.apple.com/documentation/AccessorySetupKit
- Android Companion Device Pairing: https://developer.android.com/develop/connectivity/bluetooth/companion-device-pairing
- AirPlay ESP32 ilk açılış: https://rbouteiller.github.io/airplay-esp32/getting-started/first-boot/
- AirPlay ESP32 buton altyapısı: https://rbouteiller.github.io/airplay-esp32/features/buttons/
