from blocks.base import Block
import pandas as pd
import torch
from torch.utils.data import TensorDataset, DataLoader

class ToTensorBlock(Block):
    """
    DataFrame'i PyTorch tensor'e cevirir. Bu blok "kor" calisir - gelen verinin
    X mi y mi oldugunu bilmez, X/y ayrimi bu bloktan ONCE yapilmis olmalidir.

    parameters:
    - dtype   : str, varsayilan "float32". Ornekler: "float32", "float64", "long", "int64", "int32"
                Classification label'lari icin "long", regresyon/feature tensor'leri icin "float32"
    - squeeze : bool, varsayilan False. True verilirse tek kolonlu [N, 1] DataFrame [N] seklinde
                (1 boyutlu) tensor'e cevrilir. Genelde y dalinda True, X dalinda False kullanilir.

    ONEMLI (squeeze + regresyon): classification'da squeeze=True dogrudur, cunku
    CrossEntropyLoss zaten y: [N] (sinif indeksleri) ve pred: [N, num_classes] bekler,
    sekiller kasitli farklidir. Ama regresyonda (mlp_learner, task_type="regression",
    output_size=1 iken) model ciktisi [N, 1] gelir; y de squeeze=True ile [N] yapilirsa
    MSELoss bu iki farkli sekli SESSIZCE broadcast eder ve yanlis bir loss hesaplar
    (PyTorch bir UserWarning basar ama hata vermez). Bu yuzden regresyon + output_size=1
    senaryosunda y dalinda squeeze=False kullanilmali, y'nin [N, 1] kalmasi gerekir.
    """

    name = "to_tensor"

    DTYPE_MAP = {
        "float32": torch.float32,
        "float64": torch.float64,
        "double": torch.float64,
        "long": torch.long,
        "int64": torch.long,
        "int32": torch.int32,
    }

    def validate(self, inputs: dict):
        super().validate(inputs)
        df = inputs["data"]

        dtype = self.params.get("dtype", "float32")
        if dtype not in self.DTYPE_MAP:
            raise ValueError(
                f"{self.name}: invalid dtype '{dtype}', must be one of {tuple(self.DTYPE_MAP)}"
            )

        non_numeric_cols = [c for c in df.columns if not pd.api.types.is_numeric_dtype(df[c])]
        if non_numeric_cols:
            raise ValueError(
                f"{self.name}: {non_numeric_cols} kolonu/kolonlari sayisal degil, once encode_categorical calistirin"
            )

        if df.isnull().values.any():
            raise ValueError(f"{self.name}: veride eksik (NaN) deger var, once handle_missing_values calistirin")

        squeeze = self.params.get("squeeze", False)
        if squeeze and df.shape[1] != 1:
            raise ValueError(
                f"{self.name}: squeeze=True icin DataFrame tek kolonlu olmali, mevcut kolon sayisi: {df.shape[1]}"
            )

    def run(self, inputs: dict) -> dict:
        df = self.get_data_copy(inputs)

        dtype = self.params.get("dtype", "float32")
        squeeze = self.params.get("squeeze", False)

        tensor = torch.tensor(df.values, dtype=self.DTYPE_MAP[dtype])
        if squeeze:
            tensor = tensor.squeeze(1)

        meta = {
            "shape": list(tensor.shape),
            "dtype": str(tensor.dtype),
            "columns": df.columns.tolist(),
        }

        return {"data": tensor, "meta": meta}


class CreateDataLoaderBlock(Block):
    """
    X ve y tensor'lerini TensorDataset ile satir bazinda eslestirip PyTorch
    DataLoader'a cevirir. Shuffle sirasinda X[i]-y[i] eslesmesinin bozulmamasi
    icin X ve y birlikte, tek bir dataset olarak islenir.

    inputs:
    - X : feature tensor
    - y : label tensor (X ile ayni satir/ornek sayisina sahip olmali)

    parameters:
    - batch_size : int, varsayilan 32
    - shuffle    : bool, varsayilan False. Train seti icin True, test/validation icin False onerilir
    - drop_last  : bool, varsayilan False. batch_size'a tam bolunmeyen son batch atilsin mi
    """

    name = "create_dataloader"

    # base class'in "data" bekleyen validate'ini tamamen override ediyoruz,
    # cunku bu blok tek girdi degil X + y ikilisi bekliyor
    def validate(self, inputs: dict):
        for key in ("X", "y"):
            if key not in inputs or inputs[key] is None:
                raise ValueError(f"{self.name}: '{key}' girdisi eksik")

        X = inputs["X"]
        y = inputs["y"]

        if not isinstance(X, torch.Tensor) or not isinstance(y, torch.Tensor):
            raise ValueError(f"{self.name}: 'X' ve 'y' birer tensor olmali, once to_tensor calistirin")

        if X.shape[0] != y.shape[0]:
            raise ValueError(
                f"{self.name}: X ve y satir sayilari eslesmiyor (X: {X.shape[0]}, y: {y.shape[0]})"
            )

        batch_size = self.params.get("batch_size", 32)
        if not isinstance(batch_size, int) or isinstance(batch_size, bool) or batch_size <= 0:
            raise ValueError(f"{self.name}: 'batch_size' pozitif bir tam sayi olmali")

    def run(self, inputs: dict) -> dict:
        X = inputs["X"]
        y = inputs["y"]

        batch_size = self.params.get("batch_size", 32)
        shuffle = self.params.get("shuffle", False)
        drop_last = self.params.get("drop_last", False)

        dataset = TensorDataset(X, y)
        loader = DataLoader(dataset, batch_size=batch_size, shuffle=shuffle, drop_last=drop_last)

        meta = {
            "num_batches": len(loader),
            "dataset_size": len(dataset),
            "batch_size": batch_size,
            "shuffle": shuffle,
            "drop_last": drop_last,
        }

        return {"data": loader, "meta": meta}

