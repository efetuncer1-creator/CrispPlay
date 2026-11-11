# Türkçe arayüz sözlüğü

`custom/i18n/tr.json` çevirisinde kullanılan terimler. Yeni metin çevirirken buna uy; bir terim değişecekse tüm dosyada birlikte değiştir.

## Üslup

- Kısa, İngilizce kadar öz. Düğmeler ve menüler emir kipinde, tekil: "Kaydet", "Yeniden bağlan", "Dışa aktar".
- İletiler "siz" ile: "IP'yi elle girebilirsiniz", "Günlüğe bakın".
- Durumlar edilgen veya sıfat: "Bağlandı", "Duraklatıldı", "Kullanılamıyor".
- Ondalık ayırıcı virgül (0,25; 1,7 GB), binlik nokta (10.000). Koddan gelen `{:.1f}` gibi sayılar olduğu gibi kalır.
- Yüzde: Türkçede işaret önce gelir, ama `%` + rakam Qt'de yer tutucudur (`%1`). Bu yüzden sabit yüzdeler "yüzde 50" diye yazılır; yer tutuculu yüzdeler `%%1` (ekranda `%50`).
- Ekler kesme işaretiyle: PS5'e, Veyra'yı, IP'yi, F11'e.

## Dokunulmayanlar

- Yer tutucular: `{}`, `{:.3f}`, `{:08X}` (sıra ve sayı aynı), `%1`, `%2`…; `\n`, baştaki/sondaki boşluklar (parça birleştirilen metinler).
- Ürün ve marka adları: Veyra, CrispPlay, DLSS, DLAA, RTX, RTX Video, NVENC, NVOF, NGX, HDR, HDR10, SDR, PQ, HLG, FSR, XeSS, VFG, Smooth Motion, PS5, PSN, Remote Play, Xbox, Sunshine, Moonlight, OBS, RTSS/RivaTuner, GamePP, Magewell, Blackmagic/BMD, DeckLink, Desktop Video, Elgato, VB-CABLE, VB-Audio, WASAPI, DirectShow, D3D12, Douyin, Bilibili, WeChat, Alipay, Ko-fi.
- Kısaltmalar: SR, NR, FG, LUT, VRR, VSync, OSD, FPS/fps, Mbps, nit, EV, P95.
- Çince/Japonca dil adları kendi dillerinde kalır: 简体中文, 繁體中文, 日本語 (ve 简 / 繁).
- Windows'un kendi İngilizce arayüz adları (Türkçe Windows'ta karşılığı belli olanlar Türkçe yazıldı: "Bu aygıtı dinle", "Kayıttan Yürütme").

## Terimler

| İngilizce | Türkçe |
| --- | --- |
| super resolution (SR) | süper çözünürlük (SR) |
| upscaling / upscaler | büyütme / büyütücü |
| noise reduction (NR) | NR (açık yazımda gürültü azaltma) |
| frame generation (FG) | kare üretimi (FG) |
| multiplier | çarpan |
| optical flow | optik akış |
| motion source / motion estimation | hareket kaynağı / hareket tahmini |
| anti-aliasing | kenar yumuşatma |
| anti-flicker | titreme önleme |
| deinterlacing | geçmeli tarama giderme |
| interlaced / progressive | geçmeli / aşamalı |
| tone mapping | ton eşleme |
| highlights / shadows / midtones | parlak bölgeler / gölgeler / orta tonlar |
| peak (brightness) | tepe (parlaklık) |
| roll-off | yumuşatma |
| hue / saturation / tint | renk tonu / doygunluk / renk kayması |
| color grading / color settings | renk derecelendirme / renk ayarları |
| beauty / skin smoothing | güzelleştirme / cilt yumuşatma |
| original face restore | özgün yüz geri yükleme |
| protected area | korumalı alan |
| preset | ön ayar |
| list mode / node mode | liste modu / düğüm modu |
| node / layer / chain | düğüm / katman / zincir |
| pipeline | hat |
| runtime | çalışma zamanı |
| backend | arka uç |
| inference | çıkarım |
| capture card / capture device | yakalama kartı / yakalama aygıtı |
| capture format / pixel format | yakalama biçimi / piksel biçimi |
| frame rate / refresh rate | kare hızı / yenileme hızı |
| frame cap / output cap | kare hızı sınırı / çıkış sınırı |
| dropped frames | düşen kareler |
| keyframe | anahtar kare |
| display | ekran |
| display sync / variable refresh | ekran senkronu / değişken yenileme |
| tearing | yırtılma |
| latency / low latency | gecikme / düşük gecikme |
| queue / buffer | kuyruk / ara bellek |
| drain (a queue) | boşaltmak |
| presentation / present | sunum / sunmak |
| interface drawing (GPU / software) | arayüz çizimi (GPU / yazılımla) |
| compatible rendering | uyumlu çizim |
| monitoring overlay | izleme katmanı |
| streaming / stream | akış (yayın: re-streaming = yeniden yayın) |
| host (Sunshine / PC) | ana bilgisayar |
| console | konsol |
| controller | kumanda |
| pairing / pair | eşleştirme / eşleştirmek |
| sign in / sign out | oturum açmak / oturumu kapatmak |
| wake / rest mode | uyandırmak / dinlenme modu |
| export / import | dışa aktarma / içe aktarma |
| recording / record | kayıt / kaydetmek |
| screenshot | ekran görüntüsü |
| container / track / codec | kapsayıcı / iz / kodek |
| encoder / decoder | kodlayıcı / kod çözücü |
| hardware / software decoding | donanımla / yazılımla kod çözme |
| bitrate | bit hızı |
| passthrough (audio) / bitstream | doğrudan aktarım / bit akışı |
| downmix | kanal indirgeme (stereoya indirgeme) |
| audio monitoring | ses izleme |
| audio/video sync / offset | ses/görüntü senkronu / kaydırma |
| subtitles (main / secondary / external / embedded) | altyazı (ana / ikincil / harici / gömülü) |
| playlist / playback bar | oynatma listesi / oynatma çubuğu |
| seek | atlama |
| fullscreen / borderless | tam ekran / çerçevesiz |
| game mode / mouse passthrough | oyun modu / fare geçişi |
| shortcut | kısayol |
| log / diagnostics | günlük / tanılama |
| components | bileşenler |
| prewarm | önceden ısıtma |
| VRAM / video memory | VRAM / video belleği |
| tiled processing | döşemeli işleme |
| original / enhanced | özgün / iyileştirilmiş |
| enhancement | iyileştirme |
| strength | güç |
| Auto / Manual / Custom | Otomatik / Elle / Özel |
| On / Off / On + Boost | Açık / Kapalı / Açık + Destek |
| Minimal / Pro mode | Minimal / Pro modu |
| experimental | deneysel |
