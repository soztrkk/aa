"""
Tum blok zincirini uctan uca test eder: load_csv -> handle_missing_values ->
remove_duplicates -> handle_outliers -> encode_categorical -> scale_features ->
drop_columns (X/y ayrimi) -> to_tensor -> create_dataloader -> mlp_learner.

handle_message dogrudan (subprocess olmadan) cagrilir, boylece dispatcher.store'a
erisip her adimin GERCEK DataFrame/tensor icerigini de yazdirabiliyoruz
(normalde C tarafina sadece ref+meta gider, ama burada dogrulama amacli
store'un icine bakiyoruz).
"""

import dispatcher
from dispatcher import handle_message


def run_step(title, msg):
    print(f"\n{'=' * 70}\n{title}\n{'=' * 70}")
    print("REQUEST :", msg)
    response = handle_message(msg)
    print("RESPONSE:", response)
    assert response["status"] == "ok", f"BEKLENMEYEN HATA: {response}"
    return response


def preview(ref, n=20):
    """store'daki gercek veriyi (DataFrame/tensor) dogrudan gosterir."""
    obj = dispatcher.store.get(ref)
    print(f"--- '{ref}' icerik onizleme ---")
    if hasattr(obj, "head"):
        print(obj.head(n))
    else:
        print(obj)


def main():
    # 1) CSV yukle
    r1 = run_step("1) load_csv", {
        "op": "run_block", "node_id": "node_1", "block": "load_csv",
        "params": {"file_path": "test_data_full.csv"}, "inputs": {},
    })
    ref1 = r1["outputs"]["output"]["ref"]
    preview(ref1)

    # 2) eksik degerleri doldur (mean)
    r2 = run_step("2) handle_missing_values (mean)", {
        "op": "run_block", "node_id": "node_2", "block": "handle_missing_values",
        "params": {"strategy": "mean"}, "inputs": {"data": ref1},
    })
    ref2 = r2["outputs"]["output"]["ref"]
    preview(ref2)

    # 3) tekrar eden satirlari sil
    r3 = run_step("3) remove_duplicates", {
        "op": "run_block", "node_id": "node_3", "block": "remove_duplicates",
        "params": {"keep": "first"}, "inputs": {"data": ref2},
    })
    ref3 = r3["outputs"]["output"]["ref"]
    preview(ref3)

    # 4) outlier'lari (age kolonunda) cap'le
    r4 = run_step("4) handle_outliers (iqr, cap)", {
        "op": "run_block", "node_id": "node_4", "block": "handle_outliers",
        "params": {"method": "iqr", "action": "cap", "columns": ["age"]},
        "inputs": {"data": ref3},
    })
    ref4 = r4["outputs"]["output"]["ref"]
    preview(ref4)

    # 5) city kolonunu label encode et
    r5 = run_step("5) encode_categorical (label, city)", {
        "op": "run_block", "node_id": "node_5", "block": "encode_categorical",
        "params": {"method": "label", "columns": ["city"]}, "inputs": {"data": ref4},
    })
    ref5 = r5["outputs"]["output"]["ref"]
    preview(ref5)

    # 6) age/income kolonlarini minmax olcekle
    r6 = run_step("6) scale_features (minmax, age+income)", {
        "op": "run_block", "node_id": "node_6", "block": "scale_features",
        "params": {"method": "minmax", "columns": ["age", "income"]},
        "inputs": {"data": ref5},
    })
    ref6 = r6["outputs"]["output"]["ref"]
    preview(ref6)

    # 7) X = target disindaki kolonlar
    r7 = run_step("7) drop_columns -> X (target'i at)", {
        "op": "run_block", "node_id": "node_7", "block": "drop_columns",
        "params": {"columns": ["target"]}, "inputs": {"data": ref6},
    })
    ref_X = r7["outputs"]["output"]["ref"]
    preview(ref_X)

    # 8) y = sadece target kolonu
    r8 = run_step("8) drop_columns -> y (sadece target kalsin)", {
        "op": "run_block", "node_id": "node_8", "block": "drop_columns",
        "params": {"columns": ["age", "income", "city"]}, "inputs": {"data": ref6},
    })
    ref_y = r8["outputs"]["output"]["ref"]
    preview(ref_y)

    # 9) X'i tensor'e cevir
    r9 = run_step("9) to_tensor (X, float32)", {
        "op": "run_block", "node_id": "node_9", "block": "to_tensor",
        "params": {"dtype": "float32", "squeeze": False}, "inputs": {"data": ref_X},
    })
    ref_X_tensor = r9["outputs"]["output"]["ref"]
    preview(ref_X_tensor)

    # 10) y'yi tensor'e cevir (classification -> long, squeeze True)
    r10 = run_step("10) to_tensor (y, long, squeeze)", {
        "op": "run_block", "node_id": "node_10", "block": "to_tensor",
        "params": {"dtype": "long", "squeeze": True}, "inputs": {"data": ref_y},
    })
    ref_y_tensor = r10["outputs"]["output"]["ref"]
    preview(ref_y_tensor)

    # 11) DataLoader olustur
    r11 = run_step("11) create_dataloader", {
        "op": "run_block", "node_id": "node_11", "block": "create_dataloader",
        "params": {"batch_size": 4, "shuffle": True},
        "inputs": {"X": ref_X_tensor, "y": ref_y_tensor},
    })
    ref_loader = r11["outputs"]["output"]["ref"]

    # 12) MLP egit (classification, 2 sinif: target 0/1)
    r12 = run_step("12) mlp_learner (classification)", {
        "op": "run_block", "node_id": "node_12", "block": "mlp_learner",
        "params": {
            "task_type": "classification",
            "output_size": 2,
            "layer_config": [{"type": "linear", "size": 16, "activation": "relu"}],
            "epochs": 5,
            "learning_rate": 0.01,
        },
        "inputs": {"train_dataloader": ref_loader},
    })
    print("\nEgitim ozeti:")
    print("  final_loss   :", r12["outputs"]["output"]["meta"]["final_loss"])
    print("  loss_history :", r12["outputs"]["output"]["meta"]["loss_history"])

    # --- hata senaryosu: silinen node'a erisim ---
    run_step("13) delete_node (node_1)", {"op": "delete_node", "node_id": "node_1"})
    r14 = handle_message({"op": "get_meta", "ref": ref1})
    print("\n14) get_meta (silinmis ref, HATA beklenir):", r14)
    assert r14["status"] == "error"

    print(f"\n{'=' * 70}")
    print("TUM ZINCIR BASARIYLA TAMAMLANDI (load_csv -> ... -> mlp_learner)")
    print(f"{'=' * 70}")


if __name__ == "__main__":
    main()
