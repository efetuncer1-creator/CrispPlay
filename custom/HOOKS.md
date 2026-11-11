# Upstream dosyalarındaki kancalar

Her satır `VEYRA_CUSTOM` ile işaretli. Upstream sync'te çakışma çıkarsa: upstream'in yeni hâlini al, aşağıdaki kancayı aynı anlamla yeniden ekle.

Kontrol: `git grep -n VEYRA_CUSTOM -- ':!custom'` şu an **33** sonuç vermeli (`version.rc.in` satırları yorumsuz olduğu için sayılmaz; onlar `@CRISPPLAY_` ile aranır).

| Dosya | Satır | Ne yapar |
| --- | --- | --- |
| `CMakeLists.txt` | `project()` altı, 1 | `custom/branding/Branding.cmake`: sürüm etiketi `X.Y.Z-crispplay.N`, ürün adı, `custom/include` yolu |
| `CMakeLists.txt` | dosya sonu, 1 | `add_subdirectory(custom)` |
| `apps/veyra/version.rc.in` | 2 (yorumsuz) | EXE `FileDescription` / `ProductName` → `@CRISPPLAY_FILE_DESCRIPTION@` / `@CRISPPLAY_PRODUCT_NAME@` |
| `include/veyra/ui/UpdateChecker.h` | 2 | Branding include; varsayılan endpoint `crispplay::branding::releaseApiUrl()` |
| `src/ui/UpdateChecker.cpp` | 5 | Include; `versionNumber` → `releaseVersion` (4 parçalı sürüm, `-crispplay.N`); URL doğrulama → `isReleaseUrl`; parça sayısı kontrolü → `isNull()`; gösterilen sürüm → `displayVersion` |
| `src/ui/bridge/Preferences.cpp` | 4 + 1 | Include; README / Issues / Releases adresleri branding'den. Anlık tekrar: `shortcutDefaults()` tablosuna `{"replay", "F9"}` (bir önceki satırın sonu `}};` → `},` oldu) |
| `apps/veyra-qml/main.cpp` | 2 + 2 | Include; QML'e `crispBrand` bağlam özelliği. Anlık tekrar: `ReplayQml.h` include + `crispplay::replay::installQml(engine, dataDirectory)` (`crispReplay` bağlam nesnesi) |
| `qml/Veyra/Main.qml` | 1 + 1 | Pencere başlığı `crispBrand.appName`. Anlık tekrar: kayıt `Shortcut` satırının altında `Loader { source: "qrc:/crispplay/ReplayHost.qml" }` (kısayol + bildirim) |
| `qml/Veyra/HomePage.qml` | 1 | Ana sayfa başlığı `crispBrand.homeTitle` |
| `qml/Veyra/SettingsPage.qml` | 2 + 2 | Hakkında: ad + "Veyra (Likely7) tabanlıdır" GPL atfı. Anlık tekrar: `set-recording` grubunun hemen altında `Loader { … "qrc:/crispplay/ReplaySettings.qml" }`; kısayol listesinde `record` satırının altında `{ id: "replay", … }` |
| `src/engine/EngineController.cpp` | 3 | Anlık tekrar: `Hooks.h` include; `presenter.setCapture(crispplay::replay::wrapCapture(recorder_.get()))` (sunum yakalamasını kayıt + tekrar olarak çatallar); oturum sonu `OnExit` içinde `crispplay::replay::sessionEnded()` |
| `src/sink/WasapiAudioSink.cpp` | 3 | Anlık tekrar: `Hooks.h` include; ses musluğu koşulu `recordingAudioActive() \|\| crispplay::replay::audioWanted()`; musluk `crispplay::replay::audioTap(recordingAudioSink())` |
| `tests/unit/UpdateCheckerTests.cpp` | 1 | Test release adresi branding'den (upstream testleri bizim repoyla geçsin) |

Anlık tekrar kancalarının hepsi `custom/include/crispplay/replay/Hooks.h` (başlık-içi dağıtıcı) ve `ReplayQml.h` üzerinden geçer; arka uç kurulmadıysa (araçlar, testler) upstream davranışı aynen kalır. Tasarım: `custom/docs/REPLAY.md`.

QML kancaları `typeof crispBrand !== "undefined"` / `typeof crispReplay !== "undefined"` korumalı; bağlam özelliği olmadan yüklenen QtQuick testleri eski metinle çalışır.

## Bilinçli olarak dokunulmayanlar

- `app.setApplicationName("Veyra")` / `setOrganizationName` (`apps/veyra-qml/main.cpp`): veri ve ayar klasörünü belirliyor olabilir. Değiştirmek kullanıcı ayarlarını taşır; ayrı karar gerektirir.
- Sponsor bağlantıları: orijinal yazara (Likely7) gider, olduğu gibi kalır.
- "Veyra 虚拟扬声器" (sanal hoparlör) sürücü adı: sürücünün gerçek adı.

## Bekleyen

- `assets/veyra.ico` ve `qml/Veyra/logo.png`: CrispPlay ikonu henüz yok. Hazır olunca `custom/branding/` altına konup derlemede seçilecek (yeni kanca gerekecek).
- `scripts/package-*.py`: sürüm paketleme betikleri Likely7 adreslerini yazıyor. İlk CrispPlay sürümünden önce `custom/scripts/` altında kendi paketleme betiğimiz yazılacak; upstream betiklerine dokunulmayacak.
