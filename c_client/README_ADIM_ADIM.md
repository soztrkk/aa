# C++ Pipeline Client - Adim Adim

Bu klasordeki kod, CLAUDE.md'de tanimlanan "C tarafi" (pipeline engine + UI
sorumlulugu) icin ATILAN ILK GERCEK ADIM. `block_pipeline_demo.c` (eski dosya,
dokunulmadi) sadece pipe iletisiminin nasil kuruldugunu gosteren tek seferlik
bir demoydu; bu klasordeki yeni dosyalar onun uzerine, GERCEKTEN kullanilabilir
bir yapi kuruyor: node graph, dirty/up-to-date takibi, ve "kullaniciya ne
gosterilecek" mantigi.

## Dosyalar ve sorumluluklari

| Dosya | Ne yapar |
|---|---|
| `json_value.h/.cpp` | Kendi yazdigimiz kucuk JSON kutuphanesi: JSON metnini C++ nesnesine cevirir (parse), C++ nesnesini JSON metnine cevirir (dump). Internet erisimi/3. parti kutuphane gerektirmez. |
| `python_process.h/.cpp` | `dispatcher.py`'yi ayri process olarak baslatir, stdin/stdout pipe'lariyla JSON mesajlasmayi yonetir. `block_pipeline_demo.c`'deki AYNI Windows API mantigi (CreateProcess + pipe), sadece tekrar kullanilabilir bir sinifa (class) tasindi. |
| `pipeline_engine.h/.cpp` | **Asil "C tarafi" mantigi burada.** Node graph'i tutar (`Node` struct), `state` (NOT_RUN/UP_TO_DATE/DIRTY/ERROR) takip eder, parametre degisince bagimli node'lari dirty yapar (dirty propagation), pipeline calistirildiginda SADECE calismasi gerekenleri Python'a gonderir. |
| `result_display.h/.cpp` | Python'dan gelen `meta` bilgisine bakip kullaniciya NE gosterilecegine karar verir: tablo (df onizleme), ozet, mesaj, grafik basligi, ya da genel anahtar/deger listesi (skorlar dahil). Ham veri/tam DataFrame HICBIR ZAMAN buraya gelmez - bu, Python tarafindaki blok tasariminin (bkz. asagida) dogal bir sonucu. |
| `main.cpp` | Iki test senaryosu icerir, asagida detayli anlatildi. |
| `build.bat` | Tek komutla derleme. |

## Mimari akis (ozet)

```
main.cpp
  |
  |-- PythonProcess proc;  proc.start("python", "dispatcher.py")
  |
  |-- PipelineEngine engine(proc);
  |     engine.addNode(...)      -> graph'a node ekle
  |     engine.connect(...)      -> node'lari birbirine bagla
  |     engine.setParams(...)    -> parametre degistir (dirty propagation tetikler)
  |     engine.runAll()          -> SADECE DIRTY/NOT_RUN node'lari Python'a gonder
  |
  |-- her calistirmadan sonra:
        node->outputs[slot].meta  -> result_display.h -> renderNodeOutput(...)
```

`runAll()` her calisan node icin sirasiyla:
1. JSON istek olusturur: `{"op":"run_block","node_id":...,"block":...,"params":...,"inputs":{...}}`
2. `PythonProcess::request()` ile gonderir, cevabi bekler (dispatcher.py tek
   process olarak surekli acik kalir, CLAUDE.md'deki "neden bu yontem
   secildi" bolumundeki gerekce burada).
3. `status: "ok"` ise node'un `outputs` haritasini (ref + meta) gunceller,
   state = UP_TO_DATE yapar.
4. `status: "error"` ise state = ERROR yapar, hata mesajini saklar, o node'a
   bagli asagi akis node'lari o turda ATLANIR (calistirilmaya calisilmaz).

## "Kullaniciya sadece ozet veri gosterilir" nasil saglaniyor?

Iki katmanda:

1. **Python tarafinda zaten** - `blocks/graphs_and_views.py` icindeki view
   fonksiyonlari (`data_preview`, `dataset_summary`, ...) meta'ya bir
   `"output_type"` alani koyuyor (`"table"` / `"summary"` / `"message"` /
   `"chart"`) ve icerigi ZATEN sinirli (orn. `data_preview` sadece 5 satir
   doner, tum DataFrame degil). Diger bloklar (load_csv, handle_missing_values
   vb.) meta'ya sadece `shape`/`columns` gibi kucuk bilgiler koyar, hucre
   verisi asla gitmez.
2. **C++ tarafinda** - `result_display.cpp`, `output_type` alanina bakip
   dogru gorunumu secer; boyle bir alan yoksa (orn.
   `compute_classification_metrics` skorlari) genel bir anahtar/deger listesi
   basar. Ayrica her ihtimale karsi cok uzun string/array'leri (orn. bir
   grafigin `figure_json`'u) KIRPAR/basmaz - bkz. `MAX_STRING_LEN`,
   `MAX_ARRAY_ITEMS`, `renderChart()`.

Yani "df ise ilk 5 satir, skor varsa skor gorunsun" kurali, ozel bir
`if (block_name == ...)` yazmadan, meta'nin SEKLINE gore otomatik uygulaniyor.
Ileride yeni bir view blogu eklersen (Python tarafinda), `output_type` alanini
dogru sececek sekilde eklemen yeterli - C++ tarafinda YENI KOD YAZMANA
GEREK KALMAZ (bilinmeyen `output_type` degerleri de generic gorunume duser,
program cokmez).

## Derleme

```
cd c_client
build.bat
```

Ya da elle:
```
g++ -std=c++11 -Wall -O2 -o pipeline_client.exe json_value.cpp python_process.cpp pipeline_engine.cpp result_display.cpp main.cpp
```

## Calistirma

**ONEMLI**: proje KOK dizininden calistirilmali (dispatcher.py ve
test_data_full.csv oraya gore aranir):

```
cd ..
c_client\pipeline_client.exe
```

`python` komutunun PATH'te olmasi gerekiyor (`python --version` ile kontrol et).

## Ne gormelisin / nasil test edersin

`main.cpp` iki senaryoyu sirayla calistirir, cikti bunlari GOZLE
dogrulaman icin yeterince ayrintili:

### Senaryo 1 - on-isleme + view bloklari + dirty propagation
- Ilk calistirmada 5 node'un hepsinin `basarili, state = UP_TO_DATE` bastigini gor.
- `node_4` (data_preview) ciktisinda gercekten 5 satirlik bir tablo, `node_5`
  (dataset_summary) ciktisinda kolon bazli ozet gor.
- Pipeline'i degistirmeden TEKRAR calistirdigimizda hepsinin
  "atlandi (zaten UP_TO_DATE)" dedigini, yani Python'a hicbir mesaj
  gitmedigini gor (bunu Python tarafinda dogrulamak istersen
  `python_process.cpp` icindeki `sendLine`e gecici bir `std::cerr` logu
  eklenebilir).
- `node_2`'nin parametresini `mean` -> `median` yaptigimizda, SADECE
  `node_2, node_3, node_4, node_5`'in DIRTY oldugunu, `node_1`'in
  UP_TO_DATE kaldigini gor (dirty propagation dogru calisiyorsa `node_1`
  yeniden calismaz).
- `node_5` silindikten sonra pipeline durumunda artik gorunmedigini kontrol et.

### Senaryo 2 - tam ML zinciri + skor gosterimi
- `test_full_chain.py` ile birebir ayni 18 adimlik zinciri kurar
  (yukleme -> temizlik -> encode/scale -> split -> tensor -> DataLoader ->
  MLP egitimi -> test metrikleri).
- `n17` (mlp_learner) ciktisinda `final_loss`/`loss_history` (kirpilmis,
  ilk 20 deger + "... +N tane daha") gorursun.
- `n18` (compute_classification_metrics) ciktisinda `accuracy`, `precision`,
  `recall`, `f1`, `confusion_matrix` gibi SKOR degerlerini gorursun - bu,
  "score varsa score gorunecek" kuralinin generic fallback ile nasil
  karsilandigini gosteriyor.

## Interaktif test istemcisi (interactive_client.exe)

`main.cpp`'deki senaryolar hazir/sabit bir zincir kurup calistiriyor - bu,
motoru dogrulamak icin iyi ama gercek kullanimi (kullanici tek tek blok
ekleyip her adimda sonuca bakmasi) GORMUYOR. Bunun icin ayri bir program:
`interactive_main.cpp` + `block_specs.h/.cpp`.

Artik `blocks/__init__.py`'deki `BLOCK_REGISTRY` ile AYNI 27 blogun hepsi
kayitli (on-isleme, encode/scale, split, tensor/dataloader/mlp, metrikler,
view/rapor, grafik) - `liste` yazarak hepsini gorebilirsin.

Calisma sekli:
1. Terminalde `Blok adi:` sorusu cikar (orn. `load_csv`).
2. Blogun bekledigi HER GIRDI SLOTU icin ayri ayri sorulur: "hangi
   node_id (ya da node_id:slot) bu girdiyi saglayacak?". Cogu blokta tek
   slot vardir (`data`), ama orn. `create_dataloader` icin `X` ve `y`,
   `compute_classification_metrics` icin `model`, `X`, `y` ayri ayri
   sorulur. Bos birakip Enter'a basarsan, VE bir onceki basarili node'un
   TEK bir cikisi varsa, o otomatik kullanilir (parantez icinde
   `[bos = node_3:output]` seklinde gosterilir). Onceki node birden fazla
   cikis urettiyse (orn. `train_test_split` -> `train`+`test`) BILEREK
   varsayilan sunulmaz, hangisini istedigini acikca yazman gerekir
   (orn. `node_4:train`). Var olmayan bir node/slot yazarsan hata
   mesaji basilir ve AYNI soru tekrar sorulur.
3. O blogun kabul ettigi her parametre TEK TEK sorulur (bkz.
   `block_specs.cpp` icindeki `blockRegistrySpecs()` - her blok icin hangi
   parametrelerin oldugu, zorunlu mu opsiyonel mi, Python tarafindaki
   docstring'lerden cikarilmis tablo halinde).
4. Bos birakilan OPSIYONEL parametreler JSON'a hic eklenmez (Python kendi
   varsayilanini kullanir); ZORUNLU bir alan bos birakilirsa tekrar sorulur.
5. Yeni bir node olusturulur, girdileri 2. adimda secilen kaynaklara
   baglanir, sonra SADECE bu yeni node calistirilir (onceki node'lar
   zaten UP_TO_DATE oldugu icin `PipelineEngine` onlari atlar - bunu
   `atlandi (zaten UP_TO_DATE, ...)` satirlarindan gorebilirsin).
6. Sonuc `result_display` ile basilir. **Blogun ciktisi bir DataFrame ise**
   (bkz. `BlockSpec::producesDataFrame` - `to_tensor`/`create_dataloader`/
   `mlp_learner`/`compute_*_metrics` DISINDAKI HEMEN HEMEN HER BLOK icin
   `true`), meta bilgisinin ALTINA otomatik olarak kucuk bir `data_preview`
   node'u eklenip calistirilir ve **ilk 5 satir** ayrica gosterilir - yani
   her df dondugunde hem meta (shape/columns/...) hem veri onizlemesi
   birlikte gorunur. `data_preview` blogunun kendisinde bu tekrar
   TETIKLENMEZ (zaten aynisini gosteriyor).
7. Blok hata verirse, o node pipeline'dan otomatik silinir (bir onceki
   basarili node dokunulmadan kalir), tekrar deneyebilirsin.
8. `liste` -> desteklenen blok adlarini (ve hangi girdileri bekledigini)
   gosterir, `durum` -> o ana kadarki pipeline durumunu gosterir,
   `q`/`sonlandir` -> cikar.

Ornek bir oturum (elle deneyebilirsin, tek slotlu bloklarda girdi
sorusunu bos birakip Enter'a basman yeterli):
```
Blok adi: load_csv
    file_path (orn. test_data_full.csv): test_data_full.csv
    separator [bos = ',']:
    encoding [bos = 'utf-8']:
  ...BASARILI, node_1:output -> shape/columns/dtypes + otomatik ilk 5 satir

Blok adi: handle_missing_values
    girdi 'data' -> kaynak (node_id veya node_id:slot) [bos = node_1:output]:
    strategy (mean/median/mode/constant/drop_rows/drop_columns) [bos = mean]: median
    columns (virgulle ayir) [bos = tum uygun kolonlar]:
    fill_value (sadece strategy=constant ise gerekli) [bos = -]:
  ...BASARILI, node_2:output + otomatik ilk 5 satir

Blok adi: train_test_split
    girdi 'data' -> kaynak (node_id veya node_id:slot) [bos = node_2:output]:
    train_ratio [bos = 0.8]:
    shuffle (true/false) [bos = true]:
    random_seed [bos = 42]:
  ...BASARILI, node_3:train VE node_3:test icin AYRI AYRI otomatik ilk 5 satir

Blok adi: drop_columns
    girdi 'data' -> kaynak (node_id veya node_id:slot):    <- BOS BIRAKMA, ac (train birden fazla slottan biri)
    node_3:train
    columns (virgulle ayir, ZORUNLU): target
  ...BASARILI, node_4:output (X_train) + otomatik ilk 5 satir

Blok adi: q
```

Yeni bir blok destegi eklemek istersen, SADECE `block_specs.cpp` icindeki
`blockRegistrySpecs()` fonksiyonuna yeni bir `BlockSpec` (girdi slotlari +
`producesDataFrame` + parametreler) eklemen yeterli - `interactive_main.cpp`'de
hicbir degisiklik gerekmez, cunku o zaten registry'den generic sekilde okuyor.

## ADIM 7: Gercek GUI (gui_client.exe)

`interactive_main.cpp`'nin metin tabanli soru-cevabinin GORSEL karsiligi:
blok paleti (sol), surukle-birak node-graph tuvali (orta, Dear ImGui +
ImNodes ile), secili node'un parametre formu + cikti gorunumu (sag). Mockup
DEGIL - dogrudan AYNI `PipelineEngine`/`PythonProcess`/`dispatcher.py`
uclusunu kullanir (bkz. `gui_app.cpp`, `gui_main.cpp`).

### Dosyalar

| Dosya | Ne yapar |
|---|---|
| `third_party/imgui/`, `third_party/imnodes/` | Vendor edilmis (disaridan indirilmis, oldugu gibi projeye eklenmis) kaynak kod - bkz. `third_party/README.md` icin tam surum bilgisi ve NEDEN eski bir imgui surumu secildigi (ImNodes v0.5 ile uyum). |
| `result_display_imgui.h/.cpp` | `result_display.cpp` ile AYNI "output_type'a gore gorunum sec" mantigi, ama `std::cout` yerine ImGui tablo/metin widget'lari. `result_display.cpp`'ye DOKUNULMADI. |
| `gui_app.h/.cpp` | Asil "controller": ImNodes'un node/pin/link int id'leri ile `PipelineEngine`'in string node id'leri arasinda cevrim yapar, kullanici etkilesimlerini (yeni node/baglanti/parametre/silme/calistir) GERCEK `engine.addNode/connect/disconnect/setParams/removeNode/runAll` cagrilarina cevirir. |
| `gui_main.cpp` | `main()`: Win32 penceresi + Direct3D11 kurulumu (Dear ImGui'nin resmi ornek koduyla neredeyse ayni), `PythonProcess`'i baslatir, her karede `GuiApp::render()`'i cagirir. |

`pipeline_engine.h/.cpp`'ye KUCUK bir ekleme yapildi: `disconnect(nodeId, slot)`
- `connect()`'in tersi, GUI'de bir baglanti cizgisini surukleyip bosluga
birakinca (ya da baska bir pine tasiyinca) cagrilir. Metin tabanli
`interactive_main.cpp`'de bu ihtiyac hic dogmamisti (baglantilar sadece
EKLENIYORDU). `block_specs.h`'ye de `BlockSpec::outputSlots` eklendi -
GUI'nin bir node'u CALISTIRMADAN ONCE dogru sayida cikis pini (orn.
`train_test_split` icin `train`+`test`) cizebilmesi icin.

### Derleme ONEMLI NOT

GUI, d3d11.h/dxgi.h/d3dcompiler.h/dwmapi.h gibi Windows SDK basliklarina
ihtiyac duyar - bunlar SADECE MinGW-w64 dagitimlarinda (orn. MSYS2'nin
`mingw-w64-x86_64-gcc` paketi) bulunur, projenin diger iki .exe'si icin
yeterli olan eski/minimal MinGW.org dagitiminda YOKTUR. `build.bat`,
`C:\msys64\mingw64\bin\g++.exe` varsa OTOMATIK olarak onu kullanir (ve
derleme sirasinda gerekli DLL'leri bulmasi icin PATH'e ekler); yoksa PATH'teki
`g++`'a duser ve muhtemelen `d3d11.h bulunamadi` hatasi alirsin - bu durumda
MSYS2 kurup su paketi eklemen gerekir:
```
pacman -S mingw-w64-x86_64-gcc
```

### Calistirma

```
cd ..
c_client\gui_client.exe
```

(Yine proje KOK dizininden - `dispatcher.py` oraya gore araniyor.)

Kisa kullanim: sol panelden bir blok adina tikla -> node tuvale eklenir
(otomatik secili olur, sag panelde parametrelerini doldurabilirsin).
Bir node'un cikis pininden (sagdaki nokta) baska bir node'un girdi pinine
(soldaki nokta) surukleyerek baglanti kur. Sag panelde "Parametreleri
Uygula" ile degerleri kaydet (bos birakilan alanlar Python'un varsayilanini
kullanir, `promptForParams` ile ayni kural). Ustteki "Calistir (Run All)"
DIRTY/NOT_RUN node'lari calistirir (UP_TO_DATE'ler atlanir - ayni "secici
calistirma"). Node kutusunun basligi durumuna gore renklenir (yesil =
UP_TO_DATE, sari = DIRTY, kirmizi = ERROR, gri = NOT_RUN - KNIME'daki
"traffic light" fikri). Node secip Delete tusuna basarak ya da sag panelden
"Bu Node'u Sil" ile silebilirsin.

### v1 kapsam disi (bilerek, ileride genisletilebilir)

- Grafik ureten bloklarin (plot_histogram vb.) ciktisi GERCEK bir grafik
  olarak cizilmiyor - sadece `chart_type` + bir placeholder notu gosteriliyor
  (bkz. `result_display_imgui.cpp` `renderChartPlaceholder`). Gercek Plotly
  `figure_json` render'i icin ayri bir cizim katmani gerekir.
- `engine.runAll()` GUI'de de senkron (bloklu) cagriliyor - uzun suren bir
  blok (orn. `mlp_learner`) calisirken pencere kisa sureligine "yanit
  vermiyor" gorunebilir. Asenkron calistirma CLAUDE.md'de zaten ayri,
  ileriye donuk bir madde olarak isaretli.
- Node pozisyonlarinin/pipeline'in diske kaydedilip tekrar yuklenmesi yok -
  her acilista bos bir tuvalle baslarsin.

## Bilerek basitte birakilanlar (ileride genisletilecek)

- `main.cpp` icindeki node/parametre tanimlari SABIT (hard-coded). Gercek
  UI'da bunlar kullanicinin ekranda surukleyip biraktigi bloklardan/
  girdiklerinden gelecek - `PipelineEngine`'in `addNode`/`connect`/
  `setParams`/`removeNode` API'si zaten bunun icin hazir, sadece cagiran
  taraf (gercek UI kodu) degisecek.
- Hata durumunda downstream node'lari "atlama" kurali basit tutuldu (bir
  turda calistirilmazlar, DIRTY kalirlar); gercek UI'da bu node'larin
  ekranda "engellendi" gibi ayri bir gorsel durumla isaretlenmesi gerekebilir.
- Uzun suren islemler (model egitimi gibi) icin asenkron/zaman asimi
  mekanizmasi henuz yok (CLAUDE.md'deki "Acik Kalan Konular" bolumunde de
  belirtilmis) - su an `request()` cevabi gelene kadar bloklu (senkron) bekliyor.
