/*
 * python_process.h
 *
 * ADIM 2: Python tarafiyla (dispatcher.py) konusan process/pipe katmani.
 *
 * Bu sinif, block_pipeline_demo.c'deki (bkz. o dosyanin basindaki uzun
 * aciklama) CreateProcess + pipe mantigini AYNEN kullanir, sadece:
 *   - tek seferlik bir demo yerine, tekrar tekrar cagirilabilen bir SINIF
 *     haline getirir (start/sendLine/readLine/stop),
 *   - ham string yerine JsonValue ile calisan bir ust katman (sendJson/
 *     recvJson/request) ekler.
 *
 * PipelineEngine (bir sonraki adim) bu sinifi kullanarak Python'a
 * "run_block" / "delete_node" / "get_meta" mesajlari gonderecek.
 */

#ifndef PYTHON_PROCESS_H   // bu basligin ayni derlemede iki kez islenmesini engeller
#define PYTHON_PROCESS_H   // include guard makrosunu tanimlar

#include <windows.h>   // HANDLE, PROCESS_INFORMATION, CreateProcess vb. Windows API turleri/fonksiyonlari icin
#include <string>      // std::string icin
#include <functional>
#include "json_value.h"   // sendJson/recvJson icin JsonValue turu

class PythonProcess {   // Python dispatcher.py process'ini baslatip pipe uzerinden onunla konusan sinif
public:
    PythonProcess();    // handle'lari bos/gecersiz degerlerle baslatan kurucu
    ~PythonProcess();    // process hala calisiyorsa stop() cagirip kaynaklari serbest birakan yikici (destructor)

    /* Python process'ini baslatir. workingDir bos birakilirsa mevcut
     * calisma dizini kullanilir (Bu program MUTLAKA proje kok dizininden
     * calistirilmali, cunku dispatcher.py oraya gore aranir).
     * Basarisiz olursa false doner ve errorOut'a aciklama yazar. */
    bool start(const std::string& pythonExe,      // calistirilacak python yorumlayicisinin adi/yolu (orn. "python")
               const std::string& scriptPath,      // calistirilacak script (orn. "dispatcher.py")
               std::string& errorOut);              // basarisizlik aciklamasinin yazilacagi cikti parametresi

    /* Python'un stdin'ine tek satir metin yazar (sonuna otomatik '\n' ekler). */
    void sendLine(const std::string& text);   // text'i process'in stdin pipe'ina yazar

    /* Python'un stdout'undan tek satir okur (karsilik gelen JSON cevabi).
     * Process kapanmissa bos string doner. */
    std::string readLine();   // process'in stdout pipe'indan bir satir okuyup doner
    std::string readLineWithTimeout(DWORD timeoutMs);

    /* Ust seviye yardimcilar: JsonValue gonder / al. */
    void sendJson(const JsonValue& value);   // value.dump() ile metne cevirip sendLine ile gonderir
    JsonValue recvJson();                     // readLine ile bir satir okuyup JsonValue::parse ile ayristirir

    /* Tek istek - tek cevap deseni icin kisayol: gonder, hemen cevabi oku. */
    JsonValue request(
        const JsonValue& requestValue,
        DWORD timeoutMs = 30000
    );

    JsonValue requestWithProgress(
    const JsonValue& requestValue,
    const std::function<void(const JsonValue&)>& onProgress,
    DWORD timeoutMs = 30000
);

    /* stdin'i kapatir (Python'daki "for line in sys.stdin" dongusu EOF
     * gorup dogal olarak biter), process'in kapanmasini bekler, handle'lari
     * serbest birakir. Destructor da otomatik cagirir, ama pipeline
     * bitince acikca da cagirabilirsin. */
    void stop();

    bool isRunning() const { return running_; }

private:
    HANDLE childStdinWrite_;
    HANDLE childStdoutRead_;
    PROCESS_INFORMATION processInfo_;
    bool running_;
    void forceStop();

    /* kopyalanmasin: bir process handle'ini iki nesnenin "sahiplenmesi"
     * anlamsiz ve tehlikeli olur (cift kapama vb.). Kopyalamayi
     * (copy constructor / operator=) devre disi birakiyoruz. */
    PythonProcess(const PythonProcess&);
    PythonProcess& operator=(const PythonProcess&);
};

#endif // PYTHON_PROCESS_H
