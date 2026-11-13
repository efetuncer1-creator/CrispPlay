# Oyun altyazısı çevirisi (Aşama 5)

2026-11-13 · durum: uygulamaya bağlandı, **varsayılan kapalı**. RTX 3060 Ti üzerinde gerçek oynatmayla (`sub_test.mp4`, 1080p60, 6 dk 54 sn) doğrulandı; ölçümler ve doğrulanmayanlar dosyanın sonunda. Ön çalışma ve kararların gerekçesi: `custom/docs/SUBS_SPIKE.md`.

Ne yapar: oynatılan görüntüdeki İngilizce altyazıyı ekrandan okur (OCR), Türkçeye çevirir ve Türkçe satırı İngilizce satırın üstünü kapatan koyu bir kutuya yazar. Altyazı dosyası gerekmez; video dosyası, yakalama kartı, ekran yakalama ve yayın kaynaklarının hepsinde aynı şekilde çalışır, çünkü okunan şey uygulamanın ekrana verdiği işlenmiş görüntüdür.

## Kullanım

1. **Ayarlar → Altyazı çevirisi** sekmesini aç.
2. İlk kullanımda **Yerel model ve OCR ağı** satırındaki **İndir** düğmesine bas (tek dosya, ~226 MB; açılınca ~262 MB, veri klasöründe `models\`). İndirme yarıda kalırsa kaldığı yerden sürer; her dosya SHA-256 ile doğrulanır.
3. **Oyun altyazısını Türkçeye çevir** anahtarını aç ya da oynatırken **F10**'a bas (Ayarlar → Kısayollar'dan değiştirilir). Açılıp kapandığında kısa bir bildirim çıkar.
4. Altyazı ekranın altında ortada değilse **Okunan bölge**'den uygun ön ayarı seç ya da **Özel** ile yüzdeleri gir.

Windows'un İngilizce OCR dil paketi gerekir. Yoksa sekmede sarı bir uyarı ve kurulum yolu görünür: Ayarlar > Saat ve dil > Dil ve bölge > English (United States) > Dil seçenekleri > Optik karakter tanıma (ya da yönetici PowerShell: `Add-WindowsCapability -Online -Name "Language.OCR~~~en-US~0.0.1.0"`).

### Ayarlar

| Ayar | Seçenekler | Varsayılan | Not |
|---|---|---|---|
| Oyun altyazısını Türkçeye çevir | açık / kapalı | kapalı | Kapalıyken görüntü okunmaz, model bellekte durmaz |
| Okunan bölge | Alt orta (x %14–86, y %62–97) · Alt geniş (%5–95, %58–98) · Alt kenar (%14–86, %76–98) · Üst (%14–86, %3–30) · Özel | Alt orta | Yalnız bu bölge ekran kartından kopyalanır ve okunur |
| Yazı boyutu | Küçük %85 · Normal · Büyük %120 · Çok büyük %140 | Normal | Görüntü yüksekliğinin ~%3,6'sı × oran |
| Kutu opaklığı | %75 · %85 · %92 · %100 | %92 | Kutu her zaman İngilizce satırın tamamını kaplar |
| Motor | Yerel (OPUS-MT) · Çevrimiçi | Yerel | Aşağıdaki gizlilik bölümüne bak |
| Servis, adres, model, API anahtarı | DeepL · OpenAI uyumlu | yok | Yalnız "Çevrimiçi" seçiliyken görünür; **Dene** düğmesi tek bir örnek cümle gönderir |

Ayar dosyası: `<veri klasörü>\crispplay-subs.v1.json` (çevrimiçi motorun ayarı ve şifreli anahtarı ayrı dosyalarda, `custom/subs/net/README.md`). Çeviri önbelleği `<veri klasörü>\subs-cache\`, isteğe bağlı kullanıcı sözlükleri `<veri klasörü>\subs-glossary\*.json`.

## Gizlilik

- **Varsayılan: her şey yerel.** OCR Windows'un kendi bileşeniyle ve bilgisayardaki PP-OCR ağıyla, çeviri bilgisayardaki OPUS-MT modeliyle yapılır. Hiçbir görüntü, metin ya da kullanım bilgisi dışarı gönderilmez. Tek ağ erişimi, senin başlattığın model indirmesidir (GitHub sürüm dosyası; yalnız dosya indirilir, bir şey gönderilmez).
- **Çevrimiçi motor yalnız sen açarsan çalışır**: "Çevrimiçi"yi seçmen, bir servis seçmen ve kendi API anahtarını kaydetmen gerekir. O zaman seçtiğin servise (DeepL ya da girdiğin OpenAI uyumlu adres) **yalnız o an çevrilecek altyazı cümlesinin İngilizce metni** gider. Görüntü, dosya adı, oyun adı, hesap bilgisi gitmez. Yanıt 2,5 sn içinde gelmezse ya da hata olursa o cümle yerel motorla çevrilir.
- API anahtarı Windows DPAPI ile, yalnız senin Windows hesabının açabileceği şekilde saklanır; düz metin olarak yazılmaz, günlüğe girmez.
- Okunan ve çevrilen metin uygulama günlüğüne yazılmaz (yalnız süreler ve sayılar). İstisna: geliştirici tanılama değişkenleri (aşağıda) açıkça verilirse metin ve bant görüntüsü belirtilen yerel dosyaya yazılır.
- Çeviri önbelleği (`subs-cache`) çevrilen cümleleri yerel diskte tutar; silmek için klasörü sil.

## Sınırlar

- Yalnız **İngilizce → Türkçe**. Latin harfli olmayan altyazılar okunmaz.
- Çeviri kalitesi OPUS-MT'nin kalitesidir: deneme ölçümünde yaklaşık 7 satırdan biri yanlış, deyim ve argo kelime kelime çevrilebilir (`SUBS_SPIKE.md`, "Kalite"). Çevrimiçi motor bunun için var.
- Okunan bölgeye düşen başka yazılar (ipucu, görev satırı, mektup) altyazı sanılıp çevrilebilir: test klibinde 85 gösterimin 12'si böyleydi. Bölgeyi daraltmak azaltır.
- Türkçe satır İngilizce satır belirdikten yaklaşık 0,2–0,5 sn sonra gelir; o arada İngilizce satır açıkta görünür. 1 sn'den kısa altyazılarda çeviri satır kalkarken gelebilir (o zaman okuma süresi kadar gösterilir).
- Bir altyazı ekrandayken arkadaki İngilizce satır kutunun altında kalır; kutu opaklığı %100 değilse hafifçe seçilebilir.
- Kutu, oynatıcının kendi altyazı katmanına çizilir: canlı kayıt (F8) ve anlık tekrar (F9) işlenmiş görüntüyü alır, Türkçe satır onlara **girmez**.
- Görüntü döndürülmüşse (90°/270°) ya da "doldur/uzat" görünüm kiplerinde kutunun yeri kayabilir; upstream'in resim altyazısı (PGS) yolu ile aynı eşlemeyi kullanır.
- Kare üretimi (FG) açıkken yalnız gerçek kareler okunur, üretilmiş kareler atlanır.
- CTranslate2 olmadan derlenen uygulamada yerel motor yoktur (sekme bunu söyler, yalnız çevrimiçi motor çalışır); ONNX Runtime yoksa yalnız Windows OCR kullanılır (doğruluk ~%86'ya düşer).

## Nasıl çalışıyor

```
VideoPresenter ──record()──> present::Chain ──> LiveRecorder (upstream)
  (motor iş parçacığı)            ├─> anlık tekrar (ReplayService)
                                  └─> BandCapture: yalnız altyazı bandı, CopyTextureRegion → 3 yuvalı geri okuma halkası
                                         │ çit (fence) değeri + yuva; motor iş parçacığında bekleme yok
                                         v
                              işçi iş parçacığı (SubsService): çiti bekle → eşle → BGRA8'e çevir (HDR ton eşleme, 1400 px üstü 2× küçült)
                                         │                      → HybridOcr (Windows OCR + PP-OCR) → OcrFrame
                                         v
                              SubtitlePipeline (custom/subs/core): blok seçimi, dengeleyici, kapatma bandı
                                         │ çeviri iş parçacığı: çevrimiçi motor (açıksa) → olmazsa OPUS-MT
                                         v
                              Service::current() ──> arayüz iş parçacığı, tickSubtitles() (16 ms):
                                         appendLines() → kutu + metin resmi → upstream'in katmanlı altyazı penceresi
```

**Yakalama (`custom/subs/app/src/BandCapture.*`).** `record(list, frame)` içinde, yalnız özellik açıkken, kare üretilmiş değilken ve son örnekten 250 ms geçtiyse: graf çıktısı `COMMON → COPY_SOURCE`, bant `CopyTextureRegion` ile okuma yığınındaki tampona, sonra `COMMON`'a geri. Tam kare kopyalanmaz. Tampon boyutu bant kadar (1080p'de 2,1 MB, 4K'da 8,4 MB; 16 bit kayan noktalı çıktıda iki katı), üç yuva. Boş yuva yoksa örnek atlanır (`noSlot`); `submitted()`'ın verdiği çit değeri yuvaya yazılır. Diğer karelerde maliyet bir atomik okuma ve bir saat karşılaştırması. Oturum bitince (`sessionEnded`) tamponlar beklemeden bırakılır; işçinin o an okuduğu yuva işi bitince bırakılır. Desteklenen çıktı biçimleri: RGBA8, BGRA8, R10G10B10A2 (SDR ve HDR10/PQ), RGBA16F (SDR ve scRGB); başkası gelirse yakalama durur ve günlüğe bir kez yazılır.

**OCR ve çeviri (`SubsService.*`).** Tek işçi iş parçacığı: en yeni örneği alır (eskiler bayat sayılır), çiti `SetEventOnCompletion` ile bekler (en çok 1 sn), tamponu eşler, `crispplay_subs_ocr` çeviricileriyle BGRA8'e çevirir. HDR çıktıda 203 nit beyaz kabul edilip omuzlu ton eşleme yapılır (HDR10 için 1024 girişli tablo). Bant değişmediyse (duraklatma, sabit sahne) OCR yeniden çalışmaz, önceki okuma kullanılır. Okuma `SubtitlePipeline::process()`'e verilir; çeviri hattın kendi iş parçacığında, aynı anda bir tane. Özellik kapanınca model ve OCR ağı bellekten atılır. 3 sn örnek gelmezse ya da oturum biterse ekrandaki satır silinir.

**Katman (`OverlayRender.*`, `SubsController.cpp`).** QML video üzerine çizemediği için upstream'in dosya altyazılarında kullandığı katmanlı alt pencere kullanılır. `tickSubtitles()`'taki kanca `draw` listesine bir **resim** satırı ekler (PGS altyazılarının yolu): kutu ve metin Qt ile çizilir, 256 renkli palet + uzunluk kodlu satırlara çevrilir, upstream bunu video dikdörtgenine yerleştirir. Böylece kutunun yeri ve boyutu bizde: kutu, hattın verdiği kare-oranlı kapatma bandını (İngilizce satır kutularının birleşimi + pay) her zaman tam kaplar, Türkçe metin daha uzunsa merkezden büyür ve görüntünün içinde kalır. Konuşmacı adı sarı, metin beyaz. Resim, videonun ekrandaki boyutunda üretildiği için metin keskindir; satır ya da pencere boyutu değişmedikçe yeniden çizilmez. Kanca `!s.capture` koşulunun dışında olduğu için yakalama kartı, ekran ve yayın kaynaklarında da çalışır.

**Yazı tipinde olmayan karakterler.** Çizilmeden önce her karakter Segoe UI ve Windows'la gelen yedek ailelerde (Segoe UI Symbol, Arial, Microsoft YaHei UI, Yu Gothic UI, Malgun Gothic) aranır; hiçbirinde yoksa, özel kullanım alanındaysa ya da OPUS-MT'nin bilinmeyen simgesiyse (`⁇`, U+2047) çizilmez. Kutucuk hiçbir zaman görünmez. (Hattın `⁇` kuralı ayrıca isimleri orijinal hâliyle geri koyar: test klibindeki Sicilyaca sözcük doğru görünüyor.)

**Ayarlar ve denetim (`SubsController.*`, `custom/qml/SubsSettings.qml`, `SubsHost.qml`).** `crispSubs` bağlam nesnesi; model durumu `crispplay_subs_net`'in `ModelStore`'undan, çevrimiçi ayar ve anahtar aynı kitaplığın işlevlerinden. Yalnız `ModelStore` "hazır" dediğinde (her dosyanın özeti tuttuğunda) model yüklenir.

### Upstream kancaları

| Dosya | Satır | Ne |
|---|---|---|
| `src/engine/EngineController.cpp` | 0 yeni (3 mevcut satır değişti) | Yakalama zinciri genelleşti: `crispplay::present::wrapCapture` / `sessionEnded` (`custom/include/crispplay/present/Hooks.h`); anlık tekrar zincire `addTap` ile ekleniyor |
| `src/ui/bridge/Subtitles.cpp` | 2 | include + `crispplay::subs::appendLines(draw, view, client)` |
| `apps/veyra-qml/main.cpp` | 2 | include + `crispplay::subs::installQml(engine, &bridge, dataDirectory)` |
| `qml/Veyra/SettingsPage.qml` | 3 | sekme girdisi, `Loader`, kısayol satırı |
| `src/ui/bridge/Preferences.cpp` | 0 yeni (1 mevcut satır) | varsayılan kısayol `{"subs", "F10"}` |

Toplam 7 yeni `VEYRA_CUSTOM` satırı; `HOOKS.md` sayacı 44 → 51. `Main.qml`'e satır eklenmedi (kısayol ve bildirim `custom/qml/ReplayHost.qml` üzerinden yüklenir).

### Derleme ve paket

- `custom/CMakeLists.txt`'te tek satır: `add_subdirectory(subs/app)` (en sonda kalmalı; `subs/core`, `subs/net`, `subs/mt`, `subs/ocr` hedeflerini kullanır). Kitaplık `crispplay_subs_app`, `veyra_qml_ui`'ye bağlanır.
- CTranslate2 yoksa (`deps\ctranslate2-install`) uygulama yine derlenir, yerel motor çıkar. ONNX Runtime yoksa (`deps\onnxruntime-1.31.1`) yalnız Windows OCR. `crispplay_subs_net` derlenmiyorsa (Qt Network yok) `src/NetFallback.cpp` devreye girer: indirme ve çevrimiçi motor yok, modeller yalnız diskte aranır (bu yol birleştirmeden sonra yeniden derlenip denenmedi).
- Derleme `onnxruntime.dll` ve `vcomp140.dll`'yi EXE'nin yanına kopyalar (`crispplay_subs_runtime` hedefi). `custom/scripts/package.ps1` ikisini ve lisans metinlerini (`licenses\crispplay-subs\`) pakete koyar; bildirimler `custom/THIRD_PARTY_NOTICES_CRISPPLAY.md`.

### Test ve tanılama

- Birim testleri: `crispplay_subs_app_tests` (bant geometrisi, biçim çevirme, HDR10 tablosu, zincir dağıtımı, kutu/palet, eksik karakter); `custom\scripts\build.ps1 -Test` içinde.
- Gerçek oynatma: `custom\scripts\subs-test.ps1` uygulamayı ayrı `--data-dir` ile açar, klibi oynatır, saniyede bir işlemci/bellek/GPU örnekler, pencere görüntüsü alır, `-Transcript` verilirse gösterilen satırları dökümle karşılaştırır (`custom/subs/app/tools/subs_regress.py`). Çıktı `out\subs-test\<ad>\`.

  ```powershell
  # tam klip + dökümle karşılaştırma (oyun metni depoya girmez; rapor yalnız sayı içerir)
  powershell -ExecutionPolicy Bypass -File custom\scripts\subs-test.ps1 -RunMs 420000 -Transcript ..\deps\subs-real\transcript.tsv -Name full
  # maliyet: kapalı/açık, efektsiz / SR / FG×2
  powershell -ExecutionPolicy Bypass -File custom\scripts\subs-test.ps1 -Subs off -Effects sr -RunMs 70000
  ```
- Ortam değişkenleri (yalnız geliştirme): `CRISPPLAY_SUBS_TRACE=1` her örnek için bir günlük satırı (süreler); `CRISPPLAY_SUBS_EVENT_LOG=<dosya>` örnekleri ve gösterilen satırları metniyle JSON satırları olarak yazar; `CRISPPLAY_SUBS_DUMP_DIR=<klasör>` okunan her bandı BMP olarak yazar; `CRISPPLAY_SUBS_MODEL_ROOT=<klasör>` modelleri oradan alır (indirme yapılmaz; klasör model paketinin tam içeriği olmalı). Son ikisi ve olay günlüğü **oyun görüntüsü/metni** içerir: depoya ve paylaşılan dosyalara girmemeli.
- Günlükte her 10 sn'de bir `[subs] timing …` satırı: örnek sayısı, süre yüzdelikleri, `record()` süresi, atlanan örnekler.

## Ölçümler (2026-11-13, RTX 3060 Ti, 75 Hz, `sub_test.mp4` 1080p60, pencere 1600×900)

Yöntem: `custom\scripts\subs-test.ps1`, her çalıştırma ayrı `--data-dir` ile. Model paketi `out\subs-models`'e açılmış `crispplay-subs-models-v1.zip`. Ölçüm sırasında masaüstünde başka bir CrispPlay örneği (kullanıcının açtığı, video oynatan) ve başka uygulamalar çalışıyordu; GPU süreleri bu yüzden gürültülü olabilir.

### Tam klip, uygulamanın içinden (özellik açık, efektsiz, 413 sn)

Dökümle (`deps\subs-real\transcript.tsv`, 73 altyazı) karşılaştırma, `out\subs-test\full\regress.json`:

| | |
|---|---|
| Dökümdeki altyazılardan gösterilen | **72 / 73 (%98,6)** |
| İngilizce kaynak satırı dökümle birebir (büyük/küçük harf ve noktalama dışında) | %94,5 |
| Konuşmacı adı doğru | %97,2 |
| Toplam gösterim | 85; 12'si dökümde olmayan yazı (ipucu, görev, mektup), 6'sı çevrilemeyip İngilizce gösterilen |
| Satır kalktıktan sonra gelen çeviri | 0 |
| Altyazının belirmesi → Türkçe satır (dökümün 0,5 sn'lik zaman ızgarasıyla, ±0,5 sn) | ortanca 0,23 sn, %90'ı ≤ 0,5 sn |
| Satırı içeren ilk örnek → Türkçe satır (uygulamanın kendi saati) | ortanca 194 ms, p95 516 ms |

Deneme raporundaki tahmin (~0,3 sn tipik, ~0,55 sn kötü durum) tuttu. Örnekleme aralığı 250 ms olduğu için bunun üstüne ortalama 125 ms "sıradaki örneği bekleme" eklenir.

Örnek başına süreler (1596 örnek, 4 örnek/sn):

| Adım | p50 | p95 |
|---|---|---|
| `record()` (motor iş parçacığı, kopyayı listeye yazma) | ort. 14 µs | en uzun 0,9 ms (ilk tampon oluşturma) |
| Kopya kaydı → ekran kartında hazır | 1,2 ms | 1,8 ms |
| Eşleme + BGRA8'e çevirme (1382×378) | 0,6 ms | 0,7 ms |
| OCR (Windows + PP-OCR) | 24–27 ms | 39–50 ms |
| Çeviri, satır başına (OPUS-MT, 2 iş parçacığı; önbellekte olmayan) | 94 ms | 195 ms |

Atlanan örnek yok (`noSlot=0`, `stale=0`, `failed=0`). Model yüklemesi 0,25 sn (dosya önbellekteyken).

### Maliyet: özellik kapalı / açık

70 sn'lik çalıştırmalar, ilk 12 sn ve son 2 sn hariç saniyelik örneklerin ortancası; `[player-timing]` satırlarının ortancası. Efektsiz ve SR ikişer kez ölçüldü (iki değer), FG birer kez.

| | Efektsiz, kapalı | Efektsiz, açık | SR (1080p → 4K), kapalı | SR, açık | FG×2, kapalı | FG×2, açık |
|---|---|---|---|---|---|---|
| `presentP95Ms` | 0,380 / 0,382 | 0,416 / 0,423 | 0,378 / 0,407 | 0,441 / 0,432 | 0,331 | 0,358 |
| `gpuReadyP95Ms` | 6,14 / 6,16 | 6,12 / 5,85 | 7,31 / 7,35 | 7,39 / 7,36 | 10,21 | 10,18 |
| `gpuBlitP95Ms` | 0,433 / 0,435 | 0,667 / 0,668 | 0,068 / 0,068 | 0,386 / 0,387 | 0,066 | 0,150 |
| `graphSubmitP95Ms` | 0,148 / 0,145 | 0,170 / 0,166 | 0,296 / 0,293 | 0,343 / 0,326 | 1,07 | 1,15 |
| `slotWaits` | 0 | 0 | 0 | 0 | 0 | 0 |
| GPU 3D motoru (süreç) | %7,5 | %7,5–7,6 | %28,7 | %28,7–28,9 | %23,5 | %23,6 |
| GPU belleği (süreç) | 227 MiB | 227 MiB | 868 MiB | 868 MiB | 626 MiB | 626 MiB |
| İşlemci (bir çekirdeğin yüzdesi) | %24,7 | %55,4–55,6 | %26,1–26,2 | %55,2–57,1 | %30,8 | %59,7 |
| Özel bellek | 655–658 MB | 982–984 MB | 1576–1584 MB | 1913–1914 MB | 1381 MB | 1704 MB |
| Çalışma kümesi | 498–500 MB | 836–838 MB | 745–746 MB | 1089–1091 MB | 756 MB | 1092 MB |

Okuma:

- **Sunum yolu**: `presentP95Ms` +0,03–0,06 ms, `slotWaits` 0, `gpuReadyP95Ms` gürültü içinde değişmiyor. 75 Hz'te kare bütçesi 13,3 ms; etki bütçenin %0,5'inin altında. 5c'nin kabul ölçütü ("4K çıktıda p95 kare süresi değişmiyor") SR açıkken tuttu.
- **`gpuBlitP95Ms`** artıyor (efektsiz +0,23 ms, SR'de +0,32 ms, FG×2'de +0,08 ms): bant kopyası sunum listesinin içinde ve saniyede 4 kare (60 karenin %6,7'si) kopya taşıdığı için %95'lik değer tam o kareleri gösteriyor. Yani bu, kopyalı bir karenin GPU'daki ek süresi: 1080p bandı için ~0,2 ms, 4K bandı (2764×756) için ~0,3 ms. Diğer karelerde ek yok.
- **GPU belleği** değişmiyor (okuma tamponları sistem belleğinde: 1080p'de 3 × 2,1 MB, 4K'da 3 × 8,5 MB).
- **İşlemci**: bir çekirdeğin ~%30'u ek yük (OCR saniyede 4 kez + çeviri). Deneme tahmini (%15 + %7–10) bunun altındaydı; fark büyük olasılıkla PP-OCR'ın ve Windows OCR'ın kendi iş parçacıklarından.
- **Bellek**: +327–338 MB (çeviri modeli ~257 MB, PP-OCR ağı ve ONNX Runtime, Windows OCR). Özellik kapatılınca model ve ağ bırakılır.
- **SR açıkken** çıktı 3840×2160 olur, bant 2764×756 kopyalanır ve OCR'dan önce 2× küçültülür (çevirme 3,4 ms). 70 sn'de gösterilen satır sayısı efektsiz çalıştırmayla aynı düzeyde (20'ye karşı 21); 4K'da doğruluk ayrıca dökümle ölçülmedi.
- **FG×2 açıkken** yalnız gerçek kareler okunur; 70 sn'de 20 satır gösterildi, atlanan örnek yok.
- NR + FG×6 bu ekran kartında zaten bütçenin üstünde olduğu için (BUILD.md) denenmedi.

Kısayol: `out\subs-test\toggle\` çalıştırmasında F10 gerçekten basıldı; bildirim ("Altyazı çevirisi açık · İngilizce → Türkçe (yerel)") ve ilk Türkçe satır 1,1 sn içinde ekrandaydı (model yükleme dahil), ikinci F10 kapattı ve ayar dosyasına yazıldı.

## Doğrulananlar / doğrulanmayanlar

Doğrulandı (bu makinede, gerçek uygulamayla):

- Klip oynarken Türkçe satır İngilizcenin üstünde, konuşmacı adı sarı; kutu İngilizce satırı tam kapatıyor (ekran görüntüleri `out\subs-test\full\shot-*.png`, `out\subs-test\first\`).
- Tam klipte dökümle karşılaştırma (yukarıdaki tablo); ayar sekmesi (`out\subs-test\settings-on\shot-006000.png`).
- Efektsiz, SR açık ve FG×2 açıkken sunum süreleri (yukarıda).
- Zincir değişikliğinden sonra anlık tekrar: `custom\scripts\replay-test.ps1` VALID (8,7 sn, 522 kare, ses + görüntü, 0 atlanan kare).
- Model paketi `ModelStore` üzerinden (geliştirici klasörü olarak açılmış zip, özet denetimiyle).

Doğrulanmadı:

- **Yakalama kartı, ekran yakalama, PS5/yayın kaynakları**: kod yolu aynı (sunum zinciri ve katman kaynak türüne bakmıyor), ama gerçek kaynakla denenmedi.
- **HDR çıktı** (scRGB ve HDR10): ton eşleme birim testte; HDR ekranda / HDR içerikle okuma doğruluğu ölçülmedi.
- **AMD / Intel ekran kartı**: yakalama düz D3D12 (satıcıya özel bir şey yok), denenmedi.
- **4K ve üstü çıktıda OCR doğruluğu** (2× küçültülmüş bant) ve 720p kaynak.
- **Gerçek indirme**: model sürümü henüz yayımlanmadı; "İndir" düğmesi yalnız `crispplay_subs_net` testlerinin yerel sunucusuna karşı denendi (o kitaplığın testleri).
- **Çevrimiçi motor** uygulama içinden gerçek bir API anahtarıyla denenmedi (arayüz ve yedeğe düşme yolu derlendi; ağ kodu `crispplay_subs_net` testlerinde).
- Başka oyun / altyazı stili; uzun oturum (saatler) bellek seyri.
