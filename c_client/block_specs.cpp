/*
 * block_specs.cpp
 */

#include "block_specs.h"   // BlockSpec/ParamSpec bildirimleri
#include <iostream>          // std::cin/std::cout
#include <sstream>           // std::stringstream (virgulle ayirma icin)
#include <cstdlib>           // std::strtod (metin -> sayi)
#include <cctype>            // tolower

namespace {   // bu dosyaya ozel yardimci fonksiyonlar

/* Basindaki/sonundaki bosluk, tab, \r karakterlerini temizler.
 * (\r ozellikle Windows terminalinden gelen satirlarda kalabiliyor.) */
std::string trim(const std::string& s) {
    size_t start = 0;   // baslangicta atlanacak bosluk sayisi
    while (start < s.size() && (s[start] == ' ' || s[start] == '\t' || s[start] == '\r' || s[start] == '\n')) start++;   // bastan bosluklari say
    size_t end = s.size();   // sonda atlanacak kismin sinirini tutar
    while (end > start && (s[end - 1] == ' ' || s[end - 1] == '\t' || s[end - 1] == '\r' || s[end - 1] == '\n')) end--;   // sondan bosluklari geri say
    return s.substr(start, end - start);   // sadece ortadaki "temiz" kismi doner
}

/* "age, income , city" -> ["age","income","city"] */
JsonValue parseStringList(const std::string& raw) {
    JsonValue arr = JsonValue::makeArray();      // sonuc: string'lerden olusan bir JSON dizisi
    std::stringstream ss(raw);                     // raw metni bir akis (stream) gibi okuyabilmek icin
    std::string token;                              // her seferinde bir parca (virgule kadar) tutar
    while (std::getline(ss, token, ',')) {          // ',' karakterine kadar oku, o karaktere kadar olani token'a koy
        std::string t = trim(token);                  // parcanin bas/son bosluklarini temizle
        if (!t.empty()) arr.push_back(JsonValue(t));    // bos degilse (orn. ardisik virgullerden dogan bos parca degilse) diziye ekle
    }
    return arr;   // temizlenmis kolon adlari dizisini doner
}

JsonValue parseNumber(const std::string& raw) {
    return JsonValue(std::strtod(raw.c_str(), NULL));   // metni double'a cevirip JsonValue olarak sar
}

JsonValue parseBool(const std::string& raw) {
    std::string t = raw;   // kucuk harfe cevrilecek kopya (orijinali degistirmemek icin)
    for (size_t i = 0; i < t.size(); i++) t[i] = static_cast<char>(tolower(static_cast<unsigned char>(t[i])));   // her karakteri kucuk harfe cevir
    bool value = (t == "true" || t == "evet" || t == "1" || t == "yes" || t == "e");   // hem Turkce hem Ingilizce "evet" karsiliklarini kabul et
    return JsonValue(value);   // sonucu JsonValue olarak doner
}

/* Kucuk yardimci: tek bir ParamSpec olusturur - asagida her blok
 * tanimini kisa/okunakli tutmak icin. */
ParamSpec P(const std::string& key, const std::string& prompt, ParamType type, bool required) {
    ParamSpec p;          // doldurulacak bos yapi
    p.key = key;            // JSON alan adi
    p.prompt = prompt;       // terminalde gosterilecek soru metni
    p.type = type;            // beklenen veri turu
    p.required = required;     // zorunlu mu
    return p;   // doldurulmus ParamSpec'i doner (deger olarak kopyalanir)
}

std::vector<std::string> slots1(const std::string& a) {
    std::vector<std::string> v;   // tek elemanli girdi slotu listesi olusturmak icin kisayol
    v.push_back(a);                 // tek slot adini ekle
    return v;
}

std::vector<std::string> slots2(const std::string& a, const std::string& b) {
    std::vector<std::string> v;   // iki elemanli girdi slotu listesi olusturmak icin kisayol
    v.push_back(a);                 // ilk slot adi (orn. "X")
    v.push_back(b);                 // ikinci slot adi (orn. "y")
    return v;
}

std::vector<std::string> slots3(const std::string& a, const std::string& b, const std::string& c) {
    std::vector<std::string> v;   // uc elemanli girdi slotu listesi olusturmak icin kisayol
    v.push_back(a);                 // orn. "model"
    v.push_back(b);                 // orn. "X"
    v.push_back(c);                 // orn. "y"
    return v;
}

} // anonymous namespace

const std::map<std::string, BlockSpec>& blockRegistrySpecs() {
    static std::map<std::string, BlockSpec> registry;   // fonksiyon-ici statik: sadece ILK cagrida doldurulur, sonrasinda ayni harita tekrar kullanilir
    if (!registry.empty()) return registry;               // zaten doldurulmussa yeniden hesaplamadan direkt doner

    // ============================= veri yukleme =============================

    {   // her blok icin ayri bir kapsam (scope): "spec" degiskeni sadece burada gecerli, isim carpismasi olmasin diye
        BlockSpec spec;                           // bu blogun tanimini tutacak bos yapi
        spec.inputSlots.clear();               // zincirin basi, girdi istemez
        spec.producesDataFrame = true;           // load_csv bir DataFrame uretir -> otomatik onizleme tetiklenir
        spec.params.push_back(P("file_path", "file_path (orn. test_data_full.csv): ", ParamType::String, true));   // okunacak CSV dosyasinin yolu, zorunlu
        spec.params.push_back(P("separator", "separator [bos = ',']: ", ParamType::String, false));                  // CSV ayrac karakteri, opsiyonel
        spec.params.push_back(P("encoding", "encoding [bos = 'utf-8']: ", ParamType::String, false));                // dosya kodlamasi, opsiyonel
        registry["load_csv"] = spec;   // tamamlanan tanimi "load_csv" adiyla tabloya kaydet
    }

    // ============================= on-isleme =============================

    {   // eksik degerleri doldurma/silme blogu
        BlockSpec spec;
        spec.inputSlots = slots1("data");     // tek girdi: onceki blogun ciktisi
        spec.producesDataFrame = true;          // sonuc yine bir DataFrame
        spec.params.push_back(P("strategy",
            "strategy (mean/median/mode/constant/drop_rows/drop_columns) [bos = mean]: ", ParamType::String, false));   // hangi doldurma stratejisi
        spec.params.push_back(P("columns", "columns (virgulle ayir) [bos = tum uygun kolonlar]: ", ParamType::StringList, false));   // hangi kolonlara uygulanacak
        spec.params.push_back(P("fill_value", "fill_value (sadece strategy=constant ise gerekli) [bos = -]: ", ParamType::String, false));   // sabit deger stratejisi icin doldurulacak deger
        registry["handle_missing_values"] = spec;   // tabloya "handle_missing_values" adiyla kaydet
    }
    {   // tekrar eden satirlari temizleme blogu
        BlockSpec spec;
        spec.inputSlots = slots1("data");
        spec.producesDataFrame = true;
        spec.params.push_back(P("subset", "subset (virgulle ayir) [bos = tum kolonlar]: ", ParamType::StringList, false));   // hangi kolonlara gore "ayni satir" karari verilecek
        spec.params.push_back(P("keep", "keep (first/last) [bos = tum kopyalari sil]: ", ParamType::String, false));           // kopyalardan hangisi tutulacak
        spec.params.push_back(P("ignore_index", "ignore_index (true/false) [bos = false]: ", ParamType::Bool, false));          // satir silindikten sonra index sifirlansin mi
        registry["remove_duplicates"] = spec;
    }
    {   // aykiri (outlier) deger isleme blogu
        BlockSpec spec;
        spec.inputSlots = slots1("data");
        spec.producesDataFrame = true;
        spec.params.push_back(P("columns", "columns (virgulle ayir) [bos = tum sayisal kolonlar]: ", ParamType::StringList, false));   // hangi sayisal kolonlar taranacak
        spec.params.push_back(P("method", "method (iqr/zscore) [bos = iqr]: ", ParamType::String, false));                              // aykirilik tespit yontemi
        spec.params.push_back(P("iqr_multiplier", "iqr_multiplier (sadece method=iqr) [bos = 1.5]: ", ParamType::Number, false));         // IQR yontemi katsayisi
        spec.params.push_back(P("zscore_threshold", "zscore_threshold (sadece method=zscore) [bos = 3.0]: ", ParamType::Number, false));   // zscore yontemi esik degeri
        spec.params.push_back(P("action", "action (remove/cap/impute_mean/impute_median) [bos = remove]: ", ParamType::String, false));    // aykiri deger bulununca ne yapilacak
        registry["handle_outliers"] = spec;
    }
    {   // belirtilen kolonlari tamamen silme blogu
        BlockSpec spec;
        spec.inputSlots = slots1("data");
        spec.producesDataFrame = true;
        spec.params.push_back(P("columns", "columns (virgulle ayir, ZORUNLU): ", ParamType::StringList, true));   // silinecek kolonlar, bos gecilemez
        registry["drop_columns"] = spec;
    }
    {   // tek bir kolonu yeniden adlandirma blogu
        BlockSpec spec;
        spec.inputSlots = slots1("data");
        spec.producesDataFrame = true;
        spec.params.push_back(P("column", "column (mevcut kolon adi, ZORUNLU): ", ParamType::String, true));   // hangi kolon
        spec.params.push_back(P("new_name", "new_name (yeni kolon adi, ZORUNLU): ", ParamType::String, true));   // yeni adi ne olacak
        registry["rename_columns"] = spec;
    }
    {   // bir kolonun veri turunu (dtype) degistirme blogu
        BlockSpec spec;
        spec.inputSlots = slots1("data");
        spec.producesDataFrame = true;
        spec.params.push_back(P("column", "column (ZORUNLU): ", ParamType::String, true));                                        // hangi kolon
        spec.params.push_back(P("dtype", "dtype (orn. int64/float64/str/category, ZORUNLU): ", ParamType::String, true));   // hedef veri turu
        registry["convert_dtype"] = spec;
    }

    // ============================= encode / scale =============================

    {   // kategorik kolonlari sayisallastirma (encode) blogu
        BlockSpec spec;
        spec.inputSlots = slots1("data");
        spec.producesDataFrame = true;
        spec.params.push_back(P("method", "method (label/onehot/ordinal) [bos = label]: ", ParamType::String, false));                      // encode yontemi
        spec.params.push_back(P("columns", "columns (virgulle ayir) [bos = tum kategorik kolonlar]: ", ParamType::StringList, false));        // hangi kolonlar
        spec.params.push_back(P("category_order","category_order (sadece method=ordinal, virgulle ayir, ZORUNLU): ", ParamType::StringList, false));
        spec.params.push_back(P("drop_first", "drop_first (sadece method=onehot) (true/false) [bos = false]: ", ParamType::Bool, false));   // onehot'ta ilk kategori dusurulsun mu
        registry["encode_categorical"] = spec;
    }
    {   // sayisal kolonlari olceklendirme (scale) blogu
        BlockSpec spec;
        spec.inputSlots = slots1("data");
        spec.producesDataFrame = true;
        spec.params.push_back(P("method", "method (minmax/zscore/robust) [bos = minmax]: ", ParamType::String, false));                // olceklendirme yontemi
        spec.params.push_back(P("columns", "columns (virgulle ayir) [bos = tum sayisal kolonlar]: ", ParamType::StringList, false));   // hangi kolonlar
        registry["scale_features"] = spec;
    }

    // ============================= bolme (split) =============================
    // NOT: bu bloklarin CIKISI TEK slot degil (train/test, ya da
    // train/validation/test) - bir sonraki blokta hangisini kullanacagini
    // interactive_main.cpp sana "node_id:slot" seklinde soracak
    // (orn. "node_7:train").

    {   // veriyi train/test olarak ikiye bolme blogu (birden fazla cikis slotu uretir)
        BlockSpec spec;
        spec.inputSlots = slots1("data");
        spec.producesDataFrame = true;   // train VE test ciktisi da DataFrame
        spec.params.push_back(P("train_ratio", "train_ratio [bos = 0.8]: ", ParamType::Number, false));   // train'e ayrilacak oran
        spec.params.push_back(P("shuffle", "shuffle (true/false) [bos = true]: ", ParamType::Bool, false));   // bolmeden once karistirilsin mi
        spec.params.push_back(P("random_seed", "random_seed [bos = 42]: ", ParamType::Number, false));         // tekrarlanabilirlik icin rastgelelik tohumu
        spec.outputSlots.clear();
        spec.outputSlots.push_back("train");
        spec.outputSlots.push_back("test");
        registry["train_test_split"] = spec;
    }
    {   // veriyi train/validation/test olarak uce bolme blogu
        BlockSpec spec;
        spec.inputSlots = slots1("data");
        spec.producesDataFrame = true;   // train/validation/test ciktilari da DataFrame
        spec.params.push_back(P("train_ratio", "train_ratio [bos = 0.7]: ", ParamType::Number, false));               // train orani
        spec.params.push_back(P("validation_ratio", "validation_ratio [bos = 0.15]: ", ParamType::Number, false));     // validation orani
        spec.params.push_back(P("test_ratio", "test_ratio [bos = 0.15]: ", ParamType::Number, false));                 // test orani
        spec.params.push_back(P("shuffle", "shuffle (true/false) [bos = true]: ", ParamType::Bool, false));             // bolmeden once karistirilsin mi
        spec.params.push_back(P("random_seed", "random_seed [bos = 42]: ", ParamType::Number, false));                   // tekrarlanabilirlik icin rastgelelik tohumu
        spec.outputSlots.clear();
        spec.outputSlots.push_back("train");
        spec.outputSlots.push_back("validation");
        spec.outputSlots.push_back("test");
        registry["train_validation_test_split"] = spec;
    }

    // ============================= tensor / dataloader / model =============================
    // Bu uc blogun ciktisi DataFrame DEGIL (tensor / DataLoader / nn.Module),
    // bu yuzden producesDataFrame = false - otomatik "ilk 5 satir" onizlemesi
    // burada TETIKLENMEZ (tetiklenirse data_preview DataFrame bekleyip hata verirdi).

    {   // DataFrame'i PyTorch tensor'una cevirme blogu
        BlockSpec spec;
        spec.inputSlots = slots1("data");
        spec.producesDataFrame = false;   // cikti tensor, DataFrame degil
        spec.params.push_back(P("dtype", "dtype (float32/float64/long/int64/int32) [bos = float32]: ", ParamType::String, false));   // tensor veri turu
        spec.params.push_back(P("squeeze", "squeeze (true/false) [bos = false, y icin genelde true]: ", ParamType::Bool, false));       // tek boyutlu eksenler sikistirilsin mi
        registry["to_tensor"] = spec;
    }
    {   // X/y tensorlerinden PyTorch DataLoader olusturma blogu
        BlockSpec spec;
        spec.inputSlots = slots2("X", "y");   // iki ayri girdi: ozellikler ve etiketler
        spec.producesDataFrame = false;   // cikti DataLoader, DataFrame degil
        spec.params.push_back(P("batch_size", "batch_size [bos = 32]: ", ParamType::Number, false));                                    // her batch'teki ornek sayisi
        spec.params.push_back(P("shuffle", "shuffle (true/false) [bos = false, train icin true onerilir]: ", ParamType::Bool, false));   // her epoch'ta karistirilsin mi
        spec.params.push_back(P("drop_last", "drop_last (true/false) [bos = false]: ", ParamType::Bool, false));                          // eksik kalan son batch atilsin mi
        registry["create_dataloader"] = spec;
    }
    {   // MLP (coklu katmanli algılayıcı) modeli egitme blogu
        BlockSpec spec;
        spec.inputSlots = slots1("train_dataloader");   // tek girdi: egitim DataLoader'i
        spec.producesDataFrame = false;   // cikti egitilmis model, DataFrame degil
        spec.params.push_back(P("task_type", "task_type (classification/regression, ZORUNLU): ", ParamType::String, true));   // gorev turu
        spec.params.push_back(P("output_size", "output_size (classification icin sinif sayisi ZORUNLU, regression icin bos=1): ", ParamType::Number, false));   // cikis katmani boyutu
        spec.params.push_back(P("learning_rate", "learning_rate [bos = 0.001]: ", ParamType::Number, false));   // ogrenme orani
        spec.params.push_back(P("epochs", "epochs [bos = 10]: ", ParamType::Number, false));                     // egitim tur sayisi
        spec.params.push_back(P("optimizer", "optimizer (adam/sgd) [bos = adam]: ", ParamType::String, false));   // optimizasyon algoritmasi
        // NOT: layer_config (katman mimarisi) burada sorulmuyor - komut
        // satirindan JSON liste girdirmek pratik degil, bos birakilirsa
        // Python tarafi varsayilan tek-katmanli (64 noron, relu) mimariyi kullanir.
        registry["mlp_learner"] = spec;
    }

    // ============================= metrikler (skor) =============================

    {   // siniflandirma (classification) metriklerini hesaplama blogu
        BlockSpec spec;
        spec.inputSlots = slots3("model", "X", "y");   // model + test ozellikleri + test etiketleri
        spec.producesDataFrame = false;   // cikti sayisal skorlar, DataFrame degil
        spec.params.push_back(P("metrics",
            "metrics (virgulle ayir: accuracy,precision,recall,f1,confusion_matrix) [bos = hepsi]: ", ParamType::StringList, false));   // hesaplanacak metrikler
        spec.params.push_back(P("average", "average (macro/micro/weighted) [bos = macro]: ", ParamType::String, false));   // coklu sinif ortalama yontemi
        registry["compute_classification_metrics"] = spec;
    }
    {   // regresyon metriklerini hesaplama blogu
        BlockSpec spec;
        spec.inputSlots = slots3("model", "X", "y");
        spec.producesDataFrame = false;
        spec.params.push_back(P("metrics", "metrics (virgulle ayir: mse,rmse,mae,r2) [bos = hepsi]: ", ParamType::StringList, false));   // hesaplanacak metrikler
        registry["compute_regression_metrics"] = spec;
    }

    // ============================= view / rapor bloklari =============================
    // Bu bloklarin "output" ciktisi da (degistirilmeden) bir DataFrame'dir
    // (bkz. blocks/view_blocks.py: her biri {"data": data, "meta": ...} doner),
    // bu yuzden producesDataFrame = true - onlar da otomatik onizleme alir.
    // (data_preview haric: onun meta'si zaten TAM OLARAK ayni onizlemeyi
    // icerdigi icin interactive_main.cpp onun icin otomatik onizlemeyi atlar.)

    {   // ilk/son N satiri gosteren onizleme blogu
        BlockSpec spec;
        spec.inputSlots = slots1("data");
        spec.producesDataFrame = true;
        spec.params.push_back(P("row_count", "row_count [bos = 5]: ", ParamType::Number, false));                // kac satir gosterilecek
        spec.params.push_back(P("preview_type", "preview_type (head/tail) [bos = head]: ", ParamType::String, false));   // basdan mi sondan mi
        registry["data_preview"] = spec;
    }
    {   // veri seti hakkinda genel ozet blogu (parametresiz)
        BlockSpec spec;
        spec.inputSlots = slots1("data");
        spec.producesDataFrame = true;
        registry["dataset_summary"] = spec;
    }
    {   // istatistiksel ozet (describe) blogu (parametresiz)
        BlockSpec spec;
        spec.inputSlots = slots1("data");
        spec.producesDataFrame = true;
        registry["describe_statistics"] = spec;
    }
    {   // eksik deger raporu blogu (parametresiz)
        BlockSpec spec;
        spec.inputSlots = slots1("data");
        spec.producesDataFrame = true;
        registry["missing_values_report"] = spec;
    }
    {   // tekrar eden satir raporu blogu
        BlockSpec spec;
        spec.inputSlots = slots1("data");
        spec.producesDataFrame = true;
        spec.params.push_back(P("max_rows", "max_rows [bos = 50]: ", ParamType::Number, false));   // raporda en fazla kac satir gosterilecek
        registry["duplicate_rows_report"] = spec;
    }
    {   // kolon veri turleri ozeti blogu (parametresiz)
        BlockSpec spec;
        spec.inputSlots = slots1("data");
        spec.producesDataFrame = true;
        registry["data_types_summary"] = spec;
    }

    // ============================= grafikler (chart) =============================
    // Bu bloklarin ciktisi da (degistirilmeden) DataFrame'dir; meta'si
    // "output_type": "chart" tasidigi icin result_display zaten sadece
    // basligi/boyutu basiyor (figure_json'un tamamini basmiyor). Otomatik
    // "ilk 5 satir" onizlemesi bunlarda da faydali oldugu icin acik birakildi.

    {   // histogram grafigi blogu
        BlockSpec spec;
        spec.inputSlots = slots1("data");
        spec.producesDataFrame = true;
        spec.params.push_back(P("column", "column (ZORUNLU): ", ParamType::String, true));   // hangi kolonun dagilimi cizilecek
        spec.params.push_back(P("bins", "bins [bos = 20]: ", ParamType::Number, false));        // kac araliga (bin) bolunecek
        registry["plot_histogram"] = spec;
    }
    {   // cubuk (bar) grafigi blogu
        BlockSpec spec;
        spec.inputSlots = slots1("data");
        spec.producesDataFrame = true;
        spec.params.push_back(P("column", "column (ZORUNLU): ", ParamType::String, true));   // hangi kolon
        spec.params.push_back(P("top_n", "top_n [bos = 20]: ", ParamType::Number, false));      // en fazla kac kategori gosterilecek
        registry["plot_bar_chart"] = spec;
    }
    {   // kutu grafigi (boxplot) blogu
        BlockSpec spec;
        spec.inputSlots = slots1("data");
        spec.producesDataFrame = true;
        spec.params.push_back(P("column", "column (ZORUNLU): ", ParamType::String, true));                             // hangi sayisal kolon
        spec.params.push_back(P("category_column", "category_column [bos = -]: ", ParamType::String, false));   // (opsiyonel) hangi kategoriye gore gruplanacak
        registry["plot_boxplot"] = spec;
    }
    {   // sacilim (scatter) grafigi blogu
        BlockSpec spec;
        spec.inputSlots = slots1("data");
        spec.producesDataFrame = true;
        spec.params.push_back(P("x_column", "x_column (ZORUNLU): ", ParamType::String, true));                    // yatay eksen kolonu
        spec.params.push_back(P("y_column", "y_column (ZORUNLU): ", ParamType::String, true));                    // dikey eksen kolonu
        spec.params.push_back(P("color_column", "color_column [bos = -]: ", ParamType::String, false));   // (opsiyonel) renklendirme icin kolon
        registry["plot_scatter"] = spec;
    }
    {   // korelasyon isi haritasi (heatmap) blogu (parametresiz)
        BlockSpec spec;
        spec.inputSlots = slots1("data");
        spec.producesDataFrame = true;
        registry["plot_correlation_heatmap"] = spec;
    }
    {   // eksik deger haritasi blogu (parametresiz)
        BlockSpec spec;
        spec.inputSlots = slots1("data");
        spec.producesDataFrame = true;
        registry["plot_missing_values"] = spec;
    }

    return registry;   // tamamen doldurulmus tabloyu doner
}

void printAvailableBlocks() {
    std::cout << "Desteklenen bloklar:\n";
    const std::map<std::string, BlockSpec>& registry = blockRegistrySpecs();   // tum blok tanimlarinin tablosu
    for (std::map<std::string, BlockSpec>::const_iterator it = registry.begin(); it != registry.end(); ++it) {   // her blok icin (isme gore alfabetik sirayla)
        std::cout << "  - " << it->first;                       // blok adini yaz
        if (it->second.inputSlots.empty()) {                      // girdi slotu yoksa (zincirin basi)
            std::cout << "  (girdi gerektirmez, zinciri baslatir)";
        } else {                                                    // girdi slotlari varsa
            std::cout << "  (girdi: ";
            for (size_t i = 0; i < it->second.inputSlots.size(); i++) {   // hepsini virgulle ayirarak listele
                if (i > 0) std::cout << ", ";
                std::cout << it->second.inputSlots[i];
            }
            std::cout << ")";
        }
        std::cout << "\n";
    }
}

JsonValue promptForParams(const BlockSpec& spec) {
    JsonValue params = JsonValue::makeObject();   // biriktirilecek sonuc: doldurulmus parametreler nesnesi

    for (size_t i = 0; i < spec.params.size(); i++) {   // blogun bekledigi her parametre icin sirayla
        const ParamSpec& p = spec.params[i];               // su anki parametre tanimi

        while (true) {   // gecerli/kabul edilebilir bir cevap alinana kadar tekrar sor
            std::cout << "    " << p.prompt;    // soru metnini goster
            std::string raw;                      // kullanicinin ham girdisi
            std::getline(std::cin, raw);            // bir satir oku
            raw = trim(raw);                          // bas/son bosluklari temizle

            JsonValue parsedValue;                  // basariyla ayristirilirsa buraya yazilacak
            bool treatAsEmpty = raw.empty();         // girdi bombos mu (parametre atlanacak mi)

            if (!treatAsEmpty) {                       // bir sey girilmisse turune gore ayristir
                switch (p.type) {
                    case ParamType::String: parsedValue = JsonValue(raw); break;      // metin: oldugu gibi al
                    case ParamType::Number: parsedValue = parseNumber(raw); break;    // sayi: metinden double'a cevir
                    case ParamType::Bool:   parsedValue = parseBool(raw); break;      // bool: evet/hayir varyasyonlarini tani
                    case ParamType::StringList:
                        parsedValue = parseStringList(raw);   // virgulle ayrilmis listeyi diziye cevir
                        /* "," ya da ", ," gibi bir girdi virgulle ayrildiginda
                         * TAMAMEN BOS bir listeye donusebilir. Bunu JSON'a
                         * "columns": [] olarak eklersek, Python tarafinda
                         * .get("columns", DEFAULT) artik DEFAULT'u DEGIL,
                         * bu bos listeyi kullanir - blok hicbir kolonda
                         * calismamis gibi davranir (sessiz, yanlis sonuc).
                         * Bu yuzden bos parse sonucu da "cevaplanmadi" sayilir. */
                        if (parsedValue.size() == 0) {                                  // ayristirma sonucu bos dizi cikmissa
                            std::cout << "      -> (gecerli bir kolon adi bulunamadi, bos birakilmis gibi islenecek)\n";
                            treatAsEmpty = true;                                          // "bos birakilmis" gibi davran
                        }
                        break;
                }
            }

            if (treatAsEmpty) {                    // parametre hic verilmemis (ya da bos sayilan) durum
                if (p.required) {                     // zorunluysa bos gecilemez
                    std::cout << "      -> bu alan ZORUNLU, tekrar gir.\n";
                    continue;                            // dongu basina donup tekrar sor
                }
                // opsiyonel + bos: bu alani JSON'a hic eklemiyoruz,
                // Python tarafi kendi varsayilanini kullanacak.
                break;   // bu parametre icin dongudan cik, params'a hicbir sey eklenmez
            }

            params[p.key] = parsedValue;   // gecerli deger: sonuc nesnesine yaz
            break;                            // bu parametre tamam, sonraki parametreye gec
        }
    }

    return params;   // tum parametreler islendikten sonra tam nesneyi doner
}
