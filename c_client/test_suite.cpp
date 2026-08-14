/*
 * test_suite.cpp
 *
 * AMAC: main.cpp'deki iki SABIT demo senaryosu yerine, C++ <-> Python
 * (dispatcher.py) hattini COK SAYIDA senaryo ile OTOMATIK test eden dosya.
 * interactive_main.cpp'deki gibi terminalden elle blok/parametre girmek
 * yerine, "kullanicinin yapacagi gibi" blok JSON'larini kod icinde
 * sirayla/zincirleme olusturup PipelineEngine uzerinden calistirir, her
 * node'un BEKLENEN sonuca (basarili / hatali) ulasip ulasmadigini kontrol
 * eder ve EN SONDA toplu bir PASS/FAIL ozeti basar.
 *
 * NEDEN AYRI BIR main()? main.cpp ve interactive_main.cpp de kendi main()
 * fonksiyonlarini tanimliyor - ayni derlemeye (ayni .exe) giremezler. Bu
 * yuzden test_suite.cpp de build.bat'taki gibi AYRI bir .exe olarak
 * derlenmeli (asagidaki komuta bak).
 *
 * ONEMLI VARSAYIM: test_data_full.csv'deki kolon adlarini (age, income,
 * city, target) main.cpp'den aldim. Senin CSV'in FARKLIYSA, asagidaki
 * "KOLON SABITLERI" bolumunu kendi kolonlarina gore guncelle - kodun geri
 * kalanina DOKUNMANA GEREK YOK, hepsi bu sabitler uzerinden calisiyor.
 *
 * DERLEME (proje KOK dizininden DEGIL, c_client icinden calistir):
 *   cd c_client
 *   g++ -std=c++11 -Wall -O2 -o test_suite.exe ^
 *       json_value.cpp python_process.cpp pipeline_engine.cpp ^
 *       result_display.cpp test_suite.cpp
 *
 * CALISTIRMA (main.cpp ile ayni kural: proje KOK dizininden calistir,
 * cunku dispatcher.py ve test_data_full.csv oraya gore aranir):
 *   cd ..
 *   c_client\test_suite.exe
 */

#include <iostream>
#include <string>
#include <vector>
#include <map>
#include "json_value.h"
#include "python_process.h"
#include "pipeline_engine.h"
#include "result_display.h"

// ============================= KOLON SABITLERI =============================
// Kendi CSV'in farkliysa SADECE burayi degistirmen yeterli.
static const std::string CSV_PATH        = "test_data_full.csv";
static const std::string COL_NUMERIC_A   = "age";     // sayisal kolon #1
static const std::string COL_NUMERIC_B   = "income";  // sayisal kolon #2
static const std::string COL_CATEGORICAL = "city";    // kategorik kolon
static const std::string COL_TARGET_CLS  = "target";  // siniflandirma etiketi (0/1 gibi)
static const std::string COL_NOT_EXIST   = "kesinlikle_olmayan_kolon_xyz";   // hata testleri icin bilerek yanlis kolon

// ============================= TEST ISTATISTIGI =============================
// Tum test dosyasi boyunca PASS/FAIL sayisini biriktiren tek/global bir yapi.

struct TestStats {
    int passed;
    int failed;
    std::vector<std::string> failMessages;   // en sonda detayli listelemek icin
    TestStats() : passed(0), failed(0) {}
};

static TestStats g_stats;   // dosya genelinde tek ortak sayac (global degisken)

/* Bir node'un calisma sonucunun BEKLENEN durumla (basarili mi / hatali mi
 * olmasi bekleniyordu) eslesip eslesmedigini kontrol eder, PASS/FAIL basar
 * ve g_stats'i gunceller. Test dosyasinin "assert" fonksiyonu budur. */
static void expectState(const PipelineEngine& engine, const std::string& nodeId,
                         bool expectSuccess, const std::string& testName) {
    const Node* node = engine.getNode(nodeId);
    bool actualSuccess = (node != NULL && node->state == NodeState::UP_TO_DATE);

    if (actualSuccess == expectSuccess) {
        g_stats.passed++;
        std::cout << "  [PASS] " << testName << "\n";
    } else {
        g_stats.failed++;
        std::string detail = testName + "  (beklenen: " + (expectSuccess ? "basarili" : "hatali")
                            + ", gerceklesen: " + (node ? nodeStateToString(node->state) : "node bulunamadi") + ")";
        if (node && node->state == NodeState::ERROR_STATE) {
            detail += "  | hata mesaji: " + node->lastError;
        }
        g_stats.failMessages.push_back(detail);
        std::cout << "  [FAIL] " << detail << "\n";
    }
}

/* Bir test grubunun basligini gorsel olarak ayirir (main.cpp'deki section()
 * fonksiyonuyla ayni fikir). */
static void section(const std::string& title) {
    std::cout << "\n============================================================\n"
              << "TEST GRUBU: " << title << "\n"
              << "============================================================\n";
}

/* Bir node'un TUM cikis slotlarini ekrana basar - gozlemsel testlerde
 * "gercekten ne dondu" gormek icin (main.cpp'deki showAllOutputs ile ayni). */
static void showAllOutputs(const PipelineEngine& engine, const std::string& nodeId) {
    const Node* node = engine.getNode(nodeId);
    if (!node) { std::cout << "  [" << nodeId << "] bulunamadi\n"; return; }
    if (node->state == NodeState::ERROR_STATE) {
        std::cout << "  [" << nodeId << "] HATA: " << node->lastError << "\n";
        return;
    }
    for (std::map<std::string, OutputSlot>::const_iterator it = node->outputs.begin();
         it != node->outputs.end(); ++it) {
        renderNodeOutput(nodeId, it->first, it->second.meta);
    }
}

// ========================================================================
// TEST GRUBU 1: temel yukleme + view bloklari
// ========================================================================
static void testLoadAndBasicViews(PythonProcess& proc) {
    section("1) load_csv + temel view bloklari");
    PipelineEngine engine(proc);

    engine.addNode("g01_1", "load_csv", JsonValue::parse("{\"file_path\": \"" + CSV_PATH + "\"}"));
    engine.addNode("g01_2", "dataset_summary", JsonValue::makeObject());
    engine.connect("g01_2", "data", "g01_1", "output");
    engine.addNode("g01_3", "describe_statistics", JsonValue::makeObject());
    engine.connect("g01_3", "data", "g01_1", "output");
    engine.addNode("g01_4", "missing_values_report", JsonValue::makeObject());
    engine.connect("g01_4", "data", "g01_1", "output");
    engine.addNode("g01_5", "duplicate_rows_report", JsonValue::parse("{\"max_rows\": 10}"));
    engine.connect("g01_5", "data", "g01_1", "output");
    engine.addNode("g01_6", "data_types_summary", JsonValue::makeObject());
    engine.connect("g01_6", "data", "g01_1", "output");

    engine.runAll();

    expectState(engine, "g01_1", true, "load_csv basariyla calisiyor mu");
    expectState(engine, "g01_2", true, "dataset_summary calisiyor mu");
    expectState(engine, "g01_3", true, "describe_statistics calisiyor mu");
    expectState(engine, "g01_4", true, "missing_values_report calisiyor mu");
    expectState(engine, "g01_5", true, "duplicate_rows_report calisiyor mu");
    expectState(engine, "g01_6", true, "data_types_summary calisiyor mu");

    std::cout << "\n  -- gozlem: load_csv ciktisi (shape/columns + otomatik ilk 5 satir YOK, burada elle basiyoruz) --\n";
    showAllOutputs(engine, "g01_1");
}

// ========================================================================
// TEST GRUBU 2: handle_missing_values - TUM stratejiler + 1 gecersiz strateji
// ========================================================================
static void testMissingValueStrategies(PythonProcess& proc) {
    section("2) handle_missing_values - tum stratejiler");
    PipelineEngine engine(proc);
    engine.addNode("g02_load", "load_csv", JsonValue::parse("{\"file_path\": \"" + CSV_PATH + "\"}"));
    engine.runAll();
    expectState(engine, "g02_load", true, "on-kosul: load_csv basarili");

    /* Tekrar eden addNode/connect/expectState blogu yazmamak icin, her test
     * durumunu kucuk bir struct'a koyup bir vector'de biriktiriyoruz, sonra
     * TEK bir dongu ile hepsini ekleyip calistiriyoruz. */
    struct StrategyCase {
        std::string id;
        std::string paramsJson;
        std::string label;
        bool expectOk;
    };
    std::vector<StrategyCase> cases;

    { StrategyCase c; c.id = "g02_mean";     c.paramsJson = "{\"strategy\": \"mean\"}";                              c.label = "strategy=mean";                    c.expectOk = true;  cases.push_back(c); }
    { StrategyCase c; c.id = "g02_median";   c.paramsJson = "{\"strategy\": \"median\"}";                            c.label = "strategy=median";                  c.expectOk = true;  cases.push_back(c); }
    { StrategyCase c; c.id = "g02_mode";     c.paramsJson = "{\"strategy\": \"mode\"}";                              c.label = "strategy=mode";                    c.expectOk = true;  cases.push_back(c); }
    { StrategyCase c; c.id = "g02_constant"; c.paramsJson = "{\"strategy\": \"constant\", \"fill_value\": \"0\"}";   c.label = "strategy=constant + fill_value";   c.expectOk = true;  cases.push_back(c); }
    { StrategyCase c; c.id = "g02_droprows"; c.paramsJson = "{\"strategy\": \"drop_rows\"}";                         c.label = "strategy=drop_rows";               c.expectOk = true;  cases.push_back(c); }
    { StrategyCase c; c.id = "g02_dropcols"; c.paramsJson = "{\"strategy\": \"drop_columns\"}";                      c.label = "strategy=drop_columns";            c.expectOk = true;  cases.push_back(c); }
    { StrategyCase c; c.id = "g02_invalid";  c.paramsJson = "{\"strategy\": \"boyle_bir_strateji_yok\"}";            c.label = "strategy=GECERSIZ (hata beklenir)"; c.expectOk = false; cases.push_back(c); }

    for (size_t i = 0; i < cases.size(); i++) {
        engine.addNode(cases[i].id, "handle_missing_values", JsonValue::parse(cases[i].paramsJson));
        engine.connect(cases[i].id, "data", "g02_load", "output");
    }
    engine.runAll();

    for (size_t i = 0; i < cases.size(); i++) {
        expectState(engine, cases[i].id, cases[i].expectOk, cases[i].label);
    }
}

// ========================================================================
// TEST GRUBU 3: remove_duplicates + handle_outliers
// ========================================================================
static void testDuplicatesAndOutliers(PythonProcess& proc) {
    section("3) remove_duplicates + handle_outliers");
    PipelineEngine engine(proc);
    engine.addNode("g03_load", "load_csv", JsonValue::parse("{\"file_path\": \"" + CSV_PATH + "\"}"));
    engine.runAll();

    engine.addNode("g03_dup_first", "remove_duplicates", JsonValue::parse("{\"keep\": \"first\"}"));
    engine.connect("g03_dup_first", "data", "g03_load", "output");

    engine.addNode("g03_dup_none", "remove_duplicates", JsonValue::makeObject());   // parametresiz -> tum kopyalari sil
    engine.connect("g03_dup_none", "data", "g03_load", "output");

    engine.addNode("g03_outlier_iqr", "handle_outliers",
        JsonValue::parse("{\"method\": \"iqr\", \"action\": \"cap\", \"columns\": [\"" + COL_NUMERIC_A + "\"]}"));
    engine.connect("g03_outlier_iqr", "data", "g03_dup_first", "output");

    engine.addNode("g03_outlier_zscore", "handle_outliers",
        JsonValue::parse("{\"method\": \"zscore\", \"action\": \"remove\", \"columns\": [\"" + COL_NUMERIC_B + "\"], \"zscore_threshold\": 2.5}"));
    engine.connect("g03_outlier_zscore", "data", "g03_dup_first", "output");

    engine.addNode("g03_outlier_badcol", "handle_outliers",
        JsonValue::parse("{\"columns\": [\"" + COL_NOT_EXIST + "\"]}"));   // olmayan kolon -> hata beklenir
    engine.connect("g03_outlier_badcol", "data", "g03_dup_first", "output");

    engine.runAll();

    expectState(engine, "g03_dup_first", true, "remove_duplicates (keep=first)");
    expectState(engine, "g03_dup_none", true, "remove_duplicates (parametresiz)");
    expectState(engine, "g03_outlier_iqr", true, "handle_outliers (iqr + cap)");
    expectState(engine, "g03_outlier_zscore", true, "handle_outliers (zscore + remove)");
    expectState(engine, "g03_outlier_badcol", false, "handle_outliers (olmayan kolon, hata beklenir)");
}

// ========================================================================
// TEST GRUBU 4: drop_columns / rename_columns / convert_dtype
// ========================================================================
static void testColumnOps(PythonProcess& proc) {
    section("4) drop_columns / rename_columns / convert_dtype");
    PipelineEngine engine(proc);
    engine.addNode("g04_load", "load_csv", JsonValue::parse("{\"file_path\": \"" + CSV_PATH + "\"}"));
    engine.runAll();

    engine.addNode("g04_drop", "drop_columns", JsonValue::parse("{\"columns\": [\"" + COL_TARGET_CLS + "\"]}"));
    engine.connect("g04_drop", "data", "g04_load", "output");

    engine.addNode("g04_drop_empty", "drop_columns", JsonValue::makeObject());   // "columns" ZORUNLU ama hic verilmiyor -> hata beklenir
    engine.connect("g04_drop_empty", "data", "g04_load", "output");

    engine.addNode("g04_rename", "rename_columns",
        JsonValue::parse("{\"column\": \"" + COL_NUMERIC_A + "\", \"new_name\": \"" + COL_NUMERIC_A + "_renamed\"}"));
    engine.connect("g04_rename", "data", "g04_load", "output");

    engine.addNode("g04_dtype", "convert_dtype",
        JsonValue::parse("{\"column\": \"" + COL_NUMERIC_A + "\", \"dtype\": \"float64\"}"));
    engine.connect("g04_dtype", "data", "g04_load", "output");

    engine.addNode("g04_dtype_bad", "convert_dtype",
        JsonValue::parse("{\"column\": \"" + COL_NOT_EXIST + "\", \"dtype\": \"float64\"}"));
    engine.connect("g04_dtype_bad", "data", "g04_load", "output");

    engine.runAll();

    expectState(engine, "g04_drop", true, "drop_columns (gecerli kolon)");
    expectState(engine, "g04_drop_empty", false, "drop_columns (ZORUNLU 'columns' hic verilmedi, hata beklenir)");
    expectState(engine, "g04_rename", true, "rename_columns");
    expectState(engine, "g04_dtype", true, "convert_dtype (gecerli kolon)");
    expectState(engine, "g04_dtype_bad", false, "convert_dtype (olmayan kolon, hata beklenir)");
}

// ========================================================================
// TEST GRUBU 5: encode_categorical + scale_features
// ========================================================================
static void testEncodeScale(PythonProcess& proc) {
    section("5) encode_categorical + scale_features");
    PipelineEngine engine(proc);
    engine.addNode("g05_load", "load_csv", JsonValue::parse("{\"file_path\": \"" + CSV_PATH + "\"}"));
    engine.runAll();

    engine.addNode("g05_enc_label", "encode_categorical",
        JsonValue::parse("{\"method\": \"label\", \"columns\": [\"" + COL_CATEGORICAL + "\"]}"));
    engine.connect("g05_enc_label", "data", "g05_load", "output");

    engine.addNode("g05_enc_onehot", "encode_categorical",
        JsonValue::parse("{\"method\": \"onehot\", \"columns\": [\"" + COL_CATEGORICAL + "\"], \"drop_first\": true}"));
    engine.connect("g05_enc_onehot", "data", "g05_load", "output");

    engine.addNode("g05_enc_ordinal", "encode_categorical",
        JsonValue::parse("{\"method\": \"ordinal\", \"columns\": [\"" + COL_CATEGORICAL + "\"]}"));
    engine.connect("g05_enc_ordinal", "data", "g05_load", "output");

    engine.addNode("g05_scale_minmax", "scale_features",
        JsonValue::parse("{\"method\": \"minmax\", \"columns\": [\"" + COL_NUMERIC_A + "\", \"" + COL_NUMERIC_B + "\"]}"));
    engine.connect("g05_scale_minmax", "data", "g05_load", "output");

    engine.addNode("g05_scale_zscore", "scale_features",
        JsonValue::parse("{\"method\": \"zscore\", \"columns\": [\"" + COL_NUMERIC_A + "\"]}"));
    engine.connect("g05_scale_zscore", "data", "g05_load", "output");

    engine.addNode("g05_scale_robust", "scale_features",
        JsonValue::parse("{\"method\": \"robust\", \"columns\": [\"" + COL_NUMERIC_B + "\"]}"));
    engine.connect("g05_scale_robust", "data", "g05_load", "output");

    engine.runAll();

    expectState(engine, "g05_enc_label", true, "encode_categorical (label)");
    expectState(engine, "g05_enc_onehot", true, "encode_categorical (onehot + drop_first)");
    expectState(engine, "g05_enc_ordinal", true, "encode_categorical (ordinal)");
    expectState(engine, "g05_scale_minmax", true, "scale_features (minmax)");
    expectState(engine, "g05_scale_zscore", true, "scale_features (zscore)");
    expectState(engine, "g05_scale_robust", true, "scale_features (robust)");
}

// ========================================================================
// TEST GRUBU 6: train_test_split / train_validation_test_split + coklu cikis slotu
// ========================================================================
static void testSplitsAndMultiOutput(PythonProcess& proc) {
    section("6) split bloklari + coklu cikis slotlarinin dogru ayrisip ayrismadigi");
    PipelineEngine engine(proc);
    engine.addNode("g06_load", "load_csv", JsonValue::parse("{\"file_path\": \"" + CSV_PATH + "\"}"));
    engine.runAll();

    engine.addNode("g06_split2", "train_test_split",
        JsonValue::parse("{\"train_ratio\": 0.8, \"shuffle\": true, \"random_seed\": 42}"));
    engine.connect("g06_split2", "data", "g06_load", "output");

    engine.addNode("g06_split3", "train_validation_test_split",
        JsonValue::parse("{\"train_ratio\": 0.7, \"validation_ratio\": 0.15, \"test_ratio\": 0.15, \"random_seed\": 7}"));
    engine.connect("g06_split3", "data", "g06_load", "output");

    // coklu cikisin FARKLI slotlarindan gercekten besleniyor mu diye iki ayri child:
    engine.addNode("g06_from_train", "data_preview", JsonValue::parse("{\"row_count\": 3}"));
    engine.connect("g06_from_train", "data", "g06_split2", "train");

    engine.addNode("g06_from_test", "data_preview", JsonValue::parse("{\"row_count\": 3}"));
    engine.connect("g06_from_test", "data", "g06_split2", "test");

    engine.runAll();

    expectState(engine, "g06_split2", true, "train_test_split calisiyor mu");
    expectState(engine, "g06_split3", true, "train_validation_test_split calisiyor mu");
    expectState(engine, "g06_from_train", true, "split2:train slotundan besleniyor mu");
    expectState(engine, "g06_from_test", true, "split2:test slotundan besleniyor mu");

    /* Ekstra kontrol: train ve test ciktilarinin SessionStore ref'leri farkli
     * mi? Ayni olsaydi, split blogu ikisine de ayni veriyi vermis demektir -
     * bu ciddi bir bug olurdu, sadece node state'ine bakmak bunu yakalayamaz. */
    const Node* splitNode = engine.getNode("g06_split2");
    bool distinctRefs = false;
    if (splitNode && splitNode->outputs.count("train") && splitNode->outputs.count("test")) {
        std::string trainRef = splitNode->outputs.at("train").ref;
        std::string testRef = splitNode->outputs.at("test").ref;
        distinctRefs = (!trainRef.empty() && !testRef.empty() && trainRef != testRef);
        std::cout << "  (train ref = " << trainRef << ", test ref = " << testRef << ")\n";
    }
    if (distinctRefs) {
        g_stats.passed++;
        std::cout << "  [PASS] train/test farkli SessionStore anahtarlarina sahip\n";
    } else {
        g_stats.failed++;
        g_stats.failMessages.push_back("train/test ref'leri ayni ya da bos!");
        std::cout << "  [FAIL] train/test ref'leri ayni ya da bos!\n";
    }
}

// ========================================================================
// TEST GRUBU 7: tam SINIFLANDIRMA zinciri (test_full_chain.py ile ayni akis)
// ========================================================================
static void testFullClassificationChain(PythonProcess& proc) {
    section("7) tam zincir: tensor + dataloader + mlp_learner + classification metrikleri");
    PipelineEngine engine(proc);

    engine.addNode("g07_1", "load_csv", JsonValue::parse("{\"file_path\": \"" + CSV_PATH + "\"}"));
    engine.addNode("g07_2", "handle_missing_values", JsonValue::parse("{\"strategy\": \"mean\"}"));
    engine.connect("g07_2", "data", "g07_1", "output");
    engine.addNode("g07_3", "encode_categorical", JsonValue::parse("{\"method\": \"label\", \"columns\": [\"" + COL_CATEGORICAL + "\"]}"));
    engine.connect("g07_3", "data", "g07_2", "output");
    engine.addNode("g07_4", "scale_features", JsonValue::parse("{\"method\": \"minmax\", \"columns\": [\"" + COL_NUMERIC_A + "\", \"" + COL_NUMERIC_B + "\"]}"));
    engine.connect("g07_4", "data", "g07_3", "output");
    engine.addNode("g07_5", "train_test_split", JsonValue::parse("{\"train_ratio\": 0.8, \"random_seed\": 1}"));
    engine.connect("g07_5", "data", "g07_4", "output");

    engine.addNode("g07_xtr", "drop_columns", JsonValue::parse("{\"columns\": [\"" + COL_TARGET_CLS + "\"]}"));
    engine.connect("g07_xtr", "data", "g07_5", "train");
    engine.addNode("g07_ytr", "drop_columns", JsonValue::parse("{\"columns\": [\"" + COL_NUMERIC_A + "\", \"" + COL_NUMERIC_B + "\", \"" + COL_CATEGORICAL + "\"]}"));
    engine.connect("g07_ytr", "data", "g07_5", "train");
    engine.addNode("g07_xte", "drop_columns", JsonValue::parse("{\"columns\": [\"" + COL_TARGET_CLS + "\"]}"));
    engine.connect("g07_xte", "data", "g07_5", "test");
    engine.addNode("g07_yte", "drop_columns", JsonValue::parse("{\"columns\": [\"" + COL_NUMERIC_A + "\", \"" + COL_NUMERIC_B + "\", \"" + COL_CATEGORICAL + "\"]}"));
    engine.connect("g07_yte", "data", "g07_5", "test");

    engine.addNode("g07_xtr_t", "to_tensor", JsonValue::parse("{\"dtype\": \"float32\"}"));
    engine.connect("g07_xtr_t", "data", "g07_xtr", "output");
    engine.addNode("g07_ytr_t", "to_tensor", JsonValue::parse("{\"dtype\": \"long\", \"squeeze\": true}"));
    engine.connect("g07_ytr_t", "data", "g07_ytr", "output");
    engine.addNode("g07_xte_t", "to_tensor", JsonValue::parse("{\"dtype\": \"float32\"}"));
    engine.connect("g07_xte_t", "data", "g07_xte", "output");
    engine.addNode("g07_yte_t", "to_tensor", JsonValue::parse("{\"dtype\": \"long\", \"squeeze\": true}"));
    engine.connect("g07_yte_t", "data", "g07_yte", "output");

    engine.addNode("g07_loader", "create_dataloader", JsonValue::parse("{\"batch_size\": 4, \"shuffle\": true}"));
    engine.connect("g07_loader", "X", "g07_xtr_t", "output");
    engine.connect("g07_loader", "y", "g07_ytr_t", "output");

    engine.addNode("g07_mlp", "mlp_learner", JsonValue::parse(
        "{\"task_type\": \"classification\", \"output_size\": 2, \"epochs\": 5, \"learning_rate\": 0.01}"));
    engine.connect("g07_mlp", "train_dataloader", "g07_loader", "output");

    engine.addNode("g07_metrics", "compute_classification_metrics", JsonValue::parse(
        "{\"metrics\": [\"accuracy\", \"precision\", \"recall\", \"f1\", \"confusion_matrix\"], \"average\": \"macro\"}"));
    engine.connect("g07_metrics", "model", "g07_mlp", "output");
    engine.connect("g07_metrics", "X", "g07_xte_t", "output");
    engine.connect("g07_metrics", "y", "g07_yte_t", "output");

    engine.runAll();

    expectState(engine, "g07_5", true, "train_test_split");
    expectState(engine, "g07_xtr_t", true, "X_train tensor");
    expectState(engine, "g07_ytr_t", true, "y_train tensor");
    expectState(engine, "g07_loader", true, "create_dataloader");
    expectState(engine, "g07_mlp", true, "mlp_learner egitimi tamamlaniyor mu");
    expectState(engine, "g07_metrics", true, "compute_classification_metrics hesaplaniyor mu");

    std::cout << "\n  -- gozlem: egitim ozeti --\n";
    showAllOutputs(engine, "g07_mlp");
    std::cout << "\n  -- gozlem: test skorlari --\n";
    showAllOutputs(engine, "g07_metrics");
}

// ========================================================================
// TEST GRUBU 8: tam REGRESYON zinciri
// ========================================================================
static void testRegressionChain(PythonProcess& proc) {
    section("8) tam zincir: mlp_learner(regression) + regression metrikleri");
    PipelineEngine engine(proc);

    /* NOT: burada COL_NUMERIC_B (income) sanki tahmin edilecek hedefmis gibi
     * KULLANILIYOR - anlamli bir model kurmuyoruz, sadece regresyon
     * zincirinin UCTAN UCA (tensor -> dataloader -> mlp -> metrikler)
     * calistigini dogruluyoruz. */
    engine.addNode("g08_1", "load_csv", JsonValue::parse("{\"file_path\": \"" + CSV_PATH + "\"}"));
    engine.addNode("g08_2", "handle_missing_values", JsonValue::parse("{\"strategy\": \"mean\"}"));
    engine.connect("g08_2", "data", "g08_1", "output");
    engine.addNode("g08_3", "encode_categorical", JsonValue::parse("{\"method\": \"label\", \"columns\": [\"" + COL_CATEGORICAL + "\"]}"));
    engine.connect("g08_3", "data", "g08_2", "output");
    engine.addNode("g08_4", "scale_features", JsonValue::parse("{\"method\": \"minmax\", \"columns\": [\"" + COL_NUMERIC_A + "\"]}"));
    engine.connect("g08_4", "data", "g08_3", "output");
    engine.addNode("g08_5", "train_test_split", JsonValue::parse("{\"train_ratio\": 0.8, \"random_seed\": 3}"));
    engine.connect("g08_5", "data", "g08_4", "output");

    engine.addNode("g08_xtr", "drop_columns", JsonValue::parse("{\"columns\": [\"" + COL_NUMERIC_B + "\", \"" + COL_TARGET_CLS + "\"]}"));
    engine.connect("g08_xtr", "data", "g08_5", "train");
    engine.addNode("g08_ytr", "drop_columns", JsonValue::parse("{\"columns\": [\"" + COL_NUMERIC_A + "\", \"" + COL_CATEGORICAL + "\", \"" + COL_TARGET_CLS + "\"]}"));
    engine.connect("g08_ytr", "data", "g08_5", "train");
    engine.addNode("g08_xte", "drop_columns", JsonValue::parse("{\"columns\": [\"" + COL_NUMERIC_B + "\", \"" + COL_TARGET_CLS + "\"]}"));
    engine.connect("g08_xte", "data", "g08_5", "test");
    engine.addNode("g08_yte", "drop_columns", JsonValue::parse("{\"columns\": [\"" + COL_NUMERIC_A + "\", \"" + COL_CATEGORICAL + "\", \"" + COL_TARGET_CLS + "\"]}"));
    engine.connect("g08_yte", "data", "g08_5", "test");

    engine.addNode("g08_xtr_t", "to_tensor", JsonValue::parse("{\"dtype\": \"float32\"}"));
    engine.connect("g08_xtr_t", "data", "g08_xtr", "output");
    engine.addNode("g08_ytr_t", "to_tensor", JsonValue::parse("{\"dtype\": \"float32\", \"squeeze\": true}"));
    engine.connect("g08_ytr_t", "data", "g08_ytr", "output");
    engine.addNode("g08_xte_t", "to_tensor", JsonValue::parse("{\"dtype\": \"float32\"}"));
    engine.connect("g08_xte_t", "data", "g08_xte", "output");
    engine.addNode("g08_yte_t", "to_tensor", JsonValue::parse("{\"dtype\": \"float32\", \"squeeze\": true}"));
    engine.connect("g08_yte_t", "data", "g08_yte", "output");

    engine.addNode("g08_loader", "create_dataloader", JsonValue::parse("{\"batch_size\": 4, \"shuffle\": true}"));
    engine.connect("g08_loader", "X", "g08_xtr_t", "output");
    engine.connect("g08_loader", "y", "g08_ytr_t", "output");

    engine.addNode("g08_mlp", "mlp_learner", JsonValue::parse(
        "{\"task_type\": \"regression\", \"epochs\": 5, \"learning_rate\": 0.01}"));
    engine.connect("g08_mlp", "train_dataloader", "g08_loader", "output");

    engine.addNode("g08_metrics", "compute_regression_metrics", JsonValue::parse(
        "{\"metrics\": [\"mse\", \"rmse\", \"mae\", \"r2\"]}"));
    engine.connect("g08_metrics", "model", "g08_mlp", "output");
    engine.connect("g08_metrics", "X", "g08_xte_t", "output");
    engine.connect("g08_metrics", "y", "g08_yte_t", "output");

    engine.runAll();

    expectState(engine, "g08_mlp", true, "mlp_learner (regression) egitimi");
    expectState(engine, "g08_metrics", true, "compute_regression_metrics hesaplaniyor mu");

    std::cout << "\n  -- gozlem: regresyon skorlari --\n";
    showAllOutputs(engine, "g08_metrics");
}

// ========================================================================
// TEST GRUBU 9: grafik (chart) bloklari
// ========================================================================
static void testChartBlocks(PythonProcess& proc) {
    section("9) grafik bloklari (chart)");
    PipelineEngine engine(proc);
    engine.addNode("g09_load", "load_csv", JsonValue::parse("{\"file_path\": \"" + CSV_PATH + "\"}"));
    engine.runAll();

    engine.addNode("g09_hist", "plot_histogram", JsonValue::parse("{\"column\": \"" + COL_NUMERIC_A + "\", \"bins\": 10}"));
    engine.connect("g09_hist", "data", "g09_load", "output");

    engine.addNode("g09_bar", "plot_bar_chart", JsonValue::parse("{\"column\": \"" + COL_CATEGORICAL + "\", \"top_n\": 5}"));
    engine.connect("g09_bar", "data", "g09_load", "output");

    engine.addNode("g09_box", "plot_boxplot", JsonValue::parse("{\"column\": \"" + COL_NUMERIC_A + "\"}"));
    engine.connect("g09_box", "data", "g09_load", "output");

    engine.addNode("g09_scatter", "plot_scatter", JsonValue::parse(
        "{\"x_column\": \"" + COL_NUMERIC_A + "\", \"y_column\": \"" + COL_NUMERIC_B + "\"}"));
    engine.connect("g09_scatter", "data", "g09_load", "output");

    engine.addNode("g09_heatmap", "plot_correlation_heatmap", JsonValue::makeObject());
    engine.connect("g09_heatmap", "data", "g09_load", "output");

    engine.addNode("g09_missing", "plot_missing_values", JsonValue::makeObject());
    engine.connect("g09_missing", "data", "g09_load", "output");

    engine.runAll();

    expectState(engine, "g09_hist", true, "plot_histogram");
    expectState(engine, "g09_bar", true, "plot_bar_chart");
    expectState(engine, "g09_box", true, "plot_boxplot");
    expectState(engine, "g09_scatter", true, "plot_scatter");
    expectState(engine, "g09_heatmap", true, "plot_correlation_heatmap");
    expectState(engine, "g09_missing", true, "plot_missing_values");
}

// ========================================================================
// TEST GRUBU 10: hata durumlari (bilinmeyen blok, C++ tarafi validasyonlari)
// ========================================================================
static void testErrorCases(PythonProcess& proc) {
    section("10) hata durumlari");
    PipelineEngine engine(proc);
    engine.addNode("g10_load", "load_csv", JsonValue::parse("{\"file_path\": \"" + CSV_PATH + "\"}"));
    engine.runAll();

    // 10a) Python'un TANIMADIGI bir blok adi -> Python status:error donmeli
    engine.addNode("g10_unknown_block", "boyle_bir_blok_yok", JsonValue::makeObject());
    engine.connect("g10_unknown_block", "data", "g10_load", "output");
    engine.runAll();
    expectState(engine, "g10_unknown_block", false, "bilinmeyen blok adi -> Python hata donmeli");

    /* 10b) C++ TARAFININ KENDI validasyonu: olmayan bir node'a connect etmeye
     * calismak, Python'a HIC gitmeden, C++ icinde exception firlatir. Bu
     * yuzden burada expectState (node state kontrolu) degil, try/catch
     * kullaniyoruz - cunku hata bir node state'i olarak degil, dogrudan bir
     * C++ istisnasi (exception) olarak ortaya cikiyor.
     *
     * try/catch SOZDIZIMI: try {} icindeki kod calisirken bir exception
     * firlatilirsa (throw ...), calisma HEMEN try blogundan cikar ve
     * catch (const std::exception& e) {} blogu calisir; e.what() firlatilan
     * hatanin aciklama metnini verir. */
    bool threwAsExpected = false;
    try {
        engine.connect("g10_load", "data", "olmayan_node_id", "output");
    } catch (const std::exception& e) {
        threwAsExpected = true;
        std::cout << "  (beklenen exception yakalandi: " << e.what() << ")\n";
    }
    if (threwAsExpected) {
        g_stats.passed++;
        std::cout << "  [PASS] olmayan node'a connect -> C++ exception firlatiyor\n";
    } else {
        g_stats.failed++;
        g_stats.failMessages.push_back("olmayan node'a connect exception firlatmadi!");
        std::cout << "  [FAIL] olmayan node'a connect exception firlatmadi!\n";
    }

    // 10c) Ayni id ile iki kere addNode -> C++ exception beklenir
    threwAsExpected = false;
    try {
        engine.addNode("g10_load", "load_csv", JsonValue::makeObject());   // "g10_load" zaten var
    } catch (const std::exception& e) {
        threwAsExpected = true;
        std::cout << "  (beklenen exception yakalandi: " << e.what() << ")\n";
    }
    if (threwAsExpected) {
        g_stats.passed++;
        std::cout << "  [PASS] ayni id ile addNode -> C++ exception firlatiyor\n";
    } else {
        g_stats.failed++;
        g_stats.failMessages.push_back("ayni id ile addNode exception firlatmadi!");
        std::cout << "  [FAIL] ayni id ile addNode exception firlatmadi!\n";
    }
}

// ========================================================================
// TEST GRUBU 11: dirty propagation + tekrar calistirma (skip) + removeNode
// ========================================================================
static void testDirtyPropagationAndRemove(PythonProcess& proc) {
    section("11) dirty propagation, tekrar calistirma, removeNode");
    PipelineEngine engine(proc);

    engine.addNode("g11_1", "load_csv", JsonValue::parse("{\"file_path\": \"" + CSV_PATH + "\"}"));
    engine.addNode("g11_2", "handle_missing_values", JsonValue::parse("{\"strategy\": \"mean\"}"));
    engine.connect("g11_2", "data", "g11_1", "output");
    engine.addNode("g11_3", "data_preview", JsonValue::parse("{\"row_count\": 3}"));
    engine.connect("g11_3", "data", "g11_2", "output");

    std::cout << "\n  -- ilk calistirma --\n";
    engine.runAll();
    expectState(engine, "g11_1", true, "ilk calistirma: load_csv");
    expectState(engine, "g11_2", true, "ilk calistirma: handle_missing_values");
    expectState(engine, "g11_3", true, "ilk calistirma: data_preview");

    std::cout << "\n  -- degisiklik olmadan TEKRAR calistirma (asagida SADECE 'atlandi' satirlari olmali, 'calistiriliyor' OLMAMALI) --\n";
    engine.runAll();   // hepsi UP_TO_DATE oldugu icin Python'a mesaj gitmemeli

    std::cout << "\n  -- g11_2'nin stratejisini degistiriyoruz (mean -> median) --\n";
    engine.setParams("g11_2", JsonValue::parse("{\"strategy\": \"median\"}"));

    const Node* n1 = engine.getNode("g11_1");
    const Node* n2 = engine.getNode("g11_2");
    const Node* n3 = engine.getNode("g11_3");
    bool dirtyOk = (n1 && n1->state == NodeState::UP_TO_DATE)     // g11_1 ETKILENMEMELI
                && (n2 && n2->state == NodeState::DIRTY)             // kendisi DIRTY
                && (n3 && n3->state == NodeState::DIRTY);             // asagi akis da DIRTY
    if (dirtyOk) {
        g_stats.passed++;
        std::cout << "  [PASS] dirty propagation: g11_1 UP_TO_DATE kaldi, g11_2/g11_3 DIRTY oldu\n";
    } else {
        g_stats.failed++;
        g_stats.failMessages.push_back("dirty propagation beklendigi gibi calismadi!");
        std::cout << "  [FAIL] dirty propagation beklendigi gibi calismadi!\n";
    }

    std::cout << "\n  -- sadece DIRTY olanlar (g11_2, g11_3) yeniden calisir --\n";
    engine.runAll();
    expectState(engine, "g11_2", true, "median stratejisiyle yeniden calisma");
    expectState(engine, "g11_3", true, "downstream data_preview yeniden calisma");

    std::cout << "\n  -- g11_3'u siliyoruz --\n";
    engine.removeNode("g11_3");
    bool removedOk = (engine.getNode("g11_3") == NULL);
    if (removedOk) {
        g_stats.passed++;
        std::cout << "  [PASS] removeNode sonrasi node artik grafta yok\n";
    } else {
        g_stats.failed++;
        g_stats.failMessages.push_back("removeNode sonrasi node hala grafta!");
        std::cout << "  [FAIL] removeNode sonrasi node hala grafta!\n";
    }
}

// ========================================================================
// main: tum test gruplarini SIRAYLA calistirir, en sonda ozet basar.
// ========================================================================
int main() {
    PythonProcess proc;
    std::string error;

    std::cout << "Python dispatcher.py baslatiliyor...\n";
    if (!proc.start("python", "dispatcher.py", error)) {
        std::cerr << "BASLATMA HATASI: " << error << "\n";
        return 1;
    }

    testLoadAndBasicViews(proc);
    testMissingValueStrategies(proc);
    testDuplicatesAndOutliers(proc);
    testColumnOps(proc);
    testEncodeScale(proc);
    testSplitsAndMultiOutput(proc);
    testFullClassificationChain(proc);
    testRegressionChain(proc);
    testChartBlocks(proc);
    testErrorCases(proc);
    testDirtyPropagationAndRemove(proc);

    proc.stop();

    std::cout << "\n\n============================================================\n";
    std::cout << "TEST OZETI\n";
    std::cout << "============================================================\n";
    std::cout << "Basarili: " << g_stats.passed << "\n";
    std::cout << "Basarisiz: " << g_stats.failed << "\n";
    if (!g_stats.failMessages.empty()) {
        std::cout << "\nBasarisiz testlerin detayi:\n";
        for (size_t i = 0; i < g_stats.failMessages.size(); i++) {
            std::cout << "  - " << g_stats.failMessages[i] << "\n";
        }
    }
    std::cout << "\nSONUC: " << (g_stats.failed == 0 ? "TUM TESTLER GECTI" : "BAZI TESTLER BASARISIZ") << "\n";

    return g_stats.failed == 0 ? 0 : 1;
}
