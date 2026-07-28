from dispatcher import handle_message

# ilk mesaj: load_csv, henuz onceki bir bloktan gelen input yok
msg1 = {
    "op": "run_block",
    "node_id": "node_1",
    "block": "load_csv",
    "params": {"file_path": "test_data.csv"},
    "inputs": {},
}
response1 = handle_message(msg1)
print("Response 1:", response1)

# ikinci mesaj: handle_missing_values, bir onceki bloğun "output" slot'unu kullaniyoruz
msg2 = {
    "op": "run_block",
    "node_id": "node_2",
    "block": "handle_missing_values",
    "params": {"strategy": "mean"},
    "inputs": {"data": response1["outputs"]["output"]["ref"]},
}
response2 = handle_message(msg2)
print("Response 2:", response2)

# ucuncu mesaj: remove_duplicates, node_2'nin ciktisini kullaniyoruz
msg3 = {
    "op": "run_block",
    "node_id": "node_3",
    "block": "remove_duplicates",
    "params": {},
    "inputs": {"data": response2["outputs"]["output"]["ref"]},
}
response3 = handle_message(msg3)
print("Response 3:", response3)

# dorduncu mesaj: handle_outliers, node_3'un ciktisini kullaniyoruz
msg4 = {
    "op": "run_block",
    "node_id": "node_4",
    "block": "handle_outliers",
    "params": {"method": "iqr", "action": "cap"},
    "inputs": {"data": response3["outputs"]["output"]["ref"]},
}
response4 = handle_message(msg4)
print("Response 4:", response4)

# hata senaryosu testi: olmayan bir blok ismi gonderelim
msg5 = {
    "op": "run_block",
    "node_id": "node_5",
    "block": "nonexistent_block",
    "params": {},
    "inputs": {},
}
response5 = handle_message(msg5)
print("Response 5 (hata bekleniyor):", response5)

# delete_node testi
response6 = handle_message({"op": "delete_node", "node_id": "node_4"})
print("Response 6 (delete_node):", response6)

# get_meta testi: silinen bir node'un ref'i artik bulunamamali (hata bekleniyor)
response7 = handle_message({"op": "get_meta", "ref": response4["outputs"]["output"]["ref"]})
print("Response 7 (hata bekleniyor):", response7)

# Data Preview testi
msg8 = {
    "op": "run_block",
    "node_id": "node_8",
    "block": "data_preview",
    "params": {
        "row_count": 5,
        "preview_type": "head"
    },
    "inputs": {
        "data": response2["outputs"]["output"]["ref"]
    }
}

response8 = handle_message(msg8)
print("Response 8 (data preview):", response8)

# Dataset Summary testi
msg9 = {
    "op": "run_block",
    "node_id": "node_9",
    "block": "dataset_summary",
    "params": {},
    "inputs": {
        "data": response2["outputs"]["output"]["ref"]
    }
}

response9 = handle_message(msg9)
print("Response 9 (dataset summary):", response9)


# Histogram testi
msg10 = {
    "op": "run_block",
    "node_id": "node_10",
    "block": "plot_histogram",
    "params": {
        "column": "age",
        "bins": 10
    },
    "inputs": {
        "data": response2["outputs"]["output"]["ref"]
    }
}

response10 = handle_message(msg10)
print("Response 10 (histogram):", response10)
