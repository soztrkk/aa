/*
 * json_value.h
 *
 * ADIM 1: JSON okuma/yazma katmani.
 *
 * NEDEN KENDI JSON KUTUPHANEMIZI YAZIYORUZ?
 * Genelde C++ projelerinde hazir bir kutuphane (orn. nlohmann/json)
 * kullanilir. Ama:
 *   1) Bu proje internet erisimi olmadan, tek basina derlenebilir kalsin
 *      istiyoruz (MinGW g++ 6.3 ile, tek komutla).
 *   2) Sen C/C++'a yeni basliyorsun; JSON'un "parse etme" (metni veriye
 *      cevirme) ve "serialize etme" (veriyi metne cevirme) islemlerinin
 *      NASIL calistigini gormen, hazir bir kutuphaneyi kullanmaktan daha
 *      ogretici.
 *   3) Protokolumuz (bkz. CLAUDE.md) zaten basit: duz objeler, diziler,
 *      string/sayi/bool/null. Genel amacli, standarda %100 uyan dev bir
 *      kutuphaneye ihtiyacimiz yok.
 *
 * JsonValue TEK BIR SINIF ile JSON'daki butun deger turlerini temsil eder:
 * null, bool, number (biz hepsini double olarak tutuyoruz), string,
 * array (JsonValue listesi), object (string -> JsonValue eslemesi).
 *
 * Bu, "tagged union" (etiketli birlesim) deseninin C++'taki en basit
 * ogretici hali: bir "type_" alani hangi turde oldugumuzu soyler, ama
 * biz union kullanmak yerine (daha karmasik, manuel yonetim gerektirir)
 * her turu ayri bir uye degisken olarak tutuyoruz. Bellek acisindan
 * israfli ama anlasilmasi COK daha kolay - ogretici amac icin dogru tercih.
 */

#ifndef JSON_VALUE_H   // bu basligin ayni derlemede iki kez islenmesini engeller (include guard basi)
#define JSON_VALUE_H   // JSON_VALUE_H makrosunu tanimlar; ayni dosya tekrar include edilirse yukaridaki #ifndef bunu atlar

#include <string>      // std::string icin
#include <vector>      // array turunu tutmak icin std::vector
#include <map>         // object turunu tutmak icin std::map (anahtar sirali tutulur)
#include <stdexcept>   // std::runtime_error taban sinifi icin

/* JSON metnini parse ederken (veya yanlis tipte alan okumaya calisirken)
 * bu istisna (exception) firlatilir. std::runtime_error'dan turetildigi
 * icin normal catch (const std::exception&) blogu ile de yakalanabilir. */
class JsonError : public std::runtime_error {   // std::runtime_error'dan turetilen kendi hata sinifimiz
public:
    explicit JsonError(const std::string& message) : std::runtime_error(message) {}   // mesaji dogrudan taban sinifa devreder
};

class JsonValue {   // tum JSON deger turlerini (null/bool/number/string/array/object) tek sinifta temsil eder
public:
    enum class Type { Null, Bool, Number, String, Array, Object };   // bu JsonValue'nun su an hangi turu tasidigini belirten etiket

    /* --- kurucular (constructor) --- */
    JsonValue();                          // varsayilan: Null
    JsonValue(bool b);                    // JsonValue x = true;  gibi kullanim icin
    JsonValue(int n);                     // tam sayidan JsonValue olusturur (number turune donusur)
    JsonValue(double n);                  // ondalikli sayidan JsonValue olusturur
    JsonValue(const char* s);             // C-string literalinden ("...") JsonValue olusturur
    JsonValue(const std::string& s);      // std::string'den JsonValue olusturur

    /* --- fabrika (factory) fonksiyonlari: bos array/object olusturmak icin --- */
    static JsonValue makeArray();         // Type::Array turunde, bos bir dizi JsonValue doner
    static JsonValue makeObject();        // Type::Object turunde, bos bir nesne JsonValue doner

    /* --- tur sorgulama --- */
    Type type() const { return type_; }               // bu degerin turunu (enum) dondurur
    bool isNull()   const { return type_ == Type::Null; }     // deger null mi diye kontrol eder
    bool isBool()   const { return type_ == Type::Bool; }     // deger bool mu diye kontrol eder
    bool isNumber() const { return type_ == Type::Number; }   // deger sayi mi diye kontrol eder
    bool isString() const { return type_ == Type::String; }   // deger metin mi diye kontrol eder
    bool isArray()  const { return type_ == Type::Array; }    // deger dizi mi diye kontrol eder
    bool isObject() const { return type_ == Type::Object; }   // deger nesne mi diye kontrol eder

    /* --- object erisimi ---
     * operator[]: anahtar yoksa OTOMATIK OLUSTURUR (std::map::operator[] gibi
     * davranir). "Yazma" amaciyla kullanilir: obj["ad"] = "deger";
     * has()/at(): sadece "okuma" amaciyla, var olup olmadigini guvenli
     * sekilde kontrol etmek icin. */
    JsonValue& operator[](const std::string& key);              // key yoksa otomatik olusturup referansini doner (yazma amacli)
    bool has(const std::string& key) const;                     // object icinde key var mi diye salt-okunur kontrol
    const JsonValue& at(const std::string& key) const;   // yoksa JsonError firlatir
    const std::map<std::string, JsonValue>& fields() const { return object_; }   // tum object alanlarina salt-okunur erisim

    /* --- array erisimi --- */
    void push_back(const JsonValue& v);                         // diziye sona eleman ekler
    size_t size() const;                                        // array ise eleman sayisi, object ise alan sayisi
    JsonValue& operator[](size_t index);                        // index'teki elemani yazilabilir referans olarak doner
    const JsonValue& operator[](size_t index) const;             // index'teki elemani salt-okunur referans olarak doner
    const std::vector<JsonValue>& items() const { return array_; }   // tum array elemanlarina salt-okunur erisim

    /* --- skaler degerleri okuma (varsayilan deger ile, hic exception firlatmaz) --- */
    std::string asString(const std::string& fallback = "") const;   // string degilse fallback'i doner
    double asNumber(double fallback = 0.0) const;                    // number degilse fallback'i doner
    int asInt(int fallback = 0) const;                                // number'i int'e yuvarlayarak doner, degilse fallback
    bool asBool(bool fallback = false) const;                         // bool degilse fallback'i doner

    /* --- serialize (JsonValue -> metin) --- */
    std::string dump() const;                                    // bu degeri JSON metnine cevirir

    /* --- parse (metin -> JsonValue) --- */
    static JsonValue parse(const std::string& text);             // JSON metnini okuyup JsonValue agacina cevirir

private:
    Type type_;                                   // bu degerin su anki turu
    bool bool_;                                    // yalniz type_==Bool iken anlamli
    double number_;                                 // yalniz type_==Number iken anlamli
    std::string string_;                            // yalniz type_==String iken anlamli
    std::vector<JsonValue> array_;                  // yalniz type_==Array iken anlamli
    std::map<std::string, JsonValue> object_;       // yalniz type_==Object iken anlamli

    void dumpTo(std::string& out) const;   // dump()'in ic yardimcisi: sonucu out'un sonuna ekleyerek yazar (gereksiz kopyalamayi onler)
};

#endif // JSON_VALUE_H  -- basligin sonu: yukaridaki #ifndef JSON_VALUE_H bloğunu kapatir
