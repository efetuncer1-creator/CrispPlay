# Anlık tekrar (Aşama 4) tasarım notu

2026-11-11 · durum: tasarım (ölçümler ve doğrulama bu dosyanın sonuna eklenir)

Amaç: oynatıcının gösterdiği işlenmiş görüntünün (SR/NR/FG sonrası, canlı kayıtla aynı kaynak) ve çalınan sesin son N saniyesini bir kısayolla MP4 olarak kaydetmek. Varsayılan kapalı.

## Upstream'de ne var

- `VideoPresenter::present()` sunum komut listesinin içinde `PresentCapture::record()` çağırır; `LiveRecorder` (F8 kaydı) bu arayüzü uygular. Kare `RgbToNv12` compute geçişiyle NV12/P010'a çevrilir, 6 yuvadan birine kopyalanır, sunumun fence değeri NVENC'e "bu değerden sonra oku" diye verilir (`NvencLiveEncoder`, D3D12 girişli). Motor iş parçacığı hiç beklemez; yuva yoksa kare atlanır ve sayılır.
- NVENC ayarı: P4 / high quality, IPPP (B kare yok), VBR, GOP = fps×2 kare, `idrPeriod = gopLength`, her IDR'de SPS/PPS tekrar. AMD/Intel'de Media Foundation yolu kareyi CPU'ya geri okur.
- Ses: `WasapiAudioSink` çalınan PCM'i (ses düzeyi ve sessize almadan önce) `RecordingAudioSink`'e verir; damga `recordingClockUs()` (steady_clock) üzerinde hoparlöre varış anıdır. `LiveRecorder` bunu swresample ile 48 kHz stereo AAC'ye çevirir, saat kaymasını yeniden örneklemeyle emer.
- Dosya: FFmpeg `mp4` muxer, parçalı MP4; NVENC'in Annex B çıktısı `extradata` = SPS/PPS ile yazılır.
- Kısayollar `src/ui/bridge/Preferences.cpp` içindeki `shortcutDefaults()` tablosunda (F7 ekran görüntüsü, F8 kayıt, F11 tam ekran…); kayıt klasörü `recordDir` tercihi (`veyra.recordingDirectory`, varsayılan `Videolar\Veyra`).

## Veri yolu

```
VideoPresenter ──record()──> CaptureFanout ──> LiveRecorder (upstream, değişmedi)
                                   │
                                   └─> ReplayCapture: RgbToNv12 → NV12/P010 yuvası (4 adet)
                                          │ fence + yuva no (CPU beklemesi yok)
                                          v
                                   kodlayıcı iş parçacığı: NvencLiveEncoder → Annex B paket
                                          │
WasapiAudioSink ──PCM──> AudioFanout ──> ses iş parçacığı: swr → AAC paket
                                          │
                                          v
                                   PacketRing (RAM, kodlanmış paketler, süre + bayt sınırı)
                                          │ kısayol: snapshot (yalnız işaretçi kopyası)
                                          v
                                   kaydetme iş parçacığı: FFmpeg mp4 → <kayıt klasörü>\CrispPlay-Tekrar-YYYY-AA-GG-SSDDSS.mp4
```

- Halka tampon ham kare değil **kodlanmış paket** tutar. Paket verisi `shared_ptr<const vector<uint8_t>>`; snapshot kilit altında yalnız işaretçi kopyalar, dosya yazımı kilitsiz ve arka planda olur.
- Zaman damgaları tek saatte (`recordingClockUs`, µs): video = `record()` anı, ses = hoparlöre varış anı. Kaydederken klibin ilk anahtar karesine göre sıfırlanır.
- Kesim: bitiş = tampondaki son video paketi; başlangıç = `bitiş − N` anında ya da ondan önceki son anahtar kare. Bu yüzden klip N ile N + 2 sn arası sürer. Anahtar kare aralığı zamanla da sınırlanır: kodlayıcı iş parçacığı son zorlanmış IDR'den 2 sn geçtiyse `forceKey` ister (düşük fps'te GOP = fps×2 varsayımı tutmasa bile).
- Tahliye: en eski GOP, bir sonraki anahtar kare `en yeni − N`'den eskiyse atılır (yani tampon N + ≤1 GOP tutar). Bayt sınırı aşılırsa süreye bakılmadan en eski GOP atılır. Ses paketleri en eski video paketinden eskiyse atılır.
- Ses seyrek tutulur: duraklatma/sessizlikte paket üretilmez. Dosya yazılırken boşluklar önceden kodlanmış bir sessiz AAC karesiyle doldurulur (1024 örneklik ızgara), böylece ses izi klip boyunca kesintisizdir. Pencerede hiç ses paketi yoksa dosya sessiz (yalnız video) yazılır.
- Çıktı parçalı değil, normal MP4 (`moov` sonda). Önce `.part` dosyasına yazılır, başarıyla bitince `.mp4` olarak yeniden adlandırılır; yarım dosya kalmaz.

## Kancalar (upstream'e dokunuş)

Tümü `crispplay/replay/Hooks.h` içindeki başlık-içi (header-only) dağıtıcıya gider; arka uç kurulmamışsa (testler, araçlar) hepsi geçirgen davranır ve hiçbir upstream hedefi yeni kitaplık bağlamaz.

| Dosya | Satır | İş |
| --- | --- | --- |
| `src/engine/EngineController.cpp` | 3 | include; `presenter.setCapture(wrapCapture(recorder_))`; oturum sonunda `sessionEnded()` |
| `src/sink/WasapiAudioSink.cpp` | 3 | include; ses musluğu koşuluna `audioWanted()`; musluğu `audioTap()` ile çatallama |
| `apps/veyra-qml/main.cpp` | 2 | include; `installQml(engine, dataDirectory)` |
| `qml/Veyra/Main.qml` | 1 | `ReplayHost.qml` yükleyicisi (kısayol + bildirim) |
| `qml/Veyra/SettingsPage.qml` | 2 | Oynatma sayfasına `ReplaySettings.qml`; kısayol listesine satır |
| `src/ui/bridge/Preferences.cpp` | 1 | `shortcutDefaults()` tablosuna `replay = F9` |

`LiveRecorder`, `NvencLiveEncoder`, `VideoPresenter` değişmez.

## Kayıtla birlikte çalışma: kodlayıcı neden paylaşılmıyor

Anlık tekrar kendi NVENC oturumunu ve kendi dönüştürme geçişini kullanır; F8 kaydı sürerken ikisi yan yana çalışır. Paylaşmamanın nedenleri:

1. Kaydın ayarları kullanıcıya aittir (bit hızı, H.264/HEVC, kare hızı sınırı); tekrarın bit hızı ise bellek bütçesinden türetilir. Tek kodlayıcı ikisini birden karşılayamaz.
2. Kayıt başlar/biter, boyut değişince yeni dosyaya geçer; her seferinde yeni parametre kümesi (SPS/PPS) gelir. Halka tampon tek bir akış yapılandırması tutabildiği için her kayıt başlangıcı/bitişi tamponu boşaltırdı.
3. Paylaşım `LiveRecorder::Segment` içinde yazıcı iş parçacığı, ses kodlayıcı ve birikme sayaçlarına 5 satırdan çok daha fazla dokunmayı gerektirir; upstream bu dosyayı sık değiştiriyor.

Bedeli: ikisi açıkken ikinci bir dönüştürme geçişi + ikinci NVENC oturumu (ölçüm aşağıda). Sürücünün eşzamanlı oturum sınırı (güncel sürücülerde 8) yeterlidir.

## Bellek bütçesi

- RAM (halka tampon): bayt sınırı varsayılan **512 MiB**. Bit hızı hem upstream'in otomatik formülünden (0,15 bit/piksel, HEVC ×0,65) hem de `sınır × 8 × 0,8 / (N + 2)`'den küçük olanıdır; böylece normal çalışmada süre sınırı bayt sınırından önce devreye girer. 1080p60 H.264: otomatik 19 Mbps → 30 sn ≈ 76 MiB, 120 sn ≈ 290 MiB. 4K60: 30 sn'de 75 Mbps ≈ 300 MiB, 120 sn'de bit hızı 26 Mbps'e kısılır. Sınır en fazla bir GOP kadar (≤ 2 sn) aşılabilir.
- Ses: 192 kbps AAC, 120 sn ≈ 3 MiB. PCM kuyruğu en fazla 4 sn (aşarsa en eski blok atılır).
- VRAM: 4 NV12 yuvası + Y/UV ara dokular = genişlik×yükseklik×1,5×5 bayt (1080p ≈ 15 MiB, 4K ≈ 60 MiB) + NVENC oturumunun kendi belleği. NVENC çıktı tamponları sistem belleğinde 4 × (genişlik×yükseklik×1,5) bayt (1080p ≈ 12 MiB).
- Kaydetme sırasında snapshot paketleri canlı tutar; tahliye edilenler dosya bitene kadar serbest kalmaz (geçici fazlalık ≈ bit hızı × yazma süresi).

## İş parçacıkları

| İş parçacığı | Ne yapar | Bekleme |
| --- | --- | --- |
| Motor (sunum) | `record()`: compute geçişi + kopya komutları; `submitted()`: yuvayı kuyruğa koyar | Yok. Yuva yoksa kare atlanır (`dropped`). İlk 0,5 sn kare hızı ölçülür, sonra oturum açılır |
| Kodlayıcı | NVENC aç, `submit`/`takeOldest`, paketi halkaya it | Yalnız NVENC çıktı fence'i (kendi iş parçacığında, ≤ 3 sn zaman aşımı) |
| Ses sahibi (WASAPI) | PCM'i kopyalayıp sınırlı kuyruğa koyar | Yok |
| Ses işçisi | swr + AAC, paketi halkaya iter | Koşul değişkeni |
| Kaydetme | snapshot → MP4 | Disk. Aynı anda tek kaydetme; ikincisi "meşgul" hatası alır (sınırsız kuyruk yok) |
| Arayüz | ayar, kısayol, 300 ms'de bir durum sorgusu | Yok |

Kuyrukların hepsi sınırlıdır: 4 yuva, NVENC'te en fazla 4 bekleyen çıktı, 4 sn PCM, 1 kaydetme işi. GPU karesi CPU'ya geri okunmaz; geçiş başına CPU fence beklemesi yoktur.

## Hata durumları

| Durum | Davranış |
| --- | --- |
| NVIDIA olmayan ekran kartı (AMD/Intel) | Desteklenmez: MF yolu her kareyi CPU'ya geri okur, sürekli çalışan bir özellik için kabul edilmedi. Ayar sayfasında neden yazılır, kodlayıcı açılmaz |
| NVENC açılamadı / kodlama hatası | Oturum kapatılır, hata durumda gösterilir, 15 sn sonra yeniden denenir (her karede deneme yok) |
| Görüntü boyutu veya HDR modu değişti, yeni içerik açıldı | Yeni kodlayıcı oturumu, tampon boşalır (tek dosyada iki farklı SPS olamaz). Kullanıcıya not: SR hedefini değiştirmek tamponu sıfırlar |
| Tek/çift olmayan ya da 16 pikselden küçük boyut | Kodlanmaz (upstream ile aynı kural) |
| Yuvalar dolu (kodlayıcı yetişemiyor) | Kare atlanır, `dropped` artar; sunum etkilenmez |
| Tampon boş / özellik kapalı iken kısayol | Hata bildirimi, dosya yazılmaz |
| Disk dolu, klasör yok, yazma hatası | `.part` silinir, hata bildirimi; tampon olduğu gibi kalır, yeniden denenebilir |
| Kaydetme sürerken ikinci istek | "Önceki kayıt sürüyor" bildirimi |
| AAC kodlayıcı yok / ses yok | Video tek başına yazılır |
| Uygulama kapanışı | Özellik kapatılır, iş parçacıkları en fazla 3 sn beklenir |

## Bilinen sınırlar (bu aşamada)

- Oyun modunda (fare geçişi) klavye kısayolları upstream'in `GameHotkeyListener`'ından geçer; o tablo dört sabit eylem taşıyor. Anlık tekrar kısayolu şimdilik yalnız CrispPlay penceresi odaktayken çalışır.
- Duraklatılmış içerik: sunum durduğu için kare gelmez; klip son sunulan karede biter.
