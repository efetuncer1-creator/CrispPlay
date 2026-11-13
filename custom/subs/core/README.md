# custom/subs/core: altyazı metin çekirdeği (Aşama 5b)

CouchSubs'ın metin kurallarının C++20 karşılığı: OCR satırlarından altyazı bloğunu seçer, dengeleyiciden geçirir,
çeviri sırasını yönetir ve katmana ne çizileceğini söyler. Yalnız standart kitaplık (Qt yok, WinRT yok; tek istisna
oyun paketi anahtarındaki Unicode normalleştirmesi için Windows `NormalizeString`). Lisans: çoğu dosya AGPL-3.0,
ayrıntı [NOTICE.md](NOTICE.md).

## Durum (2026-11-12)

- Kitaplık `crispplay_subs_core`, test `crispplay_subs_core_tests` (kendi küçük koşucusu, Qt gerekmez).
  `custom\scripts\build.ps1 -Test` ikisini de derler ve koşar. **186/186 test geçiyor**; 5 kez üst üste koşuldu,
  iş parçacıklı hat testleri kararlı.
- CouchSubs testlerinin karşılıkları (TextMatch, OcrNoise, Reading/SubtitleBlock, ButtonPrompts, Stabilizer,
  ReadScheduler, Segmenter, GameEnglish, Glossary, TermProtection, TurkishSuffix, TurkishPostEdit, FreeTranslation,
  RecentLines, TranslationCache, MarianTranslator metin akışı, SubtitlePipeline sıra/iptal/bayatlık) ve ortak test
  vektörleri (`golden-vectors.json`, `fuzzy-vectors.json`) geçiyor.
- Gerçek klip geri oynatması (`deps\subs-real\scan.json`, kimlik çevirmeni): 827 kare → 86 çeviri çağrısı, 77 taslak
  parçanın 71'i bulundu, her kapatma bandı o karedeki satır kutularını kapsıyor. Veri yoksa test atlanır; oyun metni
  ne yazdırılır ne repoya girer.

## Kullanım

```cpp
#include "crispplay/subs/SubtitlePipeline.h"
using namespace crispplay::subs;

MyCt2Translator engine;                 // Translator: bir İngilizce cümle -> Türkçe (nullopt = hata)
PipelineOptions options;
options.translator.cacheDirectory = dataDir / "subs-cache";    // kalıcı önbellek
options.translator.glossaryDirectory = dataDir / "glossary";   // kullanıcı sözlükleri (*.json)
SubtitlePipeline pipeline(engine, [&](const SubtitleUpdate& u) { overlay.post(u); }, options);

// OCR iş parçacığında, her okumadan sonra (OcrFrame: bant pikselleri + bandın karedeki yeri)
pipeline.process(frame);
```

- `SubtitleUpdate::Show`: `line` çizilir (`speaker`, `text`, `cover` bant pikselinde, `frameLeft..frameBottom` karenin
  oranı, `original`). `durationMs` 15000 ise satır ekrandayken gösterilir ve ardından `Clear` gelir; daha kısaysa satır
  kalktıktan sonra gelen çeviridir, o süre kadar gösterilir. `replaces`: öncekinin uzamış/düzelmiş okuması.
- `Clear`: İngilizce satır kalktı (bölge 800 ms boş). `Cover`: aynı satırın bandı büyüdü (sonraki okuma daha geniş).
- Çeviri iç iş parçacığında, aynı anda bir tane; `Translator::translate` o iş parçacığında çağrılır, `cancel()`
  daktilo efektinde ya da `reset()`'te çağrılır.
- `ReadScheduler` OCR aşaması içindir (bölgenin küçük gri imzası değişmediyse okuma atlanır).

## Demo gereksinimleri (SUBS_SPIKE.md son bölüm)

1. **`⁇` (U+2047)**: çeviride `⁇` varsa ve `⁇`'sız hâli kaynağa benziyorsa (benzerlik ≥ 0,7) o cümle orijinal kalır;
   bütün cümleler böyleyse satır `original=true` ile İngilizce gösterilir. Değilse `⁇`'nin çevresindeki parça kaynakta
   aynı başlangıç ve bitişli tek bir sözcüğe uyuyorsa o sözcük geri konur ("Str ⁇ nzu" → "Strùnzu"), kalan `⁇` silinir.
   Testler: `UnknownMark_*`, `Overlay_UnusableTranslationShowsTheOriginalLineWithSpeaker`.
2. **Kapatma bandı**: altyazı bloğundaki satır kutularının birleşimi + pay (satır yüksekliğinin %35'i, en az 2 px),
   ortalı satırda ekran ortasına göre simetrik genişletilir, banda kırpılır ama birleşimden asla küçük olmaz; aynı
   satır ekranda kaldıkça her okumada yalnız büyür. İpucu, menü ve köşe yazıları bloğa girmez. Metin eşleştirmesi
   kullanılmaz. Testler: `Cover_*`, `Replay_*`.
3. **Konuşmacı**: "MINE GUARD: …" etiketi çeviriye girmez, `ShownLine.speaker` olarak gelir; OCR'ın bozduğu etiket de
   ("MİNE GUARD•", "-GAETANO:") CouchSubs'ın LooseLabel kuralıyla ayrılır.

## CouchSubs'tan farklar

- **Girdi/çıktı:** hat OCR yapmaz, `OcrFrame` alır; TV gönderimi yerine geri çağrı; oyun paketleri, ortak havuz ve
  seslendirme yok (paketin metin kuralları ve vektörleri `PackText` içinde, arama yok).
- **Çeviri motoru:** `Translator` cümle başına çağrılır, hata `nullopt` (C#'ta istisna). Satırdaki bir cümle başarısız
  olursa satır başarısız sayılır (C# ile aynı sonuç).
- **İş parçacığı:** C#'ta ilk çeviri `Enqueue` içinde eşzamanlı başlar; burada iç iş parçacığı alır. Henüz başlamamış
  bir istek uzamış okuması gelince hiç çevrilmeden atlanır (C#'ta başlatılıp iptal edilirdi).
- **Çeviri zaten Türkçe** (`ShouldSend` yanlış) olduğunda C# gibi hiçbir şey gösterilmez; `original=true` satırlar bu
  denetimi atlar (kasıtlı: bilinmeyen harfli isim gösterilsin).
- **Büyük/küçük harf:** .NET'in değişmez kuralı taklit edildi: `İ` küçültülmez, `ı` büyütülmez (ICU'lu .NET ile aynı).
  `TextMatch.normalize("MİNER")` = `"mİner"`. Türkçe ekler ve emirler `tr-TR` kuralıyla (I↔ı, İ↔i). Unicode tabloları
  Python 3.14'ün Unicode 16.0 verisinden üretildi (.NET 8: Unicode 15); fark yalnız yeni eklenen karakterlerde olur.
  Basit büyük harf eşlemesi tam eşlemeden türetildi; çok karakterli eşlemesi olanlar (ß, ŉ, bazı Yunanca) değişmez.
- **Düzenli ifadeler:** .NET kalıpları (konuşmacı etiketi, değişken uzunluklu geriye bakışlı cümle aralığı, yer tutucu
  ve ek, emir kalıbı, `\b`) elle yazılmış tarayıcılarla aynı eşleşmeyi verir; `\d` Unicode Nd, `\w` L+Mn+Nd+Pc.
- **Önbellek dosyası:** UTF-8 metin kaçışsız yazılır (System.Text.Json ASCII dışını `\uXXXX` yapar); iki biçim de okunur.
- **Sözlük JSON'u:** yorum ve sondaki virgül serbest (System.Text.Json seçenekleriyle aynı).
- **PackFuzzy.IsDigit:** `GetDigitValue` yerine Nd + rakam değerli üst/alt simge ve daire içi rakamlar listesi.

## Doğrulanmayanlar

- Gerçek CTranslate2 motoruyla birlikte çalışma (5a ajanı) ve `⁇` kuralının gerçek model çıktısında etkisi.
- `cancel()`'ın gerçek motorda süren çeviriyi ne kadar hızlı durdurduğu.
- Kapatma bandı payının (%35) ekranda yeterli görünüp görünmediği; 5d'de katmanla bakılmalı.
