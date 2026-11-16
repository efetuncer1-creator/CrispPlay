<p align="center">
  <img src="/custom/branding/logo-256.png" width="128" alt="CrispPlay logosu">
</p>

<h1 align="center">CrispPlay</h1>

<p align="center">
  Oyun yayınları, yakalama kartı ve videolar için yapay zekâ destekli görüntü iyileştirici ve oynatıcı.<br>
  Türkçe arayüz, oyun altyazısı çevirisi ve daha fazlası.
</p>

<p align="center">
  <a href="https://github.com/efetuncer1-creator/CrispPlay/releases/latest"><b>⬇ Son sürümü indir</b></a>
</p>

---

CrispPlay, [Veyra](https://github.com/Likely7/Veyra-NRVideo) (Likely7) tabanlı bir Windows uygulamasıdır. Veyra'nın süper çözünürlük, gürültü azaltma ve kare üretimi özelliklerinin hepsini içerir; üzerine Türkçe kullanıcılar için yeni özellikler ekler.

## Özellikler

**Veyra'dan gelen görüntü iyileştirme**
- **Süper çözünürlük (SR):** NVIDIA DLSS / RTX Video Super Resolution ile örneğin 720p → 4K.
- **Gürültü azaltma (NR)** ve **kare üretimi (FG):** DLSS Frame Generation, NVIDIA VFG, AMD FSR, Intel XeSS.
- PS5 / Xbox / PC yayınları, yakalama kartları, ekran yakalama ve video dosyaları.
- Kayıt (F8), ekran görüntüsü, dışa aktarma, HDR.

**CrispPlay'e özel**
- **Türkçe arayüz.** Tüm metinler çevrildi; Windows Türkçe ise kendiliğinden Türkçe açılır.
- **Oyun altyazısı çevirisi (F10).** Oyundaki İngilizce altyazıyı ekrandan okur, Türkçeye çevirir ve İngilizcenin üstüne yazar.
  - Çeviri **bilgisayarında, çevrimdışı** yapılır. İstersen kendi DeepL ya da OpenAI uyumlu (Ollama / LM Studio gibi yerel sunucular dahil) API'nle çevrimiçi motor açılabilir; kapalıyken hiçbir yere bir şey gönderilmez.
  - Konuşmacı adı ayrı ve renkli gösterilir; çevrilecek alan önizleme üzerinde fareyle seçilir; altyazı menüsünden aç/kapat, boyut ve opaklık.
  - Modeller (226 MB) özellik ilk açıldığında indirilir.
- **Anlık tekrar (F9, NVIDIA):** son saniyeleri sesli MP4 olarak kaydeder.
- **Discord durumu:** "CrispPlay ile izliyor"; dosya adı ya da başlık asla gönderilmez.

## Kurulum

1. [Releases](https://github.com/efetuncer1-creator/CrispPlay/releases/latest) sayfasından `CrispPlay-…-win64-portable.zip` dosyasını indir.
2. İstediğin klasöre çıkar, `CrispPlay.exe`'yi çalıştır. Kurulum yok; ayarlar klasörün içinde kalır.

**Gereksinimler:** Windows 10/11 x64. SR / NR / FG için güncel sürücülü NVIDIA RTX ekran kartı. Altyazı çevirisi için Windows'un İngilizce OCR bileşeni (çoğu sistemde kurulu).

## Kaynaktan derleme

`custom/BUILD.md` adım adım anlatır (Visual Studio 2022 Build Tools, Qt 6.8.3, vcpkg FFmpeg, NVIDIA/AMD/Intel SDK'ları). CrispPlay'e özel kod `custom/` altındadır; Veyra dosyalarındaki küçük bağlantı noktaları `custom/HOOKS.md`'de listelenir. Veyra'nın yeni sürümleri otomatik olarak takip edilir.

## Lisans ve teşekkür

- CrispPlay **GPL-3.0**'dır ve **[Veyra](https://github.com/Likely7/Veyra-NRVideo) (Likely7)** üzerine kuruludur. Veyra'nın kendi belgeleri kökteki `README.md` dosyasındadır.
- Altyazı çevirisinin metin kuralları **[CouchSubs](https://github.com/yorulmazsinan/couchsubs)**'tan (AGPL-3.0) taşınmıştır.
- Çeviri modeli: OPUS-MT, Helsinki-NLP (CC BY 4.0). OCR modeli: PP-OCR, PaddlePaddle (Apache-2.0).
- NVIDIA, AMD ve Intel bileşenleri kendi lisanslarıyla dağıtılır; ayrıntılar pakette `licenses\` klasöründe ve `THIRD_PARTY_NOTICES*.md` dosyalarında.

Hata ya da öneri için [Issues](https://github.com/efetuncer1-creator/CrispPlay/issues).
