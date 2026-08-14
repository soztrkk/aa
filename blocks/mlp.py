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

            elif layer_type == "batchnorm":
                # batchnorm, bir onceki linear katmanin cikisini normalize ederek
                # egitimi hizlandirir/stabilize eder - bu yuzden HER ZAMAN bir
                # linear katmandan SONRA gelmeli (current_size'a gore boyutlanir)
                layers.append(nn.BatchNorm1d(current_size))
                # batchnorm de verinin boyutunu (current_size) DEGISTIRMEZ

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
    VALID_LAYER_TYPES = ("linear", "dropout", "batchnorm")

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

            # C tarafina (varsa) bu epoch'un bittigini HEMEN bildir - egitim
            # daha SURERKEN canli ilerleme gostermek icin (bkz. blocks/base.py
            # report_progress). progress_cb verilmediyse (orn. main.cpp/
            # interactive_main.cpp gibi eski istemciler) bu cagri no-op'tur.
            self.report_progress({
                "epoch": epoch + 1,
                "epochs": epochs,
                "loss": epoch_avg_loss,
            })

        # meta bilgisi: egitim surecinin ozeti
        meta = {
            "task_type": task_type,
            "input_size": input_size,
            "output_size": output_size,
            "layer_config": layer_config,
            "epochs": epochs,
            "final_loss": loss_history[-1],
            "loss_history": loss_history,
            # C tarafinda (gui_app.cpp -> result_display_imgui.cpp) bu alan
            # sayesinde egitim ozeti + epoch-epoch log gorunumu secilir
            "output_type": "training_log",
        }

        # DIKKAT: bu blogun ciktisi bir DataFrame degil, egitilmis bir PyTorch modeli (nn.Module).
        # Base class'taki finalize/validate_output metodlari hasattr kontrolu yaptigi icin
        # (reset_index, empty gibi ozellikler model objesinde olmadigindan) otomatik atlanir,
        # ekstra bir islem yapmamiza gerek yok.
        return {"data": model, "meta": meta}


class DeepMLPLearnerBlock(Block):
    """
    mlp_learner ile AYNI SimpleMLP mimarisini kullanir, ama varsayilanlari
    ve ek parametreleri "daha sağlam" (robust) bir egitim icin ayarlanmis
    ikinci bir learner blogu:

    - varsayilan layer_config, MLPLearnerBlock'un aksine TEK degil COK katmanli
      (linear -> batchnorm -> relu -> dropout, birkac kez tekrarlanan derin bir mimari)
    - weight_decay (L2 regularizasyon) destegi - overfitting'i azaltmak icin
    - opsiyonel learning rate scheduler (StepLR) - egitim ilerledikce
      learning_rate'i kademeli dusurur

    Ayri bir blok olarak tutulmasinin nedeni: mlp_learner'in varsayilan
    davranisini (hizli, tek katmanli, test/ogrenme amacli) BOZMADAN, gercek
    egitim senaryolarina daha yakin, daha yavas/daha agir bir model secenegi
    sunmak (orn. canli epoch ilerlemesini gozlemlemek icin de kullanislidir,
    cunku derin mimari MLPLearnerBlock'a gore fark edilir sekilde daha yavas calisir).

    Supported params (mlp_learner'a ek olarak):
    - weight_decay              : float, varsayilan 0.0 (Adam/SGD'nin L2 regularizasyon katsayisi)
    - use_lr_scheduler           : bool, varsayilan False
    - lr_scheduler_step          : int, varsayilan 10 (kac epoch'ta bir learning_rate dusurulecek)
    - lr_scheduler_gamma         : float, varsayilan 0.5 (her adimda learning_rate'in carpilacagi katsayi)
    - early_stopping              : bool, varsayilan False - True ise, egitim loss'u
                                     'early_stopping_patience' epoch boyunca yeterince
                                     iyilesmezse egitim epochs'a ulasmadan durur
    - early_stopping_patience     : int, varsayilan 5 (iyilesme olmadan kac epoch daha beklenecek)
    - early_stopping_min_delta    : float, varsayilan 0.0001 (bu miktardan kucuk iyilesmeler
                                     "iyilesme" sayilmaz)

    NOT: early stopping ayri bir validation seti degil, EGITIM (train) loss'unu izler -
    bu blok tek girdili (sadece train_dataloader) kalsin diye bilincli bir tercih.
    Gercek bir validation-loss bazli early stopping isteniyorsa, model egitildikten
    SONRA test/validation seti uzerinde compute_*_metrics bloklariyla ayrica olculebilir.
    """

    name = "deep_mlp_learner"

    VALID_TASK_TYPES = MLPLearnerBlock.VALID_TASK_TYPES
    VALID_OPTIMIZERS = MLPLearnerBlock.VALID_OPTIMIZERS
    VALID_LAYER_TYPES = MLPLearnerBlock.VALID_LAYER_TYPES

    # varsayilan derin mimari: 3 gizli blok, her biri linear -> batchnorm -> relu -> dropout
    DEFAULT_LAYER_CONFIG = [
        {"type": "linear", "size": 128},
        {"type": "batchnorm"},
        {"type": "linear", "size": 128, "activation": "relu"},
        {"type": "dropout", "rate": 0.3},
        {"type": "linear", "size": 64},
        {"type": "batchnorm"},
        {"type": "linear", "size": 64, "activation": "relu"},
        {"type": "dropout", "rate": 0.3},
        {"type": "linear", "size": 32, "activation": "relu"},
    ]

    def validate(self, inputs: dict):
        # mlp_learner ile birebir ayni girdi/param dogrulama mantigi -
        # kod tekrarindan kacinmak icin MLPLearnerBlock.validate'i cagiriyoruz
        # (iki sinif da ayni self.name/self.params yapisina sahip oldugu icin guvenli)
        MLPLearnerBlock.validate(self, inputs)

        weight_decay = self.params.get("weight_decay", 0.0)
        if not isinstance(weight_decay, (int, float)) or weight_decay < 0:
            raise ValueError(f"{self.name}: 'weight_decay' negatif olmayan bir sayi olmali")

        if "early_stopping_patience" in self.params:
            patience = self.params["early_stopping_patience"]
            if not isinstance(patience, int) or patience < 1:
                raise ValueError(f"{self.name}: 'early_stopping_patience' 1 veya daha buyuk bir tam sayi olmali")

    def run(self, inputs: dict) -> dict:
        train_dataloader = inputs["train_dataloader"]

        task_type = self.params.get("task_type")
        layer_config = self.params.get("layer_config", self.DEFAULT_LAYER_CONFIG)
        learning_rate = self.params.get("learning_rate", 0.001)
        weight_decay = self.params.get("weight_decay", 0.0)
        epochs = self.params.get("epochs", 30)
        optimizer_name = self.params.get("optimizer", "adam")
        output_size = self.params.get("output_size", 1)

        use_lr_scheduler = self.params.get("use_lr_scheduler", False)
        lr_scheduler_step = self.params.get("lr_scheduler_step", 10)
        lr_scheduler_gamma = self.params.get("lr_scheduler_gamma", 0.5)

        early_stopping = self.params.get("early_stopping", False)
        early_stopping_patience = self.params.get("early_stopping_patience", 5)
        early_stopping_min_delta = self.params.get("early_stopping_min_delta", 0.0001)

        sample_X, sample_y = next(iter(train_dataloader))
        input_size = sample_X.shape[1]

        model = SimpleMLP(
            input_size=input_size,
            layer_config=layer_config,
            output_size=output_size,
        )

        if task_type == "classification":
            loss_function = nn.CrossEntropyLoss()
        else:
            loss_function = nn.MSELoss()

        if optimizer_name == "adam":
            optimizer = torch.optim.Adam(model.parameters(), lr=learning_rate, weight_decay=weight_decay)
        else:
            optimizer = torch.optim.SGD(model.parameters(), lr=learning_rate, weight_decay=weight_decay)

        scheduler = None
        if use_lr_scheduler:
            scheduler = torch.optim.lr_scheduler.StepLR(
                optimizer, step_size=lr_scheduler_step, gamma=lr_scheduler_gamma
            )

        model.train()

        loss_history = []
        lr_history = []

        # early stopping durumu: en iyi (en dusuk) loss ve kac epoch'tur iyilesme olmadigi
        best_loss = float("inf")
        epochs_without_improvement = 0
        stopped_early = False
        epochs_ran = epochs

        for epoch in range(epochs):
            epoch_losses = []

            for X_batch, y_batch in train_dataloader:
                optimizer.zero_grad()
                predictions = model(X_batch)
                loss = loss_function(predictions, y_batch)
                loss.backward()
                optimizer.step()
                epoch_losses.append(loss.item())

            epoch_avg_loss = sum(epoch_losses) / len(epoch_losses)
            loss_history.append(epoch_avg_loss)

            current_lr = optimizer.param_groups[0]["lr"]
            lr_history.append(current_lr)

            if scheduler is not None:
                # learning_rate'i, bu epoch bittikten SONRA (bir sonraki epoch icin) guncelle
                scheduler.step()

            self.report_progress({
                "epoch": epoch + 1,
                "epochs": epochs,
                "loss": epoch_avg_loss,
                "learning_rate": current_lr,
            })

            if early_stopping:
                # loss, en iyi degerden en az min_delta kadar dustuyse "iyilesme" say
                if epoch_avg_loss < best_loss - early_stopping_min_delta:
                    best_loss = epoch_avg_loss
                    epochs_without_improvement = 0
                else:
                    epochs_without_improvement += 1

                if epochs_without_improvement >= early_stopping_patience:
                    stopped_early = True
                    epochs_ran = epoch + 1
                    break

        meta = {
            "task_type": task_type,
            "input_size": input_size,
            "output_size": output_size,
            "layer_config": layer_config,
            "epochs_ran": epochs_ran,
            "stopped_early": stopped_early,
            "epochs": epochs,
            "weight_decay": weight_decay,
            "final_loss": loss_history[-1],
            "loss_history": loss_history,
            "lr_history": lr_history,
            "output_type": "training_log",
        }

        return {"data": model, "meta": meta}