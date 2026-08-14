@echo off
REM build.bat
REM
REM Yeni pipeline_client'i (pipeline_engine + json_value + python_process +
REM result_display + main.cpp) derler. Eski block_pipeline_demo.c'ye dokunmaz,
REM o ayri/bagimsiz bir dosyadir, istersen onu da ayrica derleyebilirsin:
REM   gcc block_pipeline_demo.c -o block_pipeline_demo.exe
REM
REM BU DOSYAYI c_client KLASORU ICINDEN calistir:
REM   cd c_client
REM   build.bat

g++ -std=c++11 -Wall -O2 -o pipeline_client.exe ^
    json_value.cpp ^
    python_process.cpp ^
    pipeline_engine.cpp ^
    result_display.cpp ^
    main.cpp

if %ERRORLEVEL% NEQ 0 (
    echo.
    echo DERLEME BASARISIZ: pipeline_client.exe
    exit /b 1
)

REM interactive_client.exe: main.cpp yerine interactive_main.cpp kullanir
REM (ikisi de "main" fonksiyonu tanimladigi icin AYNI derlemeye giremezler,
REM bu yuzden ayri bir .exe olarak derleniyor).
g++ -std=c++11 -Wall -O2 -o interactive_client.exe ^
    json_value.cpp ^
    python_process.cpp ^
    pipeline_engine.cpp ^
    result_display.cpp ^
    block_specs.cpp ^
    interactive_main.cpp

if %ERRORLEVEL% NEQ 0 (
    echo.
    echo DERLEME BASARISIZ: interactive_client.exe
    exit /b 1
)

REM gui_client.exe: gercek node-graph GUI (Dear ImGui + ImNodes + Win32/DirectX11).
REM third_party/imgui ve third_party/imnodes altindaki vendor edilmis kaynaklarla
REM birlikte derlenir (bkz. third_party/README.md). d3d11/dxgi/d3dcompiler
REM Windows SDK'nin parcasidir, ayrica bir kutuphane INDIRMENE gerek yok - AMA
REM bu basliklar/importlar sadece MinGW-w64 (orn. MSYS2'nin mingw64 paketi)
REM dagitimlarinda bulunur, eski/minimal "MinGW.org" dagitiminda (bu projenin
REM diger iki .exe'si icin PATH'teki g++ ile yeterli olan dagitim) YOKTUR.
REM Bu yuzden GUI'yi, bulunabiliyorsa MSYS2'nin mingw64 g++'iyla derliyoruz;
REM yoksa PATH'teki g++'a duser (o zaman muhtemelen d3d11.h bulunamadi hatasi
REM alirsin - bu durumda MSYS2 kurup "pacman -S mingw-w64-x86_64-gcc" ile
REM mingw64 toolchain'ini kurman gerekir).
REM NOT: asagidaki iki "if" satirini BILEREK tek satirlik (parantezsiz) yazdik -
REM %PATH% degeri genelde "C:\Program Files (x86)\..." gibi parantez icerir,
REM bu da "if (...) ( ... )" tarzi COK SATIRLI bir blok icinde expand edilirse
REM cmd'nin parantez sayacini bozup "unexpected at this time" hatasi verir.
set GUI_GXX=g++
if exist "C:\msys64\ucrt64\bin\g++.exe" set GUI_GXX=C:\msys64\ucrt64\bin\g++.exe
REM g++.exe'nin kendisi calisir ama beraberindeki cc1plus.exe/collect2.exe
REM derleme SIRASINDA ihtiyac duydugu DLL'leri (libstdc++-6.dll vb.) SADECE
REM bu klasor PATH'te ise bulabiliyor - yoksa hicbir hata mesaji BASMADAN
REM sessizce basarisiz oluyor (pencere/masaustu olmadigi icin normalde cikacak
REM "DLL bulunamadi" penceresi de gorunmuyor). Bu yuzden PATH'e eklemek
REM ZORUNLU, sadece g++'i tam yoldan cagirmak yetmiyor.
if exist "C:\msys64\ucrt64\bin\g++.exe" set PATH=C:\msys64\ucrt64\bin;%PATH%

REM -DIMGUI_DEFINE_MATH_OPERATORS: imnodes.h "imgui.h"i BU MAKRO TANIMLANMADAN
REM once include ediyor (kendi .cpp'sinde makroyu daha sonra tanimlasa bile,
REM include guard yuzunden imgui.h'nin operator+/-/* tanimlari BIR DAHA
REM islenmiyor) - bu yuzden makroyu her .cpp'de ilk imgui.h include'undan
REM once garantiye almak icin burada, derleyici bayragi olarak veriyoruz.
REM -static -static-libgcc -static-libstdc++: MSYS2 mingw64 g++'i VARSAYILAN
REM olarak libstdc++-6.dll/libgcc_s_seh-1.dll'e DINAMIK baglar - bunlar sadece
REM C:\msys64\mingw64\bin PATH'teyken calisir. gui_client.exe'yi CALISTIRAN
REM kisinin PATH'inde bu klasor olmayabilir (derlerken PATH'e ekledik ama
REM CALISTIRIRKEN o PATH degisikligi yok) - bu durumda exe, hicbir hata mesaji
REM basmadan aninda cikiyor (Windows STATUS_INVALID_IMAGE_FORMAT / 0xC000007B).
REM Bu bayraklar MSYS2'nin C++ runtime'ini exe'nin ICINE gomerek bu bagimliligi
REM tamamen ortadan kaldirir - gui_client.exe herhangi bir Windows makinesinde
REM ekstra DLL/PATH ayari gerekmeden calisir.
REM -pthread: GuiApp artik "Calistir" butonunda std::thread kullaniyor
REM (egitim gibi uzun suren bloklar UI'yi kilitlemesin diye, bkz. gui_app.h
REM basindaki THREAD MODELI aciklamasi) - MinGW-w64'te std::thread'in duzgun
REM calismasi icin bu bayrak guvenlik payi olarak ekleniyor.
%GUI_GXX% -std=c++11 -Wall -O2 -pthread -DIMGUI_DEFINE_MATH_OPERATORS ^
    -I third_party/imgui -I third_party/imgui/backends -I third_party/imnodes ^
    json_value.cpp ^
    python_process.cpp ^
    pipeline_engine.cpp ^
    block_specs.cpp ^
    result_display_imgui.cpp ^
    gui_app.cpp ^
    gui_main.cpp ^
    third_party/imgui/imgui.cpp ^
    third_party/imgui/imgui_draw.cpp ^
    third_party/imgui/imgui_tables.cpp ^
    third_party/imgui/imgui_widgets.cpp ^
    third_party/imgui/backends/imgui_impl_win32.cpp ^
    third_party/imgui/backends/imgui_impl_dx11.cpp ^
    third_party/imnodes/imnodes.cpp ^
    -static -static-libgcc -static-libstdc++ ^
    -ld3d11 -ldxgi -ld3dcompiler -ldwmapi -lgdi32 -o gui_client.exe

if %ERRORLEVEL% NEQ 0 (
    echo.
    echo DERLEME BASARISIZ: gui_client.exe
    exit /b 1
)

echo.
echo Derleme basarili: c_client\pipeline_client.exe, c_client\interactive_client.exe ve c_client\gui_client.exe
echo CALISTIRMAK ICIN: once proje KOK dizinine gec (cd ..), sonra:
echo   c_client\pipeline_client.exe        (hazir senaryolu demo)
echo   c_client\interactive_client.exe     (elle blok/parametre girerek test)
echo   c_client\gui_client.exe             (gercek node-graph GUI)