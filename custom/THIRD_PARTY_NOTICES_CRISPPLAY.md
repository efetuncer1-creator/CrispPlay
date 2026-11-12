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

Çevrimiçi motor (DeepL, OpenAI uyumlu) üçüncü taraf kodu içermez; kullanıcının kendi hesabıyla kullandığı hizmetlerdir (`custom/subs/net/README.md`).