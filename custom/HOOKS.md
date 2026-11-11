# Upstream dosyalarındaki kancalar

Her satır `VEYRA_CUSTOM` ile işaretli. Upstream sync'te çakışma çıkarsa: upstream'in yeni hâlini al, aşağıdaki kancayı aynı anlamla yeniden ekle.

Kontrol: `git grep -n VEYRA_CUSTOM -- ':!custom'` şu an **31** sonuç vermeli: aşağıdaki tablodaki 30 kanca satırı + `.github/workflows/upstream-sync.yml` içindeki bu komutun kendisini anan 1 satır (`version.rc.in` satırları yorumsuz olduğu için sayılmaz; onlar `@CRISPPLAY_` ile aranır).

| Dosya | Satır | Ne yapar |
| --- | --- | --- |
| `CMakeLists.txt` | `project()` altı, 1 | `custom/branding/Branding.cmake`: sürüm etiketi `X.Y.Z-crispplay.N`, ürün adı, `custom/include` yolu |
| `CMakeLists.txt` | dosya sonu, 1 | `add_subdirectory(custom)` |
| `apps/veyra/version.rc.in` | 2 (yorumsuz) | EXE `FileDescription` / `ProductName` → `@CRISPPLAY_FILE_DESCRIPTION@` / `@CRISPPLAY_PRODUCT_NAME@` |
| `include/veyra/ui/UpdateChecker.h` | 2 | Branding include; varsayılan endpoint `crispplay::branding::releaseApiUrl()` |
| `src/ui/UpdateChecker.cpp` | 5 | Include; `versionNumber` → `releaseVersion` (4 parçalı sürüm, `-crispplay.N`); URL doğrulama → `isReleaseUrl`; parça sayısı kontrolü → `isNull()`; gösterilen sürüm → `displayVersion` |
| `src/ui/bridge/Preferences.cpp` | 5 | Include; README / Issues / Releases adresleri branding'den; `language` tercihi `crispplay::i18n::extraLanguages()` dillerini de kabul eder (Türkçe) |
| `apps/veyra-qml/main.cpp` | 2 | Include; QML'e `crispBrand` bağlam özelliği |
| `apps/veyra-qml/main.cpp` | 2 | Discord Rich Presence: include + `crispplay::discord::install(...)` (`crispDiscord` bağlam özelliği, köprüye Qt sinyaliyle bağlanır, çıkışta temizler) |
| `qml/Veyra/Main.qml` | 1 | Pencere başlığı `crispBrand.appName` |
| `qml/Veyra/HomePage.qml` | 1 | Ana sayfa başlığı `crispBrand.homeTitle` |
| `qml/Veyra/SettingsPage.qml` | 3 | Hakkında: ad + "Veyra (Likely7) tabanlıdır" GPL atfı; Arayüz dili menüsüne `crispBrand.languages` (Türkçe) eklenir |
| `qml/Veyra/SettingsPage.qml` | 1 | Hakkında bölümü: `Loader` ile `custom/qml/DiscordSettings.qml` (Discord Rich Presence ayarı; `custom/CMakeLists.txt` dosyayı `qml/CrispPlay/` altına kopyalar) |
| `src/ui/UiLanguage.cpp` | 5 | `crispplay/I18n.h` include; `load()` katalog baytlarını `catalogJson(code, …)`'dan geçirir (`tr` için `:/crispplay/i18n/tr.json` upstream kataloğun üstüne birleştirilir, eksikler İngilizce); `；` birleştirmesi Latin dillerde `; ` (`latinScript`); `resolve()` açık `tr` tercihini ve Türkçe Windows dilini `tr`'ye eşler |
| `tests/unit/UpdateCheckerTests.cpp` | 1 | Test release adresi branding'den (upstream testleri bizim repoyla geçsin) |

QML kancaları `typeof crispBrand !== "undefined"` korumalı; bağlam özelliği olmadan yüklenen QtQuick testleri eski metinle çalışır.

## Bilinçli olarak dokunulmayanlar

- `qml/Veyra/Theme.qml` yazı tipi seçimi: `ja` ve `zh-TW` dışındaki her dil (`en` ve `tr` dahil) aynı Latin yolunu (`Noto Sans SC`, yoksa Qt'nin sistem yedeği) kullanır; Türkçe için kanca gerekmedi.
- `i18n/catalog.json`: Türkçe metinler yalnızca `custom/i18n/tr.json` içinde; bakım betiği `custom/scripts/i18n_check.py`.
- `app.setApplicationName("Veyra")` / `setOrganizationName` (`apps/veyra-qml/main.cpp`): veri ve ayar klasörünü belirliyor olabilir. Değiştirmek kullanıcı ayarlarını taşır; ayrı karar gerektirir.
- Sponsor bağlantıları: orijinal yazara (Likely7) gider, olduğu gibi kalır.
- "Veyra 虚拟扬声器" (sanal hoparlör) sürücü adı: sürücünün gerçek adı.

## Bekleyen

- `assets/veyra.ico` ve `qml/Veyra/logo.png`: CrispPlay ikonu henüz yok. Hazır olunca `custom/branding/` altına konup derlemede seçilecek (yeni kanca gerekecek).
- `scripts/package-*.py`: sürüm paketleme betikleri Likely7 adreslerini yazıyor. İlk CrispPlay sürümünden önce `custom/scripts/` altında kendi paketleme betiğimiz yazılacak; upstream betiklerine dokunulmayacak.
