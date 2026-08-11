/*
 * result_display.cpp
 */

#include "result_display.h"   // renderNodeOutput bildirimi
#include <iostream>              // std::cout
#include <iomanip>               // (bicimlendirme yardimcilari icin dahil edildi)

namespace {   // bu dosyaya ozel (disariya gorunmeyen) yardimci fonksiyonlar

const size_t MAX_STRING_LEN = 300;   // tek bir string alani en fazla bu kadar karakter basilir
const size_t MAX_ARRAY_ITEMS = 20;   // bir array'den en fazla bu kadar eleman basilir

std::string truncateText(const std::string& s, size_t maxLen) {
    if (s.size() <= maxLen) return s;                      // sinir asilmiyorsa oldugu gibi doner
    return s.substr(0, maxLen) + "...(kirpildi)";           // sinir asilmissa kirpip aciklama ekler
}

/* Herhangi bir JsonValue'yu, turune gore anlasilir/kisa bicimde ekrana basar.
 * Bu, "generic fallback" gorunumun kalbi: ozel bir output_type'i olmayan
 * her turlu meta (orn. {"shape": [..], "columns": [..]}) bu fonksiyonla
 * makul bir bicimde gosterilebilir. Buyuk array/string'ler KIRPILIR - boylece
 * beklenmedik sekilde buyuk bir alan gelse bile terminal tasmaz. */
void printValueCompact(const JsonValue& v) {
    switch (v.type()) {                                // JsonValue'nun turune gore farkli yazdirma yolu izle
        case JsonValue::Type::Null:
            std::cout << "null";                         // null oldugu gibi "null" basilir
            break;
        case JsonValue::Type::Bool:
            std::cout << (v.asBool() ? "true" : "false");   // bool "true"/"false" olarak basilir
            break;
        case JsonValue::Type::Number:
            std::cout << v.dump();                          // sayi icin JSON serialize mantigi yeniden kullanilir (int/float ayrimi dogru olsun diye)
            break;
        case JsonValue::Type::String:
            std::cout << truncateText(v.asString(), MAX_STRING_LEN);   // uzun string'ler kirpilarak basilir
            break;
        case JsonValue::Type::Array: {
            std::cout << "[";                              // diziyi acan parantez
            size_t n = v.size();                             // toplam eleman sayisi
            size_t limit = n < MAX_ARRAY_ITEMS ? n : MAX_ARRAY_ITEMS;   // en fazla MAX_ARRAY_ITEMS kadar basilacak
            for (size_t i = 0; i < limit; i++) {
                if (i > 0) std::cout << ", ";                 // elemanlar arasina virgul
                printValueCompact(v[i]);                        // her elemani ozyinelemeli olarak bas
            }
            if (n > MAX_ARRAY_ITEMS) {                        // sinir asildiysa
                std::cout << ", ... (+" << (n - MAX_ARRAY_ITEMS) << " tane daha)";   // kac tane daha oldugunu belirt
            }
            std::cout << "]";                              // diziyi kapatan parantez
            break;
        }
        case JsonValue::Type::Object: {
            std::cout << "{";                              // nesneyi acan parantez
            bool first = true;                               // ilk alan mi (virgul icin)
            for (std::map<std::string, JsonValue>::const_iterator it = v.fields().begin();
                 it != v.fields().end(); ++it) {
                if (!first) std::cout << ", ";                 // ilk alan degilse once virgul
                first = false;
                std::cout << it->first << ": ";                 // anahtari yaz
                printValueCompact(it->second);                    // degeri ozyinelemeli olarak bas
            }
            std::cout << "}";                              // nesneyi kapatan parantez
            break;
        }
    }
}

/* meta icindeki her alani "anahtar: deger" seklinde, ayri satirlarda basar.
 * output_type'i olmayan (ya da taniyamadigimiz) her meta icin kullanilan
 * son care (fallback) gorunum. */
void renderGenericFallback(const JsonValue& meta) {
    if (!meta.isObject() || meta.fields().empty()) {   // meta bir nesne degilse ya da alani yoksa
        std::cout << "    (bos meta)\n";                 // gosterilecek bir sey olmadigini belirt
        return;
    }
    for (std::map<std::string, JsonValue>::const_iterator it = meta.fields().begin();
         it != meta.fields().end(); ++it) {               // tum alanlari (alfabetik/map sirasiyla) gez
        std::cout << "    " << it->first << ": ";           // "anahtar: " yaz
        printValueCompact(it->second);                        // degeri kisa/kirpilmis bicimde bas
        std::cout << "\n";
    }
}

/* output_type == "table": data_preview, describe_statistics,
 * missing_values_report gibi bloklarin ciktisi. "columns" sirasina gore,
 * her "records" elemanini bir satir olarak basar. Satir sayisi zaten
 * Python tarafinda sinirlandirildigi icin (orn. data_preview varsayilan
 * 5 satir) burada AYRICA bir sinirlama yapmiyoruz. */
void renderTable(const JsonValue& meta) {
    if (meta.has("title")) {                                      // baslik alani varsa
        std::cout << "  == " << meta.at("title").asString() << " ==\n";   // basta ayri bir baslik satiri olarak bas
    }

    if (!meta.has("columns") || !meta.has("records")) {            // tablo icin gerekli alanlar eksikse
        renderGenericFallback(meta);                                  // guvenli yola (genel gorunum) dus
        return;
    }

    const JsonValue& columns = meta.at("columns");   // kolon adlari dizisi
    const JsonValue& records = meta.at("records");    // satir (kayit) nesneleri dizisi

    // baslik satiri
    std::cout << "   ";
    for (size_t c = 0; c < columns.size(); c++) {      // her kolon adi icin
        if (c > 0) std::cout << " | ";                   // kolonlar arasina ayrac
        std::cout << columns[c].asString();               // kolon adini yaz
    }
    std::cout << "\n";

    for (size_t r = 0; r < records.size(); r++) {         // her satir (kayit) icin
        const JsonValue& record = records[r];
        std::cout << "   ";
        for (size_t c = 0; c < columns.size(); c++) {       // ayni kolon sirasina gore
            if (c > 0) std::cout << " | ";                    // kolonlar arasina ayrac
            std::string colName = columns[c].asString();
            if (record.has(colName)) {                          // bu kayitta o kolon varsa
                printValueCompact(record.at(colName));             // degerini bas
            } else {
                std::cout << "-";                                 // yoksa (eksik veri) tire ile goster
            }
        }
        std::cout << "\n";
    }

    if (meta.has("row_count")) {                            // toplam satir sayisi bilgisi varsa
        std::cout << "   (toplam " << meta.at("row_count").asInt() << " satir)\n";   // (goruntulenen satirlardan farkli olabilir, bilgi amacli)
    }
}

/* output_type == "summary": dataset_summary gibi bloklarin ciktisi.
 * "title" ve "columns" (nested detay listesi) disindaki her alani
 * duz basar, "columns" varsa (kolon detaylari) ayri bir alt tablo gibi basar. */
void renderSummary(const JsonValue& meta) {
    if (meta.has("title")) {                                  // baslik alani varsa
        std::cout << "  == " << meta.at("title").asString() << " ==\n";
    }
    for (std::map<std::string, JsonValue>::const_iterator it = meta.fields().begin();
         it != meta.fields().end(); ++it) {                     // tum alanlari gez
        if (it->first == "title" || it->first == "output_type") continue;   // bunlar zaten ayrica islendi/anlamsiz, atla
        if (it->first == "columns" && it->second.isArray()) {                 // "columns" ozel olarak alt liste seklinde gosterilir
            std::cout << "    columns:\n";
            for (size_t i = 0; i < it->second.size(); i++) {                    // her kolon detayi icin
                std::cout << "      - ";
                printValueCompact(it->second[i]);                                 // kolon detayini bas
                std::cout << "\n";
            }
            continue;
        }
        std::cout << "    " << it->first << ": ";     // diger tum alanlar duz "anahtar: deger" olarak basilir
        printValueCompact(it->second);
        std::cout << "\n";
    }
}

/* output_type == "message": ornek, "eksik deger/tekrar eden satir yok" gibi
 * bilgilendirici mesajlar. */
void renderMessage(const JsonValue& meta) {
    if (meta.has("title")) {                                    // baslik alani varsa
        std::cout << "  == " << meta.at("title").asString() << " ==\n";
    }
    if (meta.has("message")) {                                   // asil mesaj metni varsa
        std::cout << "    " << meta.at("message").asString() << "\n";   // oldugu gibi bas
    }
}

/* output_type == "chart": Plotly figure_json TAM veri kadar buyuk
 * olabilir (grafik icin tum nokta koordinatlari vs.) - CLAUDE.md'nin
 * "buyuk veri C tarafina gitmez" prensibine uymasi icin bunu ASLA
 * ekrana basmiyoruz, sadece basligini ve boyutunu bildiriyoruz.
 * Gercek UI'da bu asama, figure_json'i dogrudan bir grafik kutuphanesine
 * (orn. bir WebView icinde Plotly.js) verip render eder. */
void renderChart(const JsonValue& meta) {
    if (meta.has("title")) {                                    // baslik alani varsa
        std::cout << "  == " << meta.at("title").asString() << " (grafik) ==\n";
    }
    if (meta.has("chart_type")) {                                 // grafik turu bilgisi varsa (orn. "bar", "scatter")
        std::cout << "    chart_type: " << meta.at("chart_type").asString() << "\n";
    }
    if (meta.has("figure_json")) {                                 // Plotly figure_json (potansiyel olarak buyuk veri) varsa
        std::cout << "    figure_json alindi (" << meta.at("figure_json").asString().size()
                  << " karakter) - burada YAZDIRILMIYOR, gercek UI'da grafik olarak cizilecek\n";   // icerigi hic ekrana basma, sadece boyutunu bildir
    }
}

} // anonymous namespace

void renderNodeOutput(const std::string& nodeId, const std::string& slot, const JsonValue& meta) {
    std::cout << "  [" << nodeId << ":" << slot << "]\n";   // her ciktinin basina hangi node:slot oldugunu yazan baslik

    std::string outputType = meta.has("output_type") ? meta.at("output_type").asString() : "";   // meta'nin turunu belirleyen alan (yoksa bos)

    if (outputType == "table") {                    // data_preview / describe_statistics vb.
        renderTable(meta);
    } else if (outputType == "summary") {             // dataset_summary vb.
        renderSummary(meta);
    } else if (outputType == "message") {              // bilgilendirici tek satirlik mesajlar
        renderMessage(meta);
    } else if (outputType == "chart") {                 // Plotly grafik ciktilari
        renderChart(meta);
    } else {
        // "output_type" alani yoksa: normal islem bloklari (shape/columns)
        // ya da metrik bloklari (accuracy/f1/...) - genel gorunum yeterli.
        renderGenericFallback(meta);
    }
}
