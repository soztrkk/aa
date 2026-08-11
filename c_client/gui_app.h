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
 */

#ifndef GUI_APP_H
#define GUI_APP_H

#include <string>
#include <map>
#include <vector>
#include "pipeline_engine.h"
#include "block_specs.h"

class GuiApp {
public:
    explicit GuiApp(PipelineEngine& engine);

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

    std::string lastErrorMessage_;   // en son basarisiz engine_ cagrisinin mesaji (durum cubugunda gosterilir)

    /* --- yardimci metodlar --- */
    void createNode(const std::string& blockName);          // paletten bir blok secilince yeni node olusturur
    void deleteNode(const std::string& nodeId);              // node'u hem engine'den hem gorsel state'ten temizler
    void applyParamsForSelectedNode();                        // parametre formundaki degerleri JSON'a cevirip engine_.setParams cagirir
    JsonValue buildParamsJson(const BlockSpec& spec, const std::string& nodeId);   // form metinlerini turune gore parse eder

    void drawPalette();     // sol panel: blok listesi
    void drawCanvas();      // orta panel: ImNodes node-editor
    void drawInspector();   // sag panel: secili node'un parametreleri + ciktisi
    void drawNodeContents(const std::string& nodeId, const Node& node);   // tek bir node kutusunun ICERIGINI cizer (baslik/pinler/durum rengi)

    void handleNewLinks();       // ImNodes::IsLinkCreated() olayini engine_.connect()'e cevirir
    void handleDestroyedLinks(); // ImNodes::IsLinkDestroyed() olayini engine_.disconnect()'e cevirir
    void handleNodeDeletion();   // Delete tusuna basilinca secili node'lari siler
};

#endif // GUI_APP_H
