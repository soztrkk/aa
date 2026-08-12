/*
 * gui_app.h
 *
 * GUI'nin "controller" katmani: ImNodes/ImGui'nin GORSEL state'ini (node
 * pozisyonlari, pin/link id'leri) tutar ve kullanici etkilesimlerini
 * (yeni node ekle, baglanti kur/sok, parametre degistir, calistir, sil)
 * dogrudan GERCEK PipelineEngine cagrilarina cevirir. Yeni bir protokol
 * yok - burasi interactive_main.cpp'nin gorsel karsiligidir, ayni
 * addNode/connect/disconnect/setParams/removeNode/runAll API'sini kullanir.
 *
 * ImNodes/ImGui'nin ne oldugunu bilmiyorsan kisaca: ImGui "immediate mode"
 * bir arayuz kutuphanesidir - her karede (frame) TUM arayuzu yeniden
 * "cizersin" (ImGui::Button(...) gibi cagrilarla), kutuphane hangi widget'in
 * tiklandigini/surüklendigini o an sana geri bildirir. Kalici bir "widget
 * nesnesi" tutmazsin - GuiApp'in tuttugu tek kalici state, PipelineEngine'in
 * DISINDA, sadece "hangi node ekranda nerede duruyor" gibi gorsel bilgidir.
 *
 * ---------------------------------------------------------------------
 * THREAD MODELI (v1): "Calistir" butonuna basinca, `engine_.runAll(...)`
 * AYRI bir thread'de (runThread_) calisir - cunku mlp_learner gibi bloklar
 * uzun surebilir, UI thread'ini (ImGui cizim/pencere) bloklamak istemiyoruz.
 * Bu iki thread'in guvenli calismasi TEK BIR BASIT KURALA dayanir:
 *
 *   isRunning_ (atomic bool) true iken, UI thread `engine_`'in (ya da
 *   PythonProcess'in) HICBIR metodunu cagirmaz - o sure boyunca engine_'e
 *   SADECE runThread_ dokunur. Bu "sahiplik devri" (ownership hand-off)
 *   deseni, PipelineEngine/PythonProcess'e mutex eklemeye GEREK BIRAKMIYOR.
 *
 * Bunu saglamak icin: (1) UI thread, engine_'in guncel durumunu HER KAREDE
 * dogrudan okumak yerine kendi `displayCache_`'inden okur (cache SADECE
 * !isRunning_ iken engine_'den tazelenir); (2) grafik duzenleyen/Python'a
 * istek atan TUM metodlar (createNode, deleteNode, applyParams, link
 * ekleme/silme, onizleme/export) basinda `if (isRunning_) return;` yapar.
 * Iki thread arasinda GERCEKTEN paylasilan tek veri `liveProgress_` ve
 * `workerErrorMessage_` - onlar da kucuk bir mutex (progressMutex_) ile
 * korunuyor.
 * ---------------------------------------------------------------------
 */

#ifndef GUI_APP_H
#define GUI_APP_H

#include <string>
#include <map>
#include <vector>
#include <thread>
#include <atomic>
#include <mutex>
#include "pipeline_engine.h"
#include "block_specs.h"

class GuiApp {
public:
    explicit GuiApp(PipelineEngine& engine);

    /* Egitim SURERKEN pencere kapatilirsa, hala calisan runThread_'i
     * guvenle sonlandirmak (join etmek) icin gerekli - bkz. .cpp'deki
     * aciklama (v1'de iptal mekanizmasi yok, sadece bitmesini bekliyoruz). */
    ~GuiApp();

    /* Her karede (frame) BIR KEZ cagrilir: sol blok paleti, orta ImNodes
     * tuvali ve sag "secili node" panelini cizer, kullanici etkilesimlerini
     * isler. main.cpp'deki render dongusunun icinden cagrilir. */
    void render();

private:
    PipelineEngine& engine_;

    /* Her yeni node/pin/link'e TEK BIR global sayacdan artan id verilir -
     * ImNodes ayni id uzaninda (namespace) node/pin/link karismasin diye
     * bunlarin hepsinin BENZERSIZ olmasini ister; ayri ayri sayaç tutup
     * yanlislikla ayni id'yi iki farkli seye vermektense tek sayaç en
     * basit/guvenli cozum. */
    int nextId_;
    int allocId() { return ++nextId_; }

    /* PipelineEngine node id'lerini (orn. "load_csv_3") uretmek icin AYRI
     * bir sayaç - allocId()'den bagimsiz, sadece isim carpismasin diye. */
    int nodeNameCounter_;

    /* PipelineEngine'in node id'si (orn. "load_csv_3") <-> ImNodes'un
     * bekledigi int id arasinda iki yonlu cevrim. */
    std::map<std::string, int> nodeStrToInt_;
    std::map<int, std::string> nodeIntToStr_;

    struct PinInfo {
        std::string nodeId;
        std::string slot;
        bool isInput;
    };
    std::map<int, PinInfo> pinInfo_;   // pin int id -> hangi node/slot/yon

    struct NodeUiState {
        std::string block;
        std::map<std::string, int> inputPinId;    // slot adi -> pin id
        std::map<std::string, int> outputPinId;   // slot adi -> pin id
    };
    std::map<std::string, NodeUiState> uiState_;   // node id -> gorsel bilgisi

    struct LinkInfo {
        std::string toNode, toSlot, fromNode, fromSlot;
    };
    std::map<int, LinkInfo> linkInfo_;                    // link int id -> uc noktalari
    std::map<std::string, int> linkIdForInputSlot_;        // "toNode:toSlot" -> o slotu besleyen linkin id'si (bir input'ta en fazla bir baglanti olabilir)

    /* Yeni node'lari ekrana kademeli (cascade) yerlestirmek icin basit
     * bir "bir sonraki konum" takipçisi. */
    float nextPosX_;
    float nextPosY_;

    std::string selectedNodeId_;   // sag panelde parametre/cikti gosterilecek node

    /* Parametre formu icin, kullanicinin YAZDIGI ham metni node+parametre
     * anahtarina gore saklar (ImGui::InputText her karede AYNI arabellege
     * (buffer) ihtiyac duyar, bu yuzden bunu tek seferlik yerel degisken
     * olarak tutamayiz - kareler arasinda KALICI olmasi lazim). */
    std::map<std::string, std::map<std::string, std::string> > paramTextBuffers_;

    std::string lastErrorMessage_;   // en son basarisiz engine_ cagrisinin mesaji (durum cubugunda gosterilir) - SADECE UI thread dokunur

    /* --- calistirma durumu (UI thread + runThread_ arasinda paylasilir) --- */
    std::thread runThread_;
    std::atomic<bool> isRunning_;

    /* NEDEN isRunning_'in true->false GECISINI kare-kareye KARSILASTIRARAK
     * yakalamiyoruz: load_csv gibi hizli bloklar TEK BIR karenin icinde
     * baslayip bitebiliyor - bu durumda bir sonraki karenin basinda
     * isRunning_ zaten false OKUNUYOR, "az once calisti" bilgisi kayboluyor
     * (once boyle bir "wasRunning_" karsilastirmasi denenmisti, TAM DA bu
     * yuzden calismiyordu). Bunun yerine runThread_'in KENDISI, isini
     * bitirince bu bayragi true yapar; UI thread bunu HANGI karede
     * okursa okusun (bir kare sonra da olsa, on kare sonra da olsa) fark
     * etmez - bayrak worker bitene kadar KALICI olarak true kalir. */
    std::atomic<bool> previewRefreshPending_;

    std::mutex progressMutex_;                          // liveProgress_, liveProgressLog_ VE workerErrorMessage_'i korur
    std::map<std::string, JsonValue> liveProgress_;      // nodeId -> en son "progress" mesaji (orn. {"epoch":5,"epochs":30,"loss":0.2}) - node kutusundaki TEK SATIR gosterim icin
    std::map<std::string, std::vector<JsonValue> > liveProgressLog_;   // nodeId -> gelen TUM "progress" mesajlarinin sirali listesi - sag paneldeki CANLI log gorunumu icin (bkz. drawInspector)
    std::string workerErrorMessage_;                      // runThread_'de yakalanan istisna (varsa) - her karede lastErrorMessage_'e tasinir

    /* --- engine_'in "dondurulmus" gorunumu: cizim kodu SADECE buradan okur ---
     * (bkz. .h basindaki THREAD MODELI aciklamasi) */
    struct DisplayNodeInfo {
        std::string block;
        NodeState state;
        std::string lastError;
        std::map<std::string, OutputSlot> outputs;
    };
    std::map<std::string, DisplayNodeInfo> displayCache_;
    void refreshDisplayCache();   // SADECE !isRunning_ iken cagrilir: engine_'deki guncel durumu displayCache_'e kopyalar

    /* --- DataFrame onizleme cache'i (Ozellik A) --- */
    std::map<std::string, JsonValue> previewCache_;   // "nodeId:slot" -> get_preview meta'si
    std::string lastExportMessage_;                     // en son CSV export sonucunun kisa aciklamasi
    void refreshPreviewsForUpToDateNodes();             // producesDataFrame olan, UP_TO_DATE her node/slot icin onizlemeyi yeniler

    /* --- yardimci metodlar --- */
    void createNode(const std::string& blockName);          // paletten bir blok secilince yeni node olusturur
    void deleteNode(const std::string& nodeId);              // node'u hem engine'den hem gorsel state'ten temizler
    void applyParamsForSelectedNode();                        // parametre formundaki degerleri JSON'a cevirip engine_.setParams cagirir
    JsonValue buildParamsJson(const BlockSpec& spec, const std::string& nodeId);   // form metinlerini turune gore parse eder
    void startRun();                                             // "Calistir" butonu: runThread_'i baslatir (bkz. THREAD MODELI)

    void drawPalette();     // sol panel: blok listesi
    void drawCanvas();      // orta panel: ImNodes node-editor
    void drawInspector();   // sag panel: secili node'un parametreleri + ciktisi
    void drawNodeContents(const std::string& nodeId, const DisplayNodeInfo& node);   // tek bir node kutusunun ICERIGINI cizer (baslik/pinler/durum rengi/canli ilerleme)

    void handleNewLinks();       // ImNodes::IsLinkCreated() olayini engine_.connect()'e cevirir
    void handleDestroyedLinks(); // ImNodes::IsLinkDestroyed() olayini engine_.disconnect()'e cevirir
    void handleNodeDeletion();   // Delete tusuna basilinca secili node'lari siler
};

#endif // GUI_APP_H
