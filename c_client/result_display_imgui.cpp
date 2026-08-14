/*
 * result_display_imgui.cpp
 */

#include "result_display_imgui.h"
#include "imgui.h"
#include <cstdio>

namespace {

const size_t MAX_STRING_LEN = 300;   // result_display.cpp'deki ayni sinirlarin GUI karsiligi
const size_t MAX_ARRAY_ITEMS = 20;

std::string truncateText(const std::string& s, size_t maxLen) {
    if (s.size() <= maxLen) return s;
    return s.substr(0, maxLen) + "...(kirpildi)";
}

/* Herhangi bir JsonValue'yu kisa/okunakli TEK BIR STRING'e cevirir - ImGui
 * tarafinda std::cout yerine bunu ImGui::TextWrapped("%s", ...) ile basiyoruz. */
std::string valueToCompactString(const JsonValue& v) {
    switch (v.type()) {
        case JsonValue::Type::Null:
            return "null";
        case JsonValue::Type::Bool:
            return v.asBool() ? "true" : "false";
        case JsonValue::Type::Number:
            return v.dump();
        case JsonValue::Type::String:
            return truncateText(v.asString(), MAX_STRING_LEN);
        case JsonValue::Type::Array: {
            std::string out = "[";
            size_t n = v.size();
            size_t limit = n < MAX_ARRAY_ITEMS ? n : MAX_ARRAY_ITEMS;
            for (size_t i = 0; i < limit; i++) {
                if (i > 0) out += ", ";
                out += valueToCompactString(v[i]);
            }
            if (n > MAX_ARRAY_ITEMS) {
                out += ", ... (+" + std::to_string(n - MAX_ARRAY_ITEMS) + " tane daha)";
            }
            out += "]";
            return out;
        }
        case JsonValue::Type::Object: {
            std::string out = "{";
            bool first = true;
            for (std::map<std::string, JsonValue>::const_iterator it = v.fields().begin();
                 it != v.fields().end(); ++it) {
                if (!first) out += ", ";
                first = false;
                out += it->first + ": " + valueToCompactString(it->second);
            }
            out += "}";
            return out;
        }
    }
    return "";
}

void renderGenericFallback(const JsonValue& meta) {
    if (!meta.isObject() || meta.fields().empty()) {
        ImGui::TextDisabled("(bos meta)");
        return;
    }
    for (std::map<std::string, JsonValue>::const_iterator it = meta.fields().begin();
         it != meta.fields().end(); ++it) {
        std::string line = it->first + ": " + valueToCompactString(it->second);
        ImGui::TextWrapped("%s", line.c_str());
    }
}

void renderTable(const JsonValue& meta) {
    if (meta.has("title")) {
        ImGui::TextColored(ImVec4(0.8f, 0.85f, 1.0f, 1.0f), "%s", meta.at("title").asString().c_str());
    }
    if (!meta.has("columns") || !meta.has("records")) {
        renderGenericFallback(meta);
        return;
    }

    const JsonValue& columns = meta.at("columns");
    const JsonValue& records = meta.at("records");
    int colCount = static_cast<int>(columns.size());
    if (colCount == 0) { renderGenericFallback(meta); return; }

    ImGuiTableFlags flags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollX | ImGuiTableFlags_Resizable;
    std::string tableId = "table_" + (meta.has("title") ? meta.at("title").asString() : std::string("data"));
    if (ImGui::BeginTable(tableId.c_str(), colCount, flags)) {
        for (int c = 0; c < colCount; c++) {
            ImGui::TableSetupColumn(columns[static_cast<size_t>(c)].asString().c_str());
        }
        ImGui::TableHeadersRow();

        for (size_t r = 0; r < records.size(); r++) {
            const JsonValue& record = records[r];
            ImGui::TableNextRow();
            for (int c = 0; c < colCount; c++) {
                ImGui::TableSetColumnIndex(c);
                std::string colName = columns[static_cast<size_t>(c)].asString();
                std::string cell = record.has(colName) ? valueToCompactString(record.at(colName)) : "-";
                ImGui::TextUnformatted(cell.c_str());
            }
        }
        ImGui::EndTable();
    }

    if (meta.has("row_count")) {
        ImGui::TextDisabled("(toplam %d satir)", meta.at("row_count").asInt());
    }
}

void renderSummary(const JsonValue& meta) {
    if (meta.has("title")) {
        ImGui::TextColored(ImVec4(0.8f, 0.85f, 1.0f, 1.0f), "%s", meta.at("title").asString().c_str());
    }
    for (std::map<std::string, JsonValue>::const_iterator it = meta.fields().begin();
         it != meta.fields().end(); ++it) {
        if (it->first == "title" || it->first == "output_type") continue;
        if (it->first == "columns" && it->second.isArray()) {
            if (ImGui::CollapsingHeader("columns")) {
                for (size_t i = 0; i < it->second.size(); i++) {
                    std::string line = valueToCompactString(it->second[i]);
                    ImGui::BulletText("%s", line.c_str());
                }
            }
            continue;
        }
        std::string line = it->first + ": " + valueToCompactString(it->second);
        ImGui::TextWrapped("%s", line.c_str());
    }
}

void renderMessage(const JsonValue& meta) {
    if (meta.has("title")) {
        ImGui::TextColored(ImVec4(0.8f, 0.85f, 1.0f, 1.0f), "%s", meta.at("title").asString().c_str());
    }
    if (meta.has("message")) {
        ImGui::TextWrapped("%s", meta.at("message").asString().c_str());
    }
}

/* mlp_learner / deep_mlp_learner ciktisi: egitim ozeti + epoch-epoch loss
 * (varsa lr) - konsol/log gorunumu gibi, kaydirilabilir kucuk bir alanda. */
void renderTrainingLog(const JsonValue& meta) {
    // once loss_history/lr_history disindaki alanlari kisa bir ozet olarak yaz
    for (std::map<std::string, JsonValue>::const_iterator it = meta.fields().begin();
         it != meta.fields().end(); ++it) {
        if (it->first == "loss_history" || it->first == "lr_history" || it->first == "output_type") continue;
        std::string line = it->first + ": " + valueToCompactString(it->second);
        ImGui::TextWrapped("%s", line.c_str());
    }

    if (!meta.has("loss_history") || !meta.at("loss_history").isArray()) {
        return;
    }

    const JsonValue& losses = meta.at("loss_history");
    bool hasLr = meta.has("lr_history") && meta.at("lr_history").isArray();
    const JsonValue* lrs = hasLr ? &meta.at("lr_history") : NULL;

    ImGui::Separator();
    ImGui::TextDisabled("Egitim logu (%d epoch):", static_cast<int>(losses.size()));
    ImGui::BeginChild("training_log_child", ImVec2(0.0f, 160.0f), true, ImGuiWindowFlags_HorizontalScrollbar);
    for (size_t i = 0; i < losses.size(); i++) {
        char lineBuf[128];
        double lossVal = losses[i].asNumber();
        if (lrs != NULL && i < lrs->size()) {
            double lrVal = (*lrs)[i].asNumber();
            snprintf(lineBuf, sizeof(lineBuf), "epoch %4d - loss: %.4f - lr: %.6f",
                      static_cast<int>(i + 1), lossVal, lrVal);
        } else {
            snprintf(lineBuf, sizeof(lineBuf), "epoch %4d - loss: %.4f", static_cast<int>(i + 1), lossVal);
        }
        ImGui::TextUnformatted(lineBuf);
    }
    ImGui::EndChild();
}

/* v1 kapsam karari (bkz. plan): gercek Plotly figure_json render'i yok,
 * sadece grafik turu + kucuk bir bilgi notu gosteriliyor. */
void renderChartPlaceholder(const JsonValue& meta) {
    if (meta.has("title")) {
        ImGui::TextColored(ImVec4(0.8f, 0.85f, 1.0f, 1.0f), "%s (grafik)", meta.at("title").asString().c_str());
    }
    if (meta.has("chart_type")) {
        ImGui::Text("chart_type: %s", meta.at("chart_type").asString().c_str());
    }
    ImGui::TextDisabled("grafik onizlemesi bu surumde desteklenmiyor (v1 kapsam disi)");
}

} // anonymous namespace

void renderNodeOutputImGui(const std::string& nodeId, const std::string& slot, const JsonValue& meta) {
    ImGui::PushID((nodeId + ":" + slot).c_str());   // ayni etiketli widget'lar (orn. tablo id'leri) farkli node/slot'larda catismasin

    std::string outputType = meta.has("output_type") ? meta.at("output_type").asString() : "";
    if (outputType == "table") {
        renderTable(meta);
    } else if (outputType == "summary") {
        renderSummary(meta);
    } else if (outputType == "message") {
        renderMessage(meta);
    } else if (outputType == "chart") {
        renderChartPlaceholder(meta);
    } else if (outputType == "training_log") {
        renderTrainingLog(meta);
    } else {
        renderGenericFallback(meta);
    }

    ImGui::PopID();
}