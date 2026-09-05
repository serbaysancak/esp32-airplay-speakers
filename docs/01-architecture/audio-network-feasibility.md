---
status: partial
owner: firmware-engineer
reviewers: [orchestrator, qa-engineer]
updated: 2026-09-05
tags: [airplay, feasibility, gate]
---

# AirPlay ve senkron fizibilitesi

Yığın seçildi ve gerekçesi [[../07-decisions/ADR-0007-airplay-stack|ADR-0007]]'de kayıtlıdır: `rbouteiller/airplay-esp32`, sabit commit'e sabitlenerek vendor edilir. Entegrasyonun biçimi [[../07-decisions/ADR-0013-airplay-integration-shape|ADR-0013]]'te.

Bu sayfa neyin kanıtlandığını ve neyin hâlâ ölçülmediğini ayırır.

## Kanıtlanan

### Kaynaktan (2026-08-31)

Birincil kaynaklardan (kaynak kodu, lisans dosyaları, Espressif'in kendi kayıtları) doğrulandı:

- Seçilen yığın **gerçek bir AirPlay 2 alıcısıdır**. HomeKit SRP-6a eşleşmesi, FairPlay `/fp-setup`, IEEE-1588 PTP dinleyicisi ve `SETPEERS` / `SETRATEANCHORTIME` sınıfı AirPlay 2 metotları kaynakta mevcuttur. PTP, multiroom gruplamanın saat mekanizmasıdır.
- Yığın **ESP32-S3 üzerinde ESP-IDF v5.5.1 ile derleniyor**: 1.460.192 baytlık imaj, bir OTA slotumuzun %20,3'ü; 136.495 bayt statik DIRAM.
- **Lisanssız bir alıcı için gruplama bilinen sınırlamalar arasında değildir.** `shairport-sync`'in kendi "What Does Not Work" listesi multiroom içermiyor.
- ESP32 için **başka açık AirPlay 2 alıcısı yok**; diğer her şey AirPlay 1 (RAOP). Espressif'in AirPlay'i yok, çoklu-oda çözümü (ESP MRM) iPhone'u kaynak olarak kabul etmiyor.

### Tek kartta ölçülen (2026-09-05)

ADR-0012'nin tanımladığı geliştirme kartında, depodan derlenen imajla. Ham günlük ve karta giden bağlantılar [[../06-testing/devkit-bring-up|bring-up kaydındadır]]; buraya yalnız fizibiliteyi değiştiren satırlar alındı. Hepsi **tek karttır** ve hiçbiri fiziksel bir kapı açmaz.

- **Keşif, cihazın kendi logundan değil, dışarıdan doğrulandı.** Aynı ağdaki bir Mac'in `dns-sd`'si `_airplay._tcp` altında `Harman Kardom 06C4`'ü, `_raop._tcp` altında `A4CB8F9B06C4@Harman Kardom 06C4`'ü buluyor ve `Harman-Kardom-06C4.local:7000`'e çözüyor. TXT kayıtları: `model=AudioAccessory5,1`, `features=0x405C4A00,0x1C340`, `srcvers=377.40.00`, `deviceid=A4:CB:8F:9B:06:C4`.
- **RTSP telde cevap veriyor.** `OPTIONS` → `RTSP/1.0 200 OK`, `Server: AirTunes/377.40.00`, ve `Public` listesi `SETPEERS`, `SETRATEANCHORTIME`, `FLUSHBUFFERED` içeriyor. Kaynakta okunan AirPlay 2 metot kümesi artık cihazın kendi yanıtında da duruyor.
- **Gerçek bir oturum kuruldu.** Sahibinin iPhone'undan müzik çalındı; DMAP meta verisi geldi (albüm, sanatçı, tür, başlık, ilerleme). ADR-0013'ün "hiçbir Apple cihazı bu alıcıya bağlanmadı" satırı bu ölçümle artık geçerli değildir.
- **PTP saati kilitlendi:** `ptp_clock: LOCKED`, `dev=973672 ns`, `samples=62`, `sync=67`, `followup=67`.
- **Alıcı akış sırasında kendi zamanlamasını raporladı:** `audio_time` `err=2 ms`, `buffered=900`, tampon derinliği ~6965 ms, `gaps=0`, `under=25`. Bunlar alıcının kendi sayaçlarıdır, dış ölçüm değil; `gaps=0` iken `under=25`'in neyi saydığı bu oturumda ayrıştırılmadı.
- **Ses ilk kez duyuldu — ama ürünün kendi DAC'ından değil.** Kartta PCM5102A yok. Yığının taşıdığı S/PDIF çıkışı (`GPIO6`, üç pasif parçalı arayüz) kullanıcının kendi DAC'ına verildi ve FiiO Q15 coax girişinde 44,1 kHz'e temiz kilitlendi. Bu, çözme + PTP zamanlama + çıkış zincirinin duyulabilir ilk kanıtıdır; dönüşümü kullanıcının cihazı yaptığı ve bilinmeyen bir sürücü sürülmediği için hiçbir kapı açılmaz. Devre ve direnç değerleri bring-up kaydında.

> `dev=973672 ns` **tek cihazın kendi saat kilididir.** `G7`'nin istediği şey değildir: `G7` iki hoparlörün DAC çıkışı arasındaki `abs(Δt)` ≤ 1 ms'dir ve dört kart ister. Bu iki sayı birbirinin yerine geçemez; birini diğeri sanmak fizibiliteyi olduğundan ileride gösterir.

### Ölçülen bellek payı

Alıcının sığıp sığmadığı imajın boyutuna değil, radyolar yerleştikten ve ağa katıldıktan **sonra** kalana bağlıdır:

| Ölçüm | Değer |
|---|---:|
| Dahili boş (ağa katıldıktan sonra) | 237.843 B |
| En büyük dahili blok | 163.840 B |
| Boş PSRAM | 2.094.848 B |
| Yığının jitter tamponu (kaynaktan hesap: 1000 çerçeve × 1416 B) | 1.416.000 B |
| Tamponun boş PSRAM'e oranı | %67,6 |

Bu satırların ilk üçü ölçümdür; sonuncu ikisi **aritmetiktir**. Alıcının akış sırasındaki toplam PSRAM tüketimi ölçülmedi — mDNS, RTSP tamponları, çözücü ve yeniden örnekleyici hesaba girmiyor. Yani gösterilen şey "sığıyor" değil, "aritmetik önü kapatmıyor".

## Kanıtlanmayan

Aşağıdakiler hâlâ **yapılmadı** ve dört kart ya da ürün donanımı gerektirir:

- **Dört hedefin bir Apple cihazında birlikte seçilebilmesi.** Tek kart var; grup hiç kurulmadı.
- **Cihazlar arası gerçek gecikme ve 2 saatlik kayma.** Yukarıdaki PTP kilidi bunun yerine geçmez.
- **Paket kaybı, yeniden bağlanma ve tek cihazın kapanması davranışı.**
- **Akış sırasındaki heap/PSRAM tüketimi ve CPU yükü.** Ölçülen boş bellek, katılmış ama tüketimi ayrıştırılmamış bir andır.
- **Ürünün kendi ses yolu.** PCM5102A üzerinden hiçbir şey duyulmadı; duyulan şey tezgâh S/PDIF çıkışıdır.
- **Bu kartta ölçülenlerin ürün kartına taşınması.** Geliştirme kartı quad 2 MB PSRAM taşıyor; ürün kartı oktal 8 MB. Buradan çıkan bir zamanlama veya bellek rakamı en iyi ihtimalle karamsar bir sınırdır (ADR-0012).

Bunlar `G7` kapısıdır. Ölçüm yöntemi ve sayısal eşikler [[../07-decisions/ADR-0007-airplay-stack#G7 senkron kabul kriteri|ADR-0007'de]] kilitlenmiştir; özet olarak ölçüm elektrikseldir (DAC çıkışından çapraz korelasyon), akustik değildir, çünkü `1 ms ≈ 34 cm` yayılma gecikmesi mikrofon yerleşimi hatasının altında kalmaz.

## Dağıtım kısıtı: hoparlör ve kaynak aynı L2 ağında olmak zorunda

Bu, tezgâhta bir arıza olarak göründü ve ürün kısıtı olduğu anlaşıldı. Kart yönlendiricinin **misafir ağındayken** ana ağdaki bir Mac'ten hiç görünmedi: `ping` %100 kayıp, `arp` girdisi `incomplete`. Genel bir istemci yalıtımı değildi — aynı Mac aynı subnet'te sekiz başka komşuyu çözüyordu. TP-Link Deco'nun misafir ağı tasarımı gereği yalıtık.

Neden bu bir kurulum ayrıntısı değil: AirPlay keşfi mDNS **çoklu yayınıdır**, senkronun saati PTP **çoklu yayınıdır** (IEEE-1588, grup `224.0.1.129`, UDP `319/320`; ADR-0007). Yalıtılmış bir misafir ağını ikisi de aşmaz. Yönlendirici üzerinden yönlendirme de yetmez; çoklu yayın grubu aynı yayın alanında olmalıdır.

Sonuç, ürün için bağlayıcıdır ve kullanıcı belgesine geçmelidir: **hoparlörler ve çalan cihaz aynı L2 ağında olmalıdır.** Misafir SSID'si, istemci yalıtımı açık bir ağ ve VLAN'lara bölünmüş bir ev ağı çalışmaz. Provisioning akışında kullanıcı hangi ağı seçerse seçsin bu kısıt geçerlidir; ayrıntı [[../controls-and-provisioning-plan|kontrol ve provisioning planında]].

## Örnekleme oranı tavanı: 44,1 kHz / 16 bit

24 bit / 176,4 kHz bir kaynak çalındığında DAC yine `PCM 44k` gösteriyor. Bu doğru davranıştır ve tavan **bu kartta değildir**. Üç yerden geliyor:

1. **AirPlay hi-res taşımıyor.** Tavan protokolde. ADR-0007'de zaten kayıtlı: `shairport-sync`'in "çalışmayanlar" listesinin ilk maddesi HD lossless (96/192 kHz). Dönüştürmeyi iPhone gönderimden önce yapıyor.
2. **Çıkış oranımız sabit:** `CONFIG_OUTPUT_SAMPLE_RATE_HZ = 44100`.
3. **Yol 16 bit:** `audio_output_spdif.c` araya giren PCM'i `int16_t` olarak işliyor.

Donanım sınır değil: S/PDIF taşıma hızı 44,1 kHz'de 5,645 Mbit/s, 96 kHz'de 12,288 Mbit/s, 192 kHz'de 24,576 Mbit/s'tir ve ESP32-S3'ün I2S'i bunu saatleyebilir. Yükseltmenin yolu kaynağı değiştirmekten geçer, bir firmware ayarından değil. Yığın 48 kHz'e ayarlanabilir (yeniden örnekleyici var) ama bu **yukarı örnekleme** olur; 44,1'de olmayan bilgiyi eklemez.

Bu bölüm burada duruyor çünkü soru bu sayfaya gelecek. Cevap ürün açısından şudur: duyulanı belirleyen şey sürücü, kabin, crossover ve amfidir — örnekleme oranı değil.

## Geri çekilme yolu

Seçilen yığın AirPlay 1 (RAOP) yolunu paralel olarak korur ve `CONFIG_AIRPLAY_FORCE_V1` ile AirPlay 2 derleme dışı bırakılabilir. `G7` başarısız olursa cihazlar tek tek AirPlay 1 alıcısı olarak çalışmayı sürdürür; senkron çoklu-oda özelliği düşer ve PRD-002 yeniden müzakere edilir.

## Duran riskler

- FairPlay yanıtları sabit kodludur; bir iOS/macOS güncellemesi eşleşmeyi kırabilir. Bugünkü oturumun kurulmuş olması bunu kapatmaz, yalnız bugünkü iOS sürümünde çalıştığını gösterir.
- Apple AirPlay 2 spesifikasyonunu yayımlamamıştır; her açık uygulama gözlemlenen davranıştan türetilmiştir.
- Yığının lisansı ticari olmayan kullanımla sınırlıdır ve bu projeyi kalıcı olarak bağlar.
- Ağ topolojisi kullanıcının elindedir. Yukarıdaki L2 kısıtı bir arıza değil, kurulum koşuludur; karşılanmazsa cihaz sağlıklı çalışırken bile görünmez.
