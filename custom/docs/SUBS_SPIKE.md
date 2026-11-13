# Aşama 5 denemesi: oyun altyazısı çevirisi (OCR + çeviri)

2026-11-12 · dal `feature/subs` · ölçüm betikleri `custom/subs/spike/`

## Durum

Deneme tamamlandı. Ürün kodu yazılmadı; yalnız ölçüm betikleri ve bu rapor var. Ölçümler bu makinede yapıldı (Ryzen, 12 iş parçacığı, RTX 3060 Ti; OCR ve çeviri yalnız işlemcide). Video hattı (SR/NR/FG) çalışırken ölçüm **yapılmadı**; işlemci payı aşağıda hesapla verildi.

## Sonuç: devam, üç değişiklikle

**Devam edilebilir.** Efe'nin kaydettiği gerçek oyun klibinde (Mafia: The Old Country, 1080p60, 73 altyazı) uçtan uca hat çalışıyor: altyazıların %94,5'i ilk karede, %95,9'u ekranda kalan okumada doğru okunuyor, çeviri bir altyazı için tipik 100 ms sürüyor ve Türkçe satır altyazı göründükten tipik 0,3 sn, kötü durumda 0,6–0,85 sn sonra ekrana gelebilir.

Plandan üç sapma öneriyorum:

1. **OCR tek başına Windows OCR olmasın.** Windows OCR satırı hızlı buluyor (bant başına ~10 ms) ama harf karıştırıyor (m→rn, I→l, y→v). Bulduğu satırı ikinci kez küçük bir PP-OCR tanıma ağıyla (ONNX, 21 MB, işlemcide ~26 ms) okutup iki okumadan sözcüğe daha çok benzeyeni seçmek gerçek klipte satır doğruluğunu %86'dan %95'e, sentetik sette %63'ten %88'e çıkardı. İlk sürüm yalnız Windows OCR ile çıkabilir, ikinci okuyucu ayrı adım olarak eklenir.
2. **Çeviri çalışma zamanı CTranslate2 olsun, CouchSubs'ın ONNX yolu değil.** Aynı model (OPUS-MT `opus-mt-tc-big-en-tr`) CTranslate2 int8 olarak 238 MB (ONNX int8: 531 MB), aynı açgözlü çözmede 2,3 kat hızlı ve 5 kat az bellek kullanıyor. "~530 MB ilk indirme" 259 MB'a iner (model + OCR ağı).
3. **Çeviri kalitesi için beklenti doğru konmalı.** OPUS-MT gerçek oyun diyaloğunda satırların yaklaşık %68'ini iyi, %18'ini anlaşılır ama tuhaf, %14'ünü yanlış çeviriyor (benim okumamla; otomatik puan chrF 62). Denenen yerel alternatifler (NLLB-200 600M, M2M100 418M) hem daha kötü hem 3,5 kat yavaş, yani motor değişmiyor; ama çevirmen arayüzü takılabilir olmalı ki ileride oyun paketi ya da isteğe bağlı çevrimiçi motor eklenebilsin.

Ayrıca: "beyaz yazıyı ayıkla" ön işlemi sentetik karelerde yardımcı oldu ama gerçek klipte okumayı bozdu (%75 → %0,7). Körlemesine uygulanmamalı; yalnız ham okuma boş ya da çöpse denenmeli.

## Ne ölçüldü, neyle

| | Gerçek klip (birincil) | Sentetik set (ikincil) |
|---|---|---|
| Kaynak | `C:\Users\efetn\Downloads\sub_test.mp4`, Mafia: The Old Country, 1920×1080, 60 fps, 414 sn | 5 yerel videodan 30 kare + üzerine çizilmiş altyazı |
| Kareler | saniyede 2 kare, 827 kare; 421'inde altyazı var, 406'sında yok | 256 kare (64 cümle × 4) |
| Altyazı | 73 farklı satır; sarı, ince, ~20 px yüksek, tek ya da iki satır, başında konuşmacı etiketi | 6 stil (beyaz/sarı kenarlıklı, gölgeli, kutulu, ince, serifli), 22–49 px, 1080p ve 720p |
| Zemin | koyu oynanış, parlak kum, siyah bantlı ara sahne | koyu/açık, oyun ve anime; 54 karede altyazının arkasında başka yazı var |
| Doğru metin | Altyazı şeritlerine bakarak elle yazıldı (70 satır gözle, 3 satır iki OCR motorunun birebir aynı okumasıyla) | Çizilen metnin kendisi |
| Türkçe referans | 73 satır, elle | 64 satır, elle |

- Gerçek klibin dökümü bir oyunun metni olduğu için repoya girmedi: `deps\subs-real\transcript.tsv`. Kareler `deps\subs-real\frames\`, ham sonuçlar `deps\subs-real\*.json`, `real_report.md`.
- Sentetik setin cümleleri ve referansları `custom/subs/spike/sentences.json` içinde (bu iş için yazıldı, bir oyundan alınmadı); kareler `deps\subs-testset\`, hangi karenin nasıl çizildiği `testset_manifest.json`.
- Altyazılı kare ayrımı OCR'a bakmadan yapıldı: bu oyunun altyazısı sarı olduğu için altyazı satırlarındaki sarı piksel sayısı kullanıldı. Solarak beliren ve kaybolan kareler de sayıya dahil.
- "Sözcükler doğru" = büyük/küçük harf ve noktalama dışında bütün sözcükler doğru. CER = karakter hata oranı.
- Türkçe referansları ve "iyi / tuhaf / yanlış" değerlendirmesini ben (Claude) yaptım; tek değerlendirici, öznel. chrF ve BLEU tek referansla hesaplandı, mutlak değerleri değil motorlar arası farkı anlamlıdır.
- Sentetik sette altyazı metni OpenSubtitles tarzı kısa cümleler; OPUS-MT tam bu tür veriyle eğitildiği için sentetik çeviri puanı iyimserdir. Karar için gerçek klip sayıları kullanılmalı.

## OCR

### Gerçek klip

Okuyucular: `win_raw` Windows OCR (en-US), bant olduğu gibi; `win_white` beyaz yazı ayıklanmış bant; `hybrid` Windows OCR'ın bulduğu satır kutuları PP-OCR tanıma ağıyla yeniden okunur; `combo` ikisinden sözcüğe daha çok benzeyen; `+fix` üç küçük metin onarımı (tek başına "l" → "I", iki kez okunan satır teke iner, baştaki simge atılır).

Kare başına (421 altyazılı kare):

| okuyucu | sözcükler doğru % | CER % | boş okuma % |
|---|---|---|---|
| win_raw | 75,1 | 14,1 | 9,5 |
| win_white | 0,7 | 62,8 | 18,8 |
| hybrid | 69,8 | 16,3 | 8,6 |
| win_raw+fix | 75,8 | 14,0 | 9,5 |
| hybrid+fix | 74,8 | 15,7 | 8,6 |
| **combo+fix** | **82,7** | **12,1** | 8,1 |

Altyazı başına (73 altyazı, saniyede 2 okuma):

| okuyucu | ilk kare doğru % | ilk gönderilen (art arda aynı iki okuma) % | ekranda kalan (en sık okuma) % | herhangi bir kare % |
|---|---|---|---|---|
| win_raw | 84,9 | 84,9 | 86,3 | 87,7 |
| win_raw+fix | 86,3 | 86,3 | 87,7 | 89,0 |
| hybrid+fix | 91,8 | 91,8 | 94,5 | 98,6 |
| **combo+fix** | **94,5** | **94,5** | **95,9** | 98,6 |

- Siyah bantlı ara sahnelerde ve koyu oynanışta iki okuyucu da neredeyse hatasız. Hatalar iki yerde toplanıyor: parlak kumlu sahne (Windows OCR sözcük atlıyor, ikinci okuyucu çoğunu kurtarıyor) ve altyazının solarak belirdiği ilk kare (yarım okunmuş çöp).
- Kare başına %8 boş okuma büyük ölçüde solma kareleri; altyazı başına sonuçları etkilemiyor çünkü sonraki kare doğru okunuyor.
- Kurtarılamayan 3 altyazı: iki tanesi parlak zeminde italik yabancı sözcükle başlayan satır, biri hiçbir karede doğru okunmayan Sicilyaca tek sözcük.
- Uyarı: `+fix` kuralları bu klipteki hatalara bakılarak yazıldı; "l" → "I" CouchSubs'ta da olan genel bir kural, ama kazancın bir kısmı bu klibe özgü olabilir.

Altyazı olmayan 406 karenin 22'sinde (%5,4) "altyazı bloğu" diye yazı seçildi: bir oyun ipucu (15 kare), ekranda açılan bir mektup (4 kare), görev satırı (2 kare), bir tuş ipucu (1 kare). Bunlar çevrilirse ekranda yanlış yerde Türkçe yazı çıkar; CouchSubs'ın tuş ipucu ve menü ayıklama kuralları tam bunun için var. Altyazı varken aynı bantta duran görev satırı ve tuş ipucu "en alttaki ortalı blok" kuralıyla doğru ayıklandı.

Süre (gerçek kareler, 1383×378 piksellik bant, Python içinden):

| adım | p50 ms | p95 ms |
|---|---|---|
| Windows OCR, tek okuma | 9,8 | 22,7 |
| PP-OCR tanıma, bulunan satırlar (2 iş parçacığı) | 25,6 | 60,2 |
| Altyazısız karede PP-OCR | 0,0 | 19,5 |
| Windows OCR + PP-OCR toplam | ~36 | ~83 |

### Sentetik set

Tek kare, Windows OCR, kırpma ve ön işleme göre (256 kare; "orta" = ortadaki satırlar):

| kırpma / ön işlem | sözcükler doğru % | CER % | boş % | p50 ms | p95 ms |
|---|---|---|---|---|---|
| tam kare / ham | 29 | 37,0 | 5 | 30,8 | 78,4 |
| tam kare / beyaz yazı | 55 | 17,7 | 7 | 16,0 | 41,1 |
| bant / ham | 48 | 21,4 | 1 | 7,6 | 15,3 |
| bant / 2× büyütme | 45 | 24,4 | 2 | 21,9 | 44,2 |
| bant / gri, ters çevrilmiş | 48 | 21 | 0 | 8 | 16 |
| bant / Otsu eşikleme | 38 | 34,0 | 9 | 6,9 | 13,7 |
| bant / beyaz yazı | 59 | 14,1 | 4 | 5,5 | 12,0 |
| bant / ikisinden iyisi (ham, beyaz yazı) | 54 | 18,3 | 1 | 13,3 | 25,7 |
| dar şerit / ham | 59 | 12,8 | 1 | 2,7 | 6,1 |
| dar şerit / Otsu | 68 | 11,3 | 3 | 2,4 | 5,5 |
| dar şerit / ikisinden iyisi | 63 | 9,4 | 1 | 5,3 | 10,8 |

- "Bant" CouchSubs'ın varsayılan bölgesi (yükseklikte %62–97, genişlikte %14–86). "Dar şerit" altyazının yeri bilindiğinde kullanılacak kırpma (altyazı kutusu + 0,6 satır pay); burada doğru konumdan alındı.
- Windows OCR süresi piksel sayısıyla doğru orantılı: megapiksel başına 14–25 ms. Tam 1080p kare 31–49 ms, bant 8–10 ms, dar şerit 3 ms.
- 2× büyütme yardımcı olmadı, üç kat pahalı. Gri ve ters çevirme fark yaratmadı. Otsu yalnız dar şeritte işe yarıyor.
- Arkasında başka yazı olan 54 kare sonucu çok aşağı çekiyor (o karelerde sözcükler doğru %20, kalan 202 karede %63).

Stile göre, arkasında yazı olmayan 202 karede, bant:

| stil | Windows OCR sözcükler doğru % | Hibrit sözcükler doğru % |
|---|---|---|
| kutulu beyaz | 97 | 97 |
| kenarlıklı beyaz | 78 | 97 |
| gölgeli beyaz | 68 | 85 |
| kenarlıklı sarı | 64 | 83 |
| ince beyaz | 39 | 73 |
| serifli kenarlıklı | 32 | 94 |
| **hepsi** | **63** | **88** |

Aynı altyazıyı art arda karelerde okumak (64 cümle, 250 ms arayla 6 kare, yalnız Windows OCR): tek okuma %53, ilk 3 okumanın ortak okuması %64, 6 okumadan en az biri doğru %73. Yani bekleyip yeniden okumak yardımcı oluyor ama Windows OCR'ın bazı yazı tiplerindeki sistematik hatasını gidermiyor; onu ikinci okuyucu gideriyor.

### Alternatif: RapidOCR (PaddleOCR PP-OCRv6 small, ONNX Runtime, işlemci)

| | sözcükler doğru % (yazısız zemin) | CER % | p50 ms | p95 ms |
|---|---|---|---|---|
| Windows OCR, bant, iki okumanın iyisi | 63 | 10,4 | 13,3 | 25,7 |
| RapidOCR tam hat (bulma + tanıma), bant | 60 | 18,3 | 376 | 506 |
| RapidOCR tam hat, dar şerit | 48 (tüm kareler) | 8,9 | 551 | 839 |
| Windows OCR bulma + PP-OCR tanıma (hibrit) | 88 | 6,1 | ~36 | ~83 |

PP-OCR harfleri Windows OCR'dan iyi okuyor ama yazı bulucusu işlemcide 350–550 ms sürüyor, bütün çekirdekleri kullanıyor ve satırı sözcük sözcük parçalayıp bazı parçaları iki kez veriyor. Bulucuyu atıp yalnız tanıma ağını kullanmak hem hızlı hem en doğru sonuç. Tesseract denenmedi (kurulum gerektiriyordu; PP-OCR yeterince bilgi verdi).

### OCR için karar

- Varsayılan bölge CouchSubs'ınki; kullanıcı ayarlardan değiştirebilmeli. Altyazı bulunduktan sonra kırpmayı daraltmak okumayı 3 kat ucuzlatır ve arka plan yazısını dışarıda bırakır (sonraki iyileştirme).
- Windows OCR: satır bulucu ve birinci okuyucu. Windows ile geliyor, `en-US` paketi bu makinede kurulu; kurulu olmayan makinede ayarlarda uyarı gerekir.
- İkinci okuyucu: PP-OCR tanıma ağı (PP-OCRv6 small, 21,2 MB ONNX, Apache-2.0), ONNX Runtime ile (MIT; vcpkg'de `onnxruntime` portu var). Bulunan her satır için bir çağrı.
- Seçim: sözcüğe daha çok benzeyen okuma. Ön işlem yalnız yedek.

## Çeviri

Hepsi CTranslate2 4.9, int8, işlemci, ışın genişliği 4, altyazı cümle cümle çevriliyor. Süre bir altyazının tamamı için.

| model | disk | bellek | yükleme | p50 ms | p95 ms | chrF gerçek | chrF sentetik | lisans |
|---|---|---|---|---|---|---|---|---|
| **OPUS-MT tc-big en-tr**, 2 iş parçacığı | 230 MB | 257 MB | 0,25 sn | 100 | 154–169 | **62,2** | **76,1** | CC BY 4.0 |
| aynı, 4 iş parçacığı | | | | 66 | 100 | | | |
| aynı, 1 iş parçacığı | | | | 161 | 249 | | | |
| aynı, açgözlü çözme | | | | 78 | 126 | | 72,0 | |
| aynı, altyazı tek parça | | | | 111 | 164 | | 72,7 | |
| NLLB-200 distilled 600M | 617 MB | 682 MB | 0,62 sn | 358–365 | 535 | 58,0 | 61,8 | CC BY-NC 4.0 |
| M2M100 418M | 476 MB | 517 MB | 0,47 sn | 325–356 | 488–539 | 44,0 | 46,9 | MIT |
| OPUS-MT, ONNX Runtime int8, açgözlü (CouchSubs'ın yolu) | 531 MB | 1330 MB | 1,1 sn | 179 | 263 | | 72,1 | CC BY 4.0 |

- Yükleme süresi dosya önbellekteyken ölçüldü; bilgisayar açıldıktan sonraki ilk yükleme diskten 238 MB okur, ölçülmedi (SSD'de 1–2 sn beklenir). İlk çeviri 65 ms.
- Önbellekten gelen çeviri 0,003 ms.
- `Helsinki-NLP/opus-mt-en-tr` diye bir model yok; en→tr için yalnız `tc-big` var. CouchSubs da onu kullanıyor (onnx-community dönüşümü, 135,7 + 395,7 MB = README'deki "~530 MB").
- CouchSubs'ın iki gözlemi doğrulandı: altyazıyı tek parça çevirmek puanı düşürüyor (76,1 → 72,7), açgözlü çözme de (76,1 → 72,0).
- CTranslate2 Python paketi Intel MKL ile derlenmiş. MKL GPL-3 ile dağıtılamayacağı için üründe OpenBLAS ya da ruy ile derlenmeli; o derlemenin hızı **ölçülmedi** (uygulama adımı 5a'nın ilk işi).

### Kalite

| | iyi | anlaşılır ama tuhaf | yanlış |
|---|---|---|---|
| Gerçek klip, 73 satır | 50 (%68) | 13 (%18) | 10 (%14) |
| Sentetik, 64 cümle | 47 (%73) | 12 (%19) | 5 (%8) |

OCR hatasının çeviriye etkisi (gerçek klip): doğru metinden chrF 62,2; OCR'ın okuduğu metinden 61,3. Altyazıların %91,8'inde çeviri doğru metinden üretilenle birebir aynı. Sentetik sette yalnız Windows OCR ile chrF 76 → 62, hibrit ile 70 (yazısız zeminde 74).

Sık görülen hata türleri:

- **Deyim ve argo kelime kelime çevriliyor.** Gerçek klipte "hold-up" soygun, "washed up" yıkanmak, "crew" mürettebat, "deposit" depozit oldu; sentetikte "I didn't sign up for this" kayıt yaptırmak, "Hold the line" sırayı korumak. Yanlışların çoğu bu türden.
- **Özne düşünce kişi kayıyor.** Öznesiz yazılmış kısa İngilizce cümleler ("Never coming back.") üçüncü kişiye çevriliyor.
- **Sen / siz tutarsız.** Çoğu satır "sen" çıkıyor, ama "Dr. … wants to see you", "Welcome to …", mektup cümleleri "siz" oldu. Tekil-çoğul emir de karışıyor.
- **Özel ad çevriliyor.** "Night City" → "Gece Şehri". Sözlükteki "keep" listesi bunu çözer.
- **Yabancı sözcük ve modelin sözlüğünde olmayan harf.** Sicilyaca sözcükteki "ù" bilinmeyen simge olarak çıktı; böyle sözcükler çevirmene verilmeden aynen taşınmalı.
- **Cinsiyet** sorun olmadı (Türkçede "o"); **kırpılma** görülmedi; sayılar doğru taşındı.

15 örnek (sentetik set):

| İngilizce | OPUS-MT | Referans | |
|---|---|---|---|
| Get down! They're flanking us from the left! | Eğilin! Bizi soldan kuşatıyorlar! | Yere yat! Soldan kuşatıyorlar! | iyi |
| You've got to be kidding me. | Şaka yapıyor olmalısın. | Dalga geçiyor olmalısın. | iyi |
| Arthur, we ain't got much time. The law's coming. | Arthur, fazla zamanımız yok. Kanunlar geliyor. | Arthur, fazla vaktimiz yok. Kanun adamları geliyor. | tuhaf |
| What the hell happened here? | Burada ne oldu böyle? | Burada ne oldu böyle? | iyi |
| Enemy sniper on the rooftop, two o'clock! | Düşman keskin nişancı çatıya, saat iki yönünde! | Çatıda düşman keskin nişancı, saat iki yönünde! | tuhaf |
| Reloading! Cover me! | Dolduruyorum! Koru beni! | Şarjör değiştiriyorum! Beni koru! | iyi |
| Screw this, I'm out of here. | Boş versene, ben gidiyorum. | Boş ver, ben gidiyorum. | iyi |
| Head north, past the river. You can't miss it. | Kuzeye, nehri geç. Bunu kaçıramazsın. | Kuzeye, nehrin ötesine git. Gözünden kaçmaz. | tuhaf |
| Hold the line! Don't let them through! | Sırayı koruyun! Geçmelerine izin vermeyin! | Hattı koruyun! Geçmelerine izin vermeyin! | yanlış |
| You're gonna pay for what you did to my brother. | Kardeşime yaptıklarının bedelini ödeyeceksin. | Kardeşime yaptıklarının bedelini ödeyeceksin. | iyi |
| Dr. Kovacs wants to see you in the lab right away. | Dr. Kovacs sizi hemen laboratuvarda görmek istiyor. | Dr. Kovacs seni hemen laboratuvarda görmek istiyor. | tuhaf |
| Welcome to Night City, the city of dreams. | Gece Şehri'ne hoş geldiniz, hayaller şehri. | Night City'ye, hayaller şehrine hoş geldin. | yanlış |
| I didn't sign up for this. | Bunun için kayıt yaptırmadım. | Ben bunun için yazılmadım. | yanlış |
| Meet me behind the saloon in ten minutes. Come alone. | On dakika sonra salonda buluşalım. Yalnız gel. | On dakika sonra benimle meyhanenin arkasında buluş. Yalnız gel. | yanlış |
| You did good today, son. Your mother would be proud. | Bugün iyi iş çıkardın evlat. Annen gurur duyardı. | Bugün iyi iş çıkardın evlat. Annen gurur duyardı. | iyi |

### Çeviri için karar

- Motor: OPUS-MT `opus-mt-tc-big-en-tr`, CTranslate2 int8, 2 iş parçacığı, ışın 4, cümle cümle. Model resmî Helsinki-NLP deposunun `e539fc16a8` sürümünden `ct2-transformers-converter --quantization int8` ile üretildi; üründe bu dönüşüm CrispPlay sürüm dosyası olarak sunulur ve SHA-256 ile doğrulanır.
- NLLB ve M2M100 elendi: ikisi de daha kötü ve 3,5 kat yavaş; NLLB ayrıca ticari olmayan lisanslı.
- Ekran kartında çalışan büyük bir dil modeli (4 GB sınıfı) denenmedi. Kalitesi büyük olasılıkla daha iyi olur ama 8 GB'lık kartta SR/NR/FG ile bellek için yarışır (hta.mp4 ölçümünde hat 2,4 GB kullanıyordu, 4K'da daha fazla). İstenirse ayrı bir deneme konusu.
- Kaliteyi model değiştirmeden yükselten ve CouchSubs'ta hazır olan parçalar: oyun deyimlerini düz İngilizceye çeviren liste, terim sözlüğü ve ad koruma, kısa satırın önceki satırla birlikte çevrilmesi, Türkçe son düzeltmeler. Bunların etkisi bu denemede ölçülmedi (AGPL kodu çalıştırılmadı, kopyalanmadı).
- CouchSubs'ın oyun paketleri arasında bu klibin oyunu da var (`mafia-the-old-country.json.gz`, 1,25 MB): paketteki satır modele hiç gitmeden hazır Türkçeyle gösteriliyor. Paketlerin içeriğinin kaynağı karışık (hayran çevirileri, Claude ile çevrilmiş metin); dağıtmadan önce ayrıca bakılmalı.

## Uçtan uca gecikme

Okuma aralığı 250 ms (saniyede 4 okuma) varsayımıyla; OCR ve çeviri süreleri ölçüm, diğerleri tasarımdan hesap.

| adım | tipik | kötü durum | kaynak |
|---|---|---|---|
| Altyazının belirmesi → sıradaki okuma | 125 ms | 250 ms | okuma aralığı |
| Bandın ekran kartından kopyalanması ve hazır olması | 20 ms | 35 ms | 1–2 kare (hesap) |
| Biçim çevirme, küçültme | 2 ms | 5 ms | hesap |
| OCR (Windows + PP-OCR) | 36 ms | 83 ms | ölçüm |
| Dengeleyici: noktalamayla bitmeyen ya da çöp görünen satır ikinci okumayı bekler | 0 ms | 250 ms | okuma aralığı |
| Çeviri, önbellekte yok | 100 ms | 170 ms | ölçüm |
| Çeviri, önbellekte var | 0 ms | 0 ms | ölçüm |
| Katmana çizim (arayüz 16 ms'de bir yeniliyor) | 10 ms | 20 ms | hesap |
| **Toplam, önbellekte yok** | **~0,3 sn** | **~0,55 sn; ikinci okuma gerekirse ~0,8 sn** | |
| Toplam, önbellekte var | ~0,2 sn | ~0,4–0,65 sn | |

Sentetik karelerde kırpma + iki Windows OCR okuması + çeviri toplamı ölçüldü: p50 144 ms, p95 304 ms (kopyalama ve bekleme hariç).

2–3 sn ekranda kalan bir altyazı için Türkçe satır sürenin yaklaşık %75–90'ında görünür. Klipte altyazılar ortanca 2,5 sn ekranda kalıyor (0,5–6,5 sn); 73 altyazının 17'si 1,5 sn ya da daha kısa, 7'si 1 sn ya da daha kısa. En kısalarda çeviri altyazı kalkarken ya da kalktıktan sonra gelir. CouchSubs bunu, kalkan satırın çevirisini okuma süresi kadar (en çok 8 sn, 2,5 sn'den geç gelmişse hiç) göstererek çözüyor; aynı kural alınmalı.

İşlemci ve bellek:

- Ekran kartı belleği kullanılmıyor. Ana bellek: çeviri modeli 257 MB; PP-OCR ağı ve ONNX Runtime ölçülmedi (birkaç on MB beklenir).
- OCR saniyede 4 okumada en kötü durumda bir çekirdeğin ~%15'i; değişmeyen bölgeyi yeniden okumayan plan (CouchSubs `ReadScheduler`) bunu çoğu zaman sıfıra yaklaştırır. Çeviri 2–3 sn'de bir satır için 2 iş parçacığında 100 ms, ortalama bir çekirdeğin ~%7–10'u.
- Bandın kopyalanması: 4K çıktıda 2765×756 piksel, RGBA8 için 8,4 MB, 16 bit kayan nokta için 16,7 MB; saniyede 4 kez. Kare başına iş yok.
- Video hattıyla birlikte ölçüm yapılmadı. Hat ekran kartına bağlı (BUILD.md: kaynak karesi başına ~55 ms GPU), işlemcide çözme ve ses var; 12 iş parçacıklı işlemcide çakışma beklemiyorum ama 5c adımında kare süresine etkisi ölçülmeli.

## CouchSubs incelemesi

Depo `github.com/yorulmazsinan/couchsubs`, commit `e9019b36265548695c38cfd8599045e1e0ddd54c` (2026-11-12), AGPL-3.0-only, `deps\couchsubs` altında. İki eş uygulama var: Swift (`core/`, Mac) ve C# (`windows/src/CouchSubs.Core`, `CouchSubs.Translation`); kurallar ve testler aynı. Satır sayıları C# dosyalarından.

Nasıl çalışıyor:

- **OCR:** `Windows.Media.Ocr`, oyun dili yoksa kullanıcının dili. Bölge varsayılanı yükseklikte %62–97, genişlikte %14–86. Her kare hem olduğu gibi hem "beyaz yazı ayıklanmış" okunur, sözcüğe daha çok benzeyen seçilir. Bölge boşsa ortadaki şerit ve bütün bölge 2× büyütülüp yeniden okunur. Saniyede en çok 15 kare; bölgenin 64×12'lik gri imzası değişmediyse okuma atlanır.
- **Ayıklama:** tuş ipuçları, sözcük çöpleri, menü ve köşe göstergeleri atılır; "ortalı ve en alttaki blok" altyazı sayılır.
- **Dengeleyici:** noktalamayla biten satır ilk karede, diğeri art arda iki aynı okumada çevrilir; çöp görünen satır 700 ms bekler; bölge 800 ms boş kalınca altyazı kalkar; 1,5 sn içinde geri gelen satır yeniden çevrilmez. Benzerlik eşiği 0,85 (Levenshtein).
- **Sıra:** aynı anda bir çeviri, en çok 2 bekleyen satır; uzayan cümle (daktilo efekti) süren çeviriyi iptal eder; 2,5 sn'den geç gelen çeviri gösterilmez.
- **Çeviri:** OPUS-MT tc-big en-tr, ONNX Runtime int8, ışın 4, en çok 2 iş parçacığı, cümle cümle. Öncesinde oyun deyimleri listesi ve terim koruma (terim uydurma bir özel adla değiştirilir, çeviriden sonra Türkçe ekiyle geri konur), sonrasında Türkçe düzeltmeler. 3 sözcük ve altı satırlar önceki satırla birlikte çevrilir. Kalıcı önbellek dosyası.
- **Ölçülmüş sayı:** depoda gecikme ya da doğruluk ölçümü yok; yalnız "~530 MB ilk indirme" ve tasarım sabitleri var.
- **Test verisi:** gerçek oyun karesi ya da ekran görüntüsü yok (ekran görüntüleri arayüz maketi). Testler metin düzeyinde: `golden-vectors.json`, `fuzzy-vectors.json`, `pack-matches.json` ve test dosyalarındaki örnek satırlar.

Taşıma haritası:

| Parça | Dosyalar | Satır | Karar |
|---|---|---|---|
| Metin karşılaştırma | `TextMatch.cs` | 173 | Taşı |
| OCR gürültü temizliği | `OcrNoise.cs` | 152 | Taşı |
| Okuma, altyazı bloğu, tuş ipuçları | `Reading.cs` | 177 | Taşı |
| Dengeleyici | `SubtitleStabilizer.cs` | 164 | Taşı |
| Okuma planı | `ReadScheduler.cs` | 63 | Taşı |
| Hat ve çeviri sırası | `SubtitlePipeline.cs` | 349 | Taşı (TV gönderimi yerine katman) |
| Görüntü hazırlığı | `Frame.cs` | 200 | Kısmen (imza, beyaz yazı; yalnız yedek olarak) |
| Cümle bölme | `SubtitleSegmenter.cs` | 192 | Taşı |
| Çeviri metin hattı, kısa satır bağlamı | `FreeTranslation.cs` | 118 | Taşı |
| Oyun deyimleri | `GameEnglish.cs` | 37 | Taşı |
| Sözlük ve terim koruma | `Glossary.cs`, `TermProtection.cs` | 351 | Taşı |
| Türkçe ek ve son düzeltme | `TurkishSuffix.cs`, `TurkishPostEdit.cs` | 257 | Taşı |
| Çeviri önbelleği | `TranslationCache.cs` | 175 | Taşı |
| Model indirme ve doğrulama | `ModelStore.cs` | 221 | Deseni al, Qt ile yeniden yaz |
| Çeviri motoru | `OnnxMarianEngine.cs`, `MarianTokenizer.cs`, `MarianTranslator.cs` | 566 | Taşıma: yerine CTranslate2 + SentencePiece (ONNX'e dönülürse taşınır) |
| Satır yığını (önceki satır üstte solar) | `SubtitleStack.cs` | 143 | İsteğe bağlı, sonra |
| Oyun paketleri | `GamePacks.cs`, `PackFuzzy.cs`, `PackLookup.cs` + 7 paket (28 MB) | 818 | Sonra, içerik lisansı netleşirse |
| Çevrimiçi çevirmenler | `AiTranslator.cs`, `DeepLTranslator.cs` | 679 | Sonra, isteğe bağlı |
| Ortak havuz (satırları sunucularına gönderir) | `LinePool.cs` | 282 | Taşıma |
| TV, Remote Play, altyazı sunucusu, seslendirme, WPF arayüz | `LgTv/`, `SubtitleServer.cs`, `Dubbing/`, `remoteplay/`, `CouchSubs.App/`, `tv-*` | çok | Taşıma |

"Taşı" satırlarının toplamı yaklaşık 2.570 C# satırı; C++ karşılığı 3.000 satır dolayında olur. Bunları koruyan testler 16 dosyada 2.570 satır, 330 kadar örnek; test vektörü olarak `custom/tests` altına alınır.

Lisans notu (hukuki görüş değil): GPL-3'ün 13. maddesi AGPL-3 kodla birleştirmeye izin verir; taşınan dosyalar AGPL kalır, `custom/subs/` altında AGPL başlığı ve atıfla durur, `THIRD_PARTY_NOTICES`'e eklenir (PLAN.md'deki karar). Bu denemede AGPL kodu repoya kopyalanmadı ve çalıştırılmadı; betiklerdeki üç fikir (beyaz yazı ayıklama, sözcüğe benzerlik puanı, tek "l" → "I") tarifinden yeniden yazıldı.

## Entegrasyon taslağı

**Kareyi almak.** `VideoPresenter` sunum komut listesinin içinde `PresentCapture::record(list, frame)` çağırıyor (`src/engine/VideoPresenter.cpp:346`). `frame.texture` grafın çıktısı (SR/NR sonrası, `workWidth × workHeight`, `COMMON` durumunda), `frame.generated` üretilmiş kareyi işaretliyor. Anlık tekrar bu noktaya zaten bağlı: `EngineController.cpp:487`'deki `crispplay::replay::wrapCapture(recorder_.get())` ve `ReplayService.cpp`'deki `CaptureFanout`.

- Yeni kanca gerekmez ya da tek satır değişir: mevcut dağıtıcı genelleştirilir (`custom/include/crispplay/present/Hooks.h`: kayıt, tekrar ve altyazı aynı zincire eklenir), upstream satırı `crispplay::present::wrapCapture(...)` olur.
- `record` içinde, yalnız özellik açıkken, kare üretilmiş değilken ve son örnekten 250 ms geçtiyse: doku `COPY_SOURCE`'a alınır, bant (`D3D12_BOX`) `CopyTextureRegion` ile okuma yığınındaki bir tampona kopyalanır, doku `COMMON`'a döner. 3 yuvalı halka; `submitted(fence, value)` ile gelen çit değeri yuvaya yazılır. Diğer karelerde maliyet bir atomik okuma ve bir saat karşılaştırması.
- Ayrı iş parçacığı çiti bekler (`SetEventOnCompletion`), tamponu eşler, BGRA8'e çevirir (RGBA8, 10 bit ve 16 bit kayan nokta çıktılar; HDR'de basit bir ton eşleme), bant 1400 pikselden genişse 2× küçültür ve OCR'a verir. Aynı desen upstream'de var: `LiveRecorder.cpp:234–299, 411` ve `VideoPresenter.cpp:479`.
- Yakalanan doku pencereye çizilen görüntü değil grafın çıktısı; uygulamanın kendi altyazı katmanı içinde yok. Yani kendi Türkçe satırımızı yeniden okuma riski yok.
- Açık nokta: OCR'ın kaynak çözünürlükte mi SR sonrası çıktıda mı daha iyi okuduğu ölçülmedi. Kanca SR sonrasını veriyor; 720p kaynakta bu büyük olasılıkla lehimize.

**Katmanı çizmek.** Video, QML sahnesinin üstünde duran yerel bir alt pencere (`apps/veyra-qml/main.cpp`); QML video üzerine yarı saydam çizemiyor, yalnız "delik" açabiliyor. Ama upstream'in dosya altyazıları için kullandığı katmanlı alt pencere hazır: `createSubtitleOverlay` / `updateSubtitleOverlay` (`apps/veyra/ui/SubtitleOverlay.h`, GDI+; yazı tipi, boyut, kenarlık, arka plan kutusu, hizalama, konum).

- `QmlPlayerBridge::tickSubtitles()` (`src/ui/bridge/Subtitles.cpp`, arayüz zamanlayıcısıyla 16 ms'de bir) çizilecek satırları `draw` vektöründe topluyor ve `updateSubtitleOverlay(i.subOverlay, draw, view)` çağırıyor. Kanca: bir include + çağrıdan önce `crispplay::subs::appendLines(draw, view);`. Dosya altyazısı yakalama kaynaklarında kapalı (`!s.capture`), ama bizim satır o koşulun dışında eklenir.
- Çeviri, okunan altyazının kutusuna göre konumlanabilir (`posX`, `posY`) ve arka plan kutusuyla İngilizce satırı örtebilir; ya da altına/üstüne yazılır. Ayar olarak sunulur.

**Ayarlar ve kurulum.** `custom/qml/SubsSettings.qml` (aç/kapat, bölge, katman yeri ve boyutu, model indirme, OCR dil paketi uyarısı), kısayol, `main.cpp`'de kurulum.

**Upstream kanca satırları (tahmin):**

| Dosya | Satır | Ne |
|---|---|---|
| `src/engine/EngineController.cpp` | 0 yeni (1 mevcut satır değişir) | Yakalama zinciri genelleşir |
| `src/ui/bridge/Subtitles.cpp` | 2 | include + `appendLines` |
| `apps/veyra-qml/main.cpp` | 2 | include + `crispplay::subs::installQml(...)` |
| `qml/Veyra/SettingsPage.qml` | 2 | Ayar bileşeni `Loader`'ı + kısayol satırı |
| `src/ui/bridge/Preferences.cpp` | 1 | Varsayılan kısayol |

Toplam 7 yeni `VEYRA_CUSTOM` satırı (HOOKS.md sayacı 44 → 51), hepsi 5 satır kuralının içinde.

**Yeni bağımlılıklar:** CTranslate2 (MIT; vcpkg'de portu yok, kaynaktan derlenir, OpenBLAS ya da ruy ile), SentencePiece (Apache-2.0, vcpkg), ONNX Runtime (MIT, vcpkg) ikinci okuyucu için, C++/WinRT (Windows SDK ile geliyor). Modeller: OPUS-MT tc-big en-tr (CC BY 4.0, atıf gerekir), PP-OCRv6 small tanıma ağı (Apache-2.0; RapidOCR paketinin indirdiği dosya, üründe kaynağı ve sürümü sabitlenmeli).

## Uygulama planı

| Adım | İş | Süre | Bitti sayılır |
|---|---|---|---|
| 5a | CTranslate2 + SentencePiece'i `custom/` altında derle (MKL'siz); konsol aracıyla bu raporun 64 + 73 cümlesini çevir, Python sayılarıyla karşılaştır | 2–3 gün | 2 iş parçacığında p50 ≤ 150 ms ve aynı çeviriler. Tutmazsa ONNX Runtime yoluna dönülür (CouchSubs'ın motoru taşınır, +2 gün) |
| 5b | Metin çekirdeğini taşı (haritadaki "Taşı" satırları) ve test vektörlerini `custom/tests`'e al | 3–4 gün | CouchSubs testlerinin karşılıkları geçiyor |
| 5c | Yakalama: bant kopyalama halkası, biçim çevirme, C++/WinRT OCR iş parçacığı, hata ayıklama dökümü | 2–3 gün | SR/NR/FG açıkken kare süresine etkisi ölçüldü; 4K çıktıda p95 kare süresi değişmiyor |
| 5d | Hat + katman + ayarlar + model indirme + kısayol | 2–3 gün | `sub_test.mp4` oynarken Türkçe satır görünüyor |
| 5e | İkinci okuyucu (ONNX Runtime + PP-OCR tanıma) ve okuma seçimi | 2 gün | Klipte altyazı başına doğruluk ≥ %93 |
| 5f | Klip üzerinde geri dönüş testi (dökümle karşılaştırma), ince ayar, `HOOKS.md`, `THIRD_PARTY_NOTICES`, kullanıcı belgesi | 1–2 gün | Bu rapordaki klip sayıları uygulamanın içinden yeniden üretildi |

Toplam 12–17 iş günü; PLAN.md'deki 2–3 haftanın üst ucu. 5e atlanırsa 10–15 gün ve doğruluk %86–88'de kalır.

## Açık sorular

- Çeviri kalitesi (7 satırdan biri yanlış) kabul edilebilir mi? Değilse önce oyun paketleri mi, isteğe bağlı çevrimiçi motor mu, ekran kartında dil modeli denemesi mi?
- Türkçe satır İngilizce altyazının üstünü mü örtsün, altına/üstüne mi yazılsın?
- CouchSubs kodu AGPL başlığıyla mı taşınsın (plan böyle), yoksa kurallar tariften yeniden mi yazılsın (hepsi GPL-3 kalır, 3–4 gün fazla)?
- Model dosyaları nerede barınacak? CTranslate2 dönüşümünü biz üretiyoruz; CrispPlay sürüm dosyası olarak koymak en basiti.
- Başka oyunlardan klip: bu klip tek oyun, tek altyazı stili. Beyaz kenarlıklı altyazılı bir oyun, parlak sahnesi çok bir oyun ve 720p bir yayın (PS5 Remote Play ya da yakalama kartı) kaydı sonuçları sağlamlaştırır.

## Betikler

`custom/subs/spike/` altında, geliştirme aracı. Ortam: `deps\tools\subs-venv` (Python 3.14; `ctranslate2 4.9.0`, `sentencepiece`, `winrt-Windows.Media.Ocr`, `rapidocr 3.10`, `onnxruntime 1.31.1`, `opencv`, `sacrebleu`; yalnız model dönüşümü için `torch`, `transformers`). Hepsi `python -I -X utf8 <betik> …` ile çalışır; kullanım her dosyanın başında.

| Betik | İş |
|---|---|
| `sentences.json` | 64 sentetik cümle ve Türkçe referansları |
| `make_frames.py`, `testset_manifest.json` | Sentetik kare üreticisi ve ürettiği setin dökümü |
| `ocr_bench.py`, `ocr_report.py` | Kırpma × ön işlem × motor ölçümü ve tabloları |
| `ocr_temporal.py` | Art arda karelerde ortak okuma |
| `ocr_hybrid.py` | Windows OCR bulma + PP-OCR tanıma |
| `mt_bench.py`, `ort_greedy.py` | Çeviri süresi, belleği, puanı; ONNX Runtime karşılaştırması |
| `e2e_bench.py` | Sentetik karelerde kırpma → OCR → çeviri |
| `real_scan.py`, `real_report.py` | Gerçek klip: kareleri oku, dökümle karşılaştır |

Modeller `deps\models\` altında: `opus-mt-tc-big-en-tr-ct2-int8` (kendi dönüşümümüz), `nllb-200-distilled-600M-ct2-int8` (JustFrederik, `302d78f00e`), `m2m100_418M-ct2-int8` (jncraton, `7c1b2620a4`), `opus-mt-tc-big-en-tr-onnx-int8` (onnx-community, `ce128ea834`). İlk sentetik OCR koşusu etkenleri birbirine karıştıran bir atamayla yapılmıştı; set yeniden üretildi, eski sonuç `winocr_v1_aliased.json` olarak duruyor ve bu raporda kullanılmadı.

## Demo videosundan çıkan gereksinimler (2026-11-12)

Efe'nin izlediği demo (`deps/subs-real/demo_tr.mp4`, gerçek klip + OCR + OPUS-MT çıktısı, İngilizcenin üstünü kapatan bant) iki hatayı gösterdi; uygulamada ikisi de kural olarak bulunmalı ve test edilmeli:

1. **Bilinmeyen karakter (`⁇`, U+2047).** OPUS-MT'nin SentencePiece sözlüğünde olmayan bir harf (ör. İtalyanca "Strùnzu") çıktıda `⁇` olur ve yazı tipinde olmadığı için kutucuk çizilir. Kural: çeviride `⁇` varsa ve `⁇`'sız hâli kaynağa çok benziyorsa (isim/yabancı kelime) satır **orijinal hâliyle** gösterilir; değilse `⁇` silinir. Ekrana çizilen her karakter yazı tipinde (ya da yedek yazı tipinde) bulunmalı. Ayrıca çeviri öncesi, modelin tanımadığı harfler yer tutucuyla korunup çeviriden sonra geri konabilir (isimler bozulmasın).
2. **Kapatma bandı İngilizceyi tam örtmeli.** Bandın yeri metin eşleştirmesiyle bulunursa (OCR farkı, aksanlı harf) eşleşme kaçar ve İngilizce satır altta görünür. Kural: band, o anki karedeki **altyazı bölgesinde algılanan satır kutularının birleşiminden** (konuma göre, ipucu/görev yazıları hariç) + kenar payıyla hesaplanır ve her OCR okumasında güncellenir; Türkçe metin kısa kalsa bile band İngilizce kutunun tamamını kaplar.
