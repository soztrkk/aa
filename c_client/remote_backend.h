/*
 * remote_backend.h
 *
 * IBackend sozlesmesinin IKINCI implementasyonu (birincisi PythonProcess'ti).
 * PythonProcess pipe uzerinden KONUSURKEN, RemoteBackend TCP soketi
 * uzerinden konusur - ama PipelineEngine'in gozunde ikisi de "sadece
 * request()/requestWithProgress() olan bir sey".
 *
 * Bu sinif, server_main.cpp'nin YAPTIGININ TAM TERSINI yapar:
 * server_main.cpp dinler (accept), RemoteBackend BAGLANIR (connect).
 */

#ifndef REMOTE_BACKEND_H
#define REMOTE_BACKEND_H

#include <winsock2.h>
#include <ws2tcpip.h>  
#include <string>
#include <functional>
#include "json_value.h"
#include "backend.h"

class RemoteBackend : public IBackend {
public:
    RemoteBackend();
    ~RemoteBackend();

    /* Belirtilen IP:port'a baglanir. Basarisiz olursa false doner,
     * errorOut'a aciklama yazar - PythonProcess::start() ile AYNI desen. */
    bool connectTo(const std::string& host, int port, std::string& errorOut);

    /* Baglantiyi kapatir. Destructor da otomatik cagirir. */
    void disconnect();

    JsonValue request(const JsonValue& requestValue) override;
    JsonValue requestWithProgress(
        const JsonValue& requestValue,
        const std::function<void(const JsonValue&)>& onProgress) override;
    bool isRunning() const override { return connected_; }

private:
    SOCKET sock_;
    bool connected_;
    std::string recvBuffer_;   // parcali gelen veriyi biriktiren tampon (bkz. server_main.cpp'deki ayni fikir)

    void sendLine(const std::string& text);
    std::string readLine();

    /* kopyalanmasin - PythonProcess'teki AYNI sebep: bir soket handle'ini
     * iki nesnenin sahiplenmesi tehlikeli. */
    RemoteBackend(const RemoteBackend&);
    RemoteBackend& operator=(const RemoteBackend&);
};

#endif // REMOTE_BACKEND_H