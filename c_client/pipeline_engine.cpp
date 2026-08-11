/*
 * pipeline_engine.cpp
 */

#include "pipeline_engine.h"   // sinif/struct bildirimleri
#include <iostream>               // std::cout (durum/log yazdirma)
#include <stdexcept>              // std::runtime_error

std::string nodeStateToString(NodeState state) {
    switch (state) {                                        // enum degerini okunabilir metne cevir
        case NodeState::NOT_RUN:     return "NOT_RUN";
        case NodeState::UP_TO_DATE:  return "UP_TO_DATE";
        case NodeState::DIRTY:       return "DIRTY";
        case NodeState::ERROR_STATE: return "ERROR";
    }
    return "?";   // teorik olarak ulasilamaz, derleyici uyarisi vermesin diye
}

/* ref formati her zaman "node_id:slot" - upstream node id'sini cikarmak
  icin ilk ':' karakterine kadar olan kismi aliyoruz. */
static std::string upstreamNodeIdFromRef(const std::string& ref) {
    size_t colon = ref.find(':');                      // ':' karakterinin konumunu bul
    if (colon == std::string::npos) return ref;          // yoksa (beklenmedik durum) tum string'i doner
    return ref.substr(0, colon);                          // ':' oncesindeki kismi (node id) doner
}

PipelineEngine::PipelineEngine(PythonProcess& process) : process_(process) {}   // Python'a mesaj gondermek icin process referansini sakla

void PipelineEngine::addNode(const std::string& id, const std::string& block, const JsonValue& params) {
    if (nodes_.find(id) != nodes_.end()) {                                  // ayni id ile daha once eklenmis mi?
        throw std::runtime_error("PipelineEngine::addNode: node zaten var -> " + id);   // C tarafi zaten benzersiz id vermeli, bu bir programlama hatasi
    }
    Node node;                          // varsayilan kurucu ile NOT_RUN durumunda bir Node olustur
    node.id = id;                        // kimligini ata
    node.block = block;                   // hangi Python blogunu calistiracagini ata
    node.params = params;                  // blok parametrelerini ata
    node.state = NodeState::NOT_RUN;        // (Node() zaten NOT_RUN yapiyor, burada aciklik icin tekrar belirtiliyor)
    nodes_[id] = node;                       // graf haritasina ekle
}

void PipelineEngine::connect(const std::string& toNodeId, const std::string& toSlot,
                              const std::string& fromNodeId, const std::string& fromSlot) {
    std::map<std::string, Node>::iterator it = nodes_.find(toNodeId);   // hedef (girdiyi alacak) node'u ara
    if (it == nodes_.end()) {
        throw std::runtime_error("PipelineEngine::connect: hedef node bulunamadi -> " + toNodeId);
    }
    it->second.inputs[toSlot] = fromNodeId + ":" + fromSlot;   // "toSlot" girdisini "fromNode:fromSlot" referansina bagla

    std::set<std::string> visited;              // dirty propagation icin ziyaret takibi
    markDirtyRecursive(toNodeId, visited);        // yeni baglanti kuruldugu icin hedef node (ve baglilar) DIRTY olmali
}

void PipelineEngine::disconnect(const std::string& nodeId, const std::string& slot) {
    std::map<std::string, Node>::iterator it = nodes_.find(nodeId);   // baglantisi kaldirilacak node'u ara
    if (it == nodes_.end()) {
        throw std::runtime_error("PipelineEngine::disconnect: node bulunamadi -> " + nodeId);
    }
    it->second.inputs.erase(slot);   // slot hic baglanmamissa erase zaten sessizce hicbir sey yapmaz

    std::set<std::string> visited;
    markDirtyRecursive(nodeId, visited);   // artik eksik bir girdisi var, kendisi ve baglilari DIRTY olmali
}

void PipelineEngine::setParams(const std::string& id, const JsonValue& newParams) {
    std::map<std::string, Node>::iterator it = nodes_.find(id);   // parametresi degisecek node'u ara
    if (it == nodes_.end()) {
        throw std::runtime_error("PipelineEngine::setParams: node bulunamadi -> " + id);
    }
    it->second.params = newParams;   // yeni parametreleri ata

    std::set<std::string> visited;         // dirty propagation icin ziyaret takibi
    markDirtyRecursive(id, visited);         // kendisi ve TUM asagi akis bagimlilari DIRTY olmali
}

void PipelineEngine::removeNode(const std::string& id) {
    if (nodes_.find(id) == nodes_.end()) {                                     // olmayan bir node silinmeye calisiliyorsa
        throw std::runtime_error("PipelineEngine::removeNode: node bulunamadi -> " + id);
    }

    /* Python'daki SessionStore'da bu node'a ait tum slotlari temizle. */
    JsonValue req = JsonValue::makeObject();     // delete_node istegi icin bos bir JSON nesnesi
    req["op"] = JsonValue("delete_node");          // protokoldeki operasyon adi
    req["node_id"] = JsonValue(id);                 // silinecek node'un kimligi
    process_.request(req);                            // istegi gonderip cevabini bekle (SessionStore temizlenir)

    /* Bu node'un ciktisini kullanan node'lari, biz silmeden ONCE dirty yap -
     * markDirtyRecursive kendisiyle baslar, biz kendisini zaten silecegimiz
     * icin sadece ona bagli olanlari isaretlemek yeterli. */
    std::set<std::string> visited;
    visited.insert(id);   // kendisini tekrar ziyaret etmesin
    for (std::map<std::string, Node>::iterator it = nodes_.begin(); it != nodes_.end(); ++it) {   // tum diger node'lari tara
        if (it->first == id) continue;                                                              // silinecek node'un kendisini atla
        for (std::map<std::string, std::string>::const_iterator inIt = it->second.inputs.begin();
             inIt != it->second.inputs.end(); ++inIt) {                                              // bu node'un tum girdilerini kontrol et
            if (upstreamNodeIdFromRef(inIt->second) == id) {                                          // girdilerinden biri silinecek node'a bagliysa
                markDirtyRecursive(it->first, visited);                                                 // bu node'u (ve baglilarini) DIRTY yap
                break;                                                                                   // bu node icin bir kez yeterli, digerine gec
            }
        }
    }

    nodes_.erase(id);   // node'u haritadan (graf) kaldir
}

void PipelineEngine::markDirtyRecursive(const std::string& id, std::set<std::string>& visited) {
    if (visited.find(id) != visited.end()) return;   // dongusel baglantidan / tekrar ziyaretten koru
    visited.insert(id);   // bu node'u ziyaret edildi olarak isaretle

    std::map<std::string, Node>::iterator it = nodes_.find(id);
    if (it == nodes_.end()) return;   // removeNode sirasinda kendisi zaten silinmis olabilir
    if (it->second.state != NodeState::NOT_RUN) {   // hic calismamis bir node zaten "kirletilmeye" gerek yok
        it->second.state = NodeState::DIRTY;          // UP_TO_DATE ya da ERROR_STATE ise DIRTY'ye cevir
    }

    /* Bu node'un ciktisini girdi olarak kullanan TUM node'lari bul ve
     * ozyinelemeli (recursive) olarak onlari da dirty yap. */
    for (std::map<std::string, Node>::iterator other = nodes_.begin(); other != nodes_.end(); ++other) {   // tum node'lari gez
        for (std::map<std::string, std::string>::const_iterator inIt = other->second.inputs.begin();
             inIt != other->second.inputs.end(); ++inIt) {                                                    // her birinin tum girdilerini kontrol et
            if (upstreamNodeIdFromRef(inIt->second) == id) {                                                   // girdisi bizim node'umuza bagliysa
                markDirtyRecursive(other->first, visited);                                                       // onu da (ve onun baglilarini da) ozyinelemeli isaretle
            }
        }
    }
}

void PipelineEngine::visitForOrder(const std::string& id, std::set<std::string>& visited,
                                    std::vector<std::string>& order) const {
    if (visited.find(id) != visited.end()) return;   // zaten siraya eklenmisse tekrar isleme
    visited.insert(id);   // once ziyaret edildi olarak isaretle (dongu korumasi)

    std::map<std::string, Node>::const_iterator it = nodes_.find(id);
    if (it == nodes_.end()) return;   // graf tutarsizsa (olmamasi gereken durum) sessizce cik

    for (std::map<std::string, std::string>::const_iterator inIt = it->second.inputs.begin();
         inIt != it->second.inputs.end(); ++inIt) {                          // bu node'un tum girdi baglantilarini gez
        std::string upstreamId = upstreamNodeIdFromRef(inIt->second);          // baglandigi kaynak node'un id'sini cikar
        if (nodes_.find(upstreamId) != nodes_.end()) {                          // kaynak hala graf icinde var mi
            visitForOrder(upstreamId, visited, order);                            // once ONUN once calismasi lazim, ozyinelemeli ziyaret et
        }
    }
    order.push_back(id);   // once butun bagimliliklari ekle, sonra kendisini (post-order)
}

std::vector<std::string> PipelineEngine::topologicalRunOrder() const {
    std::vector<std::string> order;             // sonuc: calistirma sirasina gore node id listesi
    std::set<std::string> visited;                // hangi node'larin siraya zaten eklendigini takip eder
    for (std::map<std::string, Node>::const_iterator it = nodes_.begin(); it != nodes_.end(); ++it) {   // her node'dan baslayarak
        visitForOrder(it->first, visited, order);    // henuz ziyaret edilmemisse post-order gezinmeyi baslat
    }
    return order;   // upstream'lerin her zaman downstream'lerden once geldigi bir sira doner
}

bool PipelineEngine::runSingleNode(Node& node) {
    JsonValue req = JsonValue::makeObject();                                    // Python'a gonderilecek istegi olustur
    req["op"] = JsonValue("run_block");                                          // protokoldeki operasyon adi
    req["node_id"] = JsonValue(node.id);                                          // hangi node calisiyor
    req["block"] = JsonValue(node.block);                                         // hangi blok fonksiyonu calisacak
    req["params"] = node.params.isNull() ? JsonValue::makeObject() : node.params;   // params hic verilmemisse bos object gonder

    JsonValue inputsJson = JsonValue::makeObject();                              // "slot_adi": "node:slot" seklinde girdi haritasi
    for (std::map<std::string, std::string>::const_iterator it = node.inputs.begin();
         it != node.inputs.end(); ++it) {
        inputsJson[it->first] = JsonValue(it->second);                             // her girdi baglantisini JSON alanina cevir
    }
    req["inputs"] = inputsJson;                                                  // istege ekle

    JsonValue response = process_.request(req);                                 // istegi gonder, Python cevabini bekle

    std::string status = response.has("status") ? response.at("status").asString() : "error";   // "status" alani yoksa guvenli varsayilan: error
    if (status == "ok") {                                                        // basarili calisma
        node.outputs.clear();                                                      // onceki ciktilari temizle (overwrite mantigi)
        if (response.has("outputs")) {                                              // cevapta cikti alani varsa
            const JsonValue& outs = response.at("outputs");
            for (std::map<std::string, JsonValue>::const_iterator it = outs.fields().begin();
                 it != outs.fields().end(); ++it) {                                   // her cikis slotu icin
                OutputSlot slot;                                                        // ref+meta'yi tasiyacak yapi
                slot.ref = it->second.has("ref") ? it->second.at("ref").asString() : "";   // SessionStore anahtari
                slot.meta = it->second.has("meta") ? it->second.at("meta") : JsonValue::makeObject();   // ozet meta bilgisi
                node.outputs[it->first] = slot;                                          // slot adiyla node'un ciktilarina kaydet
            }
        }
        node.state = NodeState::UP_TO_DATE;   // artik guncel
        node.lastError.clear();                 // onceki hata varsa temizle
        return true;
    } else {                                  // basarisiz calisma
        node.outputs.clear();                   // yarim/gecersiz cikti kalmasin
        node.lastError = response.has("message") ? response.at("message").asString() : "bilinmeyen hata";   // hata mesajini sakla
        node.state = NodeState::ERROR_STATE;      // durumu hataya cevir
        return false;
    }
}

void PipelineEngine::runAll() {
    std::vector<std::string> order = topologicalRunOrder();   // upstream'lerin once geldigi calistirma sirasi
    std::set<std::string> failedOrSkipped;                       // bu turda hata veren/atlanan node id'leri

    for (size_t i = 0; i < order.size(); i++) {
        std::map<std::string, Node>::iterator it = nodes_.find(order[i]);   // sirali listedeki node'u haritada bul
        if (it == nodes_.end()) continue;                                     // (teorik olarak olmamali) bulunamadiysa atla
        Node& node = it->second;

        /* Bagimli oldugu node'lardan biri bu turda basarisiz/atlanmis mi? */
        bool blockedByUpstream = false;
        for (std::map<std::string, std::string>::const_iterator inIt = node.inputs.begin();
             inIt != node.inputs.end(); ++inIt) {                              // bu node'un tum girdilerini kontrol et
            std::string upstreamId = upstreamNodeIdFromRef(inIt->second);        // her girdinin kaynak node id'sini cikar
            if (failedOrSkipped.find(upstreamId) != failedOrSkipped.end()) {      // kaynak bu turda basarisiz/atlanmissa
                blockedByUpstream = true;
                break;
            }
        }

        if (blockedByUpstream) {                                              // guvenilmez bir girdiyle calistirmanin anlami yok
            std::cout << "  [" << node.id << "] ATLANDI (bagli oldugu node bu turda hata verdi/calismadi)\n";
            failedOrSkipped.insert(node.id);                                    // bu node da "kirli" sayilsin, alt bagimlilar da atlanacak
            continue;
        }

        if (node.state == NodeState::UP_TO_DATE) {                            // "secici calistirma": guncel node'lara dokunma
            std::cout << "  [" << node.id << "] atlandi (zaten UP_TO_DATE, Python'a mesaj gonderilmedi)\n";
            continue;
        }

        std::cout << "  [" << node.id << "] calistiriliyor (" << node.block << ")...\n";
        bool ok = runSingleNode(node);   // Python'a run_block gonder, sonuca gore node.state guncellenir
        if (ok) {
            std::cout << "      -> basarili, state = UP_TO_DATE\n";
        } else {
            std::cout << "      -> HATA: " << node.lastError << "\n";
            failedOrSkipped.insert(node.id);   // bu node'a bagli olanlar bir sonraki adimda bloklanacak
        }
    }
}

const Node* PipelineEngine::getNode(const std::string& id) const {
    std::map<std::string, Node>::const_iterator it = nodes_.find(id);   // id'yi haritada ara
    return it == nodes_.end() ? NULL : &it->second;                        // bulunamadiysa NULL, bulunduysa pointer doner
}

void PipelineEngine::printStatus() const {
    std::cout << "--- pipeline durumu ---\n";
    for (std::map<std::string, Node>::const_iterator it = nodes_.begin(); it != nodes_.end(); ++it) {   // tum node'lari sirayla (id'ye gore) gez
        std::cout << "  " << it->first << " (" << it->second.block << "): "
                  << nodeStateToString(it->second.state);                       // id, blok adi ve durumu yazdir
        if (it->second.state == NodeState::ERROR_STATE) {                       // hata durumundaysa
            std::cout << "  [" << it->second.lastError << "]";                    // hata mesajini da ekle
        }
        std::cout << "\n";
    }
}
