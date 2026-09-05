---
title: N8R2 geliştirme kartı hedefi ve ilk silikon
status: done
owner: firmware-engineer
reviewers: [orchestrator, verifier]
updated: 2026-09-05
tags: [development-log, firmware, devkit, board, ota, wifi]
---

# 2026-09-05 — Firmware ilk kez gerçek bir çipte çalıştı

Ürün kartı hâlâ yok. Elde bir **ESP32-S3 N8R2 sınıfı geliştirme kartı** var, ve bu oturumun işi firmware'i ona uyarlayıp gerçekten çalıştırmaktı. Kanıt ayrı duruyor: [[../06-testing/devkit-bring-up|bring-up kaydı]]. Karar: [[../07-decisions/ADR-0012-n8r2-bringup-target|ADR-0012]].

## Kartta kaynağı olmayan bir firmware bulundu

İlk yaptığım şey karta bakmaktı, ve kart boş değildi. Üzerinde `harman-kardom` 0.1.0 vardı — 3 Eylül'de derlenmiş, `ESP-IDF: GIT-NOTFOUND`, 8 MB'lık bir bölüm tablosu, `gpio48`'de bir WS2812 aynası, ve `hk_airplay` ile `mdns_airplay` bileşenleri. İkiliden çıkarılan diziler `fp-setup`, `pair-setup`, `SETPEERS`, `SETRATEANCHORTIME`, `FPLY`, `ptp`, `srp` içeriyordu: ADR-0007'nin gerçek yığını vendor edilmişti ve kart AirPlay hedefi olarak yayın yapıyordu.

O kaynak ne bu depoda ne de bu makinede var. Kullanıcı doğruladı: başka bir oturumda denenmiş.

Bunun anlamı, işi "zaten yapılmış" saymamak. **Derlenmiş bir ikili kaynak değildir**; depo, projenin kaydıdır ve orada bu uyarlamanın izi yoktu. O yüzden uyarlama sıfırdan, depoda yapıldı.

Silmeden önce 8 MB flash'ın tamamı yedeklendi (sha256 kayıtlı). Bu yedek olmasa, kaynağı olmayan tek kopya bir `erase_flash` ile yok olacaktı — kullanıcı silmeye izin vermişti ama sildiği şeyin ne olduğunu bilerek vermemişti.

## Bir kart dört ayardır

Uyarlama üç satır config değişikliği gibi görünüyor ve öyle değil. Üç ayarın üçü de yanlış eşleştiğinde **geç ve sessiz** patlıyor:

- yanlış flash boyutu imaj başlığına yazılıyor, `ota_1` `ota_0`'ın üstüne biniyor;
- yanlış PSRAM modu açılışta kendi bellek testini geçiyor, sonra yük altında heap'i bozuyor;
- yanlış bölüm tablosu ilk güncellemeye kadar sorunsuz açılıyor.

Hiçbiri derleme hatası değil. Bu yüzden derleme hatasına çevrildiler: dördüncü ayar olan `CONFIG_HK_BOARD_DEVKIT_N8R2` diğer üçünü bağlıyor, `firmware/CMakeLists.txt` uyuşmayanı configure anında reddediyor, ve imzalı bir geliştirme yapısını da reddediyor. Dördüncüsü aynı zamanda OTA donanım sürümünü `devkit-n8r2` yapıyor, yani `hk_manifest` iki kartın imajlarını birbirine kabul etmiyor.

Yorum yerine `if` yazmanın sebebi basit: "dördünü birlikte değiştirmeyi unutma" diyen yorum bir kez bile işe yaramadı.

## Depoda açtığım borçlar, aynı oturumda kapatıldı

İkinci bir hedef eklemek, tek hedef varsayan her aracı sessizce yanlış hale getiriyor. Bağımsız bir çürütme turu bunları buldu ve hepsi kapatıldı:

- **`recover.py`** ürün tablosunu ve `--flash_size 16MB`'ı sabit yazıyordu. Artık `--partitions` alıyor ve flash boyutunu **tablodan türetiyor** (son bölümü kapsayan ilk ikinin kuvveti). Ayrı bir `--flash-size` bayrağı eklemedim: tabloyla çelişebilecek bir bayrak, tam da bu betiğin engellemek için var olduğu hatayı üretir.
- **`make_manifest.py`** `--slot-size` için ürünün `0x6e0000`'ını varsayılan alıyordu. Geliştirme kartının slotu `0x2e0000`; imaj, gideceği flash'tan %76 büyük bir kapıdan geçiyordu. Artık slot donanım sürümünden türetiliyor, açıkça verilen bir değer çelişirse **ikisi de kullanılmıyor**.
- **`check_partitions.py`** 16 MB'ı sabit tutuyordu. `--flash-size` eklendi; varsayılan değişmedi, yani mevcut her çağrı aynı şeyi kontrol etmeye devam ediyor.
- **CI** yalnız ürün tablosunu görüyordu. Artık geliştirme tablosunu da denetliyor, geliştirme profilini de derliyor (guard'lar ancak yapılandırılınca çalışır, yani yalnız derleme onları sınar) ve `release.yml` bir sürümün geliştirme profiliyle yapılmadığını doğruluyor.
- **`__pycache__`** altında bir `.pyc` depoya işlenmişti. `.gitignore` kuralı sonradan eklendiği için izlenen dosyaya işlemiyordu; kaldırıldı.

Testler de yazıldı: `--flash-size` yazımları (çıplak sayı **bayt**tır — `8`'i 8 MB okumak bir yazım hatasını her tablonun geçtiği bir flash boyutuna çevirirdi), iki tablonun kendi flash boyutlarına karşı denetlenmesi, ve `BOARD_SLOT_SIZE` ile bölüm tablolarının **birbirinden sürüklenmediğini** doğrulayan bir eşleşme testi.

## Wi-Fi çalışıyor, ve nasıl test edildiği önemli

Kart ağa girdi: WPA3-SAE, RSSI −44 dBm, `192.168.68.74`, ardından mDNS.

Bunu test edebilmek için kullanıcının Wi-Fi parolasına ihtiyaç vardı ve **parola hiçbir yere girmedi**. Silmeden önce alınan yedekten yalnız `nvs` bölümü (24 KB) geri yazıldı; ESP-IDF'in Wi-Fi sürücüsü SSID/parolayı orada tutuyor. Yani ağ yolu gerçek kimlik bilgileriyle sınandı, ama o kimlik bilgileri ne depoya, ne günlüğe, ne de bir ajanın bağlamına girdi. Sözleşmenin `docs/credentials/` kuralı bu yüzden zorlanmadı bile.

Bir şey açıklanmadı: üç açılışın ikisinde ilk birleşme ~14 s'de düşüyor ve ~20 s'de temiz kuruluyor. Yeniden bağlanma her seferinde toparladı. Kopma sebebi kaydedilmedi; kayıtta "aralıklı" diye geçiştirilmeden duruyor.

## Önceki tezgâh oturumunun notlarından gelenler

Kullanıcı, aynı kartla yapılmış 2026-09-03 tarihli bir oturumun devir notunu paylaştı (`20260903n8r2buttonandprovisioning.md`). O oturumun belgeleri de bu depoda yok. İçindeki iki bulgu hâlâ geçerliydi ve burada kapatıldı:

**Buton, kendi altındaki görevi taşırıyordu.** `on_button()` `hk_ui` görevinde çalışıyor ve `hk_network_open_provisioning()`'i senkron çağırıyordu: NimBLE, protocomm ve bir SRP6a el sıkışması, debounce için ölçülmüş bir yığında. O oturum bunu çalarken basış senaryosunda çökme olarak gözlemlemiş ve başka bir ağaçta düzeltmiş; düzeltme buraya hiç gelmedi.

Burada yapısal olarak düzeltildi: **geri çağrı yalnız niyeti kaydediyor, yan etkileri ana görev uyguluyor.** Bu aynı zamanda gözden kaçması kolay bir asimetriyi kaldırdı — pencere bir görevde açılıp başka bir görevde kapanıyordu. "Yarım kurulmuş duruma gelen basış düşürülür" özelliği aynen korundu: olay ve iş aynı kilit altında, yalnız politika olayı kabul ettiyse kaydediliyor.

Ölçüldü: `hk_ui` görevi 3.072 baytının 2.332'sini hiç kullanmıyor. Yani asıl işi ~740 bayt; oraya BLE yığını koymanın neden taştığı artık bir tahmin değil. **Çökme bu oturumda yeniden üretilmedi** — kartta GPIO7'ye basacak bir şey yok. Düzeltme yapısal, kanıt dolaylı.

**Kimlik bilgisi üreticisi veri kaybettirebiliyordu.** `factory_cal.csv`, girdi dosyalarını çıplak adla veriyor ve `nvs_partition_gen.py` bunları CSV'ye göre değil **çalışma dizinine göre** çözüyor. Depo kökünden çağrıldığında girdiler bulunamıyor, araç yine de bir dosya yazıyor, ve o dosya `0x13000`'a yazılınca oradaki kimlik bilgilerini siliyor — yerine hiçbir şey koymadan. O oturumda tam olarak bu oldu.

Belgeye uyarı yazmak yerine ayak kaldırıldı: `--image` bayrağı imajı **doğru çalışma dizininden** kendisi üretiyor, boyutu (`0xd000`) ve ilk NVS sayfasının varlığını doğruluyor. Kendi testim burada ikinci bir kusur buldu — üretici hata verdiğinde yarım dosya diskte kalıyordu, ki tehlikenin kendisi tam olarak o dosyaydı. Artık hiçbir başarısız yol geride dosya bırakmıyor.

**Kapatılmayan, karar bekleyen iki şey:**

- **Telefon uygulamasıyla provisioning çalışmıyor.** Espressif SoftAP Prov uygulaması SRP6a el sıkışması **tamamlandıktan sonra** AES-GCM katmanında düşüyor (`mbedtls_gcm_auth_decrypt : -18`). ESP-IDF'in kendi referans istemcisi aynı cihaza aynı kimlik bilgileriyle bağlanıyor, yani firmware doğru, uyumsuzluk istemcide. Son kullanıcının kurulum yolu mobil uygulama olduğu için bu bir **ürün riski**dir, geliştirme sıkıntısı değil.
- **SRP6a kullanıcı adı.** Tezgâh kartındaki kimlik bilgileri `wifiprov` ile üretilmiş; `provision_credentials.py` hâlâ `harmankardom` üretiyor. Ekosistemin varsayılanı `wifiprov`. Protokolce özel bir ad geçerli, ama uyumluluk riski. Kaynağı bilerek değiştirmedim: bu bir karar konusu, sessizce yapılacak bir düzeltme değil.

## Ne yapılmadı

- **AirPlay bu depoda hâlâ vendor edilmedi.** Kartta çalışan eski yapı onu içeriyordu; bu, entegrasyonun mümkün olduğunun kanıtı ama depoda var olduğunun kanıtı değil. Sıradaki iş bu.
- Hiçbir fiziksel kapı açılmadı. Kartta sürücü, amfi, DAC, batarya yok.
- Bu kartta ölçülen hiçbir sayı ürün kartına taşınmaz: quad 2 MB PSRAM, oktal 8 MB'ın yerine geçmez.
