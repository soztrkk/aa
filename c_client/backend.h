/*
 * backend.h
 *
 * ADIM 7 (network gecisi): PipelineEngine'in Python tarafiyla NASIL
 * konustugunu (pipe mi, TCP soket mi) PipelineEngine'in kendisinden
 * tamamen ayirmak icin bir "sozlesme" (interface) tanimliyoruz.
 *
 * FIKIR: PipelineEngine su ana kadar dogrudan PythonProcess& tutuyordu.
 * Simdi bunun yerine IBackend& tutacak - IBackend, sadece "bende
 * request() ve requestWithProgress() fonksiyonlari VAR" diye soz veren,
 * gercek bir isi kendisi YAPMAYAN soyut (abstract) bir siniftir.
 *
 * IKI SINIF bu sozlesmeye uyacak:
 *   - PythonProcess : mevcut, pipe uzerinden (hic degismiyor)
 *   - RemoteBackend  : yeni, TCP soketi uzerinden (ileride yazilacak)
 *
 * PipelineEngine hangisiyle calistigini hic bilmez/bilmek zorunda
 * degildir - main()'de hangisi verilirse PipelineEngine onunla calisir.
 */

#ifndef BACKEND_H   // bu basligin ayni derlemede iki kez islenmesini engeller
#define BACKEND_H   // include guard makrosunu tanimlar

#include <string>
#include <functional>
#include "json_value.h"

/* IBackend: soyut (abstract) bir sinif - kendisinden DOGRUDAN bir nesne
 * OLUSTURULAMAZ (bkz. asagidaki "= 0" aciklamasi). Sadece bu siniftan
 * TUREYEN (PythonProcess, RemoteBackend gibi) siniflarin nesneleri
 * olusturulabilir. */
class IBackend {
public:
    /* Bir sinif baska bir siniftan TUREYEREK kullanilacaksa, taban
     * sinifin (IBackend) yikicisinin (destructor) MUTLAKA virtual olmasi
     * gerekir - bu bir C++ kuralidir, ileride hata kaynagi olmamasi icin
     * simdiden dogru yaziyoruz. */
    virtual ~IBackend() {}

    /* Tek istek - tek cevap: bir JSON mesaji gonder, karsiligini bekle,
     * cevabi doner. */
    virtual JsonValue request(const JsonValue& requestValue) = 0;

    /* request()'in "ilerleme bildirimli" hali - egitim gibi uzun suren
     * bloklarda, nihai cevaptan ONCE gelen ara "progress" satirlarini
     * onProgress callback'i ile bildirir. */
    virtual JsonValue requestWithProgress(
        const JsonValue& requestValue,
        const std::function<void(const JsonValue&)>& onProgress) = 0;

    /* Baglanti/process hala ayakta mi? PythonProcess icin: process hala
     * calisiyor mu. RemoteBackend icin: soket hala bagli mi. */
    virtual bool isRunning() const = 0;
};

#endif // BACKEND_H  -- basligin sonu