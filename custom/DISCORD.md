# Discord Rich Presence

CrispPlay, Discord profilinde "CrispPlay ile izliyor" gösterebilir. **Varsayılan kapalıdır** ve Discord uygulama kimliği (Application ID) girilmeden hiç açılamaz.

## Uygulamayı Discord'da oluşturma (bir kez, Efe)

1. https://discord.com/developers/applications adresine gir, **New Application**.
2. Ad: **CrispPlay** (Discord'da "CrispPlay ile izliyor" başlığında bu ad görünür).
3. **General Information** sayfasında **Application ID**'yi kopyala.
4. **Rich Presence > Art Assets** bölümüne CrispPlay ikonunu (en az 512x512 PNG) yükle ve anahtarını tam olarak `crispplay` yap (kod bu anahtarı `large_image` olarak gönderir; `custom/src/discord/Presence.cpp` içinde `kAssetKey`). Yüklenen görselin görünmesi birkaç dakika sürebilir.
5. `custom/include/crispplay/Branding.h` içindeki `discordAppId()` fonksiyonunu doldur:

   ```cpp
   inline QString discordAppId() { return QStringLiteral("123456789012345678"); }
   ```
6. Yeniden derle. Ayarlar > Hakkında bölümünde "Discord" grubu seçenekleri gösterir.

Kimlik gizli değildir (herkese açık bir uygulama kimliğidir), repoya yazılması sorun değildir. Token ve gizli anahtar gerekmez; bu özellik onları kullanmaz.

## Gizlilik seviyeleri

| Seviye | Discord'da görünen |
| --- | --- |
| **Kapalı** (varsayılan) | Hiçbir şey; Discord'a bağlanılmaz bile. |
| **Temel** | "CrispPlay ile izliyor" + geçen süre. |
| **Ayrıntılı** | Temel + kaynak türü (Video dosyası, Yakalama kartı, Ekran yakalama, PS5 yayını, Xbox yayını, PC yayını, Ağ videosu, Görsel) + açık iyileştirmeler (SR, NR, FG). Örnek: `Video dosyası · SR + NR + FG`. |

**Hiçbir seviyede** dosya adı, yol, medya başlığı, pencere başlığı, ana makine (host) adı, IP ya da hesap adı gönderilmez. Bu, kuralla değil yapıyla sağlanır: yük `buildActivity()` fonksiyonunda, yalnızca sabit alanlı bir `PresenceInput` yapısından (enum'lar, üç bool, başlangıç zamanı) üretilir; yapıda metin alanı yoktur. Testler (`custom/tests/DiscordTests.cpp`) tüm seviye/kaynak/iyileştirme birleşimlerini tarar.

Hiçbir kaynak açık değilken (oynatma yok) etkinlik temizlenir. Ayar `<veri klasörü>/crispplay-discord.json` dosyasına yazılır; Veyra'nın kendi tercih dosyalarına dokunulmaz.

## Nasıl çalışır

- Discord istemcisinin yerel adlandırılmış borusuna (`\\.\pipe\discord-ipc-0` ... `-9`) doğrudan konuşur; üçüncü taraf SDK yoktur. Çerçeve: little-endian `uint32` işlem kodu + `uint32` uzunluk + UTF-8 JSON. El sıkışma `{"v":1,"client_id":"..."}` (op 0), ardından `SET_ACTIVITY` (op 1).
- Tüm boru işlemleri ayrı bir işçi iş parçacığında, zaman aşımlı (overlapped) yapılır; oynatmayı hiçbir noktada bloke etmez. Ana iş parçacığı yalnızca "en son durum" kutusunu günceller (kuyruk büyümez, en yeni kazanır).
- Discord kapalıysa 2 sn'den başlayıp 30 sn'ye çıkan geri çekilmeyle yeniden dener. Ayar kapalıyken ya da gösterilecek bir şey yokken boruya hiç dokunulmaz.
- Etkinlik güncellemeleri en fazla 5 sn'de bir gönderilir (Discord hız sınırı). Çıkışta etkinlik temizlenir.
- Durum kaynağı: köprünün `sourceKind`, `srActive`, `nrActive`, `fgActive` özellikleri ve `snapshotChanged` sinyali (Qt meta-nesne üzerinden; upstream başlığı gerekmez). 750 ms bekletilerek birleştirilir.

## Dosyalar

- `custom/include/crispplay/discord/Presence.h`, `custom/src/discord/Presence.cpp`: saf yük ve çerçeve kodu.
- `custom/src/discord/IpcClient.cpp`: boru istemcisi ve işçi iş parçacığı.
- `custom/src/discord/Controller.cpp`: ayar, köprü izleme, `crispDiscord` QML nesnesi.
- `custom/qml/DiscordSettings.qml`: Ayarlar > Hakkında içindeki grup.
- `custom/tests/DiscordTests.cpp`: yük, çerçeve, boru yokken davranış ve sahte Discord ile uçtan uca test.
