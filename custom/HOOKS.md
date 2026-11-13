# Upstream dosyalarındaki kancalar

Her satır `VEYRA_CUSTOM` ile işaretli. Upstream sync'te çakışma çıkarsa: upstream'in yeni hâlini al, aşağıdaki kancayı aynı anlamla yeniden ekle.

Kontrol: `git grep -n VEYRA_CUSTOM -- ':!custom'` şu an **53** sonuç vermeli: aşağıdaki tablodaki 52 kanca satırı + `.github/workflows/upstream-sync.yml` içindeki bu komutun kendisini anan 1 satır (`version.rc.in` satırları yorumsuz olduğu için sayılmaz; onlar `@CRISPPLAY_` ile aranır).

| Dosya | Satır | Ne yapar |
| --- | --- | --- |
| `CMakeLists.txt` | `project()` altı, 1 | `custom/branding/Branding.cmake`: sürüm etiketi `X.Y.Z-crispplay.N`, ürün adı, `custom/include` yolu |
| `CMakeLists.txt` | dosya sonu, 1 | `add_subdirectory(custom)` |
| `apps/veyra/version.rc.in` | 2 (yorumsuz) | EXE `FileDescription` / `ProductName` → `@CRISPPLAY_FILE_DESCRIPTION@` / `@CRISPPLAY_PRODUCT_NAME@` |
| `include/veyra/ui/UpdateChecker.h` | 2 | Branding include; varsayılan endpoint `crispplay::branding::releaseApiUrl()` |
| `src/ui/UpdateChecker.cpp` | 5 | Include; `versionNumber` → `releaseVersion` (4 parçalı sürüm, `-crispplay.N`); URL doğrulama → `isReleaseUrl`; parça sayısı kontrolü → `isNull()`; gösterilen sürüm → `displayVersion` |
| `src/ui/bridge/Preferences.cpp` | 5 + 1 | Include; README / Issues / Releases adresleri branding'den; `language` tercihi `crispplay::i18n::extraLanguages()` dillerini de kabul eder (Türkçe). Anlık tekrar ve altyazı çevirisi: `shortcutDefaults()` tablosuna tek satırda `{"replay", "F9"}, {"subs", "F10"}` (bir önceki satırın sonu `}};` → `},` oldu) |
| `apps/veyra-qml/main.cpp` | 2 | Include; QML'e `crispBrand` bağlam özelliği |
| `apps/veyra-qml/main.cpp` | 2 | Discord Rich Presence: include + `crispplay::discord::install(...)` (`crispDiscord` bağlam özelliği, köprüye Qt sinyaliyle bağlanır, çıkışta temizler) |
| `apps/veyra-qml/main.cpp` | 2 | Anlık tekrar: `ReplayQml.h` include + `crispplay::replay::installQml(engine, dataDirectory)` (`crispReplay` bağlam nesnesi) |
| `apps/veyra-qml/main.cpp` | 2 | Altyazı çevirisi: `crispplay/subs/app/Hooks.h` include + `crispplay::subs::installQml(engine, &bridge, dataDirectory)` (`crispSubs` bağlam nesnesi, katman arka ucu; köprüden yalnız oynatma konumu okunur) |
| `qml/Veyra/Main.qml` | 1 + 1 | Pencere başlığı `crispBrand.appName`. Anlık tekrar: kayıt `Shortcut` satırının altında `Loader { source: "qrc:/crispplay/ReplayHost.qml" }` (kısayol + bildirim) |
| `qml/Veyra/HomePage.qml` | 1 | Ana sayfa başlığı `crispBrand.homeTitle` |
| `qml/Veyra/SettingsPage.qml` | 3 | Hakkında: ad + "Veyra (Likely7) tabanlıdır" GPL atfı; Arayüz dili menüsüne `crispBrand.languages` (Türkçe) eklenir |
| `qml/Veyra/SettingsPage.qml` | 2 | Discord sekmesi: `sections` listesine Sponsor'dan önce `{ id: "discord" }` girdisi; Sponsor panelinin üstünde `root.section === "discord"` iken `Loader` ile `custom/qml/DiscordSettings.qml` (`custom/CMakeLists.txt` dosyayı `qml/CrispPlay/` altına kopyalar) |
| `qml/Veyra/SettingsPage.qml` | 2 | Anlık tekrar: `set-recording` grubunun hemen altında `Loader { … "qrc:/crispplay/ReplaySettings.qml" }`; kısayol listesinde `record` satırının altında `{ id: "replay", … }` |
| `qml/Veyra/SettingsPage.qml` | 3 | Altyazı çevirisi sekmesi: `sections` listesinde Discord'un altında `{ id: "subs", label: "Altyazı çevirisi", icon: "cc" }`; Discord `Loader`'ının altında `root.section === "subs"` iken `Loader { … "qrc:/crispplay/SubsSettings.qml" }`; kısayol listesinde `replay` satırının altında `{ id: "subs", … }` |
| `qml/Veyra/CineBar.qml` | 2 | Altyazı çevirisi, oynatıcı altyazı menüsü (`字幕` `VMenu`'sü): `items` zincirinin sonuna `.concat(typeof crispSubs !== "undefined" ? crispSubs.menuItems : [])` ("Altyazı çevirisi" bölümü); `onPicked`'in ilk satırı `if (o.crisp) return crispSubs.menuPicked(o)` (bizim öğelerimiz `crisp` alanı taşır, upstream öğeleri taşımaz) |
| `src/ui/bridge/Subtitles.cpp` | 2 | Altyazı çevirisi: `crispplay/subs/app/Hooks.h` include; `tickSubtitles()` içinde `updateSubtitleOverlay`'den hemen önce `crispplay::subs::appendLines(draw, view, client)` (Türkçe satırı `draw`'a resim olarak ekler; `!s.capture` koşulunun dışında, yani yakalama kaynaklarında da çalışır) |
| `src/ui/UiLanguage.cpp` | 5 | `crispplay/I18n.h` include; `load()` katalog baytlarını `catalogJson(code, …)`'dan geçirir (`tr` için `:/crispplay/i18n/tr.json` upstream kataloğun üstüne birleştirilir, eksikler İngilizce); `；` birleştirmesi Latin dillerde `; ` (`latinScript`); `resolve()` açık `tr` tercihini ve Türkçe Windows dilini `tr`'ye eşler |
| `src/engine/EngineController.cpp` | 3 | Sunum yakalama zinciri (anlık tekrar + altyazı bandı): `crispplay/present/Hooks.h` include; `presenter.setCapture(crispplay::present::wrapCapture(recorder_.get()))` (sunum yakalamasını kayıt + kurulu dinleyiciler olarak çatallar); oturum sonu `OnExit` içinde `crispplay::present::sessionEnded()` |
| `src/sink/WasapiAudioSink.cpp` | 3 | Anlık tekrar: `Hooks.h` include; ses musluğu koşulu `recordingAudioActive() \|\| crispplay::replay::audioWanted()`; musluk `crispplay::replay::audioTap(recordingAudioSink())` |
| `tests/unit/UpdateCheckerTests.cpp` | 1 | Test release adresi branding'den (upstream testleri bizim repoyla geçsin) |

Görüntü kancası ortak: `custom/include/crispplay/present/Hooks.h` (başlık-içi zincir; `addTap` ile anlık tekrar ve altyazı bandı eklenir). Anlık tekrarın ses kancaları `custom/include/crispplay/replay/Hooks.h`, arayüzü `ReplayQml.h` üzerinden geçer; dinleyici/arka uç kurulmadıysa (araçlar, testler) upstream davranışı aynen kalır. Tasarım: `custom/docs/REPLAY.md`.

Altyazı çevirisi kancaları `custom/include/crispplay/subs/app/Hooks.h` üzerinden geçer (`appendLines` başlık-içi dağıtıcı, arka uç `installQml` ile kurulur). Kısayol, bildirim, bölge seçici penceresi ve menüden ayarlara gitme için `Main.qml`'e yeni satır eklenmedi: `custom/qml/ReplayHost.qml` kendi içinden `SubsHost.qml`'i yükler (`SubsHost` Main.qml'in `root`, `settingsPage`, `fullBar` kimliklerini kullanır). Tasarım: `custom/docs/SUBS.md`.

QML kancaları `typeof crispBrand !== "undefined"` / `typeof crispReplay !== "undefined"` / `typeof crispSubs !== "undefined"` korumalı; bağlam özelliği olmadan yüklenen QtQuick testleri eski metinle çalışır.

## Bilinçli olarak dokunulmayanlar

- Logo/ikon: upstream dosyalarına kanca yok. `custom/CMakeLists.txt` kökün derleme klasörüne kopyaladığı `veyra.ico` ve `qml/Veyra/logo.png` dosyalarının üzerine `custom/branding/crispplay.ico` ve `logo-256.png` yazar (EXE/pencere ikonu ve uygulama içi logo). Kaynak: Gemini ile üretildi, seçenekler `custom/branding/concepts/`.

- `qml/Veyra/Theme.qml` yazı tipi seçimi: `ja` ve `zh-TW` dışındaki her dil (`en` ve `tr` dahil) aynı Latin yolunu (`Noto Sans SC`) kullanır; Türkçe için kanca gerekmedi. Windows'ta Noto Sans SC yoksa Qt varsayılan GUI yazı tipine (MS Shell Dlg 2 = Tahoma) düşüyordu; Tahoma'nın ipuçlu 11 px çizimi menü başlığında "Al tyazı" gibi boşluklar açıyordu. `custom/subs/app/src/SubsController.cpp` `installInterfaceFont()` (QML yüklenmeden, `installQml` içinden) yalnız Noto Sans SC kurulu değilken `QFont::insertSubstitutions("Noto Sans SC", {"Segoe UI", "Microsoft YaHei UI"})` yapar: kanca gerekmedi. `CRISPPLAY_KEEP_UI_FONT=1` eski davranışı geri getirir (karşılaştırma için).
- `i18n/catalog.json`: Türkçe metinler yalnızca `custom/i18n/tr.json` içinde; bakım betiği `custom/scripts/i18n_check.py`.
- `app.setApplicationName("Veyra")` / `setOrganizationName` (`apps/veyra-qml/main.cpp`): veri ve ayar klasörünü belirliyor olabilir. Değiştirmek kullanıcı ayarlarını taşır; ayrı karar gerektirir.
- Sponsor bağlantıları: orijinal yazara (Likely7) gider, olduğu gibi kalır.
- "Veyra 虚拟扬声器" (sanal hoparlör) sürücü adı: sürücünün gerçek adı.

## Bekleyen

- `scripts/package-*.py`: sürüm paketleme betikleri Likely7 adreslerini yazıyor. İlk CrispPlay sürümünden önce `custom/scripts/` altında kendi paketleme betiğimiz yazılacak; upstream betiklerine dokunulmayacak.
