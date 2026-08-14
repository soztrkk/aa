# Vendored third-party kaynaklar

Bu klasordeki kod bize ait degil, disaridan (GitHub) indirilip oldugu gibi
projeye eklendi (vcpkg/CMake kullanilmadigi icin, `json_value.h` gibi elle
yonetilen bir bagimlilik).

| Kutuphane | Surum (tag) | Kaynak |
|---|---|---|
| Dear ImGui (core + win32/dx11 backend) | `v1.89.9` | https://github.com/ocornut/imgui |
| ImNodes | `v0.5` | https://github.com/Nelarius/imnodes |

**NOT - imgui surumu bilerek eski tutuldu**: ImNodes v0.5 (son surumu),
`IM_OFFSETOF` gibi Dear ImGui 1.90'da kaldirilan eski makrolari kullaniyor
(bkz. imnodes.cpp) - daha yeni bir imgui (orn. 1.92.x) ile derlenmiyor.
Ikisini birlikte guncellemek istersen ya ImNodes'un guncel bir forkunu/
yamasini bulman ya da imgui'yi 1.89.x civarinda tutman gerekir.

Guncellemek istersen, ayni tag yapisini kullanarak dosyalari tekrar indir
(bkz. `c_client/README_ADIM_ADIM.md` GUI bolumu).
