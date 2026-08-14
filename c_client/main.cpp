/*
 * main.cpp
 *
 * ADIM 5: yukaridaki tum parcalari (JsonValue, PythonProcess, PipelineEngine,
 * result_display) bir araya getiren, ELLE CALISTIRIP GOZLEMLEYEBILECEGIN
 * iki senaryo.
 *
 * NASIL CALISTIRILIR: bkz. c_client/README_ADIM_ADIM.md
 * (ozetle: proje KOK dizininden  c_client\pipeline_client.exe  calistir,
 * cunku dispatcher.py ve test_data_full.csv oraya gore aranir).
 *
 * SENARYO 1 (demoPreprocessingAndViews): CLAUDE.md'deki "C tarafinin
 * sorumluluklari" bolumunu somut olarak gosterir:
 *   - node graph kurma (addNode/connect)
 *   - ilk calistirma (hepsi NOT_RUN -> UP_TO_DATE)
 *   - AYNI pipeline'i tekrar calistirma (hicbir Python mesaji gitmiyor,
 *     hepsi zaten UP_TO_DATE - "secici calistirma")
 *   - bir node'un parametresini degistirme -> dirty propagation
 *     (o node + ona bagli TUM alt node'lar DIRTY olur, sadece onlar
 *     yeniden calisir)
 *   - bir node silme (removeNode) -> Python'daki SessionStore'dan da silinir
 *   - view bloklarinin (data_preview/dataset_summary/missing_values_report)
 *     ciktisi: HER ZAMAN sinirli/ozet veri, asla ham DataFrame
 *
 * SENARYO 2 (demoFullMlChain): test_full_chain.py'deki ile AYNI blok
 * zincirini (yukleme -> temizlik -> encode/scale -> split -> tensor ->
 * MLP egitimi -> test metrikleri) PipelineEngine uzerinden kurar ve
 * calistirir. compute_classification_metrics'in ciktisinda "output_type"
 * alani YOK - result_display bu durumda otomatik olarak "genel gorunum"e
 * duser ve accuracy/precision/recall/f1 gibi SKOR degerlerini asagi yukari
 * "anahtar: deger" seklinde basar. Boylece "score varsa score gorunecek"
 * kurali, ozel bir kod yazmadan, meta'nin sekline gore kendiliginden saglanir.
 */

#include <iostream>            // std::cout/std::cerr
#include "json_value.h"          // JsonValue::parse ile duz metin JSON parametreleri kurmak icin
#include "python_process.h"       // dispatcher.py process'ini baslatmak icin
#include "pipeline_engine.h"        // PipelineEngine, Node, NodeState
#include "result_display.h"          // renderNodeOutput

/* Bir node'un TUM cikis slotlarini (genelde tek slot: "output", ama
 * train_test_split gibi bloklarda birden fazla) ekrana basar. */
static void showAllOutputs(const PipelineEngine& engine, const std::string& nodeId) {
    const Node* node = engine.getNode(nodeId);   // node'u id ile bul
    if (!node) {                                    // graf icinde yoksa
        std::cout << "  [" << nodeId << "] bulunamadi\n";
        return;
    }
    if (node->state == NodeState::ERROR_STATE) {   // son calismasi hata ile bittiyse
        std::cout << "  [" << nodeId << "] HATA: " << node->lastError << "\n";
        return;
    }
    for (std::map<std::string, OutputSlot>::const_iterator it = node->outputs.begin();
         it != node->outputs.end(); ++it) {          // basarili calismis tum cikis slotlari icin
        renderNodeOutput(nodeId, it->first, it->second.meta);   // her birini bicimli sekilde bas
    }
}

static void section(const std::string& title) {
    std::cout << "\n============================================================\n"
              << title << "\n"
              << "============================================================\n";   // gorsel olarak ayirt edici bir baslik bloğu bas
}

static void demoPreprocessingAndViews(PythonProcess& proc) {
    section("SENARYO 1: on-isleme + view bloklari + dirty propagation");

    PipelineEngine engine(proc);   // bu senaryoya ozel yeni/bos bir pipeline

    engine.addNode("node_1", "load_csv", JsonValue::parse("{\"file_path\": \"test_data_full.csv\"}"));   // zincirin basi: CSV yukle

    engine.addNode("node_2", "handle_missing_values", JsonValue::parse("{\"strategy\": \"mean\"}"));   // eksik degerleri ortalamayla doldur
    engine.connect("node_2", "data", "node_1", "output");   // node_2'nin girdisi node_1'in ciktisi

    engine.addNode("node_3", "remove_duplicates", JsonValue::parse("{\"keep\": \"first\"}"));   // tekrar eden satirlarda ilkini tut
    engine.connect("node_3", "data", "node_2", "output");

    engine.addNode("node_4", "data_preview", JsonValue::parse("{\"row_count\": 5, \"preview_type\": \"head\"}"));   // ilk 5 satiri gosteren view blogu
    engine.connect("node_4", "data", "node_3", "output");

    engine.addNode("node_5", "dataset_summary", JsonValue::makeObject());   // parametresiz, bos object gonderilir
    engine.connect("node_5", "data", "node_3", "output");

    std::cout << "\n--- ilk calistirma (hepsi NOT_RUN) ---\n";
    engine.runAll();          // henuz hicbiri calismadigi icin hepsi Python'a gonderilir
    engine.printStatus();     // hepsinin UP_TO_DATE oldugunu goster

    std::cout << "\n--- node_4 ve node_5 ciktilari (view bloklari - SINIRLI/OZET veri) ---\n";
    showAllOutputs(engine, "node_4");
    showAllOutputs(engine, "node_5");

    std::cout << "\n--- AYNI pipeline'i tekrar calistirma (hepsi UP_TO_DATE, Python'a HICBIR mesaj gitmemeli) ---\n";
    engine.runAll();   // "secici calistirma": burada hicbir node yeniden calismaz

    std::cout << "\n--- node_2'nin stratejisini degistiriyoruz (mean -> median): dirty propagation testi ---\n";
    engine.setParams("node_2", JsonValue::parse("{\"strategy\": \"median\"}"));   // parametre degisikligi -> dirty propagation tetiklenir
    engine.printStatus();   // node_1 UP_TO_DATE kalmali, node_2..5 DIRTY olmali

    std::cout << "\n--- sadece DIRTY olanlar (node_2,3,4,5) yeniden calisir, node_1 ATLANIR ---\n";
    engine.runAll();
    engine.printStatus();

    std::cout << "\n--- node_5'i (dataset_summary) siliyoruz ---\n";
    engine.removeNode("node_5");   // hem C++ grafindan hem Python SessionStore'dan silinir
    engine.printStatus();

    std::cout << "\n--- node_4'un guncel ciktisi (median stratejisiyle) ---\n";
    showAllOutputs(engine, "node_4");
}

static void demoFullMlChain(PythonProcess& proc) {
    section("SENARYO 2: tam ML zinciri (test_full_chain.py ile ayni akis) + skor gosterimi");

    PipelineEngine engine(proc);   // bu senaryoya ozel yeni/bos bir pipeline

    engine.addNode("n1", "load_csv", JsonValue::parse("{\"file_path\": \"test_data_full.csv\"}"));   // zincirin basi: CSV yukle

    engine.addNode("n2", "handle_missing_values", JsonValue::parse("{\"strategy\": \"mean\"}"));   // eksik degerleri ortalamayla doldur
    engine.connect("n2", "data", "n1", "output");

    engine.addNode("n3", "remove_duplicates", JsonValue::parse("{\"keep\": \"first\"}"));   // tekrarlari temizle
    engine.connect("n3", "data", "n2", "output");

    engine.addNode("n4", "handle_outliers",
                   JsonValue::parse("{\"method\": \"iqr\", \"action\": \"cap\", \"columns\": [\"age\"]}"));   // "age" kolonundaki aykirilari sinirla (cap)
    engine.connect("n4", "data", "n3", "output");

    engine.addNode("n5", "encode_categorical",
                   JsonValue::parse("{\"method\": \"label\", \"columns\": [\"city\"]}"));   // "city" kategorik kolonunu sayisallastir
    engine.connect("n5", "data", "n4", "output");

    engine.addNode("n6", "scale_features",
                   JsonValue::parse("{\"method\": \"minmax\", \"columns\": [\"age\", \"income\"]}"));   // sayisal kolonlari 0-1 araligina olcekle
    engine.connect("n6", "data", "n5", "output");

    engine.addNode("n7", "train_test_split",
                   JsonValue::parse("{\"train_ratio\": 0.8, \"shuffle\": true, \"random_seed\": 42}"));   // %80 train / %20 test olarak bol
    engine.connect("n7", "data", "n6", "output");

    engine.addNode("n8", "drop_columns", JsonValue::parse("{\"columns\": [\"target\"]}"));   // train setinden hedef kolonu at -> X_train kalir
    engine.connect("n8", "data", "n7", "train");   // X_train

    engine.addNode("n9", "drop_columns", JsonValue::parse("{\"columns\": [\"age\", \"income\", \"city\"]}"));   // train setinden ozellikleri at -> sadece target (y) kalir
    engine.connect("n9", "data", "n7", "train");   // y_train

    engine.addNode("n10", "drop_columns", JsonValue::parse("{\"columns\": [\"target\"]}"));   // test setinden hedef kolonu at -> X_test kalir
    engine.connect("n10", "data", "n7", "test");   // X_test

    engine.addNode("n11", "drop_columns", JsonValue::parse("{\"columns\": [\"age\", \"income\", \"city\"]}"));   // test setinden ozellikleri at -> y_test kalir
    engine.connect("n11", "data", "n7", "test");   // y_test

    engine.addNode("n12", "to_tensor", JsonValue::parse("{\"dtype\": \"float32\", \"squeeze\": false}"));   // X_train'i tensore cevir
    engine.connect("n12", "data", "n8", "output");   // X_train tensor

    engine.addNode("n13", "to_tensor", JsonValue::parse("{\"dtype\": \"long\", \"squeeze\": true}"));   // y_train'i tensore cevir (siniflandirma etiketi -> long)
    engine.connect("n13", "data", "n9", "output");   // y_train tensor

    engine.addNode("n14", "to_tensor", JsonValue::parse("{\"dtype\": \"float32\", \"squeeze\": false}"));   // X_test'i tensore cevir
    engine.connect("n14", "data", "n10", "output");   // X_test tensor

    engine.addNode("n15", "to_tensor", JsonValue::parse("{\"dtype\": \"long\", \"squeeze\": true}"));   // y_test'i tensore cevir
    engine.connect("n15", "data", "n11", "output");   // y_test tensor

    engine.addNode("n16", "create_dataloader", JsonValue::parse("{\"batch_size\": 4, \"shuffle\": true}"));   // X_train+y_train'den egitim DataLoader'i olustur
    engine.connect("n16", "X", "n12", "output");   // X girdisi
    engine.connect("n16", "y", "n13", "output");   // y girdisi

    engine.addNode("n17", "mlp_learner", JsonValue::parse(
        "{\"task_type\": \"classification\", \"output_size\": 2, "
        "\"layer_config\": [{\"type\": \"linear\", \"size\": 16, \"activation\": \"relu\"}], "
        "\"epochs\": 30, \"learning_rate\": 0.01}"));   // 2 sinifli MLP'yi 30 epoch egit
    engine.connect("n17", "train_dataloader", "n16", "output");

    engine.addNode("n18", "compute_classification_metrics", JsonValue::parse(
        "{\"metrics\": [\"accuracy\", \"precision\", \"recall\", \"f1\", \"confusion_matrix\"], "
        "\"average\": \"macro\"}"));   // egitilmis modeli test setiyle degerlendir
    engine.connect("n18", "model", "n17", "output");   // egitilmis model
    engine.connect("n18", "X", "n14", "output");         // test ozellikleri
    engine.connect("n18", "y", "n15", "output");         // test etiketleri

    std::cout << "\n--- tum zincir calistiriliyor (18 node) ---\n";
    engine.runAll();          // hepsi NOT_RUN oldugu icin dogru sirayla (upstream once) tumu calisir
    engine.printStatus();

    std::cout << "\n--- egitim ozeti (n17: mlp_learner) ---\n";
    showAllOutputs(engine, "n17");

    std::cout << "\n--- TEST SETI SKORLARI (n18: compute_classification_metrics) ---\n";
    showAllOutputs(engine, "n18");
}

int main() {
    PythonProcess proc;   // dispatcher.py'yi arka planda calistiracak nesne
    std::string error;      // baslatma basarisiz olursa aciklama buraya yazilir

    std::cout << "Python dispatcher.py baslatiliyor...\n";
    if (!proc.start("python", "dispatcher.py", error)) {   // process'i baslat
        std::cerr << "BASLATMA HATASI: " << error << "\n";
        return 1;   // baslatilamadiysa devam etmenin anlami yok
    }

    //demoPreprocessingAndViews(proc);   // (su an kapali) senaryo 1: on-isleme + view bloklari + dirty propagation
    demoFullMlChain(proc);                  // senaryo 2: uctan uca ML zinciri calistirilir
    

    proc.stop();   // Python process'ini duzgunce kapat
    std::cout << "\nTum senaryolar tamamlandi, Python process kapatildi.\n";
    return 0;
}
