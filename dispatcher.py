# dispatcher.py

import sys
import os
import json
from session_store import SessionStore
from blocks import create_block
from blocks.graphs_and_views import data_preview

store = SessionStore()


def handle_message(msg: dict) -> dict:
    """
    Yeni mesaj formati:
    {
        "op": "run_block",
        "node_id": "node_3",              # C tarafinin verdigi SABIT node kimligi
        "block": "handle_missing_values",
        "params": {...},
        "inputs": {"data": "node_2:output"}   # slot_adi -> ref string'i
    }
    """
    # İlk olarak gelen mesajdan bilgileri al

    op = msg.get("op")

    if op == "run_block":
        node_id = msg.get("node_id")
        block_name = msg.get("block")
        params = msg.get("params", {})
        input_refs = msg.get("inputs", {})  # orn. {"data": "node_2:output"}

        # her input slotu icin, verilen ref'ten gercek veriyi cek
        inputs = {}
        for slot_name, ref in input_refs.items():
            try:
                inputs[slot_name] = store.get(ref)
            except ValueError as e:
                return {"status": "error", "node_id": node_id, "message": str(e)}

        # egitim gibi uzun suren bloklar icin: run() SURERKEN (orn. her epoch
        # sonunda) block.report_progress(...) cagrilirsa, bu HEMEN (nihai
        # sonuc donmeden ONCE) bir "status":"progress" satiri olarak C
        # tarafina yollanir. report_progress hic cagrilmazsa (bloklarin
        # cogunlugu) burasi hic calismaz, davranis eskisiyle birebir ayni.
        def emit_progress(payload: dict) -> None:
            progress_msg = {"status": "progress", "node_id": node_id}
            progress_msg.update(payload)
            print(json.dumps(progress_msg), flush=True)

        try:
            block = create_block(block_name, params)
            result = block.execute(inputs, progress_cb=emit_progress)
        except Exception as e:
            return {"status": "error", "node_id": node_id, "message": str(e)}

        # KRITIK ADIM: bu node'a ait ONCEKI tum ciktilari temizle,
        # sonra yeni ciktilari yaz. Bu, hem eski veriyi otomatik siler
        # hem de onceki calistirmada farkli sayida slot uretilmisse
        # (orn. eskiden train+test vardi, simdi tek cikis oldu) temizlik saglar.
        store.clear_node(node_id)

        outputs_response = {}
        for slot_name, data_obj in result["outputs"].items():
            ref = store.set(node_id, slot_name, data_obj)
            outputs_response[slot_name] = {
                "ref": ref,
                "meta": result["meta"].get(slot_name, {}),
            }

        return {"status": "ok", "node_id": node_id, "outputs": outputs_response}

    elif op == "delete_node":
        # kullanici bir node'u pipeline'dan tamamen sildiginde C bunu gonderir
        node_id = msg.get("node_id")
        store.delete_node(node_id)
        return {"status": "ok", "node_id": node_id}

    elif op == "get_meta":
        # sadece var olan bir ref'in meta bilgisini tekrar dondurur, hicbir sey calistirmaz
        ref = msg.get("ref")
        try:
            data = store.get(ref)
        except ValueError as e:
            return {"status": "error", "message": str(e)}

        meta = {
            "shape": list(data.shape) if hasattr(data, "shape") else None,
            "columns": data.columns.tolist() if hasattr(data, "columns") else None,
        }
        return {"status": "ok", "ref": ref, "meta": meta}

    elif op == "get_preview":
        # GUI'nin bir node calistiktan sonra OTOMATIK gostermek istedigi
        # "ilk N satir" onizlemesi - hicbir node/blok calistirmiyor, sadece
        # var olan bir ref'in ustunde data_preview'i (bkz.
        # blocks/graphs_and_views.py) dogrudan cagiriyor. Buyuk veri (tum
        # DataFrame) burada da C tarafina GITMIYOR - sadece kucuk/sinirli
        # onizleme meta'si donuyor, ayni data_preview blogu gibi.
        ref = msg.get("ref")
        row_count = msg.get("row_count", 5)
        try:
            data = store.get(ref)
            meta = data_preview(data, row_count=row_count, preview_type="head")
        except Exception as e:
            return {"status": "error", "message": str(e)}
        return {"status": "ok", "ref": ref, "meta": meta}

    elif op == "export_csv":
        # Kullanici GUI'de "CSV'ye Aktar" butonuna bastiginda: TUM veri
        # (onizleme degil) dogrudan Python tarafinda diske yaziliyor - hicbir
        # zaman JSON mesaji olarak C tarafina gonderilmiyor (CLAUDE.md'deki
        # "buyuk veri C tarafina gitmez" prensibi burada da korunuyor).
        ref = msg.get("ref")
        file_path = msg.get("file_path")
        try:
            data = store.get(ref)
            if not hasattr(data, "to_csv"):
                return {"status": "error", "message": f"'{ref}' bir DataFrame degil, CSV'ye aktarilamaz"}
            out_dir = os.path.dirname(file_path)
            if out_dir:
                os.makedirs(out_dir, exist_ok=True)
            data.to_csv(file_path, index=False)
            row_count = len(data)
        except Exception as e:
            return {"status": "error", "message": str(e)}
        return {"status": "ok", "ref": ref, "file_path": file_path, "row_count": row_count}

    else:
        return {"status": "error", "message": f"unknown op: {op}"}


def main_loop():
    for line in sys.stdin:
        line = line.strip()
        if not line:
            continue
        try:
            msg = json.loads(line)
        except json.JSONDecodeError:
            response = {"status": "error", "message": "invalid JSON received"}
        else:
            response = handle_message(msg)
        print(json.dumps(response), flush=True)


if __name__ == "__main__":
    main_loop()
