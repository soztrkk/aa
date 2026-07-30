from typing import Any

import pandas as pd
import torch
from torch import nn

from blocks.base import Block

# PyTorch tabanli Logistic Regression modeli.
# Input: Her satira ait numeric ozellikler.
# Output: Her satir icin tek bir logit degeri.

# PyTorch'daki butun modeller genellikle nn.Module classindan turetilir.
class LogisticRegressionModel(nn.Module):

    # Model olusturuldugunda calisir.
    def __init__(self, input_size: int):
        super().__init__() # super().__init__() PyTorch'un nn.Module baslangic islemlerini calistirir.

        # Linear hesaplama
        # in_features = input_size --> kac ozellik var?
        # out_features = 1 --> tek bir result uret.
        self.linear = nn.Linear(
            in_features=input_size,
            out_features=1
        )

    # forward()--> Data modelden gectiginde yapilacak islrmi tanimlar.
    def forward(self, features: torch.Tensor) -> torch.Tensor:
        logits = self.linear(features)

        return logits.squeeze(dim=1)

# Verilen sutunlarin sayisal olup olmadigini kontrol eder.
def _validate_numeric_columns(
    data: pd.DataFrame,
    columns: list[str]
) -> None:

    # Numeric olmayan sutunlari bulma:
    non_numeric_columns = [
        column
        for column in columns
        if not pd.api.types.is_numeric_dtype(data[column])
    ]

    if non_numeric_columns:
        raise TypeError(
            "Logistic Regression yalnizca sayisal özelliklerle "
            f"calisabilir. Sayisal olmayan sutunlar: "
            f"{non_numeric_columns}"
        )

# Kullanilacak sutunlarda eksik deger olup olmadigini kontrol eder.
def _validate_missing_values(
    data: pd.DataFrame,
    columns: list[str]
) -> None:
    
    columns_with_missing_values = [
        column
        for column in columns
        if data[column].isna().any()
    ]

    if columns_with_missing_values:
        raise ValueError(
            "Model girdilerinde eksik değer bulunamaz. "
            f"Eksik değer bulunan sütunlar: "
            f"{columns_with_missing_values}"
        )

# Bu class modeli egitir (Learner Block)
class LogisticRegressionLearnerBlock(Block):
    #  Parametreler:
    #   - target_column: Tahmin edilecek sutun
    #   - epochs: Train turu sayisi, varsayilan 100
    #   - learning_rate: Öğrenme oranı, varsayilan 0.01
    #   - random_seed: Tekrarlanabilirlik icin seed, varsayilan 42
    # Output --> model: Trained model paketi
        
        
    name = "logistic_regression_learner"

    def validate(self, inputs: dict) -> None:
        super().validate(inputs)

        # SessionStore'dan gelen gercek train DataFrame alinir.
        data = inputs["data"]

        # Target modelin tahmin edecegi sutundur.
        target_column = self.params.get("target_column")

        if not isinstance(target_column, str):
            raise TypeError(
                f"{self.name}: 'target_column' metin olmalidir."
            )

        if not target_column.strip(): #Target BOS MU ?
            raise ValueError(
                f"{self.name}: 'target_column' bos olamaz."
            )

        if target_column not in data.columns:
            raise ValueError(
                f"{self.name}: '{target_column}' sutunu "
                "veri setinde bulunamadi."
            )

        feature_columns = [
            column
            for column in data.columns
            if column != target_column
        ]

        if not feature_columns:
            raise ValueError(
                f"{self.name}: model eğitimi için en az "
                "bir özellik sütunu gereklidir."
            )

        _validate_numeric_columns(
            data=data,
            columns=feature_columns
        )

        _validate_missing_values(
            data=data,
            columns=feature_columns + [target_column]
        )

        if not pd.api.types.is_numeric_dtype(data[target_column]):
            raise TypeError(
                f"{self.name}: target sutunu sayisal olmalidir."
            )

        target_values = set(
            data[target_column]
            .astype(float)
            .unique() # tekrarlanan degerleri tekrarsız olarak alir.
            .tolist()
        )

        if not target_values.issubset({0.0, 1.0}):
            raise ValueError(
                f"{self.name}: target sutunu yalnizca "
                f"0 ve 1 degerlerinden olusmalidir. "
                f"Gelen degerler: {sorted(target_values)}"
            )

        # Epoch, modelin tum train datsini kac kez gorecegini belirtir.
        epochs = self.params.get("epochs", 100)

        if not isinstance(epochs, int):
            raise TypeError(
                f"{self.name}: 'epochs' tam sayi olmalidir."
            )

        if epochs <= 0:
            raise ValueError(
                f"{self.name}: 'epochs' 0'dan büyük olmalıdır."
            )

        learning_rate = self.params.get(
            "learning_rate",
            0.01
        )

        if not isinstance(learning_rate, (int, float)):
            raise TypeError(
                f"{self.name}: 'learning_rate' "
                "sayisal olmalidir."
            )

        if learning_rate <= 0:
            raise ValueError(
                f"{self.name}: 'learning_rate' "
                "0'dan buyuk olmalidir."
            )

        # Modelin ilk weightleri random olusturulur. 
        # Seed ayni olursa baslangic degerleri de yani olur.
        # Bu, tekrar uretilebilir sonuclar icin kullanilir.
        random_seed = self.params.get("random_seed", 42)

        if not isinstance(random_seed, int):
            raise TypeError(
                f"{self.name}: 'random_seed' "
                "tam sayi olmalidir."
            )

    def run(self, inputs: dict) -> dict:
        data = self.get_data_copy(inputs)

        target_column = self.params["target_column"]
        epochs = self.params.get("epochs", 100)
        learning_rate = float(
            self.params.get("learning_rate", 0.01)
        )
        random_seed = self.params.get("random_seed", 42)

        feature_columns = [
            column
            for column in data.columns
            if column != target_column
        ]
        # PyTorch'un random sayi uretimini sabitler.
        torch.manual_seed(random_seed)

        # DataFrame'i feature tensorune cevirme.
        feature_tensor = torch.tensor(
            data[feature_columns].to_numpy(
                dtype="float32"
            ),
            dtype=torch.float32
        )
        # Target column'nu tensor yapar.
        target_tensor = torch.tensor(
            data[target_column].to_numpy(
                dtype="float32"
            ),
            dtype=torch.float32
        )

        model = LogisticRegressionModel(
            input_size=len(feature_columns)
        )
        # İkili siniflandirma icin kullanilan loss func.
        loss_function = nn.BCEWithLogitsLoss()

        # Optimizer modelin weightlerini gunceller.
        optimizer = torch.optim.Adam(
            model.parameters(),
            lr=learning_rate
        )

        model.train()

        final_loss = 0.0

        # Model belirlenen epoch kadar egitilir. Her turda:
        # 1- Eski gradientleri temizle
        # 2- İleri yayılım
        # 3- Loss hesaplama
        # 4- Geri yayılım
        # 5- Update weights

        for _ in range(epochs):
            optimizer.zero_grad()

            logits = model(feature_tensor)

            loss = loss_function(
                logits,
                target_tensor
            )

            loss.backward()
            optimizer.step()

            final_loss = float(loss.item())

        model_bundle: dict[str, Any] = {
            "model": model,
            "model_type": "logistic_regression",
            "feature_columns": feature_columns,
            "target_column": target_column
        }

        return {
            "outputs": {
                "model": model_bundle
            },
            "meta": {
                "model": {
                    "output_type": "model",
                    "title": "Logistic Regression Model",
                    "model_type": "logistic_regression",
                    "feature_columns": feature_columns,
                    "target_column": target_column,
                    "input_size": len(feature_columns),
                    "training_row_count": len(data),
                    "epochs": epochs,
                    "learning_rate": learning_rate,
                    "random_seed": random_seed,
                    "final_loss": final_loss
                }
            }
        }


class LogisticRegressionPredictorBlock(Block):
    """
    Eğitilmiş Logistic Regression modeliyle tahmin yapan blok.

    Girdiler:
    - data: Tahmin yapilacak DataFrame
    - model: Learner bloğunun ürettigi model paketi

    Parametre:
    - threshold: Class karar eşiği, varsayilan 0.5

    Output:
    - output: Tahminlerin eklendiği DataFrame
    """

    name = "logistic_regression_predictor"

    # Base yalnızca data girişini kontrol eder.
    def validate(self, inputs: dict) -> None:
        super().validate(inputs)

        if "model" not in inputs:
            raise ValueError(
                f"{self.name}: 'model' girdisi eksik."
            )

        model_bundle = inputs["model"]

        if not isinstance(model_bundle, dict):
            raise TypeError(
                f"{self.name}: model girdisi gecerli "
                "bir model paketi olmalidir."
            )

        if model_bundle.get("model_type") != "logistic_regression":
            raise ValueError(
                f"{self.name}: yalnızca Logistic Regression "
                "modeli kabul edilir."
            )

        model = model_bundle.get("model")

        if not isinstance(model, LogisticRegressionModel):
            raise TypeError(
                f"{self.name}: model paketinde geçerli "
                "bir Logistic Regression modeli bulunamadi."
            )

        feature_columns = model_bundle.get("feature_columns")

        if not isinstance(feature_columns, list):
            raise TypeError(
                f"{self.name}: modelin özellik sütunlari "
                "bulunamadi."
            )

        data = inputs["data"]

        missing_columns = [
            column
            for column in feature_columns
            if column not in data.columns
        ]

        if missing_columns:
            raise ValueError(
                f"{self.name}: tahmin verisinde gerekli "
                f"sütunlar bulunamadi: {missing_columns}"
            )

        _validate_numeric_columns(
            data=data,
            columns=feature_columns
        )

        _validate_missing_values(
            data=data,
            columns=feature_columns
        )

        threshold = self.params.get("threshold", 0.5)

        if not isinstance(threshold, (int, float)):
            raise TypeError(
                f"{self.name}: 'threshold' "
                "sayisal olmalidir."
            )

        if threshold < 0 or threshold > 1:
            raise ValueError(
                f"{self.name}: 'threshold' "
                "0 ile 1 arasında olmalıdır."
            )
    # Test datasinin kopyası ve model paketi alinir.
    def run(self, inputs: dict) -> dict:
        data = self.get_data_copy(inputs)
        model_bundle = inputs["model"]

        model = model_bundle["model"]
        feature_columns = model_bundle["feature_columns"]
        threshold = float(
            self.params.get("threshold", 0.5)
        )

        feature_tensor = torch.tensor(
            data[feature_columns].to_numpy(
                dtype="float32"
            ),
            dtype=torch.float32
        )
        # Model tahmin moduna gecirilir.
        model.eval()

        with torch.no_grad():
            logits = model(feature_tensor)
            probabilities = torch.sigmoid(logits)

            predictions = (
                probabilities >= threshold
            ).to(torch.int64)

        data["prediction_probability"] = (
            probabilities.numpy()
        )

        data["prediction"] = predictions.numpy()

        return {
            "outputs": {
                "output": data
            },
            "meta": {
                "output": {
                    "output_type": "prediction",
                    "title": (
                        "Logistic Regression Predictions"
                    ),
                    "model_type": "logistic_regression",
                    "row_count": len(data),
                    "feature_columns": feature_columns,
                    "threshold": threshold,
                    "prediction_column": "prediction",
                    "probability_column": (
                        "prediction_probability"
                    )
                }
            }
        }
