# crispplay_subs_ocr: altyazı bandı OCR

Aşama 5 (oyun altyazısı çevirisi), uygulama adımları 5c'nin OCR kısmı ve 5e. Tasarımın gerekçesi ve deneme sayıları: `custom/docs/SUBS_SPIKE.md`.

## Ne yapıyor

1. **Bant görüntüsü** (`BandImage.h`): 8 bit BGRA, alfa hep 255. Sunucunun verebileceği biçimlerden çeviriciler: RGBA8/BGRA8, R10G10B10A2 (SDR gibi 8 bite ölçeklenir), RGBA16F scRGB (en büyük kanala Reinhard omzu, renk tonu korunur, sonra sRGB; HDR'de `HdrToneMap::paperWhite` = SDR beyaz düzeyi / 80). 1400 pikselden geniş bant için 2× kutu küçültme (`needsDownscale`, `downscale2x`). Kutular `read()`'e verilen görüntünün pikselleriyle döner; küçültmeyi yapan çağıran taraf koordinatları geri ölçekler.
2. **Windows OCR** (`WindowsOcr.h`): C++/WinRT `Windows.Media.Ocr`. Önce `en-US`, yoksa kullanıcı profil dilleri. Bant `SoftwareBitmap`'in kendi tamponuna tek kopyayla yazılır, bitmap boyut değişmedikçe yeniden kullanılır. Satır kutusu = sözcük kutularının birleşimi.
3. **PP-OCR tanıma** (`PpOcr.h`): PP-OCRv6 small tanıma ağı, ONNX Runtime ile işlemcide, 2 iş parçacığı. Ön işlem RapidOCR 3.10'un birebir karşılığı (kenar sınırı 30–2000 ve 32'nin katı, 48 piksel yüksekliğe OpenCV `INTER_LINEAR` geometrisiyle, BGR, [-1, 1], en az 320 genişliğe sıfırla dolgu). CTC açgözlü çözme; sözlük modelin `character` meta verisinden okunur (18.708 karakter + boş + boşluk).
4. **Hibrit okuyucu** (`HybridOcr.h`): Windows OCR satırları bulur ve okur; satırlar metin sıralarına toplanır, her sıra kenar payıyla (ortadaki sıralar bant ortasına göre simetrik genişletilerek: Windows OCR bazen ilk/son sözcüğü atlıyor) kesilip PP-OCR'a okutulur; her sıra için iki okumadan **sözcüğe daha çok benzeyen** kalır (denemedeki `wordiness`, eşitlikte PP-OCR). İki okumaya da denemenin üç küçük onarımı uygulanır (tek başına "l" → "I", baştaki çöp, iki kez okunan satır). Model yoksa yalnız Windows OCR. "Beyaz yazı" ön işlemi yalnız ham okuma boş çıkınca yedek olarak çalışır (gerçek klipte her karede uygulanınca okumayı bozuyordu).

Satır seçimi (altyazı bloğu, ipucu/görev yazısı ayıklama), dengeleyici ve fuller OcrNoise metin hattının işi (`custom/subs/core`); bu kütüphane karedeki bütün satırları `OcrFrame` olarak verir. `OcrTypes.h`, core'un `crispplay/subs/Types.h`'si görünür olunca onu kullanır; o zamana kadar aynı tanımların yerel kopyası durur (birleştirmede silinir).

**İş parçacığı kuralı:** okuyucular bir işçi iş parçacığında kurulur ve çağrılır. COM'u başlatılmamış iş parçacığı çok iş parçacıklı daireye (MTA) alınır; tek iş parçacıklı daire (Qt arayüz iş parçacığı) reddedilir, hata döner, beklemez.

## Bağımlılıklar

| Bileşen | Sürüm | Lisans | Nereden | Nasıl |
|---|---|---|---|---|
| Windows.Media.Ocr + C++/WinRT | Windows SDK 10.0.26100 | Windows bileşeni | Windows ile gelir | `windowsapp` |
| ONNX Runtime (CPU) | 1.31.1 | MIT | `https://github.com/microsoft/onnxruntime/releases/download/v1.31.1/onnxruntime-win-x64-1.31.1.zip`, SHA-256 `5869e26bb25314375f2d5ef1877e3ea06b1543d6c710e6e4a1ec2f7177ab4b2c` (GitHub'ın yayımladığı özetle aynı) | `deps\onnxruntime-1.31.1\`; yalnız başlıklar derlemede, `onnxruntime.dll` çalışırken `LoadLibraryEx` ile |
| PP-OCRv6 small tanıma ağı | RapidOCR 3.10.0 model deposundaki `PP-OCRv6_rec_small.onnx` (21.234.383 bayt) | Apache-2.0 (PaddleOCR) | `https://www.modelscope.cn/models/RapidAI/RapidOCR/resolve/v3.10.0/onnx/PP-OCRv6/rec/PP-OCRv6_rec_small.onnx`; denemede `deps\tools\subs-venv` içindeki RapidOCR paketinin indirdiği dosya | `deps\models\ppocr-rec\`, SHA-256 `6f327246b50388f3c176ae304bd95767ea6dc0c9ae92153ef8cbe210b3c14884` |
| Sözlük (bilgi için) | modelin `character` meta verisi | Apache-2.0 | modelden çıkarıldı | `deps\models\ppocr-rec\PP-OCRv6_rec_small_dict.txt`, SHA-256 `b5f2bfe2bdd9448429e3e82b51c789775d9b42f2403d082b00662eb77e401c5d`; kod meta veriyi kullanır, dosya gerekmez |

- ONNX Runtime uygulamaya bağlanmaz (içe aktarma kitaplığı yok): DLL çalışırken okuyucu kurulurken yüklenir. DLL yoksa ya da eskiyse (System32'deki Windows ML kopyası gibi) okuyucu Windows-only kalır, uygulama açılmaya devam eder. DLL aranan yer `HybridOptions::runtimeDir`, boşsa çalışan EXE'nin klasörü; CMake, bench ve testlerin yanına `onnxruntime.dll` kopyalar. Uygulama paketine ve `THIRD_PARTY_NOTICES`'e eklenmesi 5d/5f işi.
- Model ve DLL repoya girmez; model ilk açılışta CrispPlay sürüm dosyasından indirilecek (5d, bu adımın işi değil).
- CMake ONNX Runtime'ı bulamazsa `CrispPlay subs OCR: ONNX Runtime not found ...; Windows OCR only` yazar ve kütüphane Windows-only derlenir. Yol `-DCRISPPLAY_ONNXRUNTIME_ROOT=...` ile değişir.

## Ölçümler (2026-11-12, bu makine, `crispplay_ocr_bench`)

Gerçek klip (Mafia: The Old Country, 1080p, 73 altyazı, 421 altyazılı + 406 altyazısız kare, bant 1383×378). Değerlendirme `real_report.py`'nin birebir karşılığı (sarı piksel sayısıyla altyazılı kare, alt bloktaki ortalı satırlar, büyük/küçük harf ve noktalama dışı tam eşleşme).

| okuyucu | kare başına doğru % | CER % | altyazı başına ilk kare % | ilk gönderilen % | ekranda kalan % |
|---|---|---|---|---|---|
| Windows OCR ham (deneme: 75,1 / 84,9 / 86,3) | 75,1 | 14,1 | 84,9 | 84,9 | 86,3 |
| **Windows-only mod** (model yok) | 75,8 | 13,5 | **86,3** | 86,3 | 87,7 |
| **Hibrit, kütüphane çıktısı** (deneme combo+fix: 82,7 / 94,5 / 95,9) | **85,7** | **11,0** | **94,5** | **97,3** | **97,3** |

- Hedef (altyazı başına ≥ %93) tuttu; Windows-only denemedeki %86'yı yeniden üretiyor. Kare başına sonuç denemeden iyi (82,7 → 85,7) çünkü seçim blok yerine satır satır yapılıyor.
- Altyazısız 406 karenin 22'sinde (%5,4) yazı okundu: denemeyle aynı (ipucu, mektup, görev satırı; ayıklama metin hattının işi).
- Süre (`read()` toplamı, altyazılı kare; üç koşunun aralığı): p50 32–34 ms, p95 64–75 ms. Windows OCR p50 8–9 ms (p95 25–29), PP-OCR (bulunan satırlar, 2 iş parçacığı) p50 22–24 ms (p95 49–57). Altyazısız kare p50 ~17 ms (boş okumada beyaz yazı yedeği ikinci bir Windows OCR okuması yapar). Deneme (Python): ~10 / ~26 / toplam ~36 ms, p95 ~83.
- Bellek: bench sürecinin tepe çalışma kümesi 124 MB (Qt, görüntü yükleme, Windows OCR, ONNX Runtime ve model birlikte); okuyucunun kendi payı ayrıca ölçülmedi.

Sentetik set (256 kare, 6 stil; "yazısız zemin" = arkasında başka yazı olmayan 202 kare): Windows OCR ham %57 → kütüphane %80 (CER 12,5 → 9,2); tümü %48 → %67. Denemenin hibriti %88'di, ama o ölçüm her karede beyaz yazı okumasını da kullanıyordu (sentetik beyaz yazıda yardımcı, gerçek klipte zararlı); ürün kararı gerçek klibe göre.

`ppocrTrustAbove` (güveni yüksek PP-OCR okuması puanlamasız kazanır) denendi: 0,95–0,97'de sentetik %80 → %83, ama gerçek klipte kare başına %85,7 → %83,8. Varsayılan kapalı.

Ölçülmedi: video hattı (SR/NR/FG) çalışırken süre; okuyucunun tek başına bellek payı; başka oyun/altyazı stili; ONNX Runtime yokken CMake yapılandırması (yalnız `CRISPPLAY_SUBS_HAVE_ORT=0` ile `PpOcr.cpp`'nin derlendiği denendi; çalışma anında DLL/model yoksa Windows-only'ye düşme birim testte var).

Satır birleştirme denemeden farklı: deneme ham ve beyaz yazı okumalarının kutularını üst üste bindirdiği için aynı yükseklikteki her şeyi tek sıra yapıyordu. Burada tek okuma var; aynı yükseklikte ama iki satır boyundan uzak parça (HUD sayacı, sokak adı) ayrı sıra kalır, ortalı bir sıranın simetrik kesimi içine düşen parça ise ona katılır (ortadaki sözcükleri atlanmış altyazı). Bu, sentetik sette %64'ten %80'e çıkardı, gerçek klipte sonucu değiştirmedi.

## Derleme ve çalıştırma

```powershell
# uygulama + birim testleri (crispplay_subs_ocr_tests -Test listesinde)
powershell -NoProfile -ExecutionPolicy Bypass -File custom\scripts\build.ps1 -Test
# bench: hedefi build.ps1'in ürettiği out\crispplay-build.cmd ortamıyla derleyin, sonra
out\build\x64-release\custom\subs\ocr\crispplay_ocr_bench.exe              # gerçek klip + sentetik
out\build\x64-release\custom\subs\ocr\crispplay_ocr_bench.exe --windows-only --no-synthetic
out\build\x64-release\custom\subs\ocr\crispplay_ocr_bench.exe --verbose     # kaçan altyazıları yazar
```

Bench verileri `deps\subs-real\` (kareler, `transcript.tsv`, karşılaştırma için denemenin `real.json`'u) ve `deps\subs-testset\` altından çalışırken okunur. Gerçek klip bir oyunun metni ve görüntüsü: repoya girmez, `--verbose` çıktısı da paylaşılmamalı. Birim testleri oyun içeriği kullanmaz (metin testte çizilir); Windows OCR ya da model yoksa ilgili testler atlanır.

## Ayarlar arayüzü için uyarı

`WindowsOcrReader::probe("en-US")` (işçi iş parçacığında) kurulu OCR dillerini ve İngilizcenin olup olmadığını verir. `english == false` ise ayarlarda gösterilecek uyarı:

> İngilizce OCR dil paketi kurulu değil; altyazılar okunamaz ya da hatalı okunur. Ayarlar > Saat ve dil > Dil ve bölge > English (United States) > Dil seçenekleri > Optik karakter tanıma'dan kurun. (PowerShell, yönetici: `Add-WindowsCapability -Online -Name "Language.OCR~~~en-US~0.0.1.0"`)

`available == false` ise hiç OCR dili yok; özellik açılmamalı. PP-OCR kapalıysa (`ppocrStatus()` boş değil) özellik çalışır ama doğruluk %86 civarında kalır; bunu da göstermek yararlı.
