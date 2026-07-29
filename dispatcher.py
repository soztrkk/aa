# dispatcher.py

import sys
import json
from session_store import SessionStore
from blocks import create_block

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

        try:
            block = create_block(block_name, params)
            result = block.execute(inputs)
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
