/*
 * remote_backend.cpp
 */

#include "remote_backend.h"
#include <stdexcept>

RemoteBackend::RemoteBackend()
    : sock_(INVALID_SOCKET), connected_(false) {}

RemoteBackend::~RemoteBackend() {
    disconnect();   // nesne yok edilirken baglanti hala aciksa kapat
}

bool RemoteBackend::connectTo(const std::string& host, int port, std::string& errorOut) {
    if (connected_) {
        errorOut = "RemoteBackend: zaten bagli";
        return false;
    }

    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        errorOut = "WSAStartup basarisiz";
        return false;
    }

    sock_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock_ == INVALID_SOCKET) {
        errorOut = "socket() basarisiz, kod: " + std::to_string(WSAGetLastError());
        WSACleanup();
        return false;
    }

    sockaddr_in serverAddr;
    ZeroMemory(&serverAddr, sizeof(serverAddr));
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(static_cast<u_short>(port));

    /* inet_pton: "192.168.1.42" gibi metin bir IP adresini, agin
     * anlayacagi ikili (binary) forma cevirir. Basarisiz donerse (yanlis
     * formatli bir string girildiyse) 1'den FARKLI bir deger doner. */
    if (inet_pton(AF_INET, host.c_str(), &serverAddr.sin_addr) != 1) {
        errorOut = "gecersiz IP adresi: " + host;
        closesocket(sock_);
        sock_ = INVALID_SOCKET;
        WSACleanup();
        return false;
    }

    /* connect(): accept()'in CLIENT tarafindaki karsiligi. accept() bir
     * baglanti GELENE kadar bekliyordu (server tarafi); connect() ise
     * BAGLANMAYA CALISIRKEN bekliyor (client tarafi). Sunucu o an dinlemiyor
     * olabilir, yanlis port/IP verilmis olabilir - hepsi burada hata olarak
     * doner. */
    if (connect(sock_, reinterpret_cast<sockaddr*>(&serverAddr), sizeof(serverAddr)) == SOCKET_ERROR) {
        errorOut = "sunucuya baglanilamadi (" + host + ":" + std::to_string(port) +
                    "), kod: " + std::to_string(WSAGetLastError());
        closesocket(sock_);
        sock_ = INVALID_SOCKET;
        WSACleanup();
        return false;
    }

    connected_ = true;
    recvBuffer_.clear();
    return true;
}

void RemoteBackend::disconnect() {
    if (!connected_) return;
    closesocket(sock_);
    sock_ = INVALID_SOCKET;
    WSACleanup();
    connected_ = false;
}

void RemoteBackend::sendLine(const std::string& text) {
    if (!connected_) {
        throw std::runtime_error("RemoteBackend::sendLine: bagli degil");
    }
    std::string withNewline = text + "\n";

    /* NEDEN DONGU: send() de tipki recv() gibi TEK CAGRIDA butun veriyi
     * GONDERMEYI GARANTI ETMEZ - buyuk bir mesajda (orn. Plotly figure_json)
     * bir kismini gonderip "simdilik bu kadar" diyebilir, kalanini SEN
     * tekrar cagirip gondermek zorundasin. Kucuk mesajlarda genelde tek
     * seferde biter ama BUYUK mesajlarda sessizce veri kaybina yol acmamak
     * icin bu dongu SART. */
    int totalSent = 0;
    int toSend = static_cast<int>(withNewline.size());
    while (totalSent < toSend) {
        int n = send(sock_, withNewline.c_str() + totalSent, toSend - totalSent, 0);
        if (n == SOCKET_ERROR) {
            throw std::runtime_error("RemoteBackend::sendLine: gonderim basarisiz (baglanti kopmus olabilir)");
        }
        totalSent += n;
    }
}

std::string RemoteBackend::readLine() {
    /* server_main.cpp'deki readLineFromSocket ile BIREBIR AYNI mantik -
     * orada AYRI bir fonksiyondu (buffer disaridan parametre olarak
     * geliyordu), burada sinifin KENDI recvBuffer_ uyesini kullaniyor. */
    while (true) {
        size_t pos = recvBuffer_.find('\n');
        if (pos != std::string::npos) {
            std::string line = recvBuffer_.substr(0, pos);
            recvBuffer_.erase(0, pos + 1);
            return line;
        }
        char chunk[4096];
        int n = recv(sock_, chunk, sizeof(chunk), 0);
        if (n <= 0) return "";   // baglanti kapandi/hata
        recvBuffer_.append(chunk, n);
    }
}

JsonValue RemoteBackend::request(const JsonValue& requestValue) {
    sendLine(requestValue.dump());
    std::string line = readLine();
    if (line.empty()) {
        throw std::runtime_error("RemoteBackend::request: sunucudan cevap gelmedi (baglanti kopmus olabilir)");
    }
    return JsonValue::parse(line);
}

JsonValue RemoteBackend::requestWithProgress(
    const JsonValue& requestValue,
    const std::function<void(const JsonValue&)>& onProgress) {

    sendLine(requestValue.dump());

    /* PythonProcess::requestWithProgress ile AYNI dongu: "progress" gelirse
     * cagirana ilet ve okumaya DEVAM et, baska bir sey gelirse (ok/error)
     * bu NIHAI cevaptir. */
    while (true) {
        std::string line = readLine();
        if (line.empty()) {
            throw std::runtime_error("RemoteBackend::requestWithProgress: baglanti koptu");
        }
        JsonValue response = JsonValue::parse(line);
        std::string status = response.has("status") ? response.at("status").asString() : "";
        if (status == "progress") {
            if (onProgress) onProgress(response);
            continue;
        }
        return response;
    }
}