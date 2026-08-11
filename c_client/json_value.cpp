/*
 * json_value.cpp
 *
 * ADIM 1 (devami): JsonValue'nun gerceklestirimi (implementation).
 *
 * Iki ana blok var:
 *   1) dump()      : JsonValue -> tek satirlik JSON metni  (C++  ->  Python)
 *   2) parse()     : JSON metni -> JsonValue               (Python -> C++)
 *
 * parse() kismi "recursive descent parser" (ozyinelemeli inis ayristirici)
 * deseniyle yazildi: her JSON yapisi (object, array, string, number...)
 * icin ayri bir parseXxx() fonksiyonu var, ve bunlar birbirini cagirarak
 * ic ice yapilari (orn. bir object icindeki array, o array icindeki baska
 * bir object) dogal olarak cozuyor.
 */

#include "json_value.h"   // JsonValue/JsonError bildirimleri
#include <sstream>          // std::ostringstream (sayi -> metin donusumu icin)
#include <cstdlib>          // std::strtod (metin -> double donusumu icin)
#include <cctype>           // std::isdigit
#include <cstdio>           // std::sprintf (\uXXXX kacis dizisi yazarken)

/* ============================= kurucular ============================= */

JsonValue::JsonValue() : type_(Type::Null), bool_(false), number_(0.0) {}   // varsayilan kurucu: Null deger, diger alanlar sifir
JsonValue::JsonValue(bool b) : type_(Type::Bool), bool_(b), number_(0.0) {}   // bool'dan: turu Bool yapar, degeri bool_'a yazar
JsonValue::JsonValue(int n) : type_(Type::Number), bool_(false), number_(static_cast<double>(n)) {}   // int'ten: turu Number yapar, double'a cevirir
JsonValue::JsonValue(double n) : type_(Type::Number), bool_(false), number_(n) {}   // double'dan: turu Number yapar, degeri dogrudan saklar
JsonValue::JsonValue(const char* s) : type_(Type::String), bool_(false), number_(0.0), string_(s) {}   // C-string'ten: turu String yapar
JsonValue::JsonValue(const std::string& s) : type_(Type::String), bool_(false), number_(0.0), string_(s) {}   // std::string'ten: turu String yapar

JsonValue JsonValue::makeArray() {
    JsonValue v;              // varsayilan (Null) bir JsonValue olustur
    v.type_ = Type::Array;    // turunu Array'e cevir (array_ zaten bos vector olarak baslar)
    return v;                  // bos diziyi doner
}

JsonValue JsonValue::makeObject() {
    JsonValue v;               // varsayilan (Null) bir JsonValue olustur
    v.type_ = Type::Object;    // turunu Object'e cevir (object_ zaten bos map olarak baslar)
    return v;                   // bos nesneyi doner
}

/* ============================= object erisimi ============================= */

JsonValue& JsonValue::operator[](const std::string& key) {
    /* Henuz Null ise (ilk kez yaziliyorsa) otomatik olarak Object'e cevir.
     * Bu, "JsonValue obj; obj[\"x\"] = 1;" gibi kullanimlarin dogrudan
     * calismasini sağlar - kullaniciyi makeObject() cagirmaya zorlamiyoruz. */
    if (type_ == Type::Null) {           // henuz hicbir turu yoksa (yeni olusturulmus varsayilan deger)
        type_ = Type::Object;             // otomatik olarak Object turune gecir
    }
    if (type_ != Type::Object) {          // Null degildi ama Object da degilse (orn. zaten Number)
        throw JsonError("JsonValue::operator[]: bu deger bir object degil");   // yanlis kullanim - hata firlat
    }
    return object_[key];   // std::map::operator[]: key yoksa varsayilan JsonValue (Null) ile olusturup referansini doner
}

bool JsonValue::has(const std::string& key) const {
    return type_ == Type::Object && object_.find(key) != object_.end();   // once Object mi diye bak, sonra key aranir
}

const JsonValue& JsonValue::at(const std::string& key) const {
    if (type_ != Type::Object) {                       // bu deger zaten bir object degilse
        throw JsonError("JsonValue::at: bu deger bir object degil");   // erisim anlamsiz, hata firlat
    }
    std::map<std::string, JsonValue>::const_iterator it = object_.find(key);   // key'i map icinde ara
    if (it == object_.end()) {                          // bulunamadiysa
        throw JsonError("JsonValue::at: alan bulunamadi -> " + key);   // hangi alanin eksik oldugunu belirten hata firlat
    }
    return it->second;   // bulunan degerin salt-okunur referansini doner
}

/* ============================= array erisimi ============================= */

void JsonValue::push_back(const JsonValue& v) {
    if (type_ == Type::Null) {           // henuz turu yoksa
        type_ = Type::Array;              // otomatik olarak Array turune gecir
    }
    if (type_ != Type::Array) {           // Null degildi ama Array da degilse
        throw JsonError("JsonValue::push_back: bu deger bir array degil");   // yanlis kullanim - hata firlat
    }
    array_.push_back(v);   // v'nin bir kopyasini dizinin sonuna ekle
}

size_t JsonValue::size() const {
    if (type_ == Type::Array) return array_.size();     // array ise eleman sayisini doner
    if (type_ == Type::Object) return object_.size();   // object ise alan sayisini doner
    return 0;                                              // diger turler icin "boyut" kavrami yok, 0 doner
}

JsonValue& JsonValue::operator[](size_t index) {
    if (type_ != Type::Array) {          // array degilse index ile erisim anlamsiz
        throw JsonError("JsonValue::operator[](size_t): bu deger bir array degil");
    }
    return array_.at(index);   // std::vector::at: index gecersizse kendisi de exception firlatir (sinir kontrolu dahil)
}

const JsonValue& JsonValue::operator[](size_t index) const {
    if (type_ != Type::Array) {          // array degilse index ile erisim anlamsiz
        throw JsonError("JsonValue::operator[](size_t): bu deger bir array degil");
    }
    return array_.at(index);   // salt-okunur versiyon, ayni sinir kontrollu erisim
}

/* ============================= skaler okuma ============================= */

std::string JsonValue::asString(const std::string& fallback) const {
    return type_ == Type::String ? string_ : fallback;   // turu String ise degeri, degilse varsayilani doner
}

double JsonValue::asNumber(double fallback) const {
    return type_ == Type::Number ? number_ : fallback;   // turu Number ise degeri, degilse varsayilani doner
}

int JsonValue::asInt(int fallback) const {
    return type_ == Type::Number ? static_cast<int>(number_) : fallback;   // Number ise double'i int'e kirparak doner
}

bool JsonValue::asBool(bool fallback) const {
    return type_ == Type::Bool ? bool_ : fallback;   // turu Bool ise degeri, degilse varsayilani doner
}

/* ============================= dump (serialize) ============================= */

static void dumpEscapedString(const std::string& s, std::string& out) {
    out += '"';   // JSON string'i acan tirnak
    for (size_t i = 0; i < s.size(); i++) {           // kaynak metnin her byte'i icin
        unsigned char c = static_cast<unsigned char>(s[i]);   // isaretsiz (unsigned) olarak al, negatif deger sorunu olmasin
        switch (c) {
            case '"':  out += "\\\""; break;   // tirnak karakteri kacis gerektirir
            case '\\': out += "\\\\"; break;   // ters bolu kacis gerektirir
            case '\n': out += "\\n";  break;   // satir sonu kacis dizisiyle yazilir
            case '\r': out += "\\r";  break;   // carriage return kacis dizisiyle yazilir
            case '\t': out += "\\t";  break;   // tab kacis dizisiyle yazilir
            default:
                if (c < 0x20) {
                    /* diger kontrol karakterleri: \u00XX seklinde yaz */
                    char buf[8];                             // \u00XX + nul icin yeterli tampon
                    std::sprintf(buf, "\\u%04x", c);          // kodu 4 haneli hex olarak bicimlendir
                    out += buf;                                // sonuca ekle
                } else {
                    /* UTF-8 devam byte'lari (Turkce karakterler dahil)
                     * oldugu gibi kopyalanir - JSON, UTF-8'i dogrudan
                     * string icinde kabul eder, kacis (escape) gerekmez. */
                    out += static_cast<char>(c);   // normal/UTF-8 karakteri degistirmeden ekle
                }
        }
    }
    out += '"';   // JSON string'i kapatan tirnak
}

void JsonValue::dumpTo(std::string& out) const {
    switch (type_) {                       // hangi turu tasidigimiza gore farkli metin uret
        case Type::Null:
            out += "null";                  // JSON'da null aynen "null" yazilir
            break;
        case Type::Bool:
            out += bool_ ? "true" : "false";   // JSON'da bool "true"/"false" olarak yazilir
            break;
        case Type::Number: {
            /* Tam sayi degerse "5" gibi yaz (Python tarafinda int/float
             * ayrimi cogu zaman onemsiz ama daha temiz gorunuyor);
             * degilse normal ondalikli yaz. */
            std::ostringstream oss;                                       // sayi -> metin donusumu icin akis
            if (number_ == static_cast<long long>(number_)) {              // kesirli kismi yoksa (tam sayiya esitse)
                oss << static_cast<long long>(number_);                     // tam sayi olarak yaz (orn. "5", ".0" olmadan)
            } else {
                oss.precision(15);                                          // ondalikli sayilar icin yeterli hassasiyet
                oss << number_;                                             // ondalikli olarak yaz
            }
            out += oss.str();   // uretilen metni sonuca ekle
            break;
        }
        case Type::String:
            dumpEscapedString(string_, out);   // tirnak+kacis kurallarina uygun sekilde yaz
            break;
        case Type::Array: {
            out += '[';                                    // diziyi acan koseli parantez
            for (size_t i = 0; i < array_.size(); i++) {    // her eleman icin
                if (i > 0) out += ',';                       // ilk eleman degilse once virgul koy
                array_[i].dumpTo(out);                        // elemani ozyinelemeli olarak yaz (ic ice yapilari cozer)
            }
            out += ']';                                    // diziyi kapatan koseli parantez
            break;
        }
        case Type::Object: {
            out += '{';                                    // nesneyi acan suslu parantez
            bool first = true;                              // ilk alan mi diye takip (virgul koymamak icin)
            for (std::map<std::string, JsonValue>::const_iterator it = object_.begin();
                 it != object_.end(); ++it) {                // map zaten anahtara gore sirali gezilir
                if (!first) out += ',';                       // ilk alan degilse once virgul koy
                first = false;                                 // artik ilk alan degiliz
                dumpEscapedString(it->first, out);              // anahtari (key) yaz
                out += ':';                                     // anahtar-deger ayraci
                it->second.dumpTo(out);                          // degeri ozyinelemeli olarak yaz
            }
            out += '}';                                    // nesneyi kapatan suslu parantez
            break;
        }
    }
}

std::string JsonValue::dump() const {
    std::string out;   // biriktirilecek sonuc metni
    dumpTo(out);         // gercek isi yapan ic fonksiyona devret (kopyalamadan, referansla doldurur)
    return out;           // tamamlanmis JSON metnini doner
}

/* ============================= parse (parsing) ============================= */

namespace {

/* Kucuk bir yardimci sinif: metin uzerinde ilerleyen bir "okuma imleci".
 * Her parseXxx() fonksiyonu bu imleci ilerletir. */
class Parser {
public:
    explicit Parser(const std::string& text) : text_(text), pos_(0) {}   // ayristirilacak metni saklar, okuma imlecini basa koyar

    JsonValue parseValue() {
        skipWhitespace();                     // deger baslamadan once bosluklari atla
        if (pos_ >= text_.size()) {            // metin bitmisse okunacak deger yok
            throw JsonError("JSON parse: beklenmedik metin sonu");
        }
        char c = text_[pos_];                  // bir sonraki karaktere bakarak deger turunu anla (tuketmeden)
        if (c == '{') return parseObject();     // '{' ile basliyorsa object
        if (c == '[') return parseArray();       // '[' ile basliyorsa array
        if (c == '"') return JsonValue(parseString());   // '"' ile basliyorsa string
        if (c == 't' || c == 'f') return parseBool();      // 't'/'f' ile basliyorsa true/false
        if (c == 'n') return parseNull();                    // 'n' ile basliyorsa null
        if (c == '-' || std::isdigit(static_cast<unsigned char>(c))) return parseNumber();   // '-' ya da rakamsa sayi
        throw JsonError(std::string("JSON parse: beklenmeyen karakter -> ") + c);   // hicbirine uymuyorsa gecersiz JSON
    }

    void expectEnd() {
        skipWhitespace();                       // son bosluklari atla
        if (pos_ != text_.size()) {              // imlec metnin sonuna ulasmadiysa
            throw JsonError("JSON parse: metnin sonunda fazladan karakter var");   // fazladan/gecersiz icerik var demektir
        }
    }

private:
    const std::string& text_;   // ayristirilan kaynak metin (kopyalanmadan referansla tutulur)
    size_t pos_;                  // su anki okuma konumu (index)

    void skipWhitespace() {
        while (pos_ < text_.size() &&                        // metin bitmemisse ve
               (text_[pos_] == ' ' || text_[pos_] == '\t' ||   // karakter bosluk, tab,
                text_[pos_] == '\n' || text_[pos_] == '\r')) {  // yeni satir ya da CR ise
            pos_++;                                             // imleci bir ilerlet
        }
    }

    char peek() {
        if (pos_ >= text_.size()) throw JsonError("JSON parse: beklenmedik metin sonu");   // sinirin disina cikilmasin
        return text_[pos_];   // imleci ilerletmeden su anki karakteri doner
    }

    char advance() {
        if (pos_ >= text_.size()) throw JsonError("JSON parse: beklenmedik metin sonu");   // sinirin disina cikilmasin
        return text_[pos_++];   // su anki karakteri doner VE imleci bir ilerletir
    }

    void expectChar(char expected) {
        char got = advance();          // bir karakter tuket
        if (got != expected) {          // beklenenle uyusmuyorsa
            std::string msg = "JSON parse: '";
            msg += expected;             // beklenen karakteri mesaja ekle
            msg += "' bekleniyordu, '";
            msg += got;                   // gercekte bulunan karakteri mesaja ekle
            msg += "' bulundu";
            throw JsonError(msg);         // aciklayici hata firlat
        }
    }

    JsonValue parseObject() {
        expectChar('{');                          // acilis suslu parantezini tuket
        JsonValue obj = JsonValue::makeObject();    // sonucu biriktirecek bos object
        skipWhitespace();
        if (peek() == '}') { advance(); return obj; }   // bos object ("{}") ise hemen bitir

        while (true) {
            skipWhitespace();
            std::string key = parseString();        // "anahtar" kismini oku
            skipWhitespace();
            expectChar(':');                          // anahtar-deger ayracini tuket
            JsonValue value = parseValue();            // degeri ozyinelemeli olarak oku (ic ice olabilir)
            obj[key] = value;                           // okunan alani sonuc object'e yaz

            skipWhitespace();
            char c = advance();                        // ',' mi yoksa '}' mi geldigine bak
            if (c == ',') continue;                     // virgulse bir sonraki key:value cifti icin devam
            if (c == '}') break;                         // kapanis parantezi ise object tamamlandi
            throw JsonError("JSON parse: object icinde ',' ya da '}' bekleniyordu");   // baska bir sey gecersiz
        }
        return obj;   // tamamlanmis object'i doner
    }

    JsonValue parseArray() {
        expectChar('[');                          // acilis koseli parantezi tuket
        JsonValue arr = JsonValue::makeArray();     // sonucu biriktirecek bos array
        skipWhitespace();
        if (peek() == ']') { advance(); return arr; }   // bos array ("[]") ise hemen bitir

        while (true) {
            JsonValue value = parseValue();    // bir elemani ozyinelemeli olarak oku
            arr.push_back(value);               // sonuc diziye ekle

            skipWhitespace();
            char c = advance();                 // ',' mi yoksa ']' mi geldigine bak
            if (c == ',') continue;              // virgulse bir sonraki eleman icin devam
            if (c == ']') break;                  // kapanis parantezi ise array tamamlandi
            throw JsonError("JSON parse: array icinde ',' ya da ']' bekleniyordu");   // baska bir sey gecersiz
        }
        return arr;   // tamamlanmis diziyi doner
    }

    /* \uXXXX kacis dizisini (escape sequence) tek bir UTF-8 karakterine
     * cevirir. Sadece Basic Multilingual Plane (0x0000-0xFFFF) destekleniyor,
     * bu protokol icin (Turkce karakterler dahil) yeterli. */
    void appendUtf8FromCodepoint(unsigned int cp, std::string& out) {
        if (cp <= 0x7F) {                                       // ASCII araligi: tek byte yeterli
            out += static_cast<char>(cp);
        } else if (cp <= 0x7FF) {                                // 2 byte'lik UTF-8 araligi
            out += static_cast<char>(0xC0 | (cp >> 6));           // ilk byte: ust bitler + UTF-8 basligi
            out += static_cast<char>(0x80 | (cp & 0x3F));          // devam byte'i: alt 6 bit
        } else {                                                  // 3 byte'lik UTF-8 araligi (Turkce karakterler burada, orn. 'ı','ş')
            out += static_cast<char>(0xE0 | (cp >> 12));           // ilk byte: en ust bitler + UTF-8 basligi
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));    // orta devam byte'i
            out += static_cast<char>(0x80 | (cp & 0x3F));           // son devam byte'i
        }
    }

    unsigned int parseHex4() {
        unsigned int value = 0;                 // biriktirilen hex deger
        for (int i = 0; i < 4; i++) {            // \uXXXX tam olarak 4 hex hane icerir
            char c = advance();                   // bir hane oku
            value <<= 4;                            // onceki degeri 4 bit sola kaydir (yeni hane icin yer ac)
            if (c >= '0' && c <= '9') value |= (c - '0');            // rakam ise dogrudan degerini ekle
            else if (c >= 'a' && c <= 'f') value |= (c - 'a' + 10);   // kucuk harf a-f ise 10-15 arasi deger ekle
            else if (c >= 'A' && c <= 'F') value |= (c - 'A' + 10);   // buyuk harf A-F ise 10-15 arasi deger ekle
            else throw JsonError("JSON parse: gecersiz \\u kacis dizisi");   // hex olmayan karakter -> hata
        }
        return value;   // 0x0000-0xFFFF arasi tam codepoint'i doner
    }

    std::string parseString() {
        expectChar('"');            // acilis tirnagini tuket
        std::string result;          // biriktirilen (kacis dizileri cozulmus) string
        while (true) {
            char c = advance();       // bir karakter tuket
            if (c == '"') break;       // kapanis tirnagi -> string bitti
            if (c == '\\') {            // kacis dizisi basliyor
                char esc = advance();    // kacisten sonraki karakteri oku
                switch (esc) {
                    case '"':  result += '"';  break;   // \" -> "
                    case '\\': result += '\\'; break;   // iki ters bolu -> tek ters bolu
                    case '/':  result += '/';  break;   // \/ -> /
                    case 'b':  result += '\b'; break;   // \b -> backspace
                    case 'f':  result += '\f'; break;   // \f -> form feed
                    case 'n':  result += '\n'; break;   // \n -> satir sonu
                    case 'r':  result += '\r'; break;   // \r -> carriage return
                    case 't':  result += '\t'; break;   // \t -> tab
                    case 'u':  appendUtf8FromCodepoint(parseHex4(), result); break;   // \uXXXX -> UTF-8 karakter
                    default:
                        throw JsonError("JSON parse: bilinmeyen kacis dizisi");   // taninmayan kacis karakteri
                }
            } else {
                result += c;   // normal karakter, oldugu gibi ekle
            }
        }
        return result;   // cozulmus (kacissiz) string'i doner
    }

    JsonValue parseBool() {
        if (text_.compare(pos_, 4, "true") == 0) { pos_ += 4; return JsonValue(true); }    // "true" ile eslesiyorsa tuket ve true doner
        if (text_.compare(pos_, 5, "false") == 0) { pos_ += 5; return JsonValue(false); }   // "false" ile eslesiyorsa tuket ve false doner
        throw JsonError("JSON parse: gecersiz bool degeri");   // ikisiyle de eslesmezse gecersiz JSON
    }

    JsonValue parseNull() {
        if (text_.compare(pos_, 4, "null") == 0) { pos_ += 4; return JsonValue(); }   // "null" ile eslesiyorsa tuket ve Null deger doner
        throw JsonError("JSON parse: gecersiz null degeri");   // eslesmezse gecersiz JSON
    }

    JsonValue parseNumber() {
        size_t start = pos_;                  // sayinin metindeki baslangic konumunu hatirla
        if (peek() == '-') advance();          // (varsa) eksi isaretini tuket
        while (pos_ < text_.size() && std::isdigit(static_cast<unsigned char>(text_[pos_]))) advance();   // tam sayi kismindaki rakamlari tuket
        if (pos_ < text_.size() && text_[pos_] == '.') {         // ondalik nokta varsa
            advance();                                             // noktayi tuket
            while (pos_ < text_.size() && std::isdigit(static_cast<unsigned char>(text_[pos_]))) advance();   // ondalik basamaklari tuket
        }
        if (pos_ < text_.size() && (text_[pos_] == 'e' || text_[pos_] == 'E')) {   // bilimsel gosterim (üstel) varsa
            advance();                                                              // 'e'/'E' harfini tuket
            if (pos_ < text_.size() && (text_[pos_] == '+' || text_[pos_] == '-')) advance();   // (varsa) us isaretini tuket
            while (pos_ < text_.size() && std::isdigit(static_cast<unsigned char>(text_[pos_]))) advance();   // us basamaklarini tuket
        }
        std::string numText = text_.substr(start, pos_ - start);   // tuketilen tum araligi metin olarak kes
        return JsonValue(std::strtod(numText.c_str(), NULL));       // metni double'a cevirip JsonValue olarak doner
    }
};

} // anonymous namespace

JsonValue JsonValue::parse(const std::string& text) {
    Parser parser(text);                       // metni saracak parser nesnesini olustur
    JsonValue result = parser.parseValue();     // en dis JSON degerini oku (object/array/skaler ne olursa)
    parser.expectEnd();                          // deger bittikten sonra metinde baska bir sey kalmamali
    return result;                                // ayristirilmis JsonValue agacini doner
}
