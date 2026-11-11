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

**Durum (2026-11-11):** Qt'ye bağlı testler derleniyor ve geçiyor (branding 7/7, upstream güncelleme denetleyicisi 29/29, i18n 0 hata). DLSS SDK ile `veyra_qml_ui`'nin tüm kaynakları derleniyor; yalnız bağlama `veyra_nvof.lib` (Optical Flow SDK) eksik diye duruyor. Motor kodu DLSS başlıklarını (`nvsdk_ngx.h`) koşulsuz, bağlayıcı da NVOF kitaplığını istiyor. `third_party_local/` altına (git'e girmez):

- İndirildi: DLSS SDK 310.7.0 (`a291cc7`), XeSS 3.0.2 (sürüm zip'i), FidelityFX 2.3.0 (`60f4ea8`), FidelityFX 1.1.4 (sürüm zip'i; `ffx_opticalflow_x64.lib` hazır gelmiyor, SDK içinden derlenmeli, isteğe bağlı).
- Eksik: Optical Flow SDK 5.0.7 (NVIDIA hesabı ister).

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
