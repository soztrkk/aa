/*
 * pipeline_engine.h
 *
 * ADIM 3: "C tarafinin sorumluluklari" (bkz. CLAUDE.md) burada hayata
 * geciyor: node graph'i tutmak, hangi node'un DIRTY/NOT_RUN/UP_TO_DATE
 * oldugunu takip etmek, parametre degisince bagimli node'lari dirty
 * yapmak (dirty propagation) ve pipeline'i calistirirken sadece
 * calismasi gerekenleri Python'a gondermek.
 *
 * Python tarafi "karar vermez" (CLAUDE.md) - butun bu mantik BURADA,
 * C++ tarafinda. Python sadece run_block/delete_node/get_meta komutlarini
 * yerine getirir.
 *
 * ---------------------------------------------------------------------
 * NODE STATE (dugum durumu) KAVRAMI (KNIME'daki "traffic light" ile ayni
 * fikir - kirmizi/sari/yesil isikli node kenarlari):
 *
 *   NOT_RUN     -> hic calistirilmadi
 *   UP_TO_DATE  -> en son calistirildigi haliyle GUNCEL, tekrar
 *                  calistirmaya gerek yok
 *   DIRTY       -> ya kendi parametresi ya da girdisi (upstream) degisti,
 *                  tekrar calismasi GEREKIYOR
 *   ERROR       -> son calistirmada Python'dan hata dondu
 * ---------------------------------------------------------------------
 */

#ifndef PIPELINE_ENGINE_H   // bu basligin ayni derlemede iki kez islenmesini engeller
#define PIPELINE_ENGINE_H   // include guard makrosunu tanimlar

#include <string>    // std::string icin
#include <map>       // node_id -> Node haritasi ve slot haritalari icin std::map
#include <vector>    // calistirma sirasi listesi icin std::vector
#include <set>       // dirty propagation sirasinda "zaten ziyaret edildi" takibi icin std::set
#include <functional>   // runAll'a verilebilen opsiyonel ProgressCallback icin
#include "json_value.h"      // params/meta alanlari icin JsonValue
#include "backend.h"         // Python'a (ya da uzak sunucuya) mesaj gondermek icin IBackend

enum class NodeState { NOT_RUN, UP_TO_DATE, DIRTY, ERROR_STATE };   // bir node'un olabilecegi dort durum

std::string nodeStateToString(NodeState state);   // NodeState degerini ekrana basilabilir metne cevirir

/* 
Bir node'un ürettiği tek bir çıkış slotunun (output, ya da train/test gibi çoklu çıkışlarda her biri) temsili.
ref = Python tarafındaki SessionStore'da bu verinin anahtarı (örn. "node_3:output")
meta = o veriyle ilgili küçük özet bilgi (shape, columns vs.)
*/
struct OutputSlot {
    std::string ref;   // SessionStore anahtari, orn. "node_3:output"
    JsonValue meta;      // o cikisla ilgili kucuk/ozet bilgi (shape, columns, output_type, ...)
};

struct Node {
    std::string id;                              // C tarafinin verdigi SABIT kimlik
    std::string block;                            // BLOCK_REGISTRY'deki isim, orn. "load_csv"
    JsonValue params;                              // object - blok parametreleri
    std::map<std::string, std::string> inputs;     // slot_adi -> "diger_node:slot" referansi
    NodeState state;                                // node'un guncel calisma durumu
    std::string lastError;                          // son calistirmada olusan hata mesaji (yoksa bos)
    std::map<std::string, OutputSlot> outputs;     // son basarili calismanin ciktilari

    Node() : state(NodeState::NOT_RUN) {}   // yeni bir node her zaman NOT_RUN olarak baslar
};

class PipelineEngine {
public:
    explicit PipelineEngine(IBackend& process);   // Python'a mesaj gonderecek process referansini saklar

    /* --- graph'i kurma --- */

    /* Yeni bir node ekler. State = NOT_RUN. */
    void addNode(const std::string& id, const std::string& block, const JsonValue& params);   // id/block/params ile yeni Node olusturup nodes_'a ekler

    /* Bir node'un input slotunu, baska bir node'un cikis slotuna baglar.
     * orn. connect("node_2", "data", "node_1", "output")
     *      -> node_2'nin "data" girdisi node_1'in "output" ciktisindan beslenir.
     * Bu node'u (ve varsa mevcut asagi akis bagimlilarini) DIRTY yapar. */
    void connect(const std::string& toNodeId, const std::string& toSlot,     // hedef node ve onun girdi slotu
                 const std::string& fromNodeId, const std::string& fromSlot);   // kaynak node ve onun cikis slotu

    /* connect()'in tersi: bir node'un input slotundaki mevcut baglantiyi
     * kaldirir (varsa; yoksa hicbir sey yapmaz). GUI'de bir baglanti
     * cizgisini surukleyip bosluga birakma / silme davranisi icin gerekli
     * (metin tabanli interactive_main.cpp'de bu ihtiyac hic dogmamisti,
     * cunku orada baglantilar sadece EKLENIYORDU, hicbir zaman tek basina
     * SOKULMUYORDU). Node'u (ve varsa asagi akis bagimlilarini) DIRTY yapar. */
    void disconnect(const std::string& nodeId, const std::string& slot);   // belirtilen input slotundaki baglantiyi kaldirir

    /* Bir node'un parametrelerini degistirir ve DIRTY PROPAGATION uygular:
     * kendisi + ona (dogrudan ya da dolayli) bagli TUM alt node'lar
     * DIRTY isaretlenir (CLAUDE.md'deki "Dirty propagation" maddesi). */
    void setParams(const std::string& id, const JsonValue& newParams);   // parametreleri gunceller ve node'u + baglilarini DIRTY yapar

    /* Node'u hem graph'imizdan hem Python'daki SessionStore'dan siler
     * (delete_node mesaji gonderilir). Ona bagli node'lar DIRTY yapilir -
     * bir sonraki runAll() calistiginda, artik var olmayan bir input'a
     * baglanmaya calisirlarsa ERROR_STATE ile sonuclanirlar (beklenen davranis;
     * gercek UI'da kullanici o baglantiyi da silmek/degistirmek zorunda kalir). */
    void removeNode(const std::string& id);   // node'u hem C++ grafindan hem Python SessionStore'dan siler

    /* --- calistirma --- */

    /* runAll()'a opsiyonel olarak verilebilir: bir node calisirken (orn.
     * mlp_learner'in her epoch sonunda) Python'dan ara "progress" satiri
     * gelirse, bu callback (nodeId, o satirin tum JSON'u) ile cagirilir -
     * nihai sonuc gelmeden ONCE, node hala calisiyorken. Callback
     * verilmezse (varsayilan, main.cpp/interactive_main.cpp'nin kullandigi
     * hal) davranis eskisiyle birebir aynidir. */
    using ProgressCallback = std::function<void(const std::string& nodeId, const JsonValue& progress)>;

    /* Sadece NOT_RUN/DIRTY durumundaki node'lari, bagimlilik sirasina gore
     * (once upstream, sonra downstream) Python'a gonderir. UP_TO_DATE
     * node'lar ATLANIR - hic mesaj gonderilmez (CLAUDE.md: "Secici calistirma").
     * Bir node hata verirse, ona bagli asagi akis node'lari o turda
     * calistirilmaz (onlarin girdisi guvenilir degil demektir). */
    void runAll(const ProgressCallback& onProgress = ProgressCallback());   // NOT_RUN/DIRTY node'lari dogru sirayla calistirir, UP_TO_DATE olanlari atlar

    /* --- durum sorgulama --- */
    const Node* getNode(const std::string& id) const;   // id'ye ait node'un salt-okunur pointer'ini doner, yoksa nullptr
    void printStatus() const;                              // tum node'larin durumunu ekrana basar

    /* --- node graph'ina DOKUNMAYAN, dogrudan SessionStore sorgulari ---
     * (removeNode'un delete_node icin yaptigi gibi process_.request'i
     * doguden sarmalar; run_block/dirty-propagation mantigina hic girmez) */

    /* Var olan bir ref'in (node->outputs[slot].ref) "ilk N satir"
     * onizlemesini Python'dan ister (get_preview op'u, bkz. dispatcher.py).
     * Donen JsonValue ham cevaptir - cagiran taraf "status"e bakmali. */
    JsonValue fetchPreview(const std::string& ref, int rowCount = 5);

    /* Var olan bir ref'teki TAM veriyi (onizleme degil) Python'a dogrudan
     * diske (filePath) CSV olarak yazdirir - veri hicbir zaman bu JSON
     * cevabinin icinde C tarafina gelmez, sadece basari/hata + satir sayisi
     * doner (export_csv op'u, bkz. dispatcher.py). */
    JsonValue exportCsv(const std::string& ref, const std::string& filePath);

private:
    IBackend& process_;              // Python'a mesaj gondermek icin kullanilan referans
    std::map<std::string, Node> nodes_;    // node_id -> Node; tum pipeline grafi burada tutulur

    void markDirtyRecursive(const std::string& id, std::set<std::string>& visited);   // id ve tum alt bagimlilarini DIRTY yapar (ozyinelemeli)
    std::vector<std::string> topologicalRunOrder() const;   // node'lari bagimlilik sirasina gore (once upstream) diziye koyar
    void visitForOrder(const std::string& id, std::set<std::string>& visited,          // topolojik siralama icin derinlik-oncelikli gezinme
                        std::vector<std::string>& order) const;
    bool runSingleNode(Node& node, const ProgressCallback& onProgress);   // Python'a run_block gonderir, sonucu isler
};

#endif // PIPELINE_ENGINE_H  -- basligin sonu
