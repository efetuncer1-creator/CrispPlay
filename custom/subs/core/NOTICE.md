# custom/subs/core: lisans ve kaynak

Bu klasördeki kodun çoğu **CouchSubs**'tan C++20'ye taşındı:

```
CouchSubs
Copyright (C) 2026 Sinan Yorulmaz
https://github.com/yorulmazsinan/couchsubs
commit e9019b36265548695c38cfd8599045e1e0ddd54c
GNU Affero General Public License, version 3 (AGPL-3.0-only)
```

Based on CouchSubs by Sinan Yorulmaz (https://github.com/yorulmazsinan/couchsubs), modified on 2026-11-12:
C# kaynakları C++20'ye çevrildi, TV gönderimi yerine katman geri çağrısı kondu, bilinmeyen simge (`⁇`)
ve konuşmacı etiketi kuralları eklendi.

- AGPL başlıklı her dosya (`// SPDX-License-Identifier: AGPL-3.0-only`) AGPL-3.0 altındadır; başlıkta taşındığı
  CouchSubs dosyası yazılıdır. AGPL-3.0 metni: <https://www.gnu.org/licenses/agpl-3.0.txt>.
- GPL-3'ün 13. maddesi AGPL-3.0 kodla birleştirmeye izin verir; birleşik çalışmanın bu kısmı için AGPL-3.0'ın
  13. maddesi (ağ üzerinden kullanım) de geçerlidir.
- Başlığında AGPL yazmayan dosyalar (`Unicode.*`, `Json.*`, `Types.h`, test koşucusu, `tools/`) CrispPlay'in
  kendi kodudur (GPL-3.0, depo kökündeki `LICENSE`).
- Test vektörleri (`tests/fixtures/golden-vectors.json`, `fuzzy-vectors.json`) CouchSubs
  `core/Tests/CouchSubsCoreTests/Fixtures/` altından olduğu gibi alındı (AGPL-3.0). Test dosyalarındaki örnek
  satırlar CouchSubs testlerinden gelir.
- Genel terim sözlüğü (`src/BuiltInGlossary.inc` içindeki JSON) CouchSubs
  `core/Sources/CouchSubsCore/Resources/glossary-en-tr.json` dosyasıdır.

## Dosya eşlemesi

| C++ | CouchSubs kaynağı |
|---|---|
| `TextMatch.*` | `windows/src/CouchSubs.Core/TextMatch.cs` |
| `OcrNoise.*` | `windows/src/CouchSubs.Core/OcrNoise.cs` |
| `Reading.*` | `windows/src/CouchSubs.Core/Reading.cs` |
| `SubtitleStabilizer.*` | `windows/src/CouchSubs.Core/SubtitleStabilizer.cs` |
| `ReadScheduler.*` | `windows/src/CouchSubs.Core/ReadScheduler.cs` |
| `SubtitlePipeline.*` | `windows/src/CouchSubs.Core/SubtitlePipeline.cs` |
| `SubtitleSegmenter.*` | `windows/src/CouchSubs.Translation/SubtitleSegmenter.cs` |
| `FreeTranslation.*` | `windows/src/CouchSubs.Core/FreeTranslation.cs` |
| `GameEnglish.*` | `windows/src/CouchSubs.Core/GameEnglish.cs` |
| `Glossary.*` | `windows/src/CouchSubs.Core/Glossary.cs` |
| `TermProtection.*` | `windows/src/CouchSubs.Core/TermProtection.cs` |
| `TurkishSuffix.*` | `windows/src/CouchSubs.Core/TurkishSuffix.cs` |
| `TurkishPostEdit.*` | `windows/src/CouchSubs.Core/TurkishPostEdit.cs` |
| `TranslationCache.*` | `windows/src/CouchSubs.Core/TranslationCache.cs` |
| `LineTranslator.*` | `windows/src/CouchSubs.Translation/MarianTranslator.cs` (motor dışındaki metin akışı) |
| `PackText.*` | `windows/src/CouchSubs.Core/GamePacks.cs` (`PackKey`), `PackFuzzy.cs` (imza) |
