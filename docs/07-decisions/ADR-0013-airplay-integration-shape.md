---
status: accepted
decision: accepted
owner: firmware-engineer
reviewers: [orchestrator, verifier]
updated: 2026-09-05
tags: [adr, airplay, vendoring, licence, firmware]
---

# ADR-0013: AirPlay yığınının entegrasyon biçimi

## Bağlam

[[ADR-0007-airplay-stack|ADR-0007]] yığını seçti ama entegrasyonu ertelemiş ve nedenini yazmıştı: yukarı akış bir kütüphane değil, **kendi `app_main`'i olan tam bir uygulama**. İki `app_main` bir arada bulunamaz, ve onlarınki bizim kabul edilmiş kararlarımızla çakışıyordu. Karar, "gerçekten test edilebildiği zaman" verilmek üzere bırakılmıştı.

Test edilebilir donanım geldi ([[ADR-0012-n8r2-bringup-target|ADR-0012]]).

## Karar

Yığın, `firmware/components/hk_airplay/vendor/` altına sabitlenmiş commit'ten (`38027441ff4327611d26153a8e8b06636cdf009f`, v0.2.0) **birebir** kopyalanır. Vendor ağacında tek satır değişiklik yoktur.

Yukarı akışın `app_main`'i **alınmaz**. Onunla birlikte alınmayanlar: Wi-Fi yöneticisi, web sunucusu, captive DNS, LED sürücüsü, buton, kart profili, ekran, TI DAC sürücüleri, Ethernet ve A2DP. Bunların hepsinin karşılığı bu firmware'de zaten var ve sahibi bellidir.

Alınan dosyalar bir bileşen olarak derlenir; `hk_airplay_start()` yukarı akışın `start_airplay_services()` işlevini **yeniden yazar**, kopyalamaz. Çağrı sırası onlarındır çünkü taşıyıcıdır; sapmalar bilinçlidir ve kod içinde gerekçelidir.

## Ölçülen

Bu ADR'nin dayandığı sayılar tahmin değil:

| | Değer |
|---|---|
| Yukarı akış deposu | 18 MB (`components/` 17,3 MB'ı kullanmadığımız TI amfi blob'ları) |
| Vendor edilen | **81 dosya, 776 KB** |
| Alınmayan dosyaların bıraktığı bağ | **iki fonksiyon** |
| Uygulama imajı (geliştirme kartı) | 1.642.880 B; `0x2e0000` slotun **%45,5'i boş** |
| Ürün imajına etkisi | **sıfır** — `CONFIG_HK_AIRPLAY=n`, ikilide tek AirPlay dizesi yok |
| Boş PSRAM (ağa katıldıktan sonra, ölçüm) | 2.094.848 B |
| Yığının jitter tamponu (kaynaktan) | 1.416.000 B — boşun %67,6'sı |

"İki fonksiyon" şu demek: derlenen kaynaklar, almadığımız modüllerden yalnız `wifi_get_mac_str()` ve `led_audio_feed()` çağırıyor. İkisi de `shim/` altında karşılanıyor; MAC, kimliğin geri kalanıyla aynı eFuse'dan okunuyor, LED beslemesi ise düşürülüyor çünkü LED'in bu firmware'de tek sahibi var (`hk_ui`) ve ikinci bir yazıcı, arıza durumunda yalan söyleyen bir gösterge üretirdi.

## ADR-0007'nin notunda düzeltilmesi gerekenler

O not 2026-08-31'de, kaynağı okumadan önce yazılmış üç şeyi fazla karamsar söylüyordu:

- **`nvs_flash_erase()` kalibrasyon duvarını delmiyor.** Argümansız çağrı varsayılan `nvs` bölümünü siler; kalibrasyon `factory_cal`'da, ayrı bir bölümde. Zaten o kod yolu da alınmıyor.
- **Çakışma listesi eksikti.** `app_main` ayrıca `wifi_init_apsta()`, `web_server_start(80)` ve bir captive DNS sunucusu başlatıyor. Ama bu listeyi uzatmak kararı zorlaştırmıyor: hepsi aynı çözümle, o dosyaları derlemeyerek düşüyor.
- **17,3 MB blob bir flash maliyeti değildi**, kaynak ağacı boyutuydu. Atmanın kazancı depo boyutunda, imajda değil.

## Sonuçlar

- **Lisans tüm projeyi bağlar.** Yukarı akış ticari olmayan kullanım lisanslı. Bu firmware satılamaz. Lisans metni kapsadığı kodun yanında, `vendor/LICENSE`'ta duruyor.
- **Vendor ağacı yamalanmadığı için yukarı akış güncellemesi bir kopyalama işidir**, birleştirme değil. Bunun bedeli, ayarların bizim tarafta ayrı bir `Kconfig`'de durması.
- **`hk_airplay_start()` ses izni olmayan bir üründe başlamayı reddeder.** Alıcının çıkış katı gerçek pinleri saatler; ürün kartında bunu `G0`/`G2` öncesi yapmak tam olarak o kapıların engellediği şeydir. Geliştirme kartında izin fiziksel: üzerinde DAC de amfi de yok.
- **Ürün yapısı değişmedi.** `CONFIG_HK_AIRPLAY` varsayılan kapalı; açmak ayrı bir adımdır ve kapılara bağlıdır.
- I2S pinleri iki yerde adlandığı için derleyiciye anlattırıldı: `hk_airplay.c` içindeki `_Static_assert`'ler `CONFIG_I2S_*` ile `hk_pins.h`'nin aynı pinleri söylediğini zorunlu kılar.

## Kanıtlanmamış olanlar

- **Hiçbir Apple cihazı bu alıcıya bağlanmadı.** Kanıtlanan: yığın derleniyor, açılıyor, mDNS kaydını yapıyor ve RTSP 7000'i dinliyor.
- Ses çalınmadı. DAC yok.
- `G7` dört cihaz senkronu ölçülmedi ve tek kartla ölçülemez.
- Çalışma zamanı PSRAM tüketimi **akış sırasında** ölçülmedi; yalnız boşta olan ölçüldü ve tamponun aritmetik olarak sığdığı gösterildi.
