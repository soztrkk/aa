/*
 * python_process.cpp
 *
 * ADIM 2 (devami): pipe kurulumu block_pipeline_demo.c ile birebir ayni
 * mantikla calisir - detayli aciklamalar icin o dosyaya bakabilirsin.
 * Burada tekrar etmek yerine sadece ONEMLI noktalari kisaca hatirlatiyoruz.
 */

#include "python_process.h"   // sinif bildirimi
#include <stdexcept>            // std::runtime_error
#include <vector>               // CreateProcessA icin mutable komut satiri buffer'i

PythonProcess::PythonProcess()
    : childStdinWrite_(NULL), childStdoutRead_(NULL), running_(false) {   // handle'lar bos, process henuz calismiyor
    ZeroMemory(&processInfo_, sizeof(processInfo_));   // PROCESS_INFORMATION yapisini sifirla (rastgele degerler kalmasin)
}

PythonProcess::~PythonProcess() {
    stop();   // nesne yok edilirken process hala ayaktaysa duzgunce kapat
}

bool PythonProcess::start(const std::string& pythonExe,
                           const std::string& scriptPath,
                           std::string& errorOut) {
    if (running_) {                                   // zaten baslatilmis bir process varsa
        errorOut = "PythonProcess: zaten calisiyor";    // ikinci kez baslatmaya izin verme
        return false;
    }

    SECURITY_ATTRIBUTES sa;                    // pipe handle'larinin miras alinabilirligini ayarlamak icin
    sa.nLength = sizeof(SECURITY_ATTRIBUTES);   // yapi boyutu (Windows API'nin bekledigi zorunlu alan)
    sa.bInheritHandle = TRUE;                    // olusturulan handle'lar alt process'e miras gecebilsin
    sa.lpSecurityDescriptor = NULL;               // varsayilan guvenlik tanimlayicisi

    HANDLE childStdinRead = NULL;     // child'in okuyacagi uc (bizim yazdigimiz stdin pipe'inin diger ucu)
    HANDLE childStdoutWrite = NULL;    // child'in yazacagi uc (bizim okuyacagimiz stdout pipe'inin diger ucu)

    /* pipe #1: biz -> python (child'in stdin'i) */
    if (!CreatePipe(&childStdinRead, &childStdinWrite_, &sa, 0)) {   // isimsiz pipe olustur: okuma ucu child'a, yazma ucu bize
        errorOut = "CreatePipe(stdin) basarisiz";
        return false;
    }
    if (!SetHandleInformation(childStdinWrite_, HANDLE_FLAG_INHERIT, 0)) {   // bizim yazma ucumuz child'a miras gecmesin
        errorOut = "SetHandleInformation(stdin write) basarisiz";
        return false;
    }

    /* pipe #2: python -> biz (child'in stdout'u) */
    if (!CreatePipe(&childStdoutRead_, &childStdoutWrite, &sa, 0)) {   // ikinci pipe: okuma ucu bize, yazma ucu child'a
        errorOut = "CreatePipe(stdout) basarisiz";
        return false;
    }
    if (!SetHandleInformation(childStdoutRead_, HANDLE_FLAG_INHERIT, 0)) {   // bizim okuma ucumuz child'a miras gecmesin
        errorOut = "SetHandleInformation(stdout read) basarisiz";
        return false;
    }

    STARTUPINFOA si;                                 // child process'in baslangic ayarlarini tasiyan yapi
    ZeroMemory(&si, sizeof(si));                       // yapiyi sifirla, rastgele degerler kalmasin
    si.cb = sizeof(si);                                 // yapi boyutu (Windows API'nin bekledigi zorunlu alan)
    si.dwFlags |= STARTF_USESTDHANDLES;                  // hStdInput/hStdOutput/hStdError alanlarini kullanacagimizi belirt
    si.hStdInput = childStdinRead;                        // child'in stdin'i: bizim yazma ucumuzun karsisindaki okuma ucu
    si.hStdOutput = childStdoutWrite;                      // child'in stdout'u: bizim okuma ucumuzun karsisindaki yazma ucu
    si.hStdError = GetStdHandle(STD_ERROR_HANDLE);   // Python traceback'lerini kendi konsolumuzda gormek icin

    ZeroMemory(&processInfo_, sizeof(processInfo_));   // olusturulacak process bilgisini tutacak yapiyi sifirla

    /* CreateProcessA komut satirini MUTABLE (degistirilebilir) bir buffer
     * ister; std::string'in ic buffer'ini dogrudan vermek guvenli degil,
     * bu yuzden kopya bir char dizisine aliyoruz. */
    std::string commandLine = pythonExe + " " + scriptPath;                        // orn. "python dispatcher.py"
    std::vector<char> commandLineBuf(commandLine.begin(), commandLine.end());        // duzenlenebilir char dizisine kopyala
    commandLineBuf.push_back('\0');                                                   // C-string sonlandirici ekle

    BOOL ok = CreateProcessA(
        NULL,                    // uygulama adi: NULL, komut satirindan cikarilsin
        &commandLineBuf[0],       // duzenlenebilir komut satiri buffer'i
        NULL, NULL,                // process/thread guvenlik nitelikleri: varsayilan
        TRUE,               // pipe handle'lari devredilsin
        0,                    // ek process olusturma bayragi yok
        NULL,               // ortam degiskenlerimizi miras alsin
        NULL,               // calisma dizinimizi miras alsin (proje kok dizini olmali)
        &si,                 // baslangic ayarlari (stdin/stdout yonlendirmeleri dahil)
        &processInfo_);       // doldurulacak process/thread handle bilgisi

    /* Child kendi kopyalarini aldi; biz kullanmayacagimiz uclari kapatiyoruz. */
    CloseHandle(childStdinRead);     // child'in stdin okuma ucu artik bizde gereksiz
    CloseHandle(childStdoutWrite);    // child'in stdout yazma ucu artik bizde gereksiz

    if (!ok) {                                          // process olusturma basarisiz oldu
        errorOut = "CreateProcess basarisiz (python PATH'te mi? dispatcher.py dogru dizinde mi?)";
        CloseHandle(childStdinWrite_);                    // yaridan acilmis pipe uclarini temizle
        CloseHandle(childStdoutRead_);
        childStdinWrite_ = NULL;                           // sarkan (dangling) handle kalmasin
        childStdoutRead_ = NULL;
        return false;
    }

    running_ = true;   // process basariyla baslatildi, artik calisir durumda
    return true;
}

void PythonProcess::sendLine(const std::string& text) {
    if (!running_) {                                                       // process baslatilmamis/durmussa
        throw std::runtime_error("PythonProcess::sendLine: process calismiyor");
    }
    DWORD bytesWritten;                                                     // WriteFile'in kac byte yazdigini bildirdigi cikti parametresi
    if (!WriteFile(childStdinWrite_, text.data(), (DWORD)text.size(), &bytesWritten, NULL)) {   // mesaj govdesini pipe'a yaz
        throw std::runtime_error("PythonProcess::sendLine: yazma basarisiz (process kapanmis olabilir)");
    }
    if (!WriteFile(childStdinWrite_, "\n", 1, &bytesWritten, NULL)) {   // satir sonunu ayrica yaz (protokol: satir bazli JSON)
        throw std::runtime_error("PythonProcess::sendLine: satir sonu yazilamadi");
    }
}

std::string PythonProcess::readLine() {
    if (!running_) return "";   // process calismiyorsa okunacak bir sey yok

    std::string result;   // biriktirilen satir
    char ch;                // tek seferde okunan karakter
    DWORD bytesRead;        // ReadFile'in kac byte okudugunu bildirdigi cikti parametresi

    while (true) {
        BOOL ok = ReadFile(childStdoutRead_, &ch, 1, &bytesRead, NULL);   // pipe'dan tek byte oku
        if (!ok || bytesRead == 0) {
            break; // child stdout'u kapatti / process bitti
        }
        if (ch == '\n') break;         // satir sonu geldi, okuma tamamlandi
        if (ch == '\r') continue; // Windows text-mode \r\n donusumu
        result += ch;   // normal karakteri sonuca ekle
    }
    return result;   // okunan satiri (satir sonu olmadan) doner
}

std::string PythonProcess::readLineWithTimeout(DWORD timeoutMs) {
    if (!running_) {
        return "";
    }

    std::string result;
    DWORD startTime = GetTickCount();

    while (true) {
        DWORD availableBytes = 0;

        BOOL peekOk = PeekNamedPipe(
            childStdoutRead_,
            NULL,
            0,
            NULL,
            &availableBytes,
            NULL
        );

        if (!peekOk) {
            throw std::runtime_error(
                "PythonProcess::readLineWithTimeout: pipe kontrolu basarisiz"
            );
        }

        if (availableBytes > 0) {
            char ch;
            DWORD bytesRead = 0;

            BOOL readOk = ReadFile(
                childStdoutRead_,
                &ch,
                1,
                &bytesRead,
                NULL
            );

            if (!readOk || bytesRead == 0) {
                throw std::runtime_error(
                    "PythonProcess::readLineWithTimeout: Python process kapanmis olabilir"
                );
            }

            if (ch == '\n') {
                break;
            }

            if (ch == '\r') {
                continue;
            }

            result += ch;
        }
        else {
            DWORD elapsed = GetTickCount() - startTime;

            if (elapsed >= timeoutMs) {
                throw std::runtime_error(
                    "PythonProcess::readLineWithTimeout: timeout"
                );
            }

            Sleep(10);
        }
    }

    return result;
}

void PythonProcess::sendJson(const JsonValue& value) {
    sendLine(value.dump());   // once JSON metnine cevir, sonra tek satir olarak gonder
}

JsonValue PythonProcess::recvJson() {
    std::string line = readLine();   // pipe'dan bir satir oku
    if (line.empty()) {                // bos satir -> process kapanmis/cevap gelmemis demektir
        throw std::runtime_error("PythonProcess::recvJson: process'ten cevap gelmedi (kapanmis olabilir)");
    }
    return JsonValue::parse(line);   // satiri JSON olarak ayristirip doner
}

JsonValue PythonProcess::request(
    const JsonValue& requestValue,
    DWORD timeoutMs
) {
    sendJson(requestValue);

    try {
        std::string line = readLineWithTimeout(timeoutMs);

        if (line.empty()) {
            throw std::runtime_error(
                "PythonProcess::request: process'ten cevap gelmedi"
            );
        }

        return JsonValue::parse(line);
    }
    catch (const std::runtime_error&) {
        forceStop();
        throw;
    }
}

JsonValue PythonProcess::requestWithProgress(
    const JsonValue& requestValue,
    const std::function<void(const JsonValue&)>& onProgress,
    DWORD timeoutMs
) {
    sendJson(requestValue);

    try {
        DWORD startTime = GetTickCount();

        while (true) {
            DWORD elapsed = GetTickCount() - startTime;

            if (elapsed >= timeoutMs) {
                throw std::runtime_error(
                    "PythonProcess::requestWithProgress: timeout"
                );
            }

            DWORD remaining = timeoutMs - elapsed;

            std::string line = readLineWithTimeout(remaining);

            if (line.empty()) {
                throw std::runtime_error(
                    "PythonProcess::requestWithProgress: process'ten cevap gelmedi"
                );
            }

            JsonValue message = JsonValue::parse(line);

            if (message.has("type") &&
                message.at("type").asString() == "progress") {

                if (onProgress) {
                    onProgress(message);
                }

                continue;
            }

            return message;
        }
    }
    catch (const std::runtime_error&) {
        forceStop();
        throw;
    }
}

void PythonProcess::forceStop() {
    if (!running_) {
        return;
    }

    if (processInfo_.hProcess) {
        TerminateProcess(processInfo_.hProcess, 1);
        WaitForSingleObject(processInfo_.hProcess, 1000);
    }

    if (childStdinWrite_) {
        CloseHandle(childStdinWrite_);
        childStdinWrite_ = NULL;
    }

    if (childStdoutRead_) {
        CloseHandle(childStdoutRead_);
        childStdoutRead_ = NULL;
    }

    if (processInfo_.hThread) {
        CloseHandle(processInfo_.hThread);
        processInfo_.hThread = NULL;
    }

    if (processInfo_.hProcess) {
        CloseHandle(processInfo_.hProcess);
        processInfo_.hProcess = NULL;
    }

    ZeroMemory(&processInfo_, sizeof(processInfo_));

    running_ = false;
}

void PythonProcess::stop() {
    if (!running_) return;   // zaten durmussa yapilacak bir sey yok

    if (childStdinWrite_) {
        CloseHandle(childStdinWrite_);   // -> Python tarafinda stdin EOF, dongu dogal biter
        childStdinWrite_ = NULL;           // sarkan handle kalmasin
    }
    WaitForSingleObject(processInfo_.hProcess, 5000);   // process'in kendini kapatmasi icin en fazla 5 saniye bekle

    if (childStdoutRead_) { CloseHandle(childStdoutRead_); childStdoutRead_ = NULL; }   // okuma pipe ucunu kapat
    if (processInfo_.hProcess) CloseHandle(processInfo_.hProcess);   // process handle'ini serbest birak
    if (processInfo_.hThread) CloseHandle(processInfo_.hThread);      // ana thread handle'ini serbest birak
    ZeroMemory(&processInfo_, sizeof(processInfo_));                    // yapida sarkan handle degeri kalmasin diye sifirla

    running_ = false;   // artik calismiyoruz
}
