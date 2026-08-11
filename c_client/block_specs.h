/*
 * block_specs.h
 *
 * ADIM 6: interaktif test istemcisi icin blok parametre "sozlugu".
 *
 * FIKIR: her blok (orn. handle_missing_values) hangi parametreleri kabul
 * ediyor, hangileri zorunlu, hangi degerler gecerli - bunu Python
 * tarafindaki docstring'lerden (bkz. blocks/preprocessing.py,
 * blocks/encoding_scaling.py, blocks/view_blocks.py) okuyup burada
 * KUCUK BIR TABLO haline getirdik. interactive_main.cpp bu tabloya
 * bakarak terminalde soru soruyor, cevaplari JSON parametrelerine ceviriyor.
 *
 * Bu, gercek UI'da "blok paletinden bir blok surukle, sag panelde
 * parametre formu belirsin" davranisinin en basit/metin tabanli halidir.
 */

#ifndef BLOCK_SPECS_H   // bu basligin ayni derlemede iki kez islenmesini engeller
#define BLOCK_SPECS_H   // include guard makrosunu tanimlar

#include <string>    // std::string icin
#include <vector>    // inputSlots ve params listeleri icin std::vector
#include <map>       // blok adi -> BlockSpec haritasi icin std::map
#include "json_value.h"   // promptForParams'in dondurdugu JsonValue

enum class ParamType { String, Number, Bool, StringList };   // terminalde sorulacak parametrenin veri turu

struct ParamSpec {
    std::string key;          // JSON'da kullanilacak alan adi, orn. "strategy"
    std::string prompt;        // terminalde gosterilecek soru
    ParamType type;             // bu parametrenin turu (String/Number/Bool/StringList)
    bool required;              // true ise bos cevaba izin verilmez, tekrar sorulur
};

struct BlockSpec {
    /* Bu blogun bekledigi girdi slotlarinin adlari, SIRAYLA.
     * Cogu blok icin tek elemanli: {"data"}. load_csv gibi zincirin
     * basindaki bloklarda BOS ({}) - hic girdi istemez.
     * create_dataloader gibi coklu girdili bloklarda: {"X", "y"}.
     * compute_classification_metrics gibi bloklarda: {"model", "X", "y"}.
     * interactive_main.cpp, her slot icin ayri ayri "hangi node:slot'tan
     * besleniyor" diye soracak. */
    std::vector<std::string> inputSlots;   // slot adlarinin listesi, tanimlanma sirasiyla

    /* Bu blogun urettigi cikis slotlarinin adlari, SIRAYLA. Cogu blok icin
     * tek elemanli: {"output"} (bkz. blocks/base.py _normalize_result -
     * eski tek-cikisli format otomatik "output" adiyla sarilir).
     * train_test_split -> {"train","test"}, train_validation_test_split ->
     * {"train","validation","test"}. GUI'nin bir node'u HENUZ CALISTIRMADAN
     * ONCE dogru sayida cikis pini cizebilmesi icin gerekli (metin tabanli
     * interactive_main.cpp bu bilgiye ihtiyac duymuyordu, cunku calisma
     * SONRASI gercek node->outputs haritasindan okuyordu). */
    std::vector<std::string> outputSlots;

    /* true ise, bu blogun (her) cikis slotu bir pandas DataFrame'dir -
     * interactive_main.cpp basarili calismadan sonra OTOMATIK olarak
     * kucuk bir data_preview cagirip ilk 5 satiri da gosterir.
     * to_tensor (tensor), create_dataloader (DataLoader), mlp_learner
     * (model), compute_*_metrics (sayisal sozluk) icin false olmali -
     * bunlarin ciktisi DataFrame degil, data_preview'a verilirse hata alinir. */
    bool producesDataFrame;   // true ise otomatik data_preview onizlemesi tetiklenir

    std::vector<ParamSpec> params;   // bu blogun sorulacak tum parametrelerinin listesi

    BlockSpec() : producesDataFrame(false) {   // varsayilan: DataFrame uretmeyen blok
        outputSlots.push_back("output");         // varsayilan: tek cikis slotu "output" (cogu blok icin dogru)
    }
};

/* Desteklenen tum bloklarin adi -> parametre tanimlari. main.cpp / gercek
 * dispatcher.py'deki BLOCK_REGISTRY ile ayni isimleri kullanir (bkz.
 * blocks/__init__.py). Yeni bir blok eklemek istersen sadece burada yeni
 * bir eleman eklemen yeterli - interactive_main.cpp'de degisiklik gerekmez. */
const std::map<std::string, BlockSpec>& blockRegistrySpecs();   // blok adi -> BlockSpec tablosunun salt-okunur referansini doner

/* Kayitli tum blok adlarini (yardim amacli) ekrana basar. */
void printAvailableBlocks();   // blockRegistrySpecs() icindeki tum blok adlarini listeler

/* Verilen BlockSpec'e gore, kullanicidan std::cin uzerinden SIRAYLA
 * her parametreyi sorar, cevaplari turune gore JsonValue'ya cevirip
 * bir "params" objesi olarak dondurur. Bos birakilan OPSIYONEL alanlar
 * JSON'a hic eklenmez (boylece Python tarafindaki .get(key, default)
 * mantigi calisir, biz kendi tarafimizda varsayilan degeri TEKRARLAMAK
 * zorunda kalmayiz). Zorunlu bir alan bos birakilirsa tekrar sorulur. */
JsonValue promptForParams(const BlockSpec& spec);   // spec.params listesini sirayla sorup doldurulmus params objesini doner

#endif // BLOCK_SPECS_H  -- basligin sonu
