/*
 * gui_app.cpp
 */

#include "gui_app.h"
#include "result_display_imgui.h"
#include "imgui.h"
#include "imnodes.h"
#include <cstdlib>
#include <cstdio>
#include <cctype>
#include <sstream>

namespace {

/* block_specs.cpp'deki AYNI metin->JSON parse mantigi (trim/parseNumber/
 * parseBool/parseStringList). O dosyada bu yardimcilar anonim namespace
 * icinde (disariya kapali) oldugu icin burada kucuk bir kopyasini tutuyoruz -
 * tek satirlik yardimcilar oldugundan ayri bir ortak header'a cikarmaya
 * degmiyor. */
std::string trim(const std::string& s) {
    size_t start = 0;
    while (start < s.size() && (s[start] == ' ' || s[start] == '\t' || s[start] == '\r' || s[start] == '\n')) start++;
    size_t end = s.size();
    while (end > start && (s[end - 1] == ' ' || s[end - 1] == '\t' || s[end - 1] == '\r' || s[end - 1] == '\n')) end--;
    return s.substr(start, end - start);
}

JsonValue parseStringList(const std::string& raw) {
    JsonValue arr = JsonValue::makeArray();
    std::stringstream ss(raw);
    std::string token;
    while (std::getline(ss, token, ',')) {
        std::string t = trim(token);
        if (!t.empty()) arr.push_back(JsonValue(t));
    }
    return arr;
}

JsonValue parseNumber(const std::string& raw) {
    return JsonValue(std::strtod(raw.c_str(), NULL));
}

JsonValue parseBool(const std::string& raw) {
    std::string t = raw;
    for (size_t i = 0; i < t.size(); i++) t[i] = static_cast<char>(tolower(static_cast<unsigned char>(t[i])));
    return JsonValue(t == "true" || t == "evet" || t == "1" || t == "yes" || t == "e");
}

/* Node durumuna gore baslik cubugu rengi - KNIME'daki "traffic light"
 * fikrinin aynisi (bkz. pipeline_engine.h basindaki aciklama). */
void stateColors(NodeState state, ImU32& normal, ImU32& hovered, ImU32& selected) {
    switch (state) {
        case NodeState::UP_TO_DATE:
            normal = IM_COL32(46, 125, 50, 255); hovered = IM_COL32(56, 145, 60, 255); selected = IM_COL32(66, 165, 70, 255);
            break;
        case NodeState::DIRTY:
            normal = IM_COL32(180, 140, 20, 255); hovered = IM_COL32(200, 160, 30, 255); selected = IM_COL32(220, 180, 40, 255);
            break;
        case NodeState::ERROR_STATE:
            normal = IM_COL32(170, 40, 40, 255); hovered = IM_COL32(195, 50, 50, 255); selected = IM_COL32(215, 60, 60, 255);
            break;
        case NodeState::NOT_RUN:
        default:
            normal = IM_COL32(70, 70, 75, 255); hovered = IM_COL32(85, 85, 90, 255); selected = IM_COL32(100, 100, 105, 255);
            break;
    }
}

} // anonymous namespace

GuiApp::GuiApp(PipelineEngine& engine)
    : engine_(engine), nextId_(0), nodeNameCounter_(0), nextPosX_(40.0f), nextPosY_(40.0f),
      isRunning_(false), previewRefreshPending_(false) {}

GuiApp::~GuiApp() {
    /* Egitim SURERKEN pencere kapatilirsa runThread_ hala calisiyor olabilir -
     * onu yarim birakip yok edersek (std::thread'in destructor'i joinable bir
     * thread'de cagrilirsa) program std::terminate ile coker. Burada v1'de
     * bir "iptal" mekanizmasi YOK (bkz. gui_app.h basindaki THREAD MODELI) -
     * sadece o an devam eden run_block cagrisi dogal olarak bitene kadar
     * BEKLIYORUZ. Ileride gercek bir iptal istenirse, PythonProcess'e
     * "egitimi durdur" gibi ayri bir mesaj eklenmesi gerekir. */
    if (runThread_.joinable()) {
        runThread_.join();
    }
}

void GuiApp::refreshDisplayCache() {
    for (std::map<std::string, int>::const_iterator it = nodeStrToInt_.begin(); it != nodeStrToInt_.end(); ++it) {
        const Node* node = engine_.getNode(it->first);
        if (!node) continue;
        DisplayNodeInfo info;
        info.block = node->block;
        info.state = node->state;
        info.lastError = node->lastError;
        info.outputs = node->outputs;
        displayCache_[it->first] = info;
    }
}

void GuiApp::refreshPreviewsForUpToDateNodes() {
    const std::map<std::string, BlockSpec>& registry = blockRegistrySpecs();
    for (std::map<std::string, DisplayNodeInfo>::const_iterator it = displayCache_.begin(); it != displayCache_.end(); ++it) {
        const std::string& nodeId = it->first;
        const DisplayNodeInfo& info = it->second;
        if (info.state != NodeState::UP_TO_DATE) continue;

        std::map<std::string, BlockSpec>::const_iterator specIt = registry.find(info.block);
        if (specIt == registry.end() || !specIt->second.producesDataFrame) continue;
        if (info.block == "data_preview") continue;   // zaten kendisi bir onizleme, tekrar onizlemeye gerek yok

        for (std::map<std::string, OutputSlot>::const_iterator slotIt = info.outputs.begin(); slotIt != info.outputs.end(); ++slotIt) {
            JsonValue response = engine_.fetchPreview(slotIt->second.ref);
            if (response.has("status") && response.at("status").asString() == "ok" && response.has("meta")) {
                previewCache_[nodeId + ":" + slotIt->first] = response.at("meta");
            }
        }
    }
}

void GuiApp::createNode(const std::string& blockName) {
    if (isRunning_.load()) return;   // calisirken graf duzenlenemez (bkz. THREAD MODELI)

    const std::map<std::string, BlockSpec>& registry = blockRegistrySpecs();
    std::map<std::string, BlockSpec>::const_iterator specIt = registry.find(blockName);
    if (specIt == registry.end()) return;   // (olmamasi gereken durum, palet zaten registry'den doldu)
    const BlockSpec& spec = specIt->second;

    std::string nodeId = blockName + "_" + std::to_string(++nodeNameCounter_);

    try {
        engine_.addNode(nodeId, blockName, JsonValue::makeObject());   // parametreler bos baslar, kullanici sag panelden doldurup "uygula" der
    } catch (const std::exception& e) {
        lastErrorMessage_ = e.what();
        return;
    }

    int nodeIntId = allocId();
    nodeStrToInt_[nodeId] = nodeIntId;
    nodeIntToStr_[nodeIntId] = nodeId;

    NodeUiState st;
    st.block = blockName;
    for (size_t i = 0; i < spec.inputSlots.size(); i++) {
        int pinId = allocId();
        st.inputPinId[spec.inputSlots[i]] = pinId;
        PinInfo info; info.nodeId = nodeId; info.slot = spec.inputSlots[i]; info.isInput = true;
        pinInfo_[pinId] = info;
    }
    for (size_t i = 0; i < spec.outputSlots.size(); i++) {
        int pinId = allocId();
        st.outputPinId[spec.outputSlots[i]] = pinId;
        PinInfo info; info.nodeId = nodeId; info.slot = spec.outputSlots[i]; info.isInput = false;
        pinInfo_[pinId] = info;
    }
    uiState_[nodeId] = st;

    ImNodes::SetNodeGridSpacePos(nodeIntId, ImVec2(nextPosX_, nextPosY_));
    nextPosX_ += 260.0f;
    if (nextPosX_ > 1400.0f) { nextPosX_ = 40.0f; nextPosY_ += 240.0f; }

    selectedNodeId_ = nodeId;   // yeni eklenen node otomatik secili olsun (sag panelde parametrelerini hemen doldurabilsin)
    refreshDisplayCache();       // yeni node HEMEN bu karede de dogru cizilsin diye
}

void GuiApp::deleteNode(const std::string& nodeId) {
    if (isRunning_.load()) return;   // calisirken graf duzenlenemez (bkz. THREAD MODELI)

    try {
        engine_.removeNode(nodeId);
    } catch (const std::exception& e) {
        lastErrorMessage_ = e.what();
        return;
    }

    /* bu node'a ait gorsel state'i temizle */
    std::map<std::string, NodeUiState>::iterator uiIt = uiState_.find(nodeId);
    if (uiIt != uiState_.end()) {
        for (std::map<std::string, int>::iterator p = uiIt->second.inputPinId.begin(); p != uiIt->second.inputPinId.end(); ++p) pinInfo_.erase(p->second);
        for (std::map<std::string, int>::iterator p = uiIt->second.outputPinId.begin(); p != uiIt->second.outputPinId.end(); ++p) pinInfo_.erase(p->second);
        uiState_.erase(uiIt);
    }
    std::map<std::string, int>::iterator intIt = nodeStrToInt_.find(nodeId);
    if (intIt != nodeStrToInt_.end()) {
        nodeIntToStr_.erase(intIt->second);
        nodeStrToInt_.erase(intIt);
    }
    paramTextBuffers_.erase(nodeId);
    displayCache_.erase(nodeId);

    /* bu node'un onizleme cache kayitlarini da temizle ("nodeId:slot" anahtarlar) */
    for (std::map<std::string, JsonValue>::iterator it = previewCache_.begin(); it != previewCache_.end(); ) {
        if (it->first.compare(0, nodeId.size() + 1, nodeId + ":") == 0) {
            previewCache_.erase(it++);
        } else {
            ++it;
        }
    }

    /* bu node'a giden ya da bu node'dan cikan tum gorsel link kayitlarini temizle */
    for (std::map<int, LinkInfo>::iterator it = linkInfo_.begin(); it != linkInfo_.end(); ) {
        if (it->second.toNode == nodeId || it->second.fromNode == nodeId) {
            linkIdForInputSlot_.erase(it->second.toNode + ":" + it->second.toSlot);
            linkInfo_.erase(it++);
        } else {
            ++it;
        }
    }

    if (selectedNodeId_ == nodeId) selectedNodeId_.clear();
}

JsonValue GuiApp::buildParamsJson(const BlockSpec& spec, const std::string& nodeId) {
    JsonValue params = JsonValue::makeObject();
    std::map<std::string, std::map<std::string, std::string> >::iterator bufIt = paramTextBuffers_.find(nodeId);
    if (bufIt == paramTextBuffers_.end()) return params;   // hic form doldurulmamis, tum parametreler Python varsayilanini kullanacak

    for (size_t i = 0; i < spec.params.size(); i++) {
        const ParamSpec& p = spec.params[i];
        std::map<std::string, std::string>::iterator valIt = bufIt->second.find(p.key);
        std::string raw = (valIt == bufIt->second.end()) ? "" : trim(valIt->second);
        if (raw.empty()) continue;   // bos birakilan opsiyonel/zorunlu alan: Python'a hic gonderilmez (bkz. block_specs.cpp promptForParams'daki ayni kural)

        switch (p.type) {
            case ParamType::String: params[p.key] = JsonValue(raw); break;
            case ParamType::Number: params[p.key] = parseNumber(raw); break;
            case ParamType::Bool:   params[p.key] = parseBool(raw); break;
            case ParamType::StringList: {
                JsonValue list = parseStringList(raw);
                if (list.size() > 0) params[p.key] = list;   // tamamen bos parse sonucu (orn. sadece ",") yine "verilmemis" sayilir
                break;
            }
        }
    }
    return params;
}

void GuiApp::applyParamsForSelectedNode() {
    if (isRunning_.load()) return;   // calisirken parametre degistirilemez (bkz. THREAD MODELI)
    if (selectedNodeId_.empty()) return;
    const Node* node = engine_.getNode(selectedNodeId_);
    if (!node) return;
    const std::map<std::string, BlockSpec>& registry = blockRegistrySpecs();
    std::map<std::string, BlockSpec>::const_iterator specIt = registry.find(node->block);
    if (specIt == registry.end()) return;

    JsonValue params = buildParamsJson(specIt->second, selectedNodeId_);
    try {
        engine_.setParams(selectedNodeId_, params);
        lastErrorMessage_.clear();
        refreshDisplayCache();   // DIRTY propagation'in etkisi HEMEN bu karede de gorunsun
    } catch (const std::exception& e) {
        lastErrorMessage_ = e.what();
    }
}

void GuiApp::startRun() {
    if (isRunning_.load()) return;   // zaten calisiyorsa ikinci bir calistirma baslatma
    if (runThread_.joinable()) runThread_.join();   // onceki thread bitmis olmali, guvenlik icin join

    {
        std::lock_guard<std::mutex> lock(progressMutex_);
        liveProgress_.clear();
        liveProgressLog_.clear();
        workerErrorMessage_.clear();
    }
    isRunning_ = true;

    /* engine_'i BURADAN itibaren SADECE bu yeni thread kullanacak - UI
     * thread, isRunning_ tekrar false olana kadar engine_'e (ya da
     * process_'e) dokunmuyor (bkz. gui_app.h basindaki THREAD MODELI). */
    runThread_ = std::thread([this]() {
        PipelineEngine::ProgressCallback onProgress =
            [this](const std::string& nodeId, const JsonValue& progress) {
                std::lock_guard<std::mutex> lock(progressMutex_);
                liveProgress_[nodeId] = progress;
                liveProgressLog_[nodeId].push_back(progress);   // adim adim biriktir - sag paneldeki canli log bunu okuyor
            };
        try {
            engine_.runAll(onProgress);
        } catch (const std::exception& e) {
            std::lock_guard<std::mutex> lock(progressMutex_);
            workerErrorMessage_ = e.what();
        }
        isRunning_ = false;             // bundan sonra UI thread engine_'e tekrar dokunabilir
        previewRefreshPending_ = true;    // UI thread'e "onizlemeleri yenile" sinyali (bkz. gui_app.h'deki aciklama)
    });
}

void GuiApp::handleNewLinks() {
    if (isRunning_.load()) return;   // calisirken yeni baglanti kurulamaz (bkz. THREAD MODELI)

    int startPin, endPin;
    if (!ImNodes::IsLinkCreated(&startPin, &endPin)) return;

    std::map<int, PinInfo>::iterator aIt = pinInfo_.find(startPin);
    std::map<int, PinInfo>::iterator bIt = pinInfo_.find(endPin);
    if (aIt == pinInfo_.end() || bIt == pinInfo_.end()) return;

    const PinInfo& a = aIt->second;
    const PinInfo& b = bIt->second;
    if (a.isInput == b.isInput) return;   // ayni turden iki pin (girdi-girdi ya da cikis-cikis) baglanamaz, ImNodes normalde zaten engeller ama emin olalim

    const PinInfo& inPin = a.isInput ? a : b;
    const PinInfo& outPin = a.isInput ? b : a;

    try {
        engine_.connect(inPin.nodeId, inPin.slot, outPin.nodeId, outPin.slot);
    } catch (const std::exception& e) {
        lastErrorMessage_ = e.what();
        return;
    }

    std::string key = inPin.nodeId + ":" + inPin.slot;
    int linkId = allocId();
    LinkInfo info; info.toNode = inPin.nodeId; info.toSlot = inPin.slot; info.fromNode = outPin.nodeId; info.fromSlot = outPin.slot;
    linkInfo_[linkId] = info;
    linkIdForInputSlot_[key] = linkId;   // ayni input slotunda ONCEDEN baska bir link vardiysa, artik cizilmeyecek (sadece bu yeni id gosterilir) - engine_.connect zaten eski baglantinin ustune yazdi
    lastErrorMessage_.clear();
    refreshDisplayCache();   // DIRTY propagation'in etkisi HEMEN bu karede de gorunsun
}

void GuiApp::handleDestroyedLinks() {
    if (isRunning_.load()) return;   // calisirken baglanti sokulemez (bkz. THREAD MODELI)

    int linkId;
    if (!ImNodes::IsLinkDestroyed(&linkId)) return;

    std::map<int, LinkInfo>::iterator it = linkInfo_.find(linkId);
    if (it == linkInfo_.end()) return;

    try {
        engine_.disconnect(it->second.toNode, it->second.toSlot);
        refreshDisplayCache();
    } catch (const std::exception& e) {
        lastErrorMessage_ = e.what();
    }
    linkIdForInputSlot_.erase(it->second.toNode + ":" + it->second.toSlot);
    linkInfo_.erase(it);
}

void GuiApp::handleNodeDeletion() {
    if (isRunning_.load()) return;   // calisirken node silinemez (bkz. THREAD MODELI)
    if (!ImGui::IsKeyPressed(ImGuiKey_Delete)) return;
    int numSelected = ImNodes::NumSelectedNodes();
    if (numSelected <= 0) return;

    std::vector<int> ids(static_cast<size_t>(numSelected));
    ImNodes::GetSelectedNodes(ids.data());
    for (size_t i = 0; i < ids.size(); i++) {
        std::map<int, std::string>::iterator it = nodeIntToStr_.find(ids[i]);
        if (it != nodeIntToStr_.end()) deleteNode(it->second);
    }
}

void GuiApp::drawPalette() {
    ImGui::TextUnformatted("Bloklar (tiklayinca eklenir)");
    ImGui::Separator();
    if (isRunning_.load()) ImGui::BeginDisabled();
    const std::map<std::string, BlockSpec>& registry = blockRegistrySpecs();
    for (std::map<std::string, BlockSpec>::const_iterator it = registry.begin(); it != registry.end(); ++it) {
        if (ImGui::Button(it->first.c_str(), ImVec2(-1, 0))) {
            createNode(it->first);
        }
    }
    if (isRunning_.load()) ImGui::EndDisabled();
}

void GuiApp::drawNodeContents(const std::string& nodeId, const DisplayNodeInfo& node) {
    NodeUiState& ui = uiState_[nodeId];

    ImNodes::BeginNodeTitleBar();
    ImGui::TextUnformatted(nodeId.c_str());
    ImGui::TextDisabled("%s", nodeStateToString(node.state).c_str());
    ImNodes::EndNodeTitleBar();

    for (std::map<std::string, int>::iterator it = ui.inputPinId.begin(); it != ui.inputPinId.end(); ++it) {
        ImNodes::BeginInputAttribute(it->second);
        ImGui::TextUnformatted(it->first.c_str());
        ImNodes::EndInputAttribute();
    }
    for (std::map<std::string, int>::iterator it = ui.outputPinId.begin(); it != ui.outputPinId.end(); ++it) {
        ImNodes::BeginOutputAttribute(it->second);
        ImGui::TextUnformatted(it->first.c_str());
        ImNodes::EndOutputAttribute();
    }

    /* Ozellik B: bu node egitim gibi ilerleme bildiren bir blok calistiriyorsa
     * (su an SADECE mlp_learner), en son epoch bilgisini node kutusunun
     * icinde canli goster. */
    {
        std::lock_guard<std::mutex> lock(progressMutex_);
        std::map<std::string, JsonValue>::const_iterator progIt = liveProgress_.find(nodeId);
        if (progIt != liveProgress_.end()) {
            const JsonValue& p = progIt->second;
            ImGui::TextColored(ImVec4(0.6f, 0.85f, 1.0f, 1.0f), "epoch %d/%d - loss: %.4f",
                                p.has("epoch") ? p.at("epoch").asInt() : 0,
                                p.has("epochs") ? p.at("epochs").asInt() : 0,
                                p.has("loss") ? p.at("loss").asNumber() : 0.0);
        }
    }

    if (node.state == NodeState::ERROR_STATE && !node.lastError.empty()) {
        ImGui::PushTextWrapPos(ImGui::GetCursorPos().x + 180.0f);
        ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.6f, 1.0f), "%s", node.lastError.c_str());
        ImGui::PopTextWrapPos();
    }
}

void GuiApp::drawCanvas() {
    ImNodes::BeginNodeEditor();

    for (std::map<std::string, int>::iterator it = nodeStrToInt_.begin(); it != nodeStrToInt_.end(); ++it) {
        const std::string& nodeId = it->first;
        std::map<std::string, DisplayNodeInfo>::const_iterator cacheIt = displayCache_.find(nodeId);
        if (cacheIt == displayCache_.end()) continue;   // henuz cache'e girmemis (ayni karede eklenmis, bir sonraki karede gorunur)
        const DisplayNodeInfo& node = cacheIt->second;

        ImU32 normal, hovered, selected;
        stateColors(node.state, normal, hovered, selected);
        ImNodes::PushColorStyle(ImNodesCol_TitleBar, normal);
        ImNodes::PushColorStyle(ImNodesCol_TitleBarHovered, hovered);
        ImNodes::PushColorStyle(ImNodesCol_TitleBarSelected, selected);

        ImNodes::BeginNode(it->second);
        drawNodeContents(nodeId, node);
        ImNodes::EndNode();

        ImNodes::PopColorStyle();
        ImNodes::PopColorStyle();
        ImNodes::PopColorStyle();
    }

    for (std::map<std::string, int>::iterator it = linkIdForInputSlot_.begin(); it != linkIdForInputSlot_.end(); ++it) {
        int linkId = it->second;
        const LinkInfo& info = linkInfo_[linkId];
        std::map<std::string, NodeUiState>::iterator fromUi = uiState_.find(info.fromNode);
        std::map<std::string, NodeUiState>::iterator toUi = uiState_.find(info.toNode);
        if (fromUi == uiState_.end() || toUi == uiState_.end()) continue;
        std::map<std::string, int>::iterator outPinIt = fromUi->second.outputPinId.find(info.fromSlot);
        std::map<std::string, int>::iterator inPinIt = toUi->second.inputPinId.find(info.toSlot);
        if (outPinIt == fromUi->second.outputPinId.end() || inPinIt == toUi->second.inputPinId.end()) continue;
        ImNodes::Link(linkId, outPinIt->second, inPinIt->second);
    }

    ImNodes::EndNodeEditor();

    handleNewLinks();
    handleDestroyedLinks();
    handleNodeDeletion();

    /* ImNodes'ta bir node'a tiklamak "secim" (selection) sayilir - bu secimi
     * sag paneldeki inspector'un hangi node'u gosterecegine cevirelim. */
    int numSelected = ImNodes::NumSelectedNodes();
    if (numSelected == 1) {
        int id;
        ImNodes::GetSelectedNodes(&id);
        std::map<int, std::string>::iterator it = nodeIntToStr_.find(id);
        if (it != nodeIntToStr_.end()) selectedNodeId_ = it->second;
    }
}

void GuiApp::drawInspector() {
    if (selectedNodeId_.empty()) {
        ImGui::TextDisabled("Bir node secmek icin tuvaldeki kutulardan birine tikla.");
        return;
    }
    std::map<std::string, DisplayNodeInfo>::const_iterator cacheIt = displayCache_.find(selectedNodeId_);
    if (cacheIt == displayCache_.end()) { selectedNodeId_.clear(); return; }
    const DisplayNodeInfo& node = cacheIt->second;

    ImGui::TextColored(ImVec4(0.9f, 0.9f, 1.0f, 1.0f), "%s", selectedNodeId_.c_str());
    ImGui::TextDisabled("blok: %s | durum: %s", node.block.c_str(), nodeStateToString(node.state).c_str());
    ImGui::Separator();

    const std::map<std::string, BlockSpec>& registry = blockRegistrySpecs();
    std::map<std::string, BlockSpec>::const_iterator specIt = registry.find(node.block);
    bool producesDataFrame = specIt != registry.end() && specIt->second.producesDataFrame;

    if (isRunning_.load()) ImGui::BeginDisabled();

    if (specIt != registry.end() && !specIt->second.params.empty()) {
        ImGui::TextUnformatted("Parametreler");
        std::map<std::string, std::string>& buf = paramTextBuffers_[selectedNodeId_];
        for (size_t i = 0; i < specIt->second.params.size(); i++) {
            const ParamSpec& p = specIt->second.params[i];
            std::string label = p.key + (p.required ? " (ZORUNLU)" : "");
            char textBuf[256];
            std::string current = buf.count(p.key) ? buf[p.key] : "";
            size_t n = current.copy(textBuf, sizeof(textBuf) - 1);
            textBuf[n] = '\0';
            if (ImGui::InputText(label.c_str(), textBuf, sizeof(textBuf))) {
                buf[p.key] = textBuf;
            }
        }
        if (ImGui::Button("Parametreleri Uygula")) {
            applyParamsForSelectedNode();
        }
        ImGui::Separator();
    }

    if (isRunning_.load()) ImGui::EndDisabled();

    /* Egitim SURERKEN (mlp_learner/deep_mlp_learner gibi report_progress
     * cagiran bloklarda), her epoch geldikce BURAYA (Cikti panelinin
     * ustune) adim adim ekleniyor - node.outputs (nihai sonuc) sadece
     * calisma BITTIKTEN sonra dolar, bu yuzden akan epoch'lari gormek
     * icin ayri, canli bir log gerekiyor. */
    if (isRunning_.load()) {
        std::vector<JsonValue> logCopy;
        {
            std::lock_guard<std::mutex> lock(progressMutex_);
            std::map<std::string, std::vector<JsonValue> >::const_iterator logIt = liveProgressLog_.find(selectedNodeId_);
            if (logIt != liveProgressLog_.end()) logCopy = logIt->second;
        }
        if (!logCopy.empty()) {
            ImGui::TextColored(ImVec4(0.6f, 0.85f, 1.0f, 1.0f), "Canli egitim logu (%d epoch):", static_cast<int>(logCopy.size()));
            ImGui::BeginChild("live_training_log", ImVec2(0.0f, 160.0f), true, ImGuiWindowFlags_HorizontalScrollbar);
            for (size_t i = 0; i < logCopy.size(); i++) {
                const JsonValue& p = logCopy[i];
                char lineBuf[128];
                if (p.has("learning_rate")) {
                    snprintf(lineBuf, sizeof(lineBuf), "epoch %d/%d - loss: %.4f - lr: %.6f",
                              p.has("epoch") ? p.at("epoch").asInt() : 0,
                              p.has("epochs") ? p.at("epochs").asInt() : 0,
                              p.has("loss") ? p.at("loss").asNumber() : 0.0,
                              p.at("learning_rate").asNumber());
                } else {
                    snprintf(lineBuf, sizeof(lineBuf), "epoch %d/%d - loss: %.4f",
                              p.has("epoch") ? p.at("epoch").asInt() : 0,
                              p.has("epochs") ? p.at("epochs").asInt() : 0,
                              p.has("loss") ? p.at("loss").asNumber() : 0.0);
                }
                ImGui::TextUnformatted(lineBuf);
            }
            ImGui::SetScrollHereY(1.0f);   // yeni satir geldikce en alta kilitli kal (canli konsol gibi)
            ImGui::EndChild();
            ImGui::Separator();
        }
    }

    ImGui::TextUnformatted("Cikti");
    if (node.outputs.empty()) {
        ImGui::TextDisabled("(henuz calistirilmadi)");
    } else {
        for (std::map<std::string, OutputSlot>::const_iterator it = node.outputs.begin(); it != node.outputs.end(); ++it) {
            if (ImGui::CollapsingHeader(it->first.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
                renderNodeOutputImGui(selectedNodeId_, it->first, it->second.meta);

                /* Ozellik A: bu slot bir DataFrame ise, meta'nin ustune
                 * (varsa) otomatik onizlemeyi VE bir "CSV'ye Aktar" butonunu
                 * ekle. */
                if (producesDataFrame && node.block != "data_preview") {
                    std::string cacheKey = selectedNodeId_ + ":" + it->first;
                    std::map<std::string, JsonValue>::const_iterator prevIt = previewCache_.find(cacheKey);
                    if (prevIt != previewCache_.end()) {
                        ImGui::Separator();
                        ImGui::TextDisabled("Otomatik onizleme (ilk birkac satir):");
                        renderNodeOutputImGui(selectedNodeId_, it->first, prevIt->second);
                    }

                    if (isRunning_.load()) ImGui::BeginDisabled();
                    std::string buttonLabel = "CSV'ye Aktar##" + cacheKey;
                    if (ImGui::Button(buttonLabel.c_str())) {
                        std::string exportPath = "exports/" + selectedNodeId_ + "_" + it->first + ".csv";
                        JsonValue response = engine_.exportCsv(it->second.ref, exportPath);
                        if (response.has("status") && response.at("status").asString() == "ok") {
                            int rowCount = response.has("row_count") ? response.at("row_count").asInt() : 0;
                            lastExportMessage_ = exportPath + " (" + std::to_string(rowCount) + " satir) yazildi";
                        } else {
                            lastErrorMessage_ = response.has("message") ? response.at("message").asString() : "export basarisiz";
                        }
                    }
                    if (isRunning_.load()) ImGui::EndDisabled();
                    if (!lastExportMessage_.empty()) {
                        ImGui::TextDisabled("%s", lastExportMessage_.c_str());
                    }
                }
            }
        }
    }

    ImGui::Separator();
    if (isRunning_.load()) ImGui::BeginDisabled();
    if (ImGui::Button("Bu Node'u Sil")) {
        deleteNode(selectedNodeId_);
    }
    if (isRunning_.load()) ImGui::EndDisabled();
}

void GuiApp::render() {
    /* engine_'in guncel durumunu SADECE calismiyorken oku (bkz. gui_app.h
     * basindaki THREAD MODELI) - cizim kodu ("drawCanvas"/"drawInspector")
     * bundan sonra HEP displayCache_'ten okuyacak, engine_'e dogrudan
     * dokunmayacak. */
    bool runningNow = isRunning_.load();
    if (!runningNow) {
        refreshDisplayCache();
        /* previewRefreshPending_.exchange(false): "true ise oku VE hemen
         * false'a cevir" - tek bir atomik islemde. Boylece worker thread
         * TAM OLARAK ne zaman bitirdiyse bitirsin (hizli bir blok icin tek
         * karenin icinde bile olsa), bu bayrak kaybolmadan bir sonraki
         * !isRunning_ karesinde yakalanir - DataFrame onizlemeleri
         * (Ozellik A) SADECE bu gecişte bir kez yenilenir, her karede degil
         * (Python'a gereksiz istek atmamak icin). */
        if (previewRefreshPending_.exchange(false)) {
            refreshPreviewsForUpToDateNodes();
        }
    }

    {
        std::lock_guard<std::mutex> lock(progressMutex_);
        if (!workerErrorMessage_.empty()) {
            lastErrorMessage_ = workerErrorMessage_;
            workerErrorMessage_.clear();
        }
    }

    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                              ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                              ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;
    ImGui::Begin("##main", NULL, flags);

    if (runningNow) ImGui::BeginDisabled();
    if (ImGui::Button(runningNow ? "Calisiyor..." : "Calistir (Run All)")) {
        startRun();
    }
    if (runningNow) ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::TextDisabled(runningNow ? "Egitim/blok calisirken grafik duzenlenemez" : "Sadece DIRTY/NOT_RUN node'lar calisir");
    if (!lastErrorMessage_.empty()) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.5f, 1.0f), "Hata: %s", lastErrorMessage_.c_str());
    }
    ImGui::Separator();

    float h = ImGui::GetContentRegionAvail().y;
    float w = ImGui::GetContentRegionAvail().x;

    ImGui::BeginChild("palette_panel", ImVec2(220.0f, h), true);
    drawPalette();
    ImGui::EndChild();

    ImGui::SameLine();
    ImGui::BeginChild("canvas_panel", ImVec2(w - 220.0f - 380.0f - 16.0f, h), true);
    drawCanvas();
    ImGui::EndChild();

    ImGui::SameLine();
    ImGui::BeginChild("inspector_panel", ImVec2(0, h), true);
    drawInspector();
    ImGui::EndChild();

    ImGui::End();
}
