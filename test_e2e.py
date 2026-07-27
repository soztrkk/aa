"""
Gercek uctan uca test: dispatcher.py'yi AYRI BIR SUBPROCESS olarak baslatir,
C tarafinin yapacagi gibi stdin'e satir satir JSON mesaj yazar ve stdout'tan
satir satir JSON cevap okur.

Bu, bir onceki test'ten (handle_message'i dogrudan Python icinde cagirmak)
farkli olarak, GERCEKTEN "C tarafina gidecek" veriyi test eder: json.dumps
ile serialize edilip, satir + newline + flush ile yazilip, karsi taraftan
json.loads ile geri okunabiliyor mu.

Kontrol edilen noktalar:
- Gonderilen JSON, dispatcher tarafindan dogru parse ediliyor mu (okuma)
- Donen JSON gercekten sadece ref + meta iceriyor mu, DataFrame'in kendisi
  hicbir sekilde stdout'a sizmiyor mu (gonderme / veri sizintisi kontrolu)
- Zincirleme (bir onceki cikisin ref'ini bir sonraki input'a verme) dogru
  calisiyor mu
"""

import json
import subprocess
import sys


def send(proc, msg: dict) -> dict:
    """Bir mesaji subprocess'in stdin'ine yazar, stdout'tan cevabi okur.
    Burada stdin/stdout uzerinden GERCEKTEN akan ham JSON metnini de gosteriyoruz -
    yani C tarafina tam olarak ne gidecekse onu."""
    raw_request = json.dumps(msg)
    print(f"\n>>> STDIN'e yazilan ham JSON (C -> Python):\n{raw_request}")

    proc.stdin.write(raw_request + "\n")
    proc.stdin.flush()

    raw_response = proc.stdout.readline()
    print(f"<<< STDOUT'tan okunan ham JSON (Python -> C):\n{raw_response.strip()}")

    return json.loads(raw_response)


def main():
    proc = subprocess.Popen(
        [sys.executable, "dispatcher.py"],
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        bufsize=1,
    )

    try:
        # 1) CSV yukle
        r1 = send(proc, {
            "op": "run_block",
            "node_id": "node_1",
            "block": "load_csv",
            "params": {"file_path": "test_data.csv"},
            "inputs": {},
        })
        assert r1["status"] == "ok", r1
        assert "data" not in json.dumps(r1)  # DataFrame verisi sizmis mi kontrolu (kaba ama isaretci)

        # 2) eksik degerleri doldur (mean)
        r2 = send(proc, {
            "op": "run_block",
            "node_id": "node_2",
            "block": "handle_missing_values",
            "params": {"strategy": "mean"},
            "inputs": {"data": r1["outputs"]["output"]["ref"]},
        })
        assert r2["status"] == "ok", r2

        # 3) tekrar eden satirlari temizle
        r3 = send(proc, {
            "op": "run_block",
            "node_id": "node_3",
            "block": "remove_duplicates",
            "params": {"keep": "first"},
            "inputs": {"data": r2["outputs"]["output"]["ref"]},
        })
        assert r3["status"] == "ok", r3

        # 4) outlier'lari (iqr yontemiyle) cap'le
        r4 = send(proc, {
            "op": "run_block",
            "node_id": "node_4",
            "block": "handle_outliers",
            "params": {"method": "iqr", "action": "cap"},
            "inputs": {"data": r3["outputs"]["output"]["ref"]},
        })
        assert r4["status"] == "ok", r4

        # 5) get_meta: hicbir islem yapmadan sadece meta'yi tekrar sorgula
        r5 = send(proc, {"op": "get_meta", "ref": r4["outputs"]["output"]["ref"]})
        assert r5["status"] == "ok", r5

        # 6) delete_node: node_4'u sil, sonrasinda ayni ref artik bulunamamali
        r6 = send(proc, {"op": "delete_node", "node_id": "node_4"})
        assert r6["status"] == "ok", r6

        r7 = send(proc, {"op": "get_meta", "ref": r4["outputs"]["output"]["ref"]})
        assert r7["status"] == "error", r7  # silindikten sonra hata beklenir

        print(f"\n{'=' * 60}")
        print("TUM ADIMLAR BASARIYLA TAMAMLANDI - stdin/stdout uzerinden gercek JSON akisi dogrulandi")
        print(f"{'=' * 60}")

    finally:
        proc.stdin.close()
        proc.terminate()
        stderr_output = proc.stderr.read()
        if stderr_output.strip():
            print("\n--- subprocess STDERR ---")
            print(stderr_output)


if __name__ == "__main__":
    main()
