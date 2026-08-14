/*
 * result_display.h
 *
 * ADIM 4: "Kullaniciya ne gosterilecek" katmani.
 *
 * ONEMLI TASARIM KARARI: Python tarafi HICBIR ZAMAN tam veriyi
 * (DataFrame'in tamami, tum satirlar) C++'a göndermez - CLAUDE.md'de
 * tanimli protokolde her cevap sadece "ref" (SessionStore anahtari) ve
 * "meta" (kucuk/ozet bilgi) icerir. Yani "sadece ilk 5 satir gorunsun"
 * kurali aslinda C++ tarafinda degil, Python tarafindaki blok/meta
 * tasariminda zaten uygulaniyor:
 *
 *   - data_preview, dataset_summary, describe_statistics vb. "view"
 *     bloklari (bkz. blocks/graphs_and_views.py) meta icine bir
 *     "output_type" alani koyuyor: "table" | "summary" | "message" | "chart"
 *     ve records/columns gibi alanlar ZATEN SINIRLI (orn. sadece 5 satir,
 *     sadece describe() ciktisi) - hicbir zaman ham DataFrame degil.
 *   - normal islem bloklari (load_csv, handle_missing_values, ...) meta
 *     icine sadece "shape" (satir,kolon sayisi) ve "columns" (kolon adi
 *     listesi) koyuyor - veri hucrelerinin kendisi degil.
 *   - compute_classification_metrics / compute_regression_metrics gibi
 *     bloklarin meta'si zaten kucuk sayisal degerler (accuracy, f1, ...).
 *
 * Bu dosyadaki renderNodeOutput(), gelen meta'nin SEKLINE bakarak
 * (output_type alani var mi, shape/columns var mi, yoksa duz bir
 * sayisal/metin sozluk mu) otomatik olarak DOGRU gorunumu secer.
 * Yani "df ise ilk 5 satir, score ise score" kurali burada, meta'nin
 * tasidigi bilgiye gore uygulanir.
 */

#ifndef RESULT_DISPLAY_H   // bu basligin ayni derlemede iki kez islenmesini engeller
#define RESULT_DISPLAY_H   // include guard makrosunu tanimlar

#include <string>       // std::string icin
#include "json_value.h"   // meta parametresi icin JsonValue

/* Tek bir node/slot ciktisini ekrana bicimli sekilde basar.
 * nodeId/slot sadece basliklandirma icin kullanilir, meta asil
 * gosterilecek veridir. */
void renderNodeOutput(const std::string& nodeId, const std::string& slot, const JsonValue& meta);   // meta'nin sekline gore uygun gorunumu secip yazdirir

#endif // RESULT_DISPLAY_H  -- basligin sonu
