---
status: active
owner: firmware-engineer
reviewers: [orchestrator, verifier]
updated: 2026-09-05
tags: [testing, devkit, bring-up, firmware, wifi, evidence]
---

# Geliştirme kartı bring-up kaydı

Bu, deponun firmware'inin **ilk kez gerçek silikonda çalıştığı** kayıt. Kart, [[../07-decisions/ADR-0012-n8r2-bringup-target|ADR-0012]]'nin tanımladığı N8R2 sınıfı geliştirme kartıdır — ürün kartı değildir.

> Buradaki hiçbir satır bir fiziksel kapı (`G0`-`G8`) açmaz. Kartta sürücü, amfi, DAC ve batarya yoktur; ölçülen tek şey işlemci, bellek ve ağdır.

## Kart kimliği

`esptool.py chip_id` ve `flash_id`, 2026-09-05:

```text
ESP32-S3 (QFN56) revision v0.2
Features: WiFi, BLE, Embedded PSRAM 2MB (AP_3v3)
Crystal 40MHz, USB mode: USB-Serial/JTAG
MAC: a4:cb:8f:9b:06:c4
Manufacturer: 1c  Device: 3017
Detected flash size: 8MB
Flash type set in eFuse: quad (4 data lines), voltage 3.3V
```

## Karta gelmeden önce üzerinde ne vardı

Kart boş değildi. Üzerinde başka bir oturumda üretilmiş, **kaynağı bu depoda bulunmayan** bir yapı vardı: `harman-kardom` 0.1.0, derleme zamanı `Sep 3 2026 14:38:01`, `ESP-IDF: GIT-NOTFOUND`. 8 MB'lık bir bölüm tablosu, `gpio48` üzerinde bir WS2812 aynası ve `hk_airplay` / `mdns_airplay` bileşenleri taşıyordu. İmajdan çıkarılan diziler `fp-setup`, `pair-setup`, `pair-verify`, `SETPEERS`, `SETRATEANCHORTIME`, `FPLY`, `ptp`, `srp`, `ALAC`, `_airplay._tcp` ve `_raop._tcp` içeriyordu — yani [[../07-decisions/ADR-0007-airplay-stack|ADR-0007]]'nin gerçek yığını vendor edilmişti.

Kaynağı ne depoda ne de bu makinede bulundu. Silmeden önce **tüm flash yedeklendi**:

```text
flash-full-8MB-a4cb8f9b06c4.bin   8.388.608 bayt
sha256 870bf5165a0f5a56a00649785fe08bd28b70498505f5861d7bb071c3959e3f97
```

Yedek depo dışında tutuluyor (8 MB ikili, ve içinde kullanıcının Wi-Fi kimlik bilgileri var). Deponun kaydı budur: **o entegrasyonun kaynağı yoktur, yalnız derlenmiş hali vardır.** Bu yüzden bu oturumda uyarlama sıfırdan ve depoda yeniden yapıldı.

## Yazılan şey

Depodan derlenen geliştirme profili:

```text
harman-kardom.bin   1.307.008 bayt
0x2e0000 slotta 1.707.648 bayt bos (%56,6)
```

Sıra: tam `erase_flash` (29,5 s) → `write_flash @flash_args` (4 bölge, hepsi "Hash of data verified") → yedekten yalnız `nvs` bölümü (`0x9000`, 24.576 bayt) geri yazıldı.

`nvs` geri yazıldı çünkü ESP-IDF'in Wi-Fi sürücüsü SSID/parolayı orada tutuyor. Bu, **ağ yolunu kullanıcının parolası hiçbir yere yazılmadan** test etmeyi sağladı: 24 KB flash yerine kondu, o kadar. Parola ne depoya, ne bir günlüğe, ne de bir ajanın bağlamına girdi.

`factory_cal` bilerek silinmiş bırakıldı; sonuç açılışta görünüyor (`calibration=fail_safe`).

## Açılış raporu

```text
esp_psram: Found 2MB PSRAM device
esp_psram: Speed: 80MHz
esp_psram: SPI SRAM memory test OK
esp_psram: Adding pool of 2048K of PSRAM memory to heap allocator
app_init: ESP-IDF: v5.5.1
hk: board       devkit-n8r2
hk: chip        2 core(s), revision 2
hk: flash       8 MB detected
hk: psram       2 MB
hk: slot        ota_0 at 0x00020000, 3014656 bytes
hk: storage     user=use calibration=fail_safe
hk: audio       NOT permitted
hk: output      SILENT (i2s=0 dac=0 amp=0)
```

Bölüm tablosu bootloader tarafından okunduğu gibi `partitions-devkit.csv` ile birebir eşleşti (`ota_0 00020000 002e0000`, `ota_1 00300000 002e0000`, `storage 005e0000 00220000`).

## Ne PASS oldu

| Ölçüm | Sonuç |
|---|---|
| Quad PSRAM 80 MHz'de açılıyor, bellek testi geçiyor | **PASS** — 2048K heap'e eklendi |
| 8 MB bölüm tablosu doğru yükleniyor | **PASS** |
| Kart kendi varyantını bildiriyor (`devkit-n8r2`) | **PASS** |
| PSRAM boyutu ile derlenen kart profili uyuşuyor | **PASS** — uyuşmazlıkta hata basacak yol eklendi |
| `hk_storage` kalibrasyon yokluğunda `fail_safe`'e düşüyor | **PASS** |
| Ses yolu susturulmuş kalıyor | **PASS** — `audio NOT permitted`, `i2s=0 dac=0 amp=0` |
| Wi-Fi birleşmesi ve DHCP | **PASS** — `KhudrahGame_2.4Ghz`, WPA3-SAE, RSSI −44 dBm, `192.168.68.74` |
| mDNS başlıyor | **PASS** — `harman-kardom-06c4.local` |

## Ölçülen bellek bütçesi

Açılış raporuna iki nokta eklendi, çünkü AirPlay alıcısının sığıp sığmadığı parçanın boyutuna değil, radyolar yerleştikten **sonra** kalana bağlı:

| An | Dahili boş | En büyük blok | PSRAM boş |
|---|---:|---:|---:|
| Açılış (Wi-Fi sürücüsü henüz yok) | 292.787 B | 196.608 B | 2.094.964 B |
| Ağa katıldıktan sonra | 237.787 B | 163.840 B | **2.094.848 B** |

ADR-0007 yığınının jitter tamponu kaynaktan hesaplanabiliyor: `MAX_RING_BUFFER_FRAMES` 1000 × `BYTES_PER_FRAME` 1416 = **1.416.000 bayt**. Ölçülen boş PSRAM'in %67,6'sı; geriye 678.848 bayt kalıyor.

Bu, "sığıyor" demek değil — yalnız "aritmetik önü kapatmıyor" demek. Alıcının PSRAM'de başka ne ayırdığı (mDNS, RTSP tamponları, çözücüler, resampler) ölçülmedi; ancak yığın gerçekten çalıştığında ölçülebilir.

## `hk_ui` görev yığını

Ölçülen: **3.072 baytın 2.332 baytı hiç kullanılmıyor**, yani görev kendi işi için ~740 bayt harcıyor. Görev doğru boyutlanmış; sorun boyut değil, oraya konan işti.

## Ne gözlendi ama açıklanmadı

**İlk birleşme bazen düşüyor.** Üç açılışın ikisinde sıra şöyleydi: `assoc -> run` ~4,3 s'de, sonra ~14,3 s'de `run -> init`, `hk_net: disconnected, retry 1 of 5`, ardından ~20 s'de temiz birleşme ve ~21 s'de IP. Üçüncü açılışta düşme olmadı ve IP 5,4 s'de geldi. Yeniden bağlanma mantığı her seferinde toparladı, yani kullanıcıya görünen bir arıza yok — ama **10 saniyelik bu düşüş açıklanmadı.** Kopma sebebi kodu kaydedilmedi. Kartın anten yerleşimi, WPA3-SAE anlaşması ve AP'nin band steering'i aday açıklamalar; hiçbiri ölçülmedi.

Bu satır burada, "aralıklı" diye geçiştirilmesin diye duruyor. Ürün kartında tekrar bakılacak.

## AirPlay alıcısı

Yığın [[../07-decisions/ADR-0013-airplay-integration-shape|ADR-0013]]'e göre vendor edildi ve **depodan derlenen imaj** kartta çalıştı:

```text
mdns_airplay: mDNS hostname: Harman-Kardom-06C4.local (device name: Harman Kardom 06C4)
rtsp_server: RTSP server listening on port 7000
hk_airplay: receiver ready; I2S is clocked from here on, the DAC and amplifier stay muted
```

Cihaz adı bizim kimliğimizden geliyor, yukarı akışın kendi varsayılanından değil. İmaj 1.642.880 bayt, slotun %45,5'i boş.

| Ölçüm | Sonuç |
|---|---|
| Yığın derleniyor, açılıyor, çökmüyor | **PASS** |
| mDNS servisleri hatasız kaydediliyor | **PASS** (cihaz tarafı) |
| RTSP 7000 dinliyor | **PASS** (cihaz tarafı) |
| Bir Apple cihazı bağlandı | **YAPILMADI** |
| Ağdan bağımsız doğrulama | **BAŞARISIZ — ağ nedeniyle**, aşağıya bakın |

### Ağdan doğrulanamadı, ve nedeni firmware değil

Cihazın kendi logu "hazır" diyor; buna güvenmeyip Mac'ten `dns-sd` ile arattım ve **kart görünmedi**. Ardından:

- karta `ping`: %100 kayıp, `arp` girdisi `incomplete` — yani L2'de cevap yok;
- aynı Mac aynı subnet'te **sekiz başka komşuyu** ARP'la çözüyor, yani genel bir istemci yalıtımı yok;
- gateway'e ping çalışıyor, kart DHCP almış.

Sebep ağ yapılandırması: kart `KhudrahGame_2.4Ghz`'ye bağlı ve bu, TP-Link Deco'nun **misafir ağı** — tasarımı gereği ana ağdan yalıtık. Mac ana ağda.

Bu bir test kolaylığı sorunu değil, **ürün için belirleyici**: AirPlay keşfi mDNS çoklu yayınıyla, oturum RTSP ile, senkron PTP çoklu yayınıyla çalışır. Hiçbiri yalıtılmış bir misafir ağını aşmaz. Hoparlör ve telefon aynı L2 ağında olmak zorundadır.

Kart bu yüzden saklanan kimlik bilgileri silinip provisioning'e alındı; ana ağa katılması kullanıcının parolayı kendi girmesiyle olacak.

## Provisioning ilk kez donanımda açıldı

`factory_cal`, `provision_credentials.py --image` ile üretilip `0x13000`'a yazıldıktan sonra:

```text
hk_net: provisioning credentials loaded: salt 16 B, verifier 384 B
wifi_prov_mgr: Provisioning started with service name : HarmanKardom-Setup-06C4
hk_net: provisioning open over softap
```

Ayrıca `storage user=write_defaults calibration=use`: kalibrasyon bölümü artık geçerli bir şema taşıyor ve ayrı bölüm olarak okunuyor. Kimlik bilgileri depo dışında tutuluyor.

**Bir Wi-Fi ağına katılma bu yolla henüz tamamlanmadı** — parola cihaz sahibine ait ve bu oturumda hiçbir yere girilmedi.

## BLE provisioning hiç yayın yapmıyordu

Kullanıcı, Espressif'in hem SoftAP hem BLE provisioning uygulamasıyla denedi; cihaz **BLE'de hiç görünmedi**, QR'sız listeden de bulunamadı. Seri logda karşılığı vardı ve gözden kaçmıştı: `wifi_prov_mgr: Provisioning started with service name` satırı var, ama **NimBLE'dan tek satır yok**.

Kök neden `hk_network_start()`'ta. Yönetici, "provisioned mı?" sorusunu sorabilmek için SoftAP şemasıyla kuruluyor, sonra `s_scheme` BLE'ye çevriliyor — ve yöneticiyi yeniden kurması gereken adım hiç yazılmamış. Üstelik kodun kendi yorumu "reinitialised below if BLE turns out to be the right transport" diyordu. `wifi_prov_mgr_init()` taşımayı bağlar; sonrasında başlatılan şey `s_scheme`'in söylediği değil, bağlanmış olandır.

Sonuç: cihaz `provisioning open over ble` yazarken SoftAP yayınlıyordu. Log dışında her yüzey tutarlıydı; yalnız radyo değil.

Bu kusur depoda baştan beri duruyordu ve ADR-0005'in kuralı o kod yolunda zaten SoftAP istediği için hiç görünmemişti. Buton yolu (`hk_network_open_provisioning`) doğruydu — şemayı önce seçip yöneticiyi ona göre kuruyor.

Düzeltmeden sonra ölçülen:

```text
wifi_prov_scheme_ble: BT memory released
BLE_INIT: BT controller compile version [2edb0b0]
protocomm_nimble: BLE Host Task Started
NimBLE: GAP procedure initiated: advertise;
wifi_prov_mgr: Provisioning started with service name : HarmanKardom-06C4
```

Aynı turda ikinci bir kusur: `wifi_prov_mgr_start_provisioning` her iki taşımada da SoftAP adını geçiyordu. BLE `HarmanKardom-Setup-06C4` diye yayın yaparken QR `HarmanKardom-06C4` arıyordu — yani QR'lı kurulum hiçbir zaman eşleşemezdi. Taşımaya göre doğru ad geçiliyor artık.

**Bir Apple cihazının bu yayına bağlandığı hâlâ doğrulanmadı.** Kanıtlanan, yayının var olduğu.

## Kart üzerindeki durum LED'i

Geliştirme kartında harici RGB LED'in bağlı olduğu pinlerde hiçbir şey yok, yani cihazın durumu yalnız seri konsoldan okunabiliyordu. `hk_ui` artık aynı render geçişinin ürettiği değerleri kartın kendi adreslenebilir LED'ine de yazıyor (`gpio48`, Kconfig ile değiştirilebilir). Ayna, ikinci bir gösterge değil: renk aynı hesaptan geliyor, dolayısıyla ikisi birbiriyle çelişemez.

## Bu kartta kanıtlanamayacak olanlar

- `G7` dört cihaz senkronu: tek kart var.
- Gerçek DAC çıkışı, amfi davranışı, sürücü empedansı: donanım yok.
- Ürün kartının PSRAM bant genişliği: quad 2 MB, oktal 8 MB'ın yerine geçmez.
