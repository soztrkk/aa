from abc import ABC, abstractmethod
import pandas as pd


class Block(ABC):
    name = "base_block"

    def __init__(self, params: dict):
        self.params = params or {}

    def validate(self, inputs: dict):
        if "data" not in inputs or inputs["data"] is None:
            raise ValueError(f"{self.name}: 'data' girdisi eksik")
        data = inputs["data"]
        if hasattr(data, "empty") and data.empty:
            raise ValueError(f"{self.name}: gelen veri bos")

    @abstractmethod
    def run(self, inputs: dict) -> dict:
        """
        Iki farkli donus formati desteklenir:

        1) Tek cikisli eski format (mevcut bloklarin cogu):
           {"data": df, "meta": {...}}

        2) Cok cikisli yeni format (orn. train_test_split):
           {"outputs": {"train": df1, "test": df2},
            "meta":    {"train": {...}, "test": {...}}}
        """
        raise NotImplementedError

    def _normalize_result(self, result: dict) -> dict:
        """
        run()'dan gelen sonucu her zaman ayni ic formata cevirir:
        {"outputs": {slot: data, ...}, "meta": {slot: meta_dict, ...}}

        Eger blok eski tek-cikis formatinda ("data" anahtari ile) donuyorsa,
        bunu otomatik olarak "output" isimli tek bir slot'a sarar.
        Boylece mevcut bloklari degistirmemize gerek kalmiyor.
        """
        if "outputs" in result:
            return result

        # eski format -> yeni formata cevir, slot ismi varsayilan olarak "output"
        return {
            "outputs": {"output": result["data"]},
            "meta": {"output": result.get("meta", {})},
        }

    def finalize(self, result: dict) -> dict:
        """Her cikis slotu icin ayri ayri index reset uygular."""
        for slot, data in result["outputs"].items():
            if hasattr(data, "reset_index"):
                result["outputs"][slot] = data.reset_index(drop=True)
        return result

    def validate_output(self, result: dict):
        """Her cikis slotunun bos olup olmadigini ayri ayri kontrol eder."""
        for slot, data in result["outputs"].items():
            if hasattr(data, "empty") and data.empty:
                raise ValueError(f"{self.name}: '{slot}' ciktisi islem sonrasi bos kaldi")

    def get_data_copy(self, inputs: dict):
        return inputs["data"].copy()

    def execute(self, inputs: dict) -> dict:
        self.validate(inputs)
        result = self.run(inputs)
        result = self._normalize_result(result)   # <-- yeni eklenen adim
        result = self.finalize(result)
        self.validate_output(result)
        return result