# Anlık tekrar (Aşama 4) tasarım notu

2026-11-11 · durum: uygulandı, varsayılan kapalı. RTX 3060 Ti üzerinde gerçek oynatmayla doğrulandı; ölçümler ve doğrulanmayanlar dosyanın sonunda.

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
                                   └─> ReplayCapture: RgbToNv12 → NV12/P010 yuvası (6 adet)
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
- VRAM: 6 NV12 yuvası (upstream kaydıyla aynı sayı; 4 yuvayla kayıt + tekrar birlikte çalışırken kare atlandı) + Y/UV ara dokular = genişlik×yükseklik×1,5×7 bayt (1080p ≈ 21 MiB, 4K ≈ 83 MiB) + NVENC oturumunun kendi belleği. NVENC çıktı tamponları sistem belleğinde 4 × (genişlik×yükseklik×1,5) bayt (1080p ≈ 12 MiB).
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

Kuyrukların hepsi sınırlıdır: 6 yuva, NVENC'te en fazla 4 bekleyen çıktı, 4 sn PCM, 1 kaydetme işi. GPU karesi CPU'ya geri okunmaz; geçiş başına CPU fence beklemesi yoktur.

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

## Kullanım

- Ayarlar → Oynatma → **Anlık tekrar** (kayıt kartının altında): aç/kapat, süre 15 / 30 / 60 / 120 sn, durum satırı, "Şimdi kaydet".
- Kısayol **F9** (Ayarlar → Kısayollar'da "Anlık tekrarı kaydet" olarak değiştirilebilir). Mevcut bağlar: F7 ekran görüntüsü, F8 kayıt, F11 tam ekran; F9 boştu.
- Dosya: kayıt klasörü (`Kayıt klasörü` ayarı, varsayılan `Videolar\Veyra`) altında `CrispPlay-Tekrar-YYYY-AA-GG-SSDDSS.mp4`. Kaydedilince dosya adıyla bildirim çıkar.
- Codec, kayıt ayarındaki H.264 / HEVC seçimini izler. Ayarlar `<veri klasörü>\crispplay-replay.v1.json` içinde; `fpsLimit` (varsayılan 60), `maxMegabytes` (512) ve `audio` yalnız dosyadan değiştirilir.

## Testler

- `crispplay_replay_ring_tests` (Qt Test, 14 test): süre ve bayt tahliyesi, ilk anahtar kareden önceki paketin reddi, anahtar kareye hizalı başlangıç, zaman damgası sıfırlama ve süreler, ses/video sıralaması, sesin görüntüyle sınırlanması, ses ızgarası (boşluk doldurma, çakışma). Sonuç: 16 passed / 0 failed (init/cleanup dahil).
- `custom/scripts/replay-test.ps1`: gerçek uygulamayı yalıtılmış `--data-dir` ile açar, yerel bir dosyayı oynatır, kaydı tetikler (test kancası `CRISPPLAY_TEST_REPLAY_SAVE_MS` / `_DIR`, ya da `-Keys` ile gerçek F8/F9 tuşları), çıkan her MP4'ü `crispplay_replay_clip_check` ile doğrular (FFmpeg kitaplıklarıyla: kapsayıcı, akışlar, ilk paket anahtar kare, artan zaman damgaları, iki izin de tam yazılım çözümü, ses süresi = görüntü süresi ±100 ms) ve süreç sayaçlarını örnekler.

Bu makinede (RTX 3060 Ti, sürücü NVENC, kaynak: depodaki `REAMDE MP4.mp4`, 1080p60 H.264 + AAC, 14 sn) alınan sonuçlar:

| Çalıştırma | Sonuç |
| --- | --- |
| Açık, 15 sn pencere, H.264 (5 çalıştırma) | 9,1–10,3 sn klip (dosya 14 sn olduğu için pencere dolmadı), 548–618 kare, 60,00 fps, ~18 Mbps, ses 48 kHz stereo; hepsi VALID, atlanan kare 0 |
| Açık, HEVC | 9,35 sn, 561 kare, 12 Mbps hedef / 13,4 ölçülen; VALID |
| 5 sn pencere (tahliye; test kancası `CRISPPLAY_TEST_REPLAY_SECONDS`) | 803 kare kodlandı, klip 5,28 sn / 317 kare (pencere + <1 GOP), VALID |
| F8 kayıt + F9 tekrar birlikte (gerçek tuşlar) | Kayıt 3,00 sn / 180 kare / atlanan 0; tekrar 9,92 sn / 595 kare / atlanan 0; iki dosya da VALID. 4 yuvalı ilk denemede tekrar klibi 60 yerine ~56 fps çıktı; yuva sayısı 6'ya çıkarıldı |
| Duraklatılmış oynatma (istemeden: test sırasında dışarıdan gelen bir tuş oynatmayı durdurdu, iki kez) | Klip son sunulan karede bitti (1,0 sn), dosya çözülebilir |
| Kapanış | Her çalıştırmada çıkış kodu 0, `.part` kalmadı |

Yazma süresi 10 sn / 22 MiB klip için 7–16 ms.

## Maliyet ölçümü

Yöntem: aynı dosya, aynı pencere (1280×720 pencere, 1920×1080 çalışma boyutu), SR/NR/FG **kapalı**; tekrar kapalı ve açık üçer çalıştırma, sırayla. Değerler kararlı bölümdeki (ilk 6 sn ve son 1,5 sn hariç) saniyelik örneklerin ortancası: motorun kendi `[player-timing]` satırları (`presentP95Ms`, `gpuBlitP95Ms`, `slotWaits`) ve Windows sayaçları (`\GPU Engine(pid_*)\Utilization Percentage` 3D ve VideoEncode, `\GPU Process Memory`, sürecin özel belleği). Ölçüm sırasında masaüstünde başka uygulamalar (başka bir oynatıcı örneği dahil) çalışıyordu; GPU süreleri gürültülü, her çalıştırma yalnız ~8 sn kararlı örnek veriyor.

| Ölçü | Kapalı (3 çalıştırma) | Açık (3 çalıştırma) | Fark |
| --- | --- | --- | --- |
| Sunum CPU süresi `presentP95Ms` | 0,35 / 0,36 / 0,41 ms | 0,40 / 0,43 / 0,44 ms | ≈ +0,05 ms |
| Sunum listesi GPU süresi `gpuBlitP95Ms` (dönüştürme + kopya bu listede) | 0,61 / 0,65 / 0,82 ms | 1,36 / 1,52 / 1,55 ms; sonraki 6 yuvalı çalıştırmalarda 0,70 / 0,82 / 0,87 ms | +0,1 … +0,9 ms (gürültülü) |
| Yuva beklemesi `slotWaits` | 0 | 0 | 0 |
| Sunulan kare hızı | 60 | 60 | 0 |
| GPU 3D motoru (süreç) | %9,8 / 12,6 / 12,7 | %8,6 / 9,1 / 9,3 | artış görülmedi (gürültü içinde) |
| GPU VideoEncode motoru (süreç) | %0 | %35 / 41 / 43 (HEVC %45) | NVENC'in ayrı motoru; 1080p60 P4/HQ |
| Süreç VRAM'i | 220 MiB | 267 MiB (6 yuvayla 267–273) | ≈ +50 MiB |
| Süreç özel belleği (RAM) | 643–645 MiB | 799–803 MiB (tamponda ~10 sn / 22 MiB) | ≈ +157 MiB; çoğu NVENC oturumu ve sürücü. Tampon büyüdükçe artar (hesap: 1080p60'ta 30 sn ≈ +76, 120 sn ≈ +290 MiB; tavan 512 MiB) |
| Tekrar + F8 kaydı birlikte | — | VideoEncode %72, VRAM 293–317 MiB, RAM 944–964 MiB | iki oturum |

Okuma: 1080p60'ta sunum yoluna eklenen CPU süresi ~0,05 ms, GPU süresi 1 ms'nin altında; kare hızı ve yuva beklemesi değişmedi. Asıl bedel NVENC motorunun sürekli %35–45 dolu olması, ~50 MiB VRAM ve ~160 MiB + tampon kadar RAM.

### Önerilen kabul sınırı

Kullanıcının gerçek ayarında (SR/NR/FG açık) tekrar kapalı/açık aynı içerikle karşılaştırılır; açıkken:

- sunulan kare hızı %1'den fazla düşmemeli, `slotWaits` artmamalı;
- `presentP95Ms` artışı ≤ 0,3 ms, `gpuBlitP95Ms` artışı ≤ 1,5 ms (1080p) / ≤ 3 ms (4K);
- tekrarın atladığı kare ≤ %1 (`[replay] encoder session ends frames=… dropped=…` satırı);
- VRAM artışı ≤ 80 MiB (1080p) / ≤ 200 MiB (4K); RAM artışı ≤ bayt sınırı + 250 MiB.

Sınır aşılırsa ilk ayar `fpsLimit` (60 → 30), sonra süre. 3D motoru SR/NR/FG ile %90'ın üstündeyse dönüştürme geçişi o yükle yarışır; bu durum ayrıca ölçülmeli.

## Doğrulanan / doğrulanmayan

Doğrulandı (bu makine, RTX 3060 Ti): 1080p60 SDR H.264 ve HEVC, sesle; F9 kısayolu ve test kancası; F8 kaydıyla birlikte çalışma; pencere tahliyesi; duraklatma; temiz kapanış; ayar kartının çizimi (pencere görüntüsüyle); birim testleri.

Doğrulanmadı:

- **SR/NR/FG yükü altında maliyet**: NVIDIA çalışma zamanı DLL'leri (`nvngx_*` vb.) bu derlemede yok; ölçüm efektler kapalıyken yapıldı. FG açıkken sunum hızı 60'ın üstüne çıkar ve `fpsLimit = 60` örnekleme yapar; bu yol denenmedi.
- **4K ve HDR (HEVC 10 bit, P010)**: kod yolu upstream kaydınınkiyle aynı, çalıştırılmadı.
- **14 sn'den uzun gerçek tampon ve bayt sınırı tahliyesi**: eldeki yerel dosya 14 sn; 30–120 sn pencereler ve bayt sınırı yalnız birim testlerinde.
- **AMD / Intel**: bilerek desteklenmiyor (bkz. Hata durumları); "kullanılamıyor" durumu gerçek bir AMD oturumunda görülmedi.
- **Yakalama kartı, PS5/Moonlight akışı, ekran yakalama kaynakları**, oynatma bittikten sonra kaydetme, boyut değişiminde tamponun yeniden başlaması ve ses saat kayması düzeltmesinin uzun süreli (10+ dk) davranışı.
- Ayar kartındaki anahtar/süre düğmelerine ve "Şimdi kaydet"e fareyle tıklama (kart çizildi, kaydetme yolu F9 ve test kancasıyla denendi).
- Oyun modunda kısayol (bkz. Bilinen sınırlar). Kart ve bildirim metinleri şimdilik yalnız Türkçe; arayüz dili başka olsa da Türkçe görünür.
