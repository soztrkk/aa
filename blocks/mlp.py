# blocks/mlp.py
#
# Bu modul, MLP (Multi-Layer Perceptron) model egitimi ve tahmin bloklarini icerir

import torch
import torch.nn as nn
from blocks.base import Block


class SimpleMLP(nn.Module):
    """
    Katman yapisi tamamen disaridan, layer_config listesi ile belirlenen
    esnek bir MLP mimarisi. Sabit "linear->activation" siralamasi zorunlu degil,
    her katman kendi tipini ve ayarlarini tasir (orn. ust uste iki linear,
    ya da aktivasyonsuz bir katman, ya da dropout eklenebilir).

    layer_config ornegi:
    [
        {"type": "linear", "size": 64, "activation": "relu"},
        {"type": "dropout", "rate": 0.3},
        {"type": "linear", "size": 32, "activation": "tanh"},
        {"type": "linear", "size": 16}   # activation verilmezse o katmanda aktivasyon olmaz
    ]

    Son katman (gizli katmandan output_size'a gecis) her zaman bu sinif
    tarafindan otomatik eklenir, kullanicinin layer_config'ine dahil edilmez.
    Bu, output_size bilgisinin iki farkli yerde (hem ayri parametre hem
    layer_config icinde) tutulup celiskili olmasini engeller.
    """

    # aktivasyon isimlerini gercek PyTorch modullerine cevirmek icin ortak harita
    ACTIVATION_MAP = {
        "relu": nn.ReLU,
        "tanh": nn.Tanh,
        "sigmoid": nn.Sigmoid,
    }

    def __init__(self, input_size: int, layer_config: list, output_size: int):
        super().__init__()

        # katmanlari sirayla bir listede topluyoruz, sonra nn.Sequential ile birlestiriyoruz
        layers = []
        # current_size, o ana kadar gelen verinin boyutunu takip eder
        # ilk basta bu, disaridan verilen input_size'dir
        current_size = input_size

        # layer_config listesindeki her elemani sirayla isle
        for layer_spec in layer_config:
            layer_type = layer_spec.get("type")

            if layer_type == "linear":
                # bu katmanin cikis boyutu, kullanicinin verdigi 'size' degeri
                out_size = layer_spec["size"]
                layers.append(nn.Linear(current_size, out_size))
                # bir sonraki katmanin girisi, bu katmanin cikisi olacak
                current_size = out_size

                # aktivasyon opsiyonel: verilmezse bu katmanda hic aktivasyon eklenmez
                activation_name = layer_spec.get("activation")
                if activation_name is not None:
                    if activation_name not in self.ACTIVATION_MAP:
                        raise ValueError(f"Desteklenmeyen activation: {activation_name}")
                    layers.append(self.ACTIVATION_MAP[activation_name]())

            elif layer_type == "dropout":
                # dropout, egitim sirasinda rastgele noronlari "kapatarak" ezberlemeyi (overfitting) azaltir
                rate = layer_spec.get("rate", 0.5)
                layers.append(nn.Dropout(p=rate))
                # dropout, verinin boyutunu (current_size) DEGISTIRMEZ, bu yuzden current_size guncellenmiyor

            else:
                # bilinmeyen bir katman tipi verilirse acik bir hata firlat
                raise ValueError(f"Desteklenmeyen layer type: {layer_type}")

        # SON katman: gizli katmanlardan cikis boyutuna (output_size) gecis
        # bu katman her zaman otomatik eklenir, kullanicinin layer_config'inde belirtmesine gerek yok
        layers.append(nn.Linear(current_size, output_size))

        # tum katmanlari sirali bir sekilde calisacak tek bir modul haline getir
        self.network = nn.Sequential(*layers)

    def forward(self, x):
        # ileri yayilim: veri, tanimlanan katmanlar sirasiyla islenip cikis uretilir
        return self.network(x)


class MLPLearnerBlock(Block):
    """
    MLP modelini tanimlar ve verilen train_dataloader uzerinde egitir.
    Egitim sonucunda egitilmis modeli (nn.Module objesi) session store'a kaydeder.

    Supported params:
    - layer_config   : list of dict, gizli katman tanimlari (yukaridaki SimpleMLP docstring'ine bakiniz)
                        verilmezse varsayilan olarak tek bir 64-noronlu relu katmani kullanilir
    - task_type      : "classification" / "regression" (zorunlu)
    - output_size    : classification icin sinif sayisi (zorunlu),
                        regression icin genelde 1 (verilmezse 1 varsayilir)
    - learning_rate  : float, varsayilan 0.001
    - epochs         : int, varsayilan 10
    - optimizer      : "adam" / "sgd", varsayilan "adam"
    """

    name = "mlp_learner"

    VALID_TASK_TYPES = ("classification", "regression")
    VALID_OPTIMIZERS = ("adam", "sgd")
    VALID_LAYER_TYPES = ("linear", "dropout")

    def validate(self, inputs: dict):
        # bu blok base'deki "data" kontrolunu kullanmiyor, cunku girdi
        # "train_dataloader" adinda farkli bir anahtar altinda geliyor
        if "train_dataloader" not in inputs or inputs["train_dataloader"] is None:
            raise ValueError(f"{self.name}: 'train_dataloader' girdisi eksik")

        # task_type zorunlu bir parametre, verilmemisse anlamli bir hata ver
        task_type = self.params.get("task_type")
        if task_type not in self.VALID_TASK_TYPES:
            raise ValueError(
                f"{self.name}: 'task_type' gecerli olmali -> {self.VALID_TASK_TYPES}, gelen: {task_type}"
            )

        # classification icin output_size (sinif sayisi) zorunlu, cunku bu veriden
        # guvenli bir sekilde otomatik cikarilamaz
        if task_type == "classification" and "output_size" not in self.params:
            raise ValueError(f"{self.name}: classification icin 'output_size' (sinif sayisi) zorunlu")

        optimizer = self.params.get("optimizer", "adam")
        if optimizer not in self.VALID_OPTIMIZERS:
            raise ValueError(f"{self.name}: gecersiz optimizer -> {optimizer}")

        # layer_config kontrolu: liste olmali, her eleman gecerli bir 'type' ve gerekli alanlari tasimali
        layer_config = self.params.get("layer_config", [])
        if not isinstance(layer_config, list):
            raise ValueError(f"{self.name}: 'layer_config' bir liste olmali")

        for i, layer_spec in enumerate(layer_config):
            layer_type = layer_spec.get("type")
            if layer_type not in self.VALID_LAYER_TYPES:
                raise ValueError(f"{self.name}: layer_config[{i}] gecersiz type -> {layer_type}")
            # linear katman icin 'size' zorunlu, yoksa cikis boyutu bilinemez
            if layer_type == "linear" and "size" not in layer_spec:
                raise ValueError(f"{self.name}: layer_config[{i}] icin 'size' zorunlu (linear katman)")

    def run(self, inputs: dict) -> dict:
        train_dataloader = inputs["train_dataloader"]

        task_type = self.params.get("task_type")
        # layer_config verilmemisse basit bir varsayilan mimari kullan
        layer_config = self.params.get(
            "layer_config",
            [{"type": "linear", "size": 64, "activation": "relu"}]
        )
        learning_rate = self.params.get("learning_rate", 0.001)
        epochs = self.params.get("epochs", 10)
        optimizer_name = self.params.get("optimizer", "adam")
        # regression icin output_size genelde 1 (tek bir sayi tahmin edilir)
        output_size = self.params.get("output_size", 1)

        # input_size'i otomatik cikarmak icin dataloader'dan TEK bir batch cekiyoruz
        # (egitimi bozmadan, sadece shape bilgisini okumak icin)
        sample_X, sample_y = next(iter(train_dataloader))
        input_size = sample_X.shape[1]   # (batch_size, feature_sayisi) -> ikinci boyut feature sayisi

        # modeli, esnek layer_config'e gore olustur
        model = SimpleMLP(
            input_size=input_size,
            layer_config=layer_config,
            output_size=output_size,
        )

        # task_type'a gore dogru loss fonksiyonunu sec
        # classification: CrossEntropyLoss (icinde softmax barindirir, y'nin "long" tipinde
        # ve sinif indeksleri (0,1,2...) seklinde olmasini bekler)
        # regression: MSELoss (y'nin float tipinde olmasini bekler)
        if task_type == "classification":
            loss_function = nn.CrossEntropyLoss()
        else:
            loss_function = nn.MSELoss()

        # optimizer'i sec ve model parametreleriyle baglat
        if optimizer_name == "adam":
            optimizer = torch.optim.Adam(model.parameters(), lr=learning_rate)
        else:
            optimizer = torch.optim.SGD(model.parameters(), lr=learning_rate)

        # ONEMLI: egitim boyunca model 'train' modunda olmali
        # (Dropout gibi katmanlar varsa dogru (aktif) davransin diye)
        model.train()

        # her epoch'un ortalama loss'unu burada topluyoruz, meta'da donmek icin
        loss_history = []

        for epoch in range(epochs):
            epoch_losses = []

            # dataloader, her cagrildiginda (X_batch, y_batch) ciftini dondurur
            # X ve y'nin ayni sirada/eslesmis olmasi create_dataloader blogunda garanti edilmisti
            for X_batch, y_batch in train_dataloader:
                # her batch'ten once gradyanlari sifirla, yoksa onceki batch'in
                # gradyanlariyla birikerek yanlis guncelleme yapilir
                optimizer.zero_grad()

                # ileri yayilim (forward pass): model tahmin uretir
                predictions = model(X_batch)

                # tahmin ile gercek deger arasindaki hatayi (loss) hesapla
                loss = loss_function(predictions, y_batch)

                # geri yayilim (backward pass): gradyanlari hesapla
                loss.backward()

                # optimizer, hesaplanan gradyanlara gore model agirliklarini gunceller
                optimizer.step()

                # bu batch'in loss degerini kaydet (sadece sayi olarak, tensor grafinden ayir)
                epoch_losses.append(loss.item())

            # bu epoch'un ortalama loss'unu hesapla ve gecmise ekle
            epoch_avg_loss = sum(epoch_losses) / len(epoch_losses)
            loss_history.append(epoch_avg_loss)

        # meta bilgisi: egitim surecinin ozeti
        meta = {
            "task_type": task_type,
            "input_size": input_size,
            "output_size": output_size,
            "layer_config": layer_config,
            "epochs": epochs,
            "final_loss": loss_history[-1],
            "loss_history": loss_history,
        }

        # DIKKAT: bu blogun ciktisi bir DataFrame degil, egitilmis bir PyTorch modeli (nn.Module).
        # Base class'taki finalize/validate_output metodlari hasattr kontrolu yaptigi icin
        # (reset_index, empty gibi ozellikler model objesinde olmadigindan) otomatik atlanir,
        # ekstra bir islem yapmamiza gerek yok.
        return {"data": model, "meta": meta}