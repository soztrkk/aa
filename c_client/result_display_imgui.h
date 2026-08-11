/*
 * result_display_imgui.h
 *
 * result_display.h ile AYNI fikir (meta'nin "output_type" alanina bakip
 * dogru gorunumu secmek), ama std::cout yerine ImGui widget'lari uzerinden.
 * result_display.cpp'ye DOKUNULMADI - pipeline_client.exe/interactive_client.exe
 * eskisi gibi calismaya devam ediyor, GUI kendi paralel fonksiyonunu kullaniyor.
 */

#ifndef RESULT_DISPLAY_IMGUI_H
#define RESULT_DISPLAY_IMGUI_H

#include <string>
#include "json_value.h"

/* Tek bir node/slot ciktisini, su an acik olan ImGui penceresi/paneli icine
 * cizer (ImGui::Begin/End cagirmaz - cagiran taraf zaten bir pencere/panel
 * icinde oldugunu varsayar). meta'nin "output_type" alanina gore table/
 * summary/message/generic gorunumlerinden birini secer; "chart" icin v1'de
 * sadece bir placeholder metni gosterir (gercek Plotly render'i ileriye
 * birakildi). */
void renderNodeOutputImGui(const std::string& nodeId, const std::string& slot, const JsonValue& meta);

#endif // RESULT_DISPLAY_IMGUI_H
