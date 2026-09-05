---
status: active
owner: orchestrator
updated: 2026-09-05
tags: [risk, fmea]
---

# Risk kaydı

| Risk | Etki | Olasılık | Azaltma | Sahip |
|---|---|---|---|---|
| AirPlay multiroom senkronu ölçülmedi | Kritik | Orta | Yığın seçildi ve AP2/PTP yeteneği kaynakta doğrulandı (ADR-0007). 2026-09-05'te belirsizliğin bir kısmı kalktı: tek kartta gerçek bir iPhone oturumu çalıştı, PTP kilitlendi ve ses duyuldu ([[../06-testing/devkit-bring-up\|geliştirme kartı kaydı]]). O kilit rakamı (`dev` 973.672 ns) **kartın kendi saatinin** kilididir, cihazlar arası fark değildir — G7'nin istediği dört hoparlör arası ≤1 ms hiç ölçülmedi, elde tek kart var. G7 elektriksel ölçümü; başarısızsa AirPlay 1'e geri çekilme | firmware |
| Hoparlör ile telefon aynı L2 ağında değilse keşif ve saat sessizce çalışmaz | Yüksek | Yüksek | Bugün gerçekten yaşandı: kart yönlendiricinin misafir SSID'sindeyken ana ağdaki Mac kartı `dns-sd` ile hiç görmedi ve ARP girdisi `incomplete` kaldı, aynı Mac aynı subnet'te sekiz komşuyu çözerken. Genel istemci yalıtımı değil, misafir ağının tasarımı. mDNS keşfi ve PTP saati çoklu yayındır, yalıtılmış misafir/IoT ağını aşmazlar; bu firmware'de düzeltilebilecek bir kusur değil, ürünün kurulum koşulu. Cihaz durumu kendi tarafından ayırt edemiyor (birleşme ve mDNS başarılı görünür), o yüzden azaltma kurulum yönergesinde ve ağ seçimi ekranının metnindedir; dört hoparlörün de telefonla aynı SSID'de olması G7'nin önkoşulu. Tanı yordamı [[../06-testing/devkit-bring-up\|geliştirme kartı kaydında]] | firmware |
| FairPlay yanıtları sabit kodlu; iOS/macOS güncellemesi eşleşmeyi kırabilir | Yüksek | Orta | Sürüm sabitleme, vendor edilmiş kopya, kırılırsa AirPlay 1 yoluna geçiş | firmware |
| Seçilen yığının lisansı ticari olmayan kullanımla sınırlı | Orta | Kesin | Kabul edildi ve ADR-0007'ye yazıldı; release asset'leri lisans metnini taşır | orchestrator |
| Sürücü empedansı/rezonansı bilinmiyor | Kritik | Yüksek | G0 ve düşük seviyeli G2 | acoustics |
| Tweeter'a DC/düşük frekans gider | Kritik | Orta | HPF, limiter, mute | hardware |
| BMS ilan akımı gerçek değil | Kritik | Orta | Teknik doğrulama, yük testi, sigorta | power |
| Hücre termal kaçağı | Kritik | Düşük-Orta | Eşleme, NTC, profesyonel puntalama, G4/G5 | power |
| Buck/LED/Wi-Fi ses gürültüsü | Yüksek | Orta | Yerleşim, yıldız toprak, filtre, G3 | hardware |
| Seçilen KM103 güç anahtarının 16,8 V DC kontak değeri belgesiz | Kritik | Orta | Satıcıdan yazılı değer, LED pinini ayırma, akım sınırlı G3 yük/ark testi; başarısızsa DC-rated alternatif | hardware |
| OTA sırasında brick | Yüksek | Orta | A/B OTA, düşük batarya kilidi, G6 | firmware |
| XL4015 şarj sonlandırma garantisi yok | Kritik | Yüksek | G4'te CV akım düşüşü ve sonlandırma ölçümü; sonlandırma yoksa ADR ile alternatif; ölçümden önce gözetimsiz şarj yasak (ADR-0009) | power |
| XL4015'te ters polarite koruması yok | Yüksek | Orta | Kutup etiketleme, bağlantı sırası prosedürü, ilk enerjilendirmede akım sınırlı kaynak | power |
| BMS pasif balans akımı şarj akımından çok küçük | Orta | Yüksek | Balans akımını/eşiğini satıcıdan yazılı al; G4'te hücre sapmasını çok çevrimde ölç | power |
| Cihaz başına provisioning kimlik bilgisi hiçbir cihaza yazılmadı | Yüksek | Kesin | Üretim aracı yazıldı (`firmware/tools/provision_credentials.py`, ESP-IDF'in kendi SRP6a'sını kullanır) ve firmware kimlik bilgisi yoksa provisioning'i açmayı reddediyor, zayıf moda düşmüyor. Kalan risk yazma adımı: hiçbir cihaza salt/verifier yazılmadı, çünkü donanım yok | firmware |
| Kurulum belirli bir mobil uygulamaya bağlı; ADR-0005'in uygulamasız yolu henüz yol değil | Yüksek | Kesin | `wifi_prov_scheme_softap` 192.168.4.1'de yalnız protocomm uç noktalarını sunuyor, sayfa sunmuyor: o ağa katılan kullanıcının önüne hiçbir şey açılmaz ve `HK_PORTAL_TITLE` var olmayan bir sayfayı adlandırır. ADR-0005'in ilk açılış taşıması SoftAP, ama ona karşılık gelen Espressif "ESP SoftAP Prov" uygulaması SRP6a el sıkışması geçtikten sonra AES-GCM katmanında düşüyor (`mbedtls_gcm_auth_decrypt : -18`); ESP-IDF'in kendi `esp_prov.py`'si aynı cihaza aynı kimlikle bağlandığı için kusur firmware'de değil o uygulamada. Çalışan yol 2026-09-05'te iPhone'da uçtan uca doğrulandı ama farklı bir uygulama istiyor: "ESP BLE Provisioning", QR ile. Yani kutuyu açan kişi doğru uygulamayı kurmuş olmak zorunda. Captive portal yazılana veya ADR-0005 revize edilene kadar PRD-004'ün uygulamasız kurulum gereksinimi karşılanmıyor | firmware |
| İlk imzalama anahtarı public depodayken ifşa oldu | Orta | Kesin | **Kapatıldı.** Depo `ktahatastan`'a taşındı ve private yapıldı; anahtar depodan kaldırıldı ve parmak izi `docs/credentials/burned-keys.txt`'e yazıldı. Yayın hattı o anahtarla imzalamayı kalıcı reddediyor — git geçmişinden geri getirilse bile, sınandı. Yerine çevrimdışı üretilmiş, depoya hiç girmemiş anahtar; açık yarısı sabitlenmiş, özel yarısı `release` ortam secret'ında. Sahada cihaz ve yayımlanmış sürüm olmadığı için ifşa penceresinde imzalanmış hiçbir şey yok | orchestrator |
| Canary/stable akışı hiç çalıştırılmadı | Orta | Kesin | Adresler, işaretçi sürümler ve `promote.yml` yazıldı; terfinin aynı ikiliyi taşıdığı yerelde ölçüldü (iki manifest arasında yalnız `channel` farklı). Ama hiç sürüm yayımlanmadı ve hiçbir cihaz güncellenmedi: `G6` matrisinin tamamı açık | firmware |
| AirPlay yığını vendor'lanmadı; F1 boyut kanıtı yeniden üretilemiyor | Yüksek | Kesin | Yığın seçildi ve incelendi (`v0.2.0`, `38027441ff43`) ama depoda değil: F1'in 1.460.192 baytlık ölçümü bugün yeniden üretilemez ve yukarı akış kaybolursa kopya yok. Ertelenmesinin sebebi kopyalama değil, entegrasyonun mimari karar olması — yukarı akışın kendi `app_main`'i bizim NVS duvarımız, LED ve pin katmanlarımızla çakışıyor. Donanım gelince, gerçekten test edilebilirken karara bağlanacak (ADR-0007 notu) | firmware |
| Paralel agent çakışması | Orta | Orta | Tek orkestratör ve tek yazma sahibi | orchestrator |
| Belge sapması: kanonik plan ile şema/BOM ayrışması | Yüksek | Orta | `scripts/check_docs.py` birleşme öncesi zorunlu; mimari değişiklik ADR'siz birleşmez | orchestrator |
