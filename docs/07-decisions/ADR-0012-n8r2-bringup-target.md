---
status: accepted
decision: accepted
owner: firmware-engineer
reviewers: [orchestrator, verifier]
updated: 2026-09-05
tags: [adr, esp32, board, devkit, ota, bring-up]
---

# ADR-0012: N8R2 geliştirme kartı bring-up hedefi

## Bağlam

Ürün kartı gelmedi. Elde bir **ESP32-S3 N8R2 sınıfı geliştirme kartı** var ve 2026-09-05'te tezgâhta bağlı. Çipin kendisinden okunan değerler, etiketten değil:

```text
ESP32-S3 (QFN56) revision v0.2, MAC a4:cb:8f:9b:06:c4
Features: WiFi, BLE, Embedded PSRAM 2MB (AP_3v3)
Detected flash size: 8MB
Flash type set in eFuse: quad (4 data lines), flash voltage 3.3V
USB mode: USB-Serial/JTAG
```

[[ADR-0010-esp32-s3-n16r8-board|ADR-0010]] ürün kartını `N16R8`'e kilitliyor: 16 MB flash + 8 MB **oktal** PSRAM. Tezgâhtaki kart bunun ikisinde de farklı. `partitions.csv` tam olarak `0x1000000`'da bittiği için 8 MB'lık parçada derleme durur; `CONFIG_SPIRAM_MODE_OCT` ise quad donanımda daha sinsi davranır.

ADR-0010, 8 MB'lık bir parçanın nasıl kullanılabileceğini kendisi tarif ediyor. İlgili maddesi ikincil yedek olarak adı geçen `N8R8` içindir: "Yalnız partition bütçesi ölçülüp 8 MB'a sığdığı kanıtlanırsa **ve yeni bir ADR ile açılırsa** kullanılabilir."

Tezgâhtaki kart o yedek değil — `N8R8` 8 MB PSRAM taşır, bu kartta 2 MB var. Ama maddenin koşulu aynı koşuldur ve ikisi de burada karşılanıyor: bütçe ölçüldü (aşağıda), ve açan ADR budur. Bu ADR bir ürün alternatifi önermiyor; yalnız gönderilemez bir bring-up hedefi açıyor.

## Karar

Firmware, **ürünün yanında ikinci ve gönderilemez bir hedef** olarak N8R2 geliştirme kartına derlenir.

Bir kartı tanımlayan şey dört ayardır ve bunlar birlikte değişmek zorundadır:

| | Ürün | Geliştirme kartı |
|---|---|---|
| Flash | `CONFIG_ESPTOOLPY_FLASHSIZE_16MB` | `CONFIG_ESPTOOLPY_FLASHSIZE_8MB` |
| PSRAM | `CONFIG_SPIRAM_MODE_OCT` | `CONFIG_SPIRAM_MODE_QUAD` |
| Bölüm tablosu | `partitions.csv` | `partitions-devkit.csv` |
| OTA donanım sürümü | `prototype-n16r8` | `devkit-n8r2` |

Dördü tek bir Kconfig seçeneğine, `CONFIG_HK_BOARD_DEVKIT_N8R2`'ye bağlıdır ve `firmware/CMakeLists.txt` uyuşmayan bir yapılandırmayı **configure anında** reddeder. Ayrıca imzalı bir geliştirme yapısı üretmeyi reddeder.

Bölüm tablosu `0x20000`'ın altında ürün tablosuyla **bayt bayt aynıdır**. Yalnız uygulama slotları küçülür:

```text
kullanilabilir   0x800000 - 0x20000 = 0x7e0000
iki esit slot    2 x 0x2e0000       = 0x5c0000
storage          0x220000
toplam           0x7e0000  -> tam, artik yok
```

`0x2e0000` = 3.014.656 bayt. ADR-0008'in %30 boş alan kuralı burada da geçerlidir ve **gevşetilmemiştir**: azami uygulama 2.110.259 bayt.

## Gerekçe

**Neden ürün ayarlarını geçici olarak değiştirmek yerine ayrı bir profil.** Üç ayarın üçü de yanlış eşleştiğinde geç ve sessiz patlar. Yanlış flash boyutu imaj başlığına yazılır ve `ota_1`, `ota_0`'ın üstüne biner. Yanlış PSRAM modu açılışta kendi bellek testini geçer, sonra yük altında heap'i bozar. Yanlış bölüm tablosu ilk güncellemeye kadar sorunsuz açılır. Hiçbiri derleme hatası değildir; bu yüzden derleme hatasına çevrildiler.

**Neden ayrı `sdkconfig`.** ESP-IDF varsayılanları yalnız `sdkconfig` dosyasını ilk ürettiğinde uygular. Tek dosya paylaşılsaydı, ikinci profil ilkinin ayarlarıyla derlenir ve bu, açılmayan bir kart olarak görünürdü.

**Neden farklı OTA donanım sürümü.** `hk_manifest`, donanım sürümü kendisininkiyle eşleşmeyen bir manifesti zaten reddediyor. İki kartın farklı flash boyutu ve farklı bölüm tablosu olduğu için, birinin imajı diğerinde yanlış yere iner. Bu mekanizma zaten vardı; ADR-0012'nin yaptığı, iki kartın onu **farklı** doldurmasını sağlamak.

**Neden `0x20000`'ın altı aynı.** `tools/recover.py` korunacak bölümlerin ofsetlerini tablodan okur; aynı ofsetler tek bir kurtarma yordamının iki kartı da kapsaması demektir. Kalibrasyon duvarı (PRD-008) her ikisinde de aynı adrestedir, yani burada test edilen şey gönderilecek şeydir.

## Sonuçlar ve açık koşullar

- **Bu kart hiçbir zaman ürün olmaz.** ADR-0010 yürürlükte, supersede edilmiyor.
- **Bu kartta ölçülen hiçbir sayı ürün kartı için geçerli değildir.** Quad PSRAM'in veri yolu oktalin yarısıdır ve bu kartta 2 MB var, 8 MB değil. Buradan çıkan bir CPU yükü, DMA veya jitter rakamı en iyi ihtimalle karamsar bir sınırdır; buradaki bir başarı orada bir başarı kanıtı değildir.
- **Fiziksel kapılar açılmadı.** `G0`-`G8` bu kartla ilgisizdir: kartta sürücü, amfi, DAC ve batarya yoktur.
- Kanıt: [[../06-testing/devkit-bring-up|geliştirme kartı bring-up kaydı]].
- Ses tarafı GPIO'ları ([[ADR-0011-audio-side-gpio-reservation|ADR-0011]]) bu kartta hiçbir şeye bağlı değildir; `hk_audio` zaten `audio NOT permitted` durumundadır ve mute hatları hiçbir yükü sürmez.
