/*
 * server_main.cpp
 *
 * GPU'lu (server) makinede calisacak program. UI YOK - ekransiz calisir.
 * Tek isi: TCP uzerinden gelen JSON isteklerini AYNEN Python'a (pipe ile)
 * iletmek, cevabi AYNEN client'a geri gondermek. Node graph / dirty
 * propagation / topological siralama gibi hicbir KARAR burada verilmiyor -
 * o mantik hala client makinedeki PipelineEngine'de.
 */

#include <winsock2.h>
#include <ws2tcpip.h>
#include "python_process.h"
#include "json_value.h"
#include <iostream>
#include <string>

#pragma comment(lib, "ws2_32.lib")

/* socket_test_server.cpp'deki tek-seferlik recv()'den FARKLI: burada
 * bir buffer'i FONKSIYON DISINDA (cagiran taraftan referansla) tutuyoruz,
 * cunku tek bir TCP baglantisi uzerinden BIRDEN FAZLA mesaj gelecek -
 * onceki konusmalarimizdaki "TCP stream, mesaj sinirlarini korumaz" konusu
 * tam olarak burada devreye giriyor. */
std::string readLineFromSocket(SOCKET sock, std::string& buffer) {
    while (true) {
        size_t pos = buffer.find('\n');
        if (pos != std::string::npos) {
            std::string line = buffer.substr(0, pos);
            buffer.erase(0, pos + 1);   // tuketileni at, KALANI (varsa bir sonraki mesajin basi) sakla
            return line;
        }
        char chunk[4096];
        int n = recv(sock, chunk, sizeof(chunk), 0);
        if (n <= 0) return "";           // baglanti kapandi ya da hata
        buffer.append(chunk, n);
    }
}

void sendLineToSocket(SOCKET sock, const std::string& text) {
    std::string withNewline = text + "\n";
    send(sock, withNewline.c_str(), (int)withNewline.size(), 0);
}

int main() {
    /* Python'u BASLAT - mevcut PythonProcess, hic degismeden. Bu satir,
     * gui_main.cpp'deki ayni satirla BIREBIR ayni. */
    PythonProcess proc;
    std::string startError;
    std::cout << "Python dispatcher.py baslatiliyor...\n";
    if (!proc.start("python", "dispatcher.py", startError)) {
        std::cerr << "BASLATMA HATASI: " << startError << "\n";
        return 1;
    }
    std::cout << "Python hazir.\n";

    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        std::cerr << "WSAStartup basarisiz\n";
        proc.stop();
        return 1;
    }

    SOCKET listenSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listenSocket == INVALID_SOCKET) {
        std::cerr << "socket() basarisiz, kod: " << WSAGetLastError() << "\n";
        WSACleanup();
        proc.stop();
        return 1;
    }

    sockaddr_in serverAddr;
    ZeroMemory(&serverAddr, sizeof(serverAddr));
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = INADDR_ANY;   // makinedeki HERHANGI bir arayuzden gelen baglantiyi kabul et
    serverAddr.sin_port = htons(5555);

    if (bind(listenSocket, (sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
        std::cerr << "bind() basarisiz, kod: " << WSAGetLastError() << "\n";
        closesocket(listenSocket);
        WSACleanup();
        proc.stop();
        return 1;
    }

    if (listen(listenSocket, 1) == SOCKET_ERROR) {
        std::cerr << "listen() basarisiz, kod: " << WSAGetLastError() << "\n";
        closesocket(listenSocket);
        WSACleanup();
        proc.stop();
        return 1;
    }

    std::cout << "5555 portunda dinleniyor, client bekleniyor...\n";

    /* DIS DONGU: client baglanip kopsa bile, PROGRAM (ve icindeki Python
     * process'i) AYAKTA KALIR, bir sonraki client'i beklemeye devam eder.
     * Bu, onceki konusmalarimizdaki "Python'un omru, tek bir client
     * baglantisindan BAGIMSIZ ve daha uzun" fikrinin tam burada hayata
     * gectigi yer. */
    while (true) {
        SOCKET clientSocket = accept(listenSocket, NULL, NULL);
        if (clientSocket == INVALID_SOCKET) {
            std::cerr << "accept() basarisiz, kod: " << WSAGetLastError() << "\n";
            continue;   // bu client'i atla, dinlemeye devam et
        }
        std::cout << "Bir client baglandi.\n";

        std::string recvBuffer;   // bu client'a OZEL, her yeni baglantida sifirlanir

        /* IC DONGU: bu TEK client baglantisi uzerinden gelen HER mesaji isle. */
        while (true) {
            std::string line = readLineFromSocket(clientSocket, recvBuffer);
            if (line.empty()) {
                std::cout << "Client baglantisi kapandi.\n";
                break;   // ic donguden cik, dis dongude BIR SONRAKI client'i bekle
            }

            JsonValue request;
            try {
                request = JsonValue::parse(line);
            } catch (const std::exception& e) {
                JsonValue errResp = JsonValue::makeObject();
                errResp["status"] = JsonValue("error");
                errResp["message"] = JsonValue(std::string("gecersiz JSON: ") + e.what());
                sendLineToSocket(clientSocket, errResp.dump());
                continue;
            }

            /* requestWithProgress KULLANIYORUZ (sadece request DEGIL) -
             * cunku mlp_learner gibi bloklar egitim SURERKEN ara "progress"
             * satirlari gonderiyor (bkz. dispatcher.py/base.py). Bu ara
             * satirlar ANINDA client'a iletilmezse, GUI'deki canli
             * epoch/loss gostergesi hic calismaz. */
            JsonValue response = proc.requestWithProgress(request,
                [&](const JsonValue& progress) {
                    sendLineToSocket(clientSocket, progress.dump());
                });

            sendLineToSocket(clientSocket, response.dump());
        }

        closesocket(clientSocket);
        /* NOT: proc (Python) burada KAPANMIYOR - dis donguye donup bir
         * sonraki client'i bekliyoruz, Python process'i ve icindeki
         * session store aynen ayakta kaliyor. */
    }

    // (bu satira normalde hic ulasilmaz, Ctrl+C ile program kapatilir)
    closesocket(listenSocket);
    WSACleanup();
    proc.stop();
    return 0;
}