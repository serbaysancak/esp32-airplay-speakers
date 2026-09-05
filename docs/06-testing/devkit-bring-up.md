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

## Ne gözlendi ama açıklanmadı

**İlk birleşme bazen düşüyor.** Üç açılışın ikisinde sıra şöyleydi: `assoc -> run` ~4,3 s'de, sonra ~14,3 s'de `run -> init`, `hk_net: disconnected, retry 1 of 5`, ardından ~20 s'de temiz birleşme ve ~21 s'de IP. Üçüncü açılışta düşme olmadı ve IP 5,4 s'de geldi. Yeniden bağlanma mantığı her seferinde toparladı, yani kullanıcıya görünen bir arıza yok — ama **10 saniyelik bu düşüş açıklanmadı.** Kopma sebebi kodu kaydedilmedi. Kartın anten yerleşimi, WPA3-SAE anlaşması ve AP'nin band steering'i aday açıklamalar; hiçbiri ölçülmedi.

Bu satır burada, "aralıklı" diye geçiştirilmesin diye duruyor. Ürün kartında tekrar bakılacak.

## Bu kartta kanıtlanamayacak olanlar

- `G7` dört cihaz senkronu: tek kart var.
- Gerçek DAC çıkışı, amfi davranışı, sürücü empedansı: donanım yok.
- Ürün kartının PSRAM bant genişliği: quad 2 MB, oktal 8 MB'ın yerine geçmez.
