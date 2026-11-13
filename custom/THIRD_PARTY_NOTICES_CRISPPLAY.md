# CrispPlay üçüncü taraf bildirimleri

Upstream Veyra'nın bildirimleri depo kökündeki `THIRD_PARTY_NOTICES.md` dosyasındadır; bu dosya yalnız
CrispPlay'in (`custom/`) eklediklerini listeler.

## Oyun altyazısı çevirisi (Aşama 5)

### CouchSubs (metin kuralları, `custom/subs/core`)

| Kaynak | Lisans | CrispPlay kullanımı |
|---|---|---|
| CouchSubs, Copyright (C) 2026 Sinan Yorulmaz, <https://github.com/yorulmazsinan/couchsubs>, commit `e9019b36265548695c38cfd8599045e1e0ddd54c` | AGPL-3.0-only | OCR gürültü temizliği, altyazı bloğu ve tuş ipucu ayıklama, dengeleyici, okuma planı, çeviri sırası, cümle bölme, oyun deyimleri, terim sözlüğü ve koruma, Türkçe ek ve son düzeltmeler, çeviri önbelleği C++20'ye taşındı; genel terim sözlüğü ve test vektörleri alındı. Ayrıntı ve dosya eşlemesi: [custom/subs/core/NOTICE.md](subs/core/NOTICE.md) |

Based on CouchSubs by Sinan Yorulmaz (https://github.com/yorulmazsinan/couchsubs), modified on 2026-11-12.
Taşınan dosyalar AGPL-3.0 başlığını taşır; GPL-3'ün 13. maddesi bu birleştirmeye izin verir.

### Altyazı modelleri (ayrı sürüm dosyası, `custom/subs/net`)

Uygulama paketine girmez; özellik ilk açıldığında `crispplay-subs-models-v1.zip` (GitHub sürümü `subs-models-v1`) indirilir. Atıf ve lisans dosyaları zip'in içindedir (kaynakları `custom/subs/net/model-licenses/`).

| Model | Lisans | Not |
|---|---|---|
| OPUS-MT `opus-mt-tc-big-en-tr` (Helsinki-NLP, University of Helsinki), CTranslate2 int8'e dönüştürüldü | CC BY 4.0 | atıf zorunlu: `custom/subs/net/model-licenses/opus-mt-ATTRIBUTION.txt` |
| PP-OCRv6 small tanıma ağı (PaddleOCR), RapidOCR 3.10.0 ONNX | Apache-2.0 | değiştirilmedi |

### Çalışma zamanı kitaplıkları (uygulama paketinde, `custom/subs/mt`, `custom/subs/ocr`, `custom/subs/app`)

| Bileşen | Sürüm | Lisans | Pakette |
|---|---|---|---|
| ONNX Runtime (Microsoft), CPU | 1.31.1, resmî `onnxruntime-win-x64-1.31.1.zip` | MIT | `onnxruntime.dll` (değiştirilmedi); `licenses\crispplay-subs\onnxruntime-LICENSE`, `onnxruntime-ThirdPartyNotices.txt` |
| CTranslate2 (OpenNMT) | 4.9.0, `a22ba1c6` | MIT | EXE'ye statik bağlı; `ctranslate2-LICENSE` |
| oneDNN (UXL Foundation / Intel) | 3.1.1, `64f6bcbc` | Apache-2.0 | EXE'ye statik bağlı; `oneDNN-LICENSE`, `oneDNN-THIRD-PARTY-PROGRAMS` |
| SentencePiece (Google) | 0.2.1, `31646a46` | Apache-2.0 | EXE'ye statik bağlı; `sentencepiece-LICENSE` |
| cpu_features (Google) | CTranslate2 ile gelen | Apache-2.0 | EXE'ye statik bağlı; `cpu_features-LICENSE` |
| MSVC OpenMP çalışma zamanı | VS 2022 (14.44), `Microsoft.VC143.OpenMP` | Microsoft Visual C++ yeniden dağıtım koşulları | `vcomp140.dll` (değiştirilmedi) |
| Windows.Media.Ocr | Windows bileşeni | Windows ile gelir | pakete girmez |

Intel MKL kullanılmaz (GPL-3 ile dağıtılamaz); CTranslate2 oneDNN ile derlenir (`custom/scripts/build-ctranslate2.ps1`,
sabitlenen sürümler `licenses\crispplay-subs\ctranslate2-build-pins.txt`). Lisans metinlerini pakete
`custom/scripts/package.ps1` koyar.

Çevrimiçi motor (DeepL, OpenAI uyumlu) üçüncü taraf kodu içermez; kullanıcının kendi hesabıyla kullandığı hizmetlerdir (`custom/subs/net/README.md`).