# CrispPlay Yol Haritası

2026-11-11 · Efe Tuncer

> **Claude Code için not:** Bu dosya, CrispPlay fork'unun planıdır. Aşama 0 kodu yazıldı (bkz. "Durum"); sıradaki iş Aşama 1 (derleme ortamı). Upstream dosyalarına dokunmadan önce `custom/HOOKS.md`'yi oku.

## Durum (2026-11-11)

- Aşama 0 kodu hazır: `custom/` iskeleti, marka, güncelleme adresi ve sürüm politikası, Hakkında'da GPL atfı, `upstream-sync.yml`. Upstream dosyalarında 20 işaretli kanca satırı + `version.rc.in`'de 2 değer; liste `custom/HOOKS.md`.
- Aşama 1: derleme ortamı kuruldu, uygulama derleniyor ve açılıyor; ayrıntı ve eksikler `custom/BUILD.md`. Açık kalan: paketleme (windeployqt + çalışma zamanı DLL'leri), PS5 slice yamalı FFmpeg, FidelityFX 1.1.4 optical flow kitaplığı.
- Aşama 2 (Türkçe) tamam: `custom/i18n/tr.json` upstream kataloğun 2808 girdisinin tamamını kapsıyor, Ayarlar → Arayüz dili'nde "Türkçe" var, Türkçe Windows'ta "Sistemi izle" Türkçeyi seçer. Upstream'e 7 yeni kanca satırı (toplam 27, liste `custom/HOOKS.md`). Terimler `custom/i18n/GLOSSARY.md`; her sync'te `python custom/scripts/i18n_check.py`.
- Test GPU'su: NVIDIA GeForce RTX 3060 Ti (+ AMD tümleşik grafik).
- Aşama 5 denemesi (2026-11-12, dal `feature/subs`): OCR + çeviri hattı gerçek bir oyun klibinde ölçüldü, sonuç "devam, üç değişiklikle" (ikinci OCR okuyucusu, CTranslate2, çeviri kalitesi beklentisi). Rapor ve uygulama planı `custom/docs/SUBS_SPIKE.md`.

## Amaç ve temel ilke

Veyra 2.1.5 tabanlı, Türkçe ve kendine özgü özellikleri olan bir fork yapıyoruz; upstream (Likely7/Veyra-NRVideo) güncellemelerini her sürümde alabilmeye devam ederek.

Temel ilke: **upstream kodu ile bizim kodumuz fiziksel olarak ayrı durur.** Bizim özelliklerimiz `custom/` altında yaşar; upstream dosyalarına yalnızca birkaç satırlık, `VEYRA_CUSTOM` işaretli kancalar eklenir. Böylece her upstream sürümünde çakışma birkaç satırla sınırlı kalır.

## Repo ve dal düzeni

Upstream yalnızca sürüm etiketleri üzerinden (`v2.1.5`, `v2.1.6`…) alınır; ara commit'ler alınmaz.

- **`upstream` remote:** `https://github.com/Likely7/Veyra-NRVideo`, sadece okunur.
- **`main`:** bizim ürünümüz = son alınan upstream etiketi + `custom/` + kancalar.
- **`sync/vX.Y.Z`:** her yeni upstream etiketi için açılan geçici birleştirme dalı; PR ile `main`'e girer.
- **`feature/<ad>`:** her özellik ayrı dal (ör. `feature/replay`).

```
Upstream (Likely7)  --yeni etiket-->  sync/vX.Y.Z  --PR birleşir-->  main (bizim ürün)
sadece sürüm etiketleri               otomatik PR                    upstream + custom/ + kancalar
                                                                       ^                |
                                                       feature/<ad> --+                v
                                                                         Bizim sürüm etiketi (ör. v2.1.6-crispplay.1)
```

Özellik dalları main'e girer; upstream yalnızca sync dalı üzerinden gelir, sürüm etiketi main'den atılır.

Klasör ve dosya kuralları:

| Yer | Sahibi | Kural |
| --- | --- | --- |
| `custom/src`, `custom/include` | Biz | Tüm özellik kodumuz |
| `custom/qml` | Biz | Yeni arayüz parçalarımız |
| `custom/i18n/tr.json` | Biz | Türkçe çeviri, upstream `catalog.json`'a dokunmaz; anahtar `zh` (koddaki kaynak metin) |
| `custom/branding/` | Biz | İsim, ikon, güncelleme adresi, sürüm eki |
| `custom/CMakeLists.txt` | Biz | Kökteki CMake'e tek satır `add_subdirectory` ile bağlanır; `custom/branding/Branding.cmake` ayrıca `project()` altından include edilir (sürüm etiketi erken gerekiyor) |
| `custom/HOOKS.md` | Biz | Upstream dosyalarındaki her kancanın listesi |
| Diğer her şey | Upstream | Yalnızca `// VEYRA_CUSTOM` işaretli kısa kancalar |
| `AGENTS.md`, `docs/`, worklog'lar | Upstream | Asla değiştirilmez |

Kanca kuralı: bir upstream dosyasındaki değişiklik ≤ 5 satır olur ve `custom/` içindeki bir fonksiyonu çağırır. Daha fazlası gerekiyorsa önce yeni bir genişleme noktası tasarlanır.

## Aşamalar

Toplam tahmini süre yaklaşık 7–9 hafta; kodu Claude yazar, Windows'ta derleme ve test Efe'de. Sıra, kolay ve bağımsız işlerden zor ve riskliye doğru. Aşama 0 kodu derlenmeden yazıldı; Aşama 1'in ilk işi onu derleyip testlerini koşmak.

| # | Aşama | Süre | Zorluk | Upstream'e dokunuş |
| --- | --- | --- | --- | --- |
| 0 | Hazırlık: fork, `custom/` iskeleti, marka, güncelleme adresi, upstream takip Action'ı | 1–2 gün | Düşük | CMake'e 1 satır, `UpdateChecker` adresi |
| 1 | Derleme ortamı: MSVC 2022, Qt 6.8.3, CMake/Ninja, vcpkg, kapalı kaynak runtime bileşenleri | 1–2 gün | Orta | Yok |
| 2 | Türkçe: `custom/i18n/tr.json` + yükleyicide birleştirme, dil menüsüne Türkçe | 2–3 gün | Düşük | `UiLanguage.cpp`'de kanca (`load()` ve `resolve()`'daki dil listesi) |
| 3 | Discord Rich Presence: varsayılan kapalı, ayrıntı seviyesi seçimi, dosya adı asla gösterilmez | 0.5–1 gün | Düşük | Ayarlar sayfasına 1 bileşen |
| 4 | Anlık tekrar: halka tampon, kısayol, ses dahil, son N saniyeyi MP4'e yazma. Varsayılan kapalı; sürekli NVENC'in SR/NR/FG açıkken GPU süresine etkisi ölçülüp kabul sınırı konur | 4–6 gün | Orta | `LiveRecorder` çıkışına kanca |
| 5 | Oyun altyazısı çevirisi: ekrandaki İngilizce altyazıyı OCR ile okuyup Türkçeye çevirip uygulama içinde gösterme (CouchSubs mantığının C++'a taşınması) | 2–3 hafta | Orta–Yüksek | Sunulan kareye okuma kancası |
| 6 | Akıllı profiller: kaynak türüne göre otomatik preset | 2–4 gün | Düşük–Orta | Kaynak açılışına kanca |
| 7 | Benchmark sihirbazı: kombinasyonları ölçüp öneri | 4–7 gün | Orta | GPU süre ölçümüne okuma kancası |
| 8 | Sanal kamera: Windows 11 `MFCreateVirtualCamera`, 1080p60 SDR çıkış | 1–2 hafta | Yüksek | Sunum çıkışına kanca |

**Aşama 5 kapsamı (oyun altyazısı çevirisi):** Akış: Veyra'daki kare → altyazı bölgesini kes → Windows OCR → OPUS-MT ile Türkçeye çeviri → uygulama içinde katman. TV uygulaması ve ikinci PSN hesabı gerekmez; PS5 yayını, capture kart, Xbox, PC ve ekran yakalamanın hepsinde çalışır.

- **CouchSubs'tan (github.com/yorulmazsinan/couchsubs) taşınacak mantık:** OCR gürültü temizleme, altyazı dengeleyici, cümle birleştirme, oyun sözlüğü, Türkçe ek düzeltme, çeviri önbelleği; doğrulama için onların test vektörleri.
- **Taşınmayacaklar:** TV kurulumu (LG/Samsung/Android), ikinci hesapla Remote Play, altyazı sunucusu, seslendirme.
- **Çalışma yeri:** OCR ve çeviri CPU'da (int8 model, ~530 MB ilk indirme); SR/NR/FG ile VRAM için yarışmaz.
- **Lisans:** CouchSubs AGPL-3.0; taşınan dosyalar `custom/subs/` altında AGPL başlığı ve atıfla durur, `THIRD_PARTY_NOTICES`'e eklenir.
- **Önce deneme (1–2 gün):** taşımaya başlamadan, birkaç gerçek oyun karesiyle OCR + OPUS-MT hattını tek başına çalıştırıp çeviri kalitesine ve gecikmeye bak. Sonuç kötüyse çeviri motoru seçimi baştan konuşulur. *Yapıldı, sonuç ve ölçümler `custom/docs/SUBS_SPIKE.md`.*
- **Efe'nin kararları (2026-11-12):**
  - Varsayılan çeviri yerel OPUS-MT (CTranslate2 int8); ayarlardan açılan **isteğe bağlı çevrimiçi motor** da eklenecek (kullanıcının kendi API anahtarıyla; varsayılan kapalı, kapalıyken ağa hiçbir şey gitmez). Çevirmen arayüzü değiştirilebilir tasarlanır.
  - Türkçe satır İngilizce altyazının **üstünü kapatan** bir bantta gösterilir.
  - CouchSubs kuralları **AGPL başlığı ve atıfla** `custom/subs/` altına taşınır, test vakalarıyla birlikte; `THIRD_PARTY_NOTICES`'e eklenir.
  - OCR ve çeviri modelleri pakete gömülmez: **özellik ilk açıldığında** CrispPlay GitHub release'indeki model dosyasından indirilir (SHA-256 doğrulamalı).

Arayüzde büyük yeniden tasarım yok; yeni ekranlar `custom/qml` altında ayrı bileşenler olarak eklenir.

## Upstream güncelleme prosedürü

Yeni upstream etiketi çıktığında bir GitHub Action günlük kontrolde bunu fark eder ve `sync/vX.Y.Z` dalıyla otomatik PR açar; insan işi yalnızca çakışma çözmek ve test etmektir.

1. Action (`.github/workflows/upstream-sync.yml`) her gün upstream etiketlerini okur, `main`'de olmayan en yeni sürüm etiketini bulur (yalnız `vX.Y.Z`; `-sources` gibi ekli etiketler atlanır).
2. Birleştirme temizse `sync/vX.Y.Z` = `main` + etiket; çakışma varsa runner'da merge yarıda kalacağı için `sync/vX.Y.Z` doğrudan upstream etiketine işaret eder ve GitHub çakışmayı PR'da gösterir.
3. PR gövdesi çakışan dosyaları ve kontrol listesini yazar.
4. Yetki: upstream `.github/workflows/` altında değişiklik yaparsa `GITHUB_TOKEN` o dalı push edemez; `workflow` yetkili bir PAT `SYNC_TOKEN` secret'ı olarak eklenmeli. Repo ayarında "Allow GitHub Actions to create and approve pull requests" açık olmalı. Fork'larda zamanlanmış workflow'lar Actions sekmesinden bir kez elle etkinleştirilmeli; repo 60 gün hareketsiz kalırsa GitHub zamanlamayı durdurur.
5. Çakışmalar `custom/HOOKS.md`'deki kanca listesine bakılarak çözülür: upstream değişikliği alınır, bizim kanca satırı yeniden eklenir.
6. `python custom/scripts/i18n_check.py` çalıştırılır: upstream'de yeni eklenen metinler "çevrilmedi" listesine düşer (çevrilene kadar İngilizce görünür), eskimiş girdiler ve yer tutucu uyuşmazlıkları raporlanır; uyuşmazlıkta çıkış kodu 1. `--add-missing` yeni girdileri boş `tr` ile dosyaya ekler.
7. `custom/branding/Branding.cmake` içinde `CRISPPLAY_PATCH` 1'e çekilir.
8. Windows'ta derleme + duman testi; sorun yoksa PR birleştirilir, bizim sürüm etiketi atılır (ör. `v2.1.6-crispplay.1`).

Sürüm adı kuralı: `<upstream sürümü>-crispplay.<bizim yamamız>`; hangi upstream'e dayandığımız her zaman görünür kalır. Etiket CMake'te `PROJECT_VERSION` + `CRISPPLAY_PATCH`'ten türetilir. Güncelleme denetleyicisi sürümleri dört parça karşılaştırır (`2.1.6-crispplay.3` → 2.1.6.3); upstream'in kendi denetleyicisi yalnız `vX.Y.Z` kabul ettiğinden bu politika `custom/include/crispplay/Branding.h` içinde. GitHub Release'ler "pre-release" işaretlenmemeli, yoksa denetleyici yok sayar.

## GPL-3 ve marka kuralları

Fork'u kendin kullanırken yükümlülük yok; dağıttığın anda kaynak kodun da GPL-3 ile açık olmalı. Bu bir hukuki görüş değil, temel kuralların özeti.

- `LICENSE`, `THIRD_PARTY_NOTICES.md` ve `licenses/` aynen korunur.
- Hakkında sayfasında: "Veyra (Likely7) tabanlıdır" atfı ve orijinal repo bağlantısı.
- Her dağıtılan paketin yanında, birebir karşılık gelen kaynak arşivi (upstream'in `-sources` yaklaşımı gibi).
- Kapalı kaynak NVIDIA/AMD bileşenleri repoya konmaz; upstream'in `.gitignore` kuralları korunur.
- Uygulamanın görünen adı ve ikonu farklı olur, böylece kullanıcı orijinalle karıştırmaz ve hata bildirimlerini doğru yere yapar.

## Açık sorular ve sonraki adımlar

- [x] Fork'un adı: CrispPlay (kod içi kısa ad: `CrispPlay`, sürüm eki `-crispplay.N`)
- [x] GitHub fork'u: `efetuncer1-creator/CrispPlay`
- [x] Test GPU'su: NVIDIA GeForce RTX 3060 Ti
- [x] Discord uygulama kimliği `Branding.h`'de
- [x] Aşama 0–5 tamamlandı ve `main`'de
- [ ] Efe: `SYNC_TOKEN` secret'ı ve Actions'ın PR açma izni (repo ayarları)
- [x] CrispPlay ikonu (Gemini, `custom/branding/`)
- [x] İlk sürüm: `v2.1.5-crispplay.1` (latest) ve `subs-models-v1` (ön sürüm, altyazı modelleri) 2026-11-13'te yayınlandı
- [ ] Veri/ayar klasörü: `setApplicationName("Veyra")` değişsin mi? Değişirse orijinal Veyra ile ayarlar ayrılır ama mevcut ayarlar taşınmaz.

## Claude Code için başlangıç talimatı

Ön incelemede (upstream commit `d57ac32`, v2.1.5) tespit edilen, Aşama 0 için önemli noktalar:

- **Güncelleme kontrolü:** `src/ui/UpdateChecker.cpp` satır ~82–84, release URL'sini sıkı doğruluyor: host `github.com`, yol `/Likely7/Veyra-NRVideo/releases/tag/<tag>`. Endpoint kurucuda parametre (`QUrl endpoint`). İkisi de CrispPlay reposuna çevrilmeli; adres sabitleri `custom/branding/` içindeki tek bir header'dan gelmeli, upstream dosyasında sadece kanca kalmalı.
- **Çeviri:** `i18n/catalog.json` → `{"entries":[{"zh","zh-TW","en","ja","src"}]}`. `src/ui/UiLanguage.cpp` satır ~83 bunu `:/i18n/catalog.json` kaynağından yüklüyor; Qt kaynağı `CMakeLists.txt` satır ~1270 (`qt_add_resources(veyra_qml_ui "veyra_i18n" …)`). **Düzeltme:** kaynak dil Çince; çevirmen `zh` metnine göre arıyor (`ctx` ayırt edici ayrı anahtar). Türkçe `custom/i18n/tr.json` de `zh` (+ varsa `ctx`) ile anahtarlanır, `en` yalnız referans. `resolve()`'daki bilinen diller listesine `tr` eklenecek.
- **Kayıt altyapısı (Aşama 4 için):** `include/veyra/engine/LiveRecorder.h` → `LiveRecorder : PresentCapture` (`record`, `submitted`, `discarded`). Encoder'lar `src/sink/NvencLiveEncoder.cpp`, `src/sink/MfVideoEncoder.cpp`.
- **Presetler (Aşama 6 için):** `include/veyra/engine/PresetLibrary.h`.
- **CI yok:** upstream'de sadece star-history workflow'u var, testler CTest'e bağlı değil. Kendi Windows build workflow'umuzu eklemek iyi olur ama upstream dosyalarını değiştirmeden.
- **Repo boyutu:** kökteki `REAMDE MP4.mp4` (28 MB) ve büyük medya upstream'e ait; dokunma (silmek her sync'te çakışma yaratır).

Aşama 0 için yapılacaklar sırasıyla:

1. GitHub'da Likely7/Veyra-NRVideo'yu fork et (CrispPlay reposu), `upstream` remote ekle, etiketleri çek.
2. `custom/` iskeletini oluştur: `CMakeLists.txt`, `HOOKS.md`, `PLAN.md` (bu dosya), `branding/`, `i18n/tr.json` (boş başlangıç), `src/`, `include/`, `qml/`.
3. Kök `CMakeLists.txt` sonuna tek satır kanca: `add_subdirectory(custom)` (`# VEYRA_CUSTOM` işaretli) ve `HOOKS.md`'ye kaydet.
4. Marka: uygulama adı "CrispPlay", güncelleme adresi CrispPlay reposu, sürüm eki `-crispplay.N`. Hakkında sayfasına Veyra/Likely7 atfı.
5. `.github/workflows/upstream-sync.yml`: günlük çalışır, yeni upstream sürüm etiketini (`v*`, `-sources` hariç) bulur, `sync/vX.Y.Z` dalı açıp merge eder ve PR açar.
6. Bu sistem Windows-only; Linux runner'da derleme denenmez. Derleme ve test Efe'nin Windows makinesinde yapılır.
