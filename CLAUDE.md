# Proje: Node-Tabanli ML Pipeline Araci

## Genel Bakis

KNIME benzeri, gorsel/node-tabanli bir makine ogrenmesi pipeline araci gelistiriliyor.
Kullanici arayuzden (C tarafi) bloklar ekleyip birbirine baglayacak, her blok arka planda
(Python tarafinda) yazilmis bir fonksiyonu/sinifi calistiracak ve sonucu bir sonraki bloga
aktaracak. Model egitimi kisminda PyTorch kullanilacak.

Mimari iki ana parcadan olusuyor:
- **C tarafi**: UI, pipeline graph'i, node durumu (state) takibi, hangi bloğun ne zaman
  calisacagina karar verme
- **Python tarafi**: Gercek islem mantigi (veri yukleme, preprocessing, model egitimi vb.),
  sadece kendisine gonderilen komutu yerine getirir, "karar vermez"

Henuz C tarafinin kodu yazilmadi, su ana kadar sadece Python tarafi ve genel mimari
tasarlandi/kismen yazildi.

---

## C-Python Haberlesme Yontemi

**Secilen yontem: Tek, uzun sureli Python process + stdin/stdout uzerinden JSON mesajlasma.**

### Neden bu yontem secildi

Degerlendirilen alternatifler: her blok icin ayri subprocess, socket, Python embedding
(Python.h), dosya uzerinden haberlesme, HTTP/REST API.

Her blok PyTorch gibi agir bir kutuphaneye bagimli calisacak. Her blok cagrisinda yeni
bir Python process baslatmak, kutuphanenin (ozellikle GPU kullaniminda) her seferinde
yeniden yuklenmesine ve saniyeler mertebesinde gecikmeye yol acar. Bunun yerine:

- Python process'i uygulama baslangicinda **bir kez** baslatilir, arka planda acik tutulur
- PyTorch ve diger kutuphaneler yalnizca bir kez yuklenir
- Bloklar arasi veri aktarimi, kucuk JSON mesajlari (id/referans tasiyarak) uzerinden yapilir
- Buyuk veri (DataFrame, tensor vb.) hicbir zaman C tarafina gonderilmez, sadece Python
  tarafindaki bellekte (session store) tutulur

### Protokol formati

Satir bazli JSON mesajlasma (her mesaj tek satir, sonunda newline, `flush=True` ile
hemen gonderilir).

**Istek (C -> Python):**
```json
{
  "op": "run_block",
  "node_id": "node_3",
  "block": "handle_missing_values",
  "params": {"strategy": "mean"},
  "inputs": {"data": "node_1:output"}
}
```

**Cevap (Python -> C):**
```json
{
  "status": "ok",
  "node_id": "node_3",
  "outputs": {
    "output": {"ref": "node_3:output", "meta": {"shape": [1000, 8], "columns": ["..."]}}
  }
}
```

**Desteklenen operasyonlar:**
- `run_block` — bir blogu calistir
- `delete_node` — bir node'a ait tum session verisini temizle
- `get_meta` — hicbir islem calistirmadan, mevcut bir ciktinin meta bilgisini tekrar dondur

---

## Session Store (Veri Aktarim Mimarisi)

### Ilk tasarimdaki sorun

Ilk versiyonda her blok ciktisi otomatik artan bir sayacla (`obj_1`, `obj_2`, ...)
saklaniyordu. Iki eksiklik vardi:
1. Bir blok farkli parametrelerle tekrar calistirildiginda eski cikti silinmiyor,
   bellekte birikiyordu (bellek sizintisi riski)
2. Bir bloğun birden fazla ciktisi olma senaryosu (orn. train/test split) desteklenmiyordu

### Cozum: node_id + slot bazli anahtarlama

Veri, Python tarafinda rastgele uretilen bir id yerine, **C tarafinin belirledigi sabit
`node_id` + cikis adi (slot)** kombinasyonuyla saklaniyor:

```
anahtar = "{node_id}:{slot_adi}"
```

Bunun sagladiklari:
- Bir node tekrar calistirildiginda eski ciktisi ayni anahtara otomatik yazilir
  (overwrite), ayri bir silme mekanizmasina gerek kalmaz
- Bir bloğun birden fazla ciktisi (`train`, `test` gibi) ayni `node_id` altinda farkli
  slot isimleriyle saklanabilir, her biri bagimsiz olarak farkli bir sonraki bloga
  yonlendirilebilir

`SessionStore` sinifinin metodlari: `set(node_id, slot, obj)`, `get(ref)`,
`clear_node(node_id)`, `delete_node(node_id)`.

---

## Blok Mimarisi (Python Tarafi)

Fonksiyon tabanli yapi yerine, ortak bir **`Block` temel sinifindan (base class) tureyen
nesne tabanli bir yapi** kullaniliyor.

### Sabit akis (her blokta ayni)

```
validate  ->  run  ->  finalize  ->  validate_output
```

- **validate**: Calisma ONCESI kontrol (girdi var mi, bos mu, parametreler gecerli mi).
  `LoadCSVBlock` gibi ilk bloklar bu metodu tamamen override eder (henuz elde `data`
  olmadigi icin base'in "data var mi" kontrolu anlamsizdir)
- **run**: Asil islemin yapildigi, her alt sinifin kendine gore yazdigi kisim
- **finalize**: Calisma SONRASI ortak temizlik (orn. satir silme islemlerinde index
  sifirlama / `reset_index`)
- **validate_output**: Calisma SONRASI kontrol (orn. islem sonucu veri tamamen bos kaldi mi)

### Coklu cikis destegi (_normalize_result)

`run()` metodu iki farkli formatta sonuc donebilir:

```python
# Eski / tek cikisli format (cogu blok bunu kullanir)
{"data": df, "meta": {...}}

# Yeni / cok cikisli format (orn. train_test_split)
{"outputs": {"train": df1, "test": df2},
 "meta": {"train": {...}, "test": {...}}}
```

Base class'taki `_normalize_result` metodu, eski formati otomatik olarak yeni formata
cevirir (`{"outputs": {"output": df}, "meta": {"output": meta}}`), boylece mevcut
bloklarin kodunu degistirmeye gerek kalmiyor.

### Onemli kurallar / prensipler

- **Orijinal veri asla mutasyona ugratilmaz**: her `run` metodu, `get_data_copy(inputs)`
  ile kopya uzerinde calisir
- **`inplace=True` kullanilmaz**: pandas'ta `inplace=True` olan metodlar `None` doner,
  bu ciddi bir hata kaynagidir — bu yuzden bloklarda bu parametre desteklenmiyor
- **Ortak kolon secim mantigi helper'a alinir**: `columns` parametresi verilmemisse
  otomatik olarak sayisal kolonlar secilir (`select_dtypes(include="number")`),
  bu mantik hem `validate` hem `run` icinde kullanildigi icin `_resolve_columns`
  gibi bir yardimci metoda cikarilmasi onerilir

### Su ana kadar yazilan bloklar

| Blok | Durum | Aciklama |
|---|---|---|
| `LoadCSVBlock` | Tamamlandi | CSV dosyasindan veri yukleme |
| `HandleMissingValuesBlock` | Tamamlandi | mean/median/mode/constant/drop_rows/drop_columns stratejileri |
| `RemoveDuplicatesBlock` | Tamamlandi | subset/keep/ignore_index parametreleri, tekrar eden satirlari temizler |
| `HandleOutliersBlock` | Tasarim asamasinda | method (iqr/zscore), iqr_multiplier, zscore_threshold, action (remove/cap/impute_mean/impute_median) |

### Dosya yapisi (onerilen)

```
project/
├── dispatcher.py
├── session_store.py
├── blocks/
│   ├── __init__.py          <- BLOCK_REGISTRY ve create_block() burada
│   ├── base.py               <- Block (base class)
│   ├── data_loading.py       <- LoadCSVBlock, ...
│   ├── preprocessing.py      <- HandleMissingValuesBlock, RemoveDuplicatesBlock, HandleOutliersBlock, ...
│   ├── encoding_scaling.py   <- (ileride) NormalizeBlock, EncodeCategoricalBlock
│   ├── splitting.py          <- (ileride) TrainTestSplitBlock
│   └── metrics.py            <- (ileride) ComputeClassificationMetricsBlock, ...
```

---

## C Tarafinin Sorumluluklari (Pipeline Engine) — Henuz Yazilmadi

Python tarafi "karar vermez", sadece komut yerine getirir. Hangi bloğun ne zaman
calisacagina C tarafi karar verir. Asagidakiler C tarafinda pseudocode olarak
tasarlandi, gercek C kodu henuz yazilmadi (proje sahibi C'ye yeni basliyor).

### Her node icin tutulmasi gereken bilgiler

- `node_id` (sabit, UI tarafindan atanan kimlik)
- `block_name`, `params`
- girdi baglantilari (hangi node'un hangi slot'undan besleniyor)
- cikis slotlari (son calistirmada uretilenler)
- `state`: `NOT_RUN` / `UP_TO_DATE` / `DIRTY` / `ERROR`
- `last_error`
- `downstream_nodes` (bu node'un ciktisini kullanan node'lar — dirty propagation icin)

### Temel mekanizmalar

- **Secici calistirma**: Pipeline calistirildiginda yalnizca `DIRTY`/`NOT_RUN`
  durumundaki node'lar Python'a gonderilir, `UP_TO_DATE` olanlar atlanir. Bu, kullanicinin
  pipeline'a adim adim blok ekleyip her adimda sonucu gormesi senaryosunu destekler
  (onceki bloklar tekrar calismaz)
- **Dirty propagation**: Bir node'un parametresi degisince, kendisi ve ona bagimli TUM
  alt node'lar `DIRTY` isaretlenir (ozyinelemeli/recursive graph gezinmesi ile)
- **Process yonetimi**: Python process'i uygulama acilisinda bir kez baslar. Cokerse
  yeniden baslatilir; bu durumda session store sifirlandigi icin TUM node'lar dirty
  isaretlenmelidir
- **Node silme**: Bir node silindiginde Python'daki karsilik gelen veri (`delete_node`
  mesaji ile) temizlenir, bagimli node'lar dirty yapilir

---

## Acik Kalan / Ileride Ele Alinacak Konular

- Uzun suren islemler (ozellikle model egitimi) icin zaman asimi ve olasi asenkron
  calistirma mekanizmasi (orn. "training basladi" cevabi hemen donup, ilerlemenin ayri
  bir mesajla sorgulanmasi)
- Model egitimi bloklarinin session store'da nasil tutulacagi (PyTorch model/tensor
  nesnelerinin bellek yonetimi — bunlar DataFrame'den çok daha agir olabilir)
- Encoding/scaling, train-test split, model training/evaluation, metrik hesaplama
  bloklarinin yazimi
- C tarafinin gercek implementasyonu (su ana kadar sadece pseudocode duzeyinde tasarlandi)

---

## Proje Sahibi Hakkinda Notlar (Claude Code icin baglam)

- C dilini yeni ogreniyor, C tarafiyla ilgili aciklamalar somut/pseudocode duzeyinde
  tutulmali, dogrudan gercek C sozdizimi yerine adim adim mantik anlatilmali
- KNIME'da hands-on deneyimi var (Iris dataset ile classification workflow denemis),
  bu yuzden KNIME benzetmeleri faydali
- Kod yazip review icin getiriyor, adim adim ilerlemeyi tercih ediyor
- Formal rapor gereksinimleri var (Turkce, Turkce karakter kullanilmadan)
