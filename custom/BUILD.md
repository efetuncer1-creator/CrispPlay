# CrispPlay derleme ortamı (Windows)

Kurulum bir kez yapılır. Bağımlılıklar repo dışında, repo ile aynı seviyedeki `deps\` klasöründe durur (`crisp_play\deps`, `crisp_play\CrispPlay`). Yollar boşluk içermemeli (FFmpeg derlemesi boşluklu yolu reddeder).

| Bileşen | Sürüm | Nasıl |
| --- | --- | --- |
| VS 2022 Build Tools | MSVC 19.44, CMake + Ninja dahil | `winget install Microsoft.VisualStudio.2022.BuildTools --override "--quiet --wait --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended"` |
| Qt | 6.8.3 `win64_msvc2022_64` | `pip install aqtinstall`, sonra `aqt install-qt windows desktop 6.8.3 win64_msvc2022_64 -O deps\qt` |
| FFmpeg | 9.0.1#1 (vcpkg, `vcpkg.json` temeli `30ef65c`) | `git clone https://github.com/microsoft/vcpkg deps\vcpkg`, `bootstrap-vcpkg.bat`, sonra repo kökünden `deps\vcpkg\vcpkg.exe install --x-manifest-root=. --x-install-root=..\deps\vcpkg_installed --triplet x64-windows` (~12 dk) |
| nv-codec-headers | n13.0.19.0 (`e844e5b`) | `git clone --depth 1 --branch n13.0.19.0 https://github.com/FFmpeg/nv-codec-headers third_party_local\nvidia\nv-codec-headers-13.0` |

Derleme ve testler:

```powershell
powershell -ExecutionPolicy Bypass -File custom\scripts\build.ps1 -Test
```

Çıktı `out\build\x64-release\` altında (`veyra_qml_ui.exe`).

Çalıştırma: `custom\scripts\CrispPlay.cmd` Qt/FFmpeg yollarını ayarlayıp EXE'yi açar; ayarlar `%APPDATA%\CrispPlay` altında (ilk açılışta Türkçe), üzerine bırakılan video açılır. Masaüstündeki `CrispPlay` kısayolu bu dosyayı çağırır.

**Durum (2026-11-11):** `veyra_qml_ui.exe` derleniyor (EXE bilgisi `CrispPlay 2.1.5-crispplay.1`); testler geçiyor (branding 7/7, upstream güncelleme denetleyicisi 29/29, i18n 0 hata; Aşama 2'den beri Türkçe `crispplay_i18n_tests` 9/9). Duman testi: `--data-dir out\smoke-data --page set --tab about --exit-after 10000` ile açıldı, pencere başlığı doğru, RTX 3060 Ti seçildi, çıkış 0. Güncelleme denetimi `http-404` (henüz CrispPlay release yok, beklenen). PS5/Xbox iletişim kutularında iki upstream QML uyarısı var (bu derlemede o özellikler kapalı). Video oynatma, SR/NR/FG denenmedi.

Motor kodu DLSS başlıklarını (`nvsdk_ngx.h`) koşulsuz, bağlayıcı da NVOF kitaplığını istiyor. `third_party_local/` altında (git'e girmez):

- DLSS SDK 310.7.0 (`a291cc7`), Optical Flow SDK 5.0.7 (NVIDIA hesabıyla indirilen zip), XeSS 3.0.2 (sürüm zip'i), FidelityFX 2.3.0 (`60f4ea8`), FidelityFX 1.1.4 (sürüm zip'i; `ffx_opticalflow_x64.lib` hazır gelmiyor, SDK içinden derlenmeli, isteğe bağlı, henüz derlenmedi).
- Çalıştırma: `deps\qt\6.8.3\msvc2022_64\bin` ve `deps\vcpkg_installed\x64-windows\bin` PATH'te olmalı (paketleme/windeployqt henüz yok).
- Çalışma zamanı bileşenleri: upstream `Veyra-2.1.5-NVIDIA-win64-portable.zip` (SHA-256 `f78a3dca…80ca`, upstream `SHA256SUMS.txt` ile doğrulandı) içinden `runtime\`, `runtime_local\` ve `licenses\` EXE'nin yanına (`out\build\x64-release\`) konur; uygulama bunları `src/base/RuntimePaths.cpp` ile EXE yanında arar. Zip `deps\downloads\veyra-2.1.5\` altında.
- 2026-11-11 RTX 3060 Ti, hta.mp4 (720p30 H.264), SR + NR + FG açık: DLSS 4 SR 1280×720 → 3840×2160 (`gpuSrP95` ≈ 4,2 ms), NR `community-SF-RTX20-RTX50` iç 1920×1080 (≈ 25 ms), DLSS-G FG ×6 (toplu ≈ 15 ms), NVOF akış ≈ 1,8 ms; VRAM 2,4 GB. Kaynak karesi başına GPU ≈ 55 ms > 33 ms bütçe: bu kombinasyonda GPU sınırda, ekrana ~60 kare/sn gidiyor (75 Hz ekran). NR'yi düşürmek ya da FG'yi ×2 yapmak bütçeye sığdırır.

| SDK | Beklenen yer | Kaynak |
| --- | --- | --- |
| NVIDIA DLSS SDK 310.7.0 | `third_party_local/nvidia/DLSS_SDK_310.7.0` | github.com/NVIDIA/DLSS, etiket `v310.7.0`, NVIDIA lisansı |
| NVIDIA Optical Flow SDK 5.0.7 | `third_party_local/nvidia/Optical_Flow_SDK_5.0.7` | developer.nvidia.com, hesap gerekir |
| AMD FidelityFX SDK 1.1.4 / 2.3.0 | `third_party_local/amd/...` | GitHub (GPUOpen) |
| Intel XeSS SDK 3.0.2 | `third_party_local/intel/xess-3.0.2` | GitHub (intel/xess), Intel lisansı |

## Upstream sürüm paketinden farkları (bilerek)

- **PS5 H.264 slice yaması yok:** vcpkg FFmpeg'i yamasız. PS5 1080p H.264 akışında 32'den fazla slice gelince donanım çözme bozulabilir. Upstream yöntemi: `scripts/ffmpeg/README.md`.
- **dav1d (AV1 yazılım çözme), zlib (sıkıştırılmış PGS altyazı) ve deinterlace modülü yok:** `VEYRA_ENABLE_DEINTERLACE=OFF`.
- **Kapalı kaynak NVIDIA/AMD bileşenleri (DLSS/NGX, VFG, RTX Video, AMD NR) yok:** SR/NR/FG özellikleri bu bileşenler `third_party_local/` altına konmadan ve ilgili CMake seçenekleri açılmadan çalışmaz. Repoya asla girmezler.
- **Remote Play (Chiaki), Moonlight, Xbox kapalı:** CMake varsayılanı.

Bunlar dağıtılacak bir CrispPlay sürümünden önce upstream'in sürüm derlemesiyle eşitlenmeli; plan `custom/PLAN.md`.
