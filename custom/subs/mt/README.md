# Altyazı çeviri motoru (Aşama 5, adım 5a)

İngilizce altyazı satırını Türkçeye çeviren kitaplık: OPUS-MT `opus-mt-tc-big-en-tr`, CTranslate2 (işlemci, int8) + SentencePiece. Karar ve deneme ölçümleri `custom/docs/SUBS_SPIKE.md`'de.

**Durum (2026-11-12):** 5a tamam. MKL'siz C++ derlemesi Python paketiyle **aynı çevirileri** üretiyor (137/137 satır bayt bayt aynı) ve aynı hızda: 2 iş parçacığında p50 86–88 ms (hedef ≤ 150 ms). ONNX Runtime yedek yoluna gerek kalmadı, ölçülmedi. Uygulamaya bağlanmadı (5d); şimdilik yalnız kitaplık, test ve ölçüm aracı derleniyor.

## İçerik

| Dosya | İş |
|---|---|
| `include/crispplay/subs/mt/TranslatorInterface.h` | Çekirdeğin `Translator` arayüzü (`custom/subs/core/.../Types.h`). Çekirdek dalı birleşene kadar yerel kopya; `Types.h` include yolundaysa onu kullanır, kopya devre dışı kalır |
| `include/.../OpusMtTranslator.h`, `src/OpusMtTranslator.cpp` | `OpusMtTranslator : Translator` |
| `include/.../TextSplit.h`, `src/TextSplit.cpp` | Cümle bölme/birleştirme (denemedeki `mt_bench.py` düzenli ifadesinin birebir karşılığı), kırpma, `⁇` denetimi |
| `tools/MtBench.cpp` | `crispplay_mt_bench` ölçüm aracı |
| `tests/MtTests.cpp` | `crispplay_subs_mt_tests` (Qt Test) |

### `OpusMtTranslator` davranışı

- Kurucu hiçbir şey okumaz; model ilk `translate()` (ya da `load()`) çağrısında, çağıran iş parçacığında yüklenir. Yükleme süresi `loadTimeMs()`, son çeviri süresi `lastLatencyMs()`.
- Varsayılanlar (`OpusMtOptions`): 2 iş parçacığı, ışın 4, cümle başına en çok 128 hedef parçası, kaynak cümle 128 parçada kesilir, satır 1000 bayta kırpılır (UTF-8 sınırında, mümkünse boşlukta).
- Satır cümlelere bölünür, cümleler **tek toplu çağrıda** çevrilip bir boşlukla birleştirilir (denemedeki gibi; satırı tek parça çevirmek chrF'yi 76,1 → 72,7 düşürüyordu).
- Boş/yalnız boşluk satır → `""`. Model yok, bozuk ya da CTranslate2 hata attı → `nullopt` (çökme yok; başarısız yükleme hatırlanır, `lastError()`).
- `cancel()`: o an süren çeviri `nullopt` döner. Bir satırın cümleleri tek toplu çağrıda çözüldüğü için iptal, o toplu çağrı bitince etkili olur (≤ ~0,3 sn). Sonraki çağrılar etkilenmez.
- Bilinmeyen parça: çıktıdaki `⁇` (U+2047) **silinmez**; ne gösterileceğine çekirdek hat karar verir (SUBS_SPIKE.md "Demo videosundan çıkan gereksinimler"). `lastHadUnknown()` son çeviride `<unk>` hedef parçası olup olmadığını, `containsUnknownMarker(text)` metinde `⁇` olup olmadığını söyler.
- Model yolu Unicode olabilir (CTranslate2 Windows'ta UTF-8 → geniş karakter çeviriyor; `.spm` dosyalarını biz okuyup belleğe veriyoruz).
- `load()` ve `translate()` aynı kilitle sıraya girer; tek çalışan iş parçacığı için tasarlandı.

## Derleme

Bir kez, bağımlılıklar (~15 dk, hepsi `..\deps\` altına):

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File custom\scripts\build-ctranslate2.ps1
```

Betik sabitlenmiş kaynakları indirir (`deps\ctranslate2-src`, `deps\onednn-src`, `deps\sentencepiece-src`), `deps\ctranslate2-build\` altında derler ve `deps\ctranslate2-install\` altına kurar (`lib\ctranslate2.lib, dnnl.lib, cpu_features.lib, sentencepiece.lib` + başlıklar, `CRISPPLAY_BUILD.txt` sürüm kaydı). `-Clean` derleme klasörünü siler; `-Backend ruy` karşılaştırma için ruy ile ayrı bir kurulum (`ctranslate2-install-ruy`) üretir (ölçülmedi).

Sonra uygulama her zamanki gibi: `custom\scripts\build.ps1 -Test`. `build.ps1` kurulum varsa `-DCRISPPLAY_CT2_ROOT=...` geçirir ve `crispplay_mt_bench`'i hedeflere ekler. Kurulum yoksa CMake `CTranslate2 not found ... skipped` der, uygulama yine derlenir; testlerin çevirmen bölümleri atlanır (19 geçti, 4 atlandı — denendi).

CMake değişkenleri: `CRISPPLAY_CT2_ROOT` (varsayılan `${PROJECT_SOURCE_DIR}/../deps/ctranslate2-install`), `CRISPPLAY_MT_MODEL_DIR` (testler ve ölçüm aracı için geliştirme modeli, varsayılan `../deps/models/opus-mt-tc-big-en-tr-ct2-int8`; çalışırken `CRISPPLAY_MT_MODEL` ortam değişkeni de olur).

### Neden oneDNN, neden statik

- **Arka uç oneDNN (Apache-2.0), MKL yok.** Python paketi (ctranslate2 4.9.0) bu işlemcide (AMD) zaten MKL'yi kullanmıyor: `CT2_VERBOSE=1` çıktısı `Use Intel MKL: false`, `SGEMM backend: DNNL`, `GEMM_S8 backend: DNNL`. Denemedeki sayılar yani zaten oneDNN sayılarıydı; aynı oneDNN sürümünü (3.1.1, paketin kendi Windows derleme betiğindeki) aynı seçeneklerle derleyince çeviriler birebir aynı çıktı. ruy x86'da daha zayıf ve int8 yolu farklı (çıktılar değişebilir); OpenBLAS yalnız float32 GEMM veriyor. ruy derlenmedi/ölçülmedi.
- Intel işlemcide MKL'li paket MKL'yi seçerdi; bizim derlemede MKL hiç yok, her işlemcide oneDNN çalışır. Intel işlemcide hız ölçülmedi.
- **OpenMP:** paket Intel `libiomp5md.dll` kullanıyor (Intel lisansı); biz MSVC'nin OpenMP'sini (`OPENMP_RUNTIME=COMP`, `vcomp140.dll`) kullanıyoruz. Hız farkı görülmedi.
- **Statik, /MT:** uygulama statik CRT ile derleniyor (kök `CMakeLists.txt`, `MultiThreaded`). İlk denemede `ctranslate2.dll` (/MD) + statik SentencePiece bağlayıcıda CRT çakışması verdi; hepsini /MT statik kitaplık yaptık. Böylece ayrı DLL yok, tek CRT; çevirmen EXE'ye ~11 MB ekliyor (ölçüm aracı 11,9 MB). Çalışma zamanında gereken tek dosya `vcomp140.dll` (VC++ yeniden dağıtılabilir paketinin `Microsoft.VC143.OpenMP` klasörü) — **paketleme betiğine eklenmeli** (5d/5f), uygulama statik CRT kullandığı için redist kurulu olduğu varsayılamaz.
- SentencePiece **v0.2.1**: Python'daki 0.2.3 değil. 0.2.2 ve sonrası derleme sırasında abseil/protobuf'u ağdan indiriyor (0.2.3 protobuf v36.2 + abseil; bu makinede protobuf'un `.rc` dosyası `rc.exe` hatası verdi, 0.2.2 sembolik bağlantı izni istedi). 0.2.1 protobuf-lite ve abseil alt kümesini kendi içinde getiriyor; kodlama/çözme aynı (137/137 aynı çıktı).

## Sürümler ve lisanslar

| Bileşen | Sürüm / commit | Lisans | Not |
|---|---|---|---|
| CTranslate2 | v4.9.0, `a22ba1c63ee35d1a0d25db149aa2a66d36a6a732` | MIT | `WITH_MKL=OFF WITH_DNNL=ON WITH_CUDA=OFF OPENMP_RUNTIME=COMP BUILD_SHARED_LIBS=OFF` |
| ↳ spdlog (alt modül) | `76fb40d95455f249bd70824ecfcae7a8f0930fa3` | MIT | yalnız başlık |
| ↳ cpu_features (alt modül) | `8a494eb1e158ec2050e5f699a504fbc9b896a43b` | Apache-2.0 | |
| ↳ half_float, nlohmann/json başlıkları | CT2 içinde | MIT | kurulumla gelen başlıklar |
| oneDNN | v3.1.1, `64f6bcbcbab628e96f33a62c3e975f8535a7bde4` | Apache-2.0 | içinde Xbyak (BSD-3), ittnotify (BSD-3), Boost parçası (BSL-1.0): `deps\onednn-src\THIRD-PARTY-PROGRAMS` |
| SentencePiece | v0.2.1, `31646a467d2051eb904e0b45de3a73e91fe1c1e3` | Apache-2.0 | içinde protobuf-lite (BSD-3), abseil alt kümesi (Apache-2.0), darts-clone (BSD-2), esaxx (MIT) |
| MSVC OpenMP `vcomp140.dll` | VS 2022 (14.44) | Microsoft VC++ redist lisansı | uygulamanın zaten taşıdığı MSVC çalışma zamanı ile aynı statü |
| OPUS-MT `opus-mt-tc-big-en-tr` | Helsinki-NLP `e539fc16a8`, `ct2-transformers-converter --quantization int8` | **CC BY 4.0** | atıf zorunlu (aşağıda) |

Hepsi GPL-3 ile birlikte dağıtılabilir (Apache-2.0 GPL-3 ile uyumlu). Sürüm paketinde `THIRD_PARTY_NOTICES` / `licenses\` altına bu lisans metinleri konmalı (5f).

**Model atfı (CC BY 4.0, uygulamada "Hakkında"da ve model indirme sayfasında gösterilecek):**

> Çeviri modeli: OPUS-MT `opus-mt-tc-big-en-tr`, Helsinki-NLP / Language Technology Research Group, University of Helsinki (Tiedemann & Thottingal 2020, "OPUS-MT – Building open translation services for the World"; Tiedemann vd. 2023, "Democratizing neural machine translation with OPUS-MT"), CC BY 4.0. CrispPlay bu modeli CTranslate2 int8 biçimine dönüştürmüştür; dönüştürme dışında değişiklik yoktur.

## Ölçüm (2026-11-12, bu makine: Ryzen, AVX2 seçili; ekran kartı kullanılmadı, video hattı kapalı)

`crispplay_mt_bench` (`out\build\x64-release\custom\subs\mt\`) — Python `mt_bench.py` ile aynı yöntem: bir ısınma turu + 5 ölçüm turu, p50 = satır başı medyanların medyanı, p95 = bütün ölçümler üzerinden, chrF = sacrebleu varsayılanı (karakter 6-gram, β = 2, boşluksuz), RSS = model yükleme öncesine göre çalışma kümesi artışı.

| | yükleme | ilk çağrı | p50 ms | p95 ms | en kötü | chrF sentetik (64) | chrF gerçek (73) | Python ile aynı | RSS |
|---|---|---|---|---|---|---|---|---|---|
| **C++ oneDNN, 2 iş parçacığı** | 260–273 ms | 71–79 ms | **86,4 / 88,1** (sentetik / gerçek) | 135 / 148 | 196 / 294 | **76,1** | **62,2** | **64/64, 73/73** | 260 MB |
| Python paketi, aynı oturum, 2 i.p. | 280 ms | 74 ms | 86,1 | 136,6 | 334 | 76,1 | — | — | 257 MB |
| Python paketi, SUBS_SPIKE.md | 255 ms | 68 ms | 100 | 154–169 | | 76,1 | 62,2 | | 258 MB |
| C++ oneDNN, 1 iş parçacığı | 298 ms | 107 ms | 152,5 | 238 | 293 | 76,1 | | | 257 MB |
| C++ oneDNN, 4 iş parçacığı | 300 ms | 75 ms | 65,8 | 101 | 186 | 76,1 | | | 259 MB |

- Denemedeki 100 ms ile bugünkü 86 ms arasındaki fark ölçüm gürültüsü/makine durumu; aynı oturumda Python ve C++ eşit. Ölçüm sırasında başka ajanlar aynı makinede derleme yapıyor olabilirdi; en kötü değerler (max) bundan etkilenebilir.
- Gerçek klipte 1 satırda `⁇` var ("Strùnzu..." → "Str ⁇ nzu...", Python ile aynı).
- Disk: model 230 MB (`model.bin` 237 977 893 bayt).

Yeniden üretmek:

```powershell
$env:PATH = "..\deps\qt\6.8.3\msvc2022_64\bin;$env:PATH"
out\build\x64-release\custom\subs\mt\crispplay_mt_bench.exe [--threads 2] [--beam 4] [--repeat 5] [--whole] `
    [--compare py_synth.json] [--real ..\deps\subs-real\real.json] [--out sonuc.json]
```

`--compare`: `mt_bench.py --out` çıktısı (sentetik satırlar için Python çevirisi); gerçek klipte Python çevirisi `real.json`'ın `mt` alanından okunur. Farklı satırlar ekrana yazılır. `deps\subs-real\` oyun metni içerir, repoya girmez.

## Testler

`crispplay_subs_mt_tests` (23): cümle bölme (13 durum: kısaltma, üç nokta, tırnak, rakam, boşluksuz nokta, satır sonu…), birleştirme, boş/boşluk girdi, UTF-8 güvenli kırpma, `⁇` denetimi, model yok → `nullopt` (Unicode yol, ikinci çağrı da çökmez), bozuk model → `nullopt`, modelle çeviri (beklenen Türkçe, `⁇` korunuyor, `lastHadUnknown`, çok uzun girdi kırpılıp çevriliyor), iptal (yükleme sırasında `cancel()` → `nullopt`, sonraki çağrı normal). Model yoksa model testleri atlanır.

## Modelin dağıtımı (dalga 2'de yapılacak, burada yalnız plan)

- Model uygulama paketine girmez; CrispPlay GitHub sürümüne ayrı dosya olarak konur: `opus-mt-tc-big-en-tr-ct2-int8.zip` (içinde `model.bin`, `config.json`, `shared_vocabulary.json`, `source.spm`, `target.spm`, `LICENSE-CC-BY-4.0.txt`, `ATTRIBUTION.txt`).
- Uygulama çeviriyi ilk açışta kullanıcıya boyutu (~230 MB) ve lisansı gösterip indirmeyi sorar; `%APPDATA%\CrispPlay\models\opus-mt-tc-big-en-tr-ct2-int8\` altına açar. Sürüm dosyasının ve her dosyanın SHA-256'sı uygulamaya gömülü listede durur; tutmayan dosya kullanılmaz, `OpusMtTranslator` bu klasörle kurulur.
- Bugünkü dönüşümün SHA-256 değerleri (`deps\models\opus-mt-tc-big-en-tr-ct2-int8`):

| Dosya | Bayt | SHA-256 |
|---|---|---|
| `model.bin` | 237 977 893 | `a5a148aae553f2573fd1f9ba735a03385f050437f98e51f8b5c4e4f9c4de3719` |
| `config.json` | 233 | `72901fbd8abd89fb5cf4a388f26fc681f5c4c58a1e1a88b30b879f107270e7ee` |
| `shared_vocabulary.json` | 1 170 026 | `44dd886d9f538a4b22e58d89758bd336e73b06c753a436f7531436daafcaedee` |
| `source.spm` | 796 953 | `180d3f94ddfee5585d78b702290ded0f4f9bd40f299349c8383b32a579fefad4` |
| `target.spm` | 833 391 | `cd0c475692178b6c626aadaef01bd8f98d8053946938e4e67005fc1e4811d797` |

## Açık / doğrulanmadı

- Intel işlemcide hız; AVX2'siz işlemcide çalışma (CT2 çalışma anında ISA seçiyor, denenmedi).
- ruy ve ONNX Runtime yolu ölçülmedi (gerek kalmadı).
- Video hattı (SR/NR/FG) çalışırken gecikme ölçülmedi.
- `vcomp140.dll` paketlemeye henüz eklenmedi; uygulama çevirmene henüz bağlı değil.
