# Altyazı: model indirme ve isteğe bağlı çevrimiçi çeviri (Aşama 5, dalga 2)

`crispplay_subs_net` kitaplığı iki iş yapar:

1. **`ModelStore`** (`include/crispplay/subs/ModelStore.h`): çeviri (OPUS-MT) ve OCR (PP-OCR) modellerini özellik ilk açıldığında CrispPlay'in GitHub sürümünden indirir, SHA-256 ile doğrular, kurar.
2. **Çevrimiçi motor** (`include/crispplay/subs/OnlineTranslator.h`): kullanıcının kendi API anahtarıyla DeepL ya da OpenAI uyumlu bir sohbet uç noktası. **Varsayılan kapalı.**

GPL-3.0; CouchSubs'tan kod alınmadı (yalnız akış fikri ve DeepL'in herkese açık API biçimi aynı).

## Ne, nereden indirilir

| | |
|---|---|
| Adres | `https://github.com/efetuncer1-creator/CrispPlay/releases/download/subs-models-v1/crispplay-subs-models-v1.zip` (`custom/include/crispplay/Branding.h`: `subsModelsTag()`, `subsModelsAsset()`, `subsModelsUrl()`) |
| Dosya | `crispplay-subs-models-v1.zip`, **237 136 360 bayt (~226 MiB)** |
| SHA-256 | `58cefbdc8a16f41596affb1e89d6c8b62e83fe35c8cac0bfaf1217e9d29f0108` |
| Açılınca | ~262 MB, `<veri klasörü>\models\` altına |
| Gömülü liste | `src/ModelManifestData.inc` (betik üretir; zip ve içindeki her dosyanın boyutu + SHA-256'sı) |

Zip içeriği (her dosya ayrıca doğrulanır):

| Dosya | Bayt | SHA-256 | Lisans |
|---|---|---|---|
| `opus-mt-tc-big-en-tr-ct2-int8/model.bin` | 237 977 893 | `a5a148aa…de3719` | CC BY 4.0 |
| `opus-mt-tc-big-en-tr-ct2-int8/config.json` | 233 | `72901fbd…70e7ee` | CC BY 4.0 |
| `opus-mt-tc-big-en-tr-ct2-int8/shared_vocabulary.json` | 1 170 026 | `44dd886d…caedee` | CC BY 4.0 |
| `opus-mt-tc-big-en-tr-ct2-int8/source.spm` | 796 953 | `180d3f94…efad4` | CC BY 4.0 |
| `opus-mt-tc-big-en-tr-ct2-int8/target.spm` | 833 391 | `cd0c4756…1d797` | CC BY 4.0 |
| `opus-mt-tc-big-en-tr-ct2-int8/ATTRIBUTION.txt`, `LICENSE-CC-BY-4.0.txt` | 1 388 / 767 | manifestte | atıf, lisans adresi |
| `ppocr-rec/PP-OCRv6_rec_small.onnx` | 21 234 383 | `6f327246…c14884` | Apache-2.0 |
| `ppocr-rec/PP-OCRv6_rec_small_dict.txt` | 74 947 | `b5f2bfe2…401c5d` | Apache-2.0 (bilgi için; kod modelin meta verisini okur) |
| `ppocr-rec/ATTRIBUTION.txt`, `LICENSE-Apache-2.0.txt` | 870 / 11 560 | manifestte | atıf, tam lisans metni |

Tam özetler `src/ModelManifestData.inc`'de; model dosyalarının değerleri `custom/subs/mt/README.md` ve `custom/subs/ocr/README.md` ile aynı (betik farklıysa durur). Lisans/atıf metinlerinin kaynağı `model-licenses/` (satır sonları korunur, `.gitattributes`). CC BY 4.0'ın tam metni yerine lisansın 3(a)(1)(C) maddesinin izin verdiği gibi adresi konuldu.

## `ModelStore` davranışı

- **Kök:** `<dataDirectory>/models`. `CRISPPLAY_SUBS_MODEL_ROOT` var olan bir klasörü gösteriyorsa (ör. `..\deps\models`) o kullanılır (`usesDeveloperOverride()`): özetler tutarsa Ready; oraya hiçbir zaman indirme yapılmaz, damga yazılmaz. Klasör yoksa değişken yok sayılır.
- **Açılışta:** kökte `.crispplay-models.json` damgası varsa ve dosyaların boyutu + değişiklik zamanı + manifest özeti tutuyorsa hemen **Ready** (260 MB yeniden okunmaz). Yoksa arka planda tüm dosyalar SHA-256 ile denetlenir; bu sırada `checking == true`, durum **Missing**. Bitince `stateChanged`.
- **`refresh()`:** her zaman arka planda tam SHA-256 denetimi. Bozuk dosya → Missing + `error` hangi dosya olduğunu söyler; hiç kurulum yoksa `error` boş.
- **`download()`:** Ready ya da Downloading iken hiçbir şey yapmaz. Yalnız HTTPS. `<kök>/.download/crispplay-subs-models-v1.zip.part` dosyasına akıtır; dosya varsa `Range: bytes=N-` ile **kaldığı yerden sürer** (sunucu 200 dönerse baştan). `Accept-Encoding: identity`. Boyut sınırı manifestteki zip boyutu; aşan yanıt durdurulur. 30 sn hiç bayt gelmezse zaman aşımı (`stallTimeoutMs`). Yönlendirme: en çok 5, yalnız aynı ana makine ya da `github.com`, `objects.githubusercontent.com`, `release-assets.githubusercontent.com`, hepsi HTTPS, kullanıcı adı/parola içermeyen adres; başka yer → indirme durur, Failed.
- **Doğrulama ve kurulum (arka iş parçacığı):** zip'in SHA-256'sı (tutmazsa `.part` silinir) → sıkı zip okuyucu (`src/ZipReader.*`) → her dosya `.download/staging`'e açılırken boyut, CRC-32 ve SHA-256 denetlenir → her üst klasör iki `rename` ile yer değiştirir (eski klasör `.download/old-*`'e, yenisi yerine; hata olursa eski geri konur) → damga yazılır → Ready. Model dosyası uygulamada açıksa (eski kurulum kullanılıyorsa) rename başarısız olur, hata yeniden başlatmayı önerir.
- **`cancel()`:** indirmeyi keser, `.part` kalır (sonraki `download()` sürdürür), durum Missing, ardından kısa bir denetim.
- **Zip okuyucu:** yalnız merkezi dizin; store (0) ve deflate (8); zlib olarak Qt'nin içindeki zlib (`Qt6::ZlibPrivate`, Qt6Core.dll zaten dışa veriyor, yeni bağımlılık yok). Reddedilenler: şifreli giriş, zip64, çok diskli arşiv, yinelenen ad (büyük/küçük harf duyarsız), `..`/`.`/boş parça, mutlak yol, sürücü harfi/`:`, ters bölü, denetim karakteri, Windows aygıt adları (`CON`, `NUL`, `COM1`…), nokta/boşlukla biten parça, manifestte olmayan dosya.
- Hata metinleri Türkçe; arayüz `error`'ı doğrudan gösterebilir.

API eklemeleri (sözleşme kaynak uyumlu kaldı): `ModelStore(dataDirectory, ModelStoreOptions)`, `checking` özelliği, `usesDeveloperOverride()`, `ModelManifest.h` (`builtInModelManifest()`, `defaultModelStoreOptions()`).

## Çevrimiçi motor ve gizlilik

**Kapalıyken (varsayılan) ağa hiçbir şey gitmez.** `makeOnlineTranslator()` yalnız `provider != None` **ve** o sağlayıcı için kayıtlı bir anahtar varsa bir çevirmen döndürür; aksi halde `nullptr` ve hiçbir bağlantı kurulmaz. İstek yalnız bu çevirmenin `translate()` çağrısıyla çıkar.

Açıkken gönderilenler:

| Sağlayıcı | Adres | Giden veri |
|---|---|---|
| DeepL | Anahtar `:fx` ile bitiyorsa `https://api-free.deepl.com/v2/translate`, yoksa `https://api.deepl.com/v2/translate` (ayardaki `endpoint` doluysa o) | O an çevrilecek **tek altyazı satırının İngilizce metni**, `source_lang=EN`, `target_lang=TR`, `formality=prefer_less`, `preserve_formatting=true`; başlıkta `Authorization: DeepL-Auth-Key <anahtar>` |
| OpenAI uyumlu | `<endpoint>/chat/completions` (kullanıcının girdiği temel adres, ör. `https://api.openai.com/v1`; yerel sunucu için `http://localhost:11434/v1`) | Sabit sistem yönergesi (altyazı Türkçesi, sen/siz tutarlılığı, adları koru) + **tek satırın İngilizce metni**, `model`, `temperature: 0`, `max_tokens` (satır uzunluğu + 64, 96–512 arası); başlıkta `Authorization: Bearer <anahtar>` |

- Ekran görüntüsü, dosya adı, oyun adı, kullanıcı bilgisi gönderilmez. Boş satır gönderilmez; 4000 bayttan uzun girdi gönderilmez (`nullopt`).
- HTTPS zorunlu; düz HTTP yalnız `127.0.0.1` / `localhost` / `::1` (yerel LLM sunucusu) için. Yönlendirme izlenmez (anahtar başka adrese taşınmasın), çerez yok.
- Hiçbir şey günlüğe yazılmaz: ne anahtar, ne metin, ne yanıt.
- Zaman aşımı ayardan (varsayılan 2,5 sn, 500 ms–30 sn arasına kırpılır). HTTP hatası, 401/403/456 (kota), bozuk JSON, kesilmiş yanıt (`finish_reason: length`), zaman aşımı, iptal → `nullopt`; çağıran yerel motora düşer.
- `translate()` engelleyicidir, hat iş parçacığında çağrılır. **WinHTTP seçildi** (QNetworkAccessManager değil): o iş parçacığında Qt olay döngüsü yok, QObject iş parçacığı bağlılığıyla uğraşılmıyor; WinHTTP'nin eşzamanlı modu döngü istemiyor, her adımın zaman aşımı kalan süreye göre kısaltılıyor (toplam süre sınırı), `cancel()` istek tutamacını başka iş parçacığından kapatarak bekleyen çağrıyı bitiriyor (Microsoft'un önerdiği iptal yolu). Sistem vekil sunucusu (`WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY`) kullanılır; TLS 1.2/1.3.

### Ayar ve anahtar dosyaları (`<dataDirectory>`)

- `crispplay-subs-online.v1.json`: `{"version":1,"provider":"none|deepl|openai-compatible","endpoint":"…","model":"…","timeoutMs":2500}` — **anahtar içermez**.
- `crispplay-subs-online-deepl.key`, `crispplay-subs-online-openai-compatible.key`: Windows DPAPI (`CryptProtectData`, geçerli kullanıcı, sabit ek entropi `CrispPlay.subs.online.key.v1`) ile şifreli anahtar. Yalnız aynı Windows kullanıcısı çözebilir; dosya başka makineye/kullanıcıya taşınırsa anahtar okunamaz (`hasApiKey` false). Boş anahtar kaydetmek = silmek. Bellekteki kopyalar iş bitince `SecureZeroMemory` ile siliniyor.

## Model paketini üretmek

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File custom\scripts\package-subs-models.ps1 [-Models ..\deps\models] [-OutDir ..\dist] [-DryRun]
```

Betik model dosyalarının özetlerini bilinen değerlerle karşılaştırır (farklı dönüşüm → durur), `model-licenses/` altındaki atıf/lisans dosyalarını ekler, zip'i sabit sıra ve sabit tarihle (.NET `ZipArchive`, deflate) `..\dist\crispplay-subs-models-v1.zip` olarak yazar, yanına `.sha256` ve `crispplay-subs-models-v1.manifest.json` koyar ve `src/ModelManifestData.inc`'i yeniden üretir. **Hiçbir şey yüklemez.** Zip'in özeti uygulamaya gömülü olduğundan yeniden üretilen zip ancak yeni `.inc` ile birlikte yayımlanabilir; yayımlanmış bir sürüm dosyası **asla değiştirilmez**, yeni model seti yeni etiket/ad (`subs-models-v2`) alır.

2026-11-12'de üretilen dosya: `C:\Users\efetn\Desktop\efe\crisp_play\dist\crispplay-subs-models-v1.zip`, 237 136 360 bayt, SHA-256 `58cefbdc8a16f41596affb1e89d6c8b62e83fe35c8cac0bfaf1217e9d29f0108` (depodaki `.inc` ile aynı).

## Sürümü yayımlama (Efe onaylayınca, elle)

1. Yerel zip'in özetini denetle: `Get-FileHash ..\dist\crispplay-subs-models-v1.zip -Algorithm SHA256` → yukarıdaki değer ve `src/ModelManifestData.inc` ile aynı olmalı.
2. GitHub'da `efetuncer1-creator/CrispPlay` deposunda yeni sürüm: etiket **`subs-models-v1`**, başlık "Altyazı modelleri v1", **pre-release** işaretli (uygulamanın güncelleme denetimi bu etiketi zaten saymaz: `releaseVersion()` yalnız `vX.Y.Z[-crispplay.N]` kabul eder), "Set as latest" **kapalı**.
   `gh release create subs-models-v1 ..\dist\crispplay-subs-models-v1.zip ..\dist\crispplay-subs-models-v1.zip.sha256 --repo efetuncer1-creator/CrispPlay --prerelease --latest=false --title "Altyazı modelleri v1" --notes-file <notlar>`
3. Sürüm notuna: içerik tablosu, SHA-256, model atıfları (OPUS-MT CC BY 4.0 atıf metni `model-licenses/opus-mt-ATTRIBUTION.txt`, PP-OCR Apache-2.0) ve "bu dosyayı uygulama kendisi indirir" notu.
4. Yükledikten sonra adresten indirip (`https://github.com/efetuncer1-creator/CrispPlay/releases/download/subs-models-v1/crispplay-subs-models-v1.zip`) özeti tekrar karşılaştır; uygulamada "indir" ile uçtan uca dene (ilk gerçek ağ denemesi bu olacak).
5. Dosyayı bir daha değiştirme/silme: eski uygulama sürümleri bu adrese ve özete bağlı.

## Derleme ve testler

`custom/CMakeLists.txt` içinde `add_subdirectory(subs/net)`; Qt Network ya da Qt zlib yoksa kitaplık atlanır. `custom\scripts\build.ps1 -Test` `crispplay_subs_net_tests`'i derler ve çalıştırır. Testler yerel bir HTTP sunucusuna (`tests/TestHttpServer.*`, 127.0.0.1, kendi iş parçacığı) bağlanır, **gerçek ağa çıkmaz**:

- zip: güvenli ad kuralları, yol aşımı içeren arşivin reddi (`../`, `C:/`, `\`, mutlak yol), store/deflate açma, CRC hatası, zip olmayan dosya
- gömülü manifest ve indirme adresi
- indirme: başarılı kurulum + damga + yeniden açılışta anında Ready + bozulan dosyayı `refresh()`'in bulması ve yeniden indirmeyle onarma; yanlış zip özeti; yanlış dosya özeti; yarıda kesilen indirme → `Range` ile sürdürme; büyük yanıt; HTTP 404; aynı makineye yönlendirme kabul; yabancı makine / HTTP CDN / kullanıcı-parolalı adrese yönlendirme reddi; HTTPS zorunluluğu; iptal + sürdürme; durma zaman aşımı; geliştirici klasörü
- DPAPI gidiş-dönüş (Unicode yol, dosyada açık metin yok, bozulmuş blob), ayar kaydet/yükle (kırpma, bozuk dosya), fabrika kuralları (None → `nullptr`, anahtarsız → `nullptr`, uzak düz HTTP reddi, yerel HTTP kabul)
- gerçek model paketi: `..\dist\crispplay-subs-models-v1.zip` varsa tam `.part` olarak konup ağsız kurulur (zip özeti, .NET'in ürettiği zip'in okunması, gömülü manifestteki 11 dosya); dosya yoksa atlanır
- DeepL ve OpenAI uyumlu istek/yanıt biçimi (yol, başlıklar, JSON alanları), hata kodları, yönlendirmenin izlenmemesi, zaman aşımı → `nullopt`, iptal sonrası yeniden çalışma

## Doğrulanmayanlar

- Gerçek DeepL ve OpenAI çağrısı yapılmadı (anahtar yok, testler yerel sunucuyla). Bazı yeni OpenAI modelleri `max_tokens` yerine `max_completion_tokens` istiyor ya da `temperature: 0` kabul etmiyor; bu modellerde istek 400 döner ve yerel motora düşülür. Gerekirse ayara alan eklenir.
- GitHub'dan gerçek indirme (yönlendirme zinciri ve CDN adı) sürüm yayımlanınca denenecek.
- Uygulamaya bağlantı (`custom/subs/app`) başka dalda yapılıyor.
