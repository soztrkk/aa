/*
 * interactive_main.cpp
 *
 * ADIM 6 (devami): interaktif test istemcisi.
 *
 * NASIL CALISIR:
 *   1) program acilir, dispatcher.py arka planda baslar
 *   2) terminalde "Blok adi:" sorulur
 *   3) blogun bekledigi HER GIRDI SLOTU icin (cogu blokta tek slot: "data",
 *      ama create_dataloader gibi bloklarda "X"+"y", compute_classification_metrics
 *      gibi bloklarda "model"+"X"+"y") hangi ONCEKI node:slot'un kullanilacagi
 *      sorulur - bos birakilirsan ve tek cikisli bir onceki basarili node
 *      varsa, o otomatik kullanilir
 *   4) o bloga ait parametreler TEK TEK sorulur (bkz. block_specs.cpp)
 *   5) yeni bir node olusturulur, sadece BU node calistirilir (oncekiler
 *      zaten UP_TO_DATE, PipelineEngine onlari atlar), sonuc ekrana basilir
 *   6) blogun ciktisi bir DataFrame ise (bkz. BlockSpec::producesDataFrame),
 *      OTOMATIK olarak kucuk bir data_preview cagrilir ve ilk 5 satir da
 *      gosterilir - boylece meta (shape/columns/...) VE veri onizlemesi
 *      bir arada gorunur
 *   7) 3-6 tekrarlanir; "q" / "sonlandir" ile cikilir
 *
 * Bu, gercek UI'nin "kullanici bir blok surukler, girdi/parametrelerini
 * girer, calistirir, sonuca bakar, bir blok daha ekler" davranisinin
 * bire bir metin tabanli simulasyonudur.
 *
 * DERLEME:  build.bat  (pipeline_client.exe ile ayni anda, ayri .exe olarak)
 * CALISTIRMA: proje KOK dizininden ->  c_client\interactive_client.exe
 */

#include <iostream>        // std::cin/std::cout
#include <string>            // std::string
#include <utility>           // std::pair/std::make_pair
#include "json_value.h"        // JsonValue
#include "python_process.h"     // PythonProcess
#include "pipeline_engine.h"      // PipelineEngine, Node, NodeState
#include "result_display.h"        // renderNodeOutput
#include "block_specs.h"             // BlockSpec, blockRegistrySpecs, promptForParams

namespace {   // bu dosyaya ozel yardimci fonksiyonlar

std::string trimLine(const std::string& s) {
    size_t start = 0;   // bastan atlanacak bosluk sayisi
    while (start < s.size() && (s[start] == ' ' || s[start] == '\t' || s[start] == '\r')) start++;   // bosluk/tab/CR'yi bastan atla
    size_t end = s.size();   // sondan atlanacak kismin sinirini tutar
    while (end > start && (s[end - 1] == ' ' || s[end - 1] == '\t' || s[end - 1] == '\r')) end--;   // sondan geriye dogru ayni sekilde
    return s.substr(start, end - start);   // temizlenmis orta kismi doner
}

bool isQuitCommand(const std::string& s) {
    return s == "q" || s == "quit" || s == "exit" || s == "sonlandir" || s == "cikis";   // hem Ingilizce hem Turkce cikis komutlarini kabul et
}

void showAllOutputs(const PipelineEngine& engine, const std::string& nodeId) {
    const Node* node = engine.getNode(nodeId);   // node'u id ile bul
    if (!node) return;                              // yoksa gosterilecek bir sey yok
    for (std::map<std::string, OutputSlot>::const_iterator it = node->outputs.begin();
         it != node->outputs.end(); ++it) {           // bu node'un tum cikis slotlari icin
        renderNodeOutput(nodeId, it->first, it->second.meta);   // her birini bicimli sekilde bas
    }
}

/* "node_3" ya da "node_3:train" seklindeki bir kullanici girdisini,
 * gercekten var olan ve BASARIYLA calismis bir node+slot'a cozer.
 * ':' verilmemisse slot "output" varsayilir (bloklarin buyuk cogunlugu
 * tek cikis urettigi ve o cikisin adi hep "output" oldugu icin - bkz.
 * blocks/base.py _normalize_result). Basarisiz olursa false doner ve
 * errorOut'a kullanicidan tekrar sorulacak aciklamayi yazar. */
bool resolveInputRef(const PipelineEngine& engine, const std::string& raw,
                      std::string& outNodeId, std::string& outSlot, std::string& errorOut) {
    size_t colon = raw.find(':');                                              // "node_3:train" gibi bir ':' var mi diye bak
    std::string nodeId = (colon == std::string::npos) ? raw : raw.substr(0, colon);   // ':' yoksa tum girdi node id'dir, varsa oncesi
    std::string slot = (colon == std::string::npos) ? "output" : raw.substr(colon + 1);   // ':' yoksa varsayilan slot "output", varsa sonrasi

    const Node* node = engine.getNode(nodeId);   // boyle bir node var mi kontrol et
    if (!node) {
        errorOut = "'" + nodeId + "' adinda bir node yok";
        return false;
    }
    if (node->state != NodeState::UP_TO_DATE) {                              // node bulunmus ama basariyla calismamissa
        errorOut = "'" + nodeId + "' henuz basariyla calismadi (durum: " + nodeStateToString(node->state) + ")";
        return false;
    }
    if (node->outputs.find(slot) == node->outputs.end()) {                    // istenen slot bu node'da yoksa
        errorOut = "'" + nodeId + "' node'unun '" + slot + "' adinda bir ciktisi yok";
        return false;
    }

    outNodeId = nodeId;   // basarili: cozulen node id'sini cikti parametresine yaz
    outSlot = slot;         // basarili: cozulen slot adini cikti parametresine yaz
    return true;
}

/* Eger tam olarak bir onceki basarili node varsa ve onun TEK bir cikis
 * slotu varsa (orn. "output"), onu "node_id:slot" seklinde varsayilan
 * deger olarak onerir. Onceki node birden fazla slot urettiyse (orn.
 * train_test_split -> train+test) BILEREK varsayilan sunulmuyor -
 * kullanicinin hangisini istedigini ACIKCA yazmasi gerekiyor. */
std::string defaultRefFor(const PipelineEngine& engine, const std::string& lastSuccessfulNodeId) {
    if (lastSuccessfulNodeId.empty()) return "";                          // henuz basarili bir node yoksa varsayilan onerilemez
    const Node* node = engine.getNode(lastSuccessfulNodeId);
    if (!node || node->outputs.size() != 1) return "";                     // node yoksa ya da BIRDEN FAZLA cikisi varsa (belirsizlik) varsayilan onerme
    return lastSuccessfulNodeId + ":" + node->outputs.begin()->first;      // tek cikisi varsa "node_id:slot" seklinde oner
}

/* Blogun bekledigi HER girdi slotu icin kullanicidan kaynak sorar.
 * Donen harita: slot_adi -> (kaynak_node_id, kaynak_slot). */
std::map<std::string, std::pair<std::string, std::string> >
promptForInputs(const PipelineEngine& engine, const BlockSpec& spec, const std::string& lastSuccessfulNodeId)
{
    std::map<std::string, std::pair<std::string, std::string> > connections;   // sonuc: slot_adi -> (kaynak_node, kaynak_slot)

    for (size_t i = 0; i < spec.inputSlots.size(); i++)   // blogun bekledigi her girdi slotu icin
    {
        const std::string& slotName = spec.inputSlots[i];             // su anki slotun adi (orn. "data", "X", "y")
        std::string defaultRef = defaultRefFor(engine, lastSuccessfulNodeId);   // varsa onerilecek varsayilan kaynak

        while (true)   // gecerli bir kaynak girilene kadar tekrar sor
        {
            std::cout << "    girdi '" << slotName << "' -> kaynak (node_id veya node_id:slot)";
            if (!defaultRef.empty()) std::cout << " [bos = " << defaultRef << "]";   // varsayilan varsa kullaniciya goster
            std::cout << ": ";

            std::string raw;
            std::getline(std::cin, raw);      // kullanicidan bir satir oku
            raw = trimLine(raw);                 // bas/son bosluklari temizle
            std::string useValue = raw.empty() ? defaultRef : raw;   // bos birakildiysa varsayilani kullan

            if (useValue.empty())   // varsayilan da yoksa ve girdi bossa
            {
                std::cout << "      -> gecerli bir kaynak belirtmelisin (henuz varsayilan yok).\n";
                continue;   // tekrar sor
            }

            std::string fromNode, fromSlot, err;
            if (!resolveInputRef(engine, useValue, fromNode, fromSlot, err))   // girilen degeri gercek node:slot'a cozmeye calis
            {
                std::cout << "      -> " << err << "\n";   // gecersizse hatayi goster
                continue;                                     // tekrar sor
            }

            connections[slotName] = std::make_pair(fromNode, fromSlot);   // basarili: bu slot icin baglantiyi kaydet
            break;                                                          // bu slot icin dongu bitti, sonraki slota gec
        }
    }
    return connections;   // tum slotlar icin cozulmus baglantilari doner
}

/* producesDataFrame=true olan bloklarin her cikis slotu icin OTOMATIK
 * olarak kucuk bir "data_preview" node'u ekleyip calistirir ve ilk 5
 * satiri gosterir. data_preview blogunun kendisi icin bu ATLANIR, cunku
 * onun meta'si zaten AYNI onizlemeyi iceriyor (cift gosterim olmasin diye). */
void showAutoDataFramePreview(PipelineEngine& engine, const std::string& sourceNodeId, const std::string& blockName) {
    if (blockName == "data_preview") return;   // data_preview'in kendisi icin cift gosterim olmasin diye atla

    const Node* node = engine.getNode(sourceNodeId);
    if (!node) return;   // (teorik olarak olmamali) node bulunamadiysa yapacak bir sey yok

    for (std::map<std::string, OutputSlot>::const_iterator it = node->outputs.begin();
         it != node->outputs.end(); ++it) {                     // bu node'un tum cikis slotlari icin (orn. train+test)
        const std::string& sourceSlot = it->first;                 // onizlenecek slotun adi
        std::string previewId = sourceNodeId + "_" + sourceSlot + "_preview";   // benzersiz bir onizleme node id'si uret

        JsonValue previewParams = JsonValue::makeObject();      // data_preview blogu icin parametreler
        previewParams["row_count"] = JsonValue(5);                // ilk 5 satir
        previewParams["preview_type"] = JsonValue("head");         // basdan

        engine.addNode(previewId, "data_preview", previewParams);         // yeni onizleme node'unu ekle
        engine.connect(previewId, "data", sourceNodeId, sourceSlot);        // girdisini kaynak slot'a bagla
        engine.runAll();   // sadece bu yeni onizleme node'u calisir, geri kalani UP_TO_DATE

        const Node* previewNode = engine.getNode(previewId);
        if (previewNode && previewNode->state == NodeState::UP_TO_DATE) {   // onizleme basariliysa
            std::cout << "  -- '" << sourceNodeId << ":" << sourceSlot << "' icin ilk 5 satir (otomatik onizleme) --\n";
            showAllOutputs(engine, previewId);                                // ciktisini bas
        } else if (previewNode) {                                            // basarisizsa (orn. cikti DataFrame degildi)
            std::cout << "  (otomatik onizleme yapilamadi: " << previewNode->lastError << ")\n";
        }
    }
}

} // anonymous namespace

int main() {

    PythonProcess proc;         // dispatcher.py'yi arka planda calistiracak nesne
    std::string startError;      // baslatma basarisiz olursa aciklama buraya yazilir

    std::cout << "Python dispatcher.py baslatiliyor...\n";
    if (!proc.start("python", "dispatcher.py", startError)) {   // process'i baslat
        std::cerr << "BASLATMA HATASI: " << startError << "\n";
        return 1;   // baslatilamadiysa devam etmenin anlami yok
    }
    std::cout << "Hazir. 'liste' -> desteklenen bloklar, 'durum' -> pipeline durumu, 'q' -> cikis.\n";

    PipelineEngine engine(proc);          // interaktif oturum boyunca kullanilacak tek pipeline
    std::string lastSuccessfulNodeId;   // hicbir blok calismadiysa bos
    int nodeCounter = 0;                    // otomatik node id uretmek icin sayac (node_1, node_2, ...)

    while (true) {   // kullanici cikis komutu verene kadar surer
        std::cout << "\nBlok adi: ";
        std::string blockName;
        if (!std::getline(std::cin, blockName)) break;   // stdin kapandiysa (orn. Ctrl+Z) cik
        blockName = trimLine(blockName);                   // bas/son bosluklari temizle

        if (blockName.empty()) continue;          // bos satir girildiyse tekrar sor
        if (isQuitCommand(blockName)) break;         // "q"/"quit"/... ise dongudan cik

        if (blockName == "liste") {                 // ozel komut: desteklenen bloklari listele
            printAvailableBlocks();
            continue;
        }
        if (blockName == "durum") {                  // ozel komut: pipeline durumunu goster
            engine.printStatus();
            continue;
        }

        const std::map<std::string, BlockSpec>& registry = blockRegistrySpecs();   // tum blok tanimlari
        std::map<std::string, BlockSpec>::const_iterator specIt = registry.find(blockName);   // girilen ad tabloda var mi
        if (specIt == registry.end()) {                                              // yoksa taninmayan bir blok adi
            std::cout << "  Bilinmeyen blok: '" << blockName << "'. 'liste' yazarak secenekleri gorebilirsin.\n";
            continue;
        }
        const BlockSpec& spec = specIt->second;   // bulunan blogun tanimi

        // --- girdi kaynaklarini sor (varsa) ---
        std::map<std::string, std::pair<std::string, std::string> > connections;   // slot_adi -> (kaynak_node, kaynak_slot)
        if (!spec.inputSlots.empty()) {                                              // bu blok en az bir girdi bekliyorsa
            std::cout << "  '" << blockName << "' girdileri:\n";
            connections = promptForInputs(engine, spec, lastSuccessfulNodeId);         // her slot icin kullaniciya sor
        }

        // --- parametreleri sor ---
        std::cout << "  '" << blockName << "' parametreleri:\n";
        JsonValue params = promptForParams(spec);   // blogun tum parametrelerini sirayla sor

        nodeCounter++;                                        // yeni node icin sayaci ilerlet
        std::string nodeId = "node_" + std::to_string(nodeCounter);   // benzersiz node id uret (orn. "node_3")

        engine.addNode(nodeId, blockName, params);   // yeni node'u pipeline'a ekle
        for (std::map<std::string, std::pair<std::string, std::string> >::const_iterator it = connections.begin();
             it != connections.end(); ++it) {          // toplanan tum girdi baglantilarini uygula
            const std::string& toSlot = it->first;        // bu node'un hangi girdi slotu
            const std::string& fromNode = it->second.first;   // hangi kaynak node'dan
            const std::string& fromSlot = it->second.second;   // hangi kaynak slot'tan
            engine.connect(nodeId, toSlot, fromNode, fromSlot);   // baglantiyi kur
        }

        std::cout << "  calistiriliyor...\n";
        engine.runAll();   // sadece bu yeni node calisir (oncekiler zaten UP_TO_DATE)

        const Node* node = engine.getNode(nodeId);
        if (node->state == NodeState::ERROR_STATE) {                                  // calistirma basarisiz olduysa
            std::cout << "  HATA: " << node->lastError << "\n";
            std::cout << "  (bu node pipeline'dan kaldiriliyor, tekrar deneyebilirsin)\n";
            engine.removeNode(nodeId);   // basarisiz node'u pipeline'dan cikar
            nodeCounter--;   // basarisiz denemeyi node id sayacindan da geri al
            continue;         // bir sonraki komutu bekle
        }

        std::cout << "  BASARILI:\n";
        showAllOutputs(engine, nodeId);   // basarili node'un tum ciktilarini goster

        if (spec.producesDataFrame) {                              // cikti bir DataFrame ise
            showAutoDataFramePreview(engine, nodeId, blockName);      // otomatik olarak ilk 5 satirini da goster
        }

        lastSuccessfulNodeId = nodeId;   // bir sonraki blogun varsayilan girdi onerisi icin guncelle
    }

    proc.stop();   // Python process'ini duzgunce kapat
    std::cout << "\nCikildi, Python process kapatildi.\n";
    return 0;
}
