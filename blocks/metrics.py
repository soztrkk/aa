# blocks/metrics.py
#
# Egitilmis modelin performansini olcen bloklar. Ayri bir "predict" blogu
# henuz olmadigi icin, bu bloklar model + X + y'yi girdi olarak alip
# tahmini (forward pass) kendi icinde yapiyor.

import torch
from blocks.base import Block


class ComputeClassificationMetricsBlock(Block):
    """
    Egitilmis bir classification modelinin performansini olcer.

    inputs:
    - model : egitilmis nn.Module (mlp_learner ciktisi)
    - X     : feature tensor, sekli [N, input_size]
    - y     : gercek sinif etiketleri, sekli [N] (sinif indeksleri, long dtype)

    parameters:
    - metrics : hesaplanacak metrik isimlerinin listesi, varsayilan
                ["accuracy", "precision", "recall", "f1", "confusion_matrix"]
    - average : precision/recall/f1 coklu-sinif durumunda nasil ortalanacak
                "macro" / "micro" / "weighted", varsayilan "macro"
    """

    name = "compute_classification_metrics"

    VALID_METRICS = ("accuracy", "precision", "recall", "f1", "confusion_matrix")
    VALID_AVERAGES = ("macro", "micro", "weighted")

    def validate(self, inputs: dict):
        for key in ("model", "X", "y"):
            if key not in inputs or inputs[key] is None:
                raise ValueError(f"{self.name}: '{key}' girdisi eksik")

        model = inputs["model"]
        X = inputs["X"]
        y = inputs["y"]

        if not isinstance(model, torch.nn.Module):
            raise ValueError(f"{self.name}: 'model' egitilmis bir nn.Module olmali")
        if not isinstance(X, torch.Tensor) or not isinstance(y, torch.Tensor):
            raise ValueError(f"{self.name}: 'X' ve 'y' birer tensor olmali, once to_tensor calistirin")
        if X.shape[0] != y.shape[0]:
            raise ValueError(
                f"{self.name}: X ve y satir sayilari eslesmiyor (X: {X.shape[0]}, y: {y.shape[0]})"
            )
        if y.dim() != 1:
            raise ValueError(
                f"{self.name}: 'y' tek boyutlu (sinif indeksleri) olmali, once to_tensor'i squeeze=True ile calistirin"
            )

        metrics = self.params.get("metrics", list(self.VALID_METRICS))
        invalid = [m for m in metrics if m not in self.VALID_METRICS]
        if invalid:
            raise ValueError(f"{self.name}: gecersiz metrik(ler) -> {invalid}, secenekler: {self.VALID_METRICS}")

        average = self.params.get("average", "macro")
        if average not in self.VALID_AVERAGES:
            raise ValueError(f"{self.name}: gecersiz average '{average}', secenekler: {self.VALID_AVERAGES}")

    def run(self, inputs: dict) -> dict:
        model = inputs["model"]
        X = inputs["X"]
        y_true = inputs["y"]

        metrics = self.params.get("metrics", list(self.VALID_METRICS))
        average = self.params.get("average", "macro")

        # ONEMLI: degerlendirme sirasinda gradyan hesaplamaya gerek yok
        # (egitim degil, sadece ileri yayilim), bu yuzden no_grad kullaniyoruz
        model.eval()
        with torch.no_grad():
            logits = model(X)
            y_pred = torch.argmax(logits, dim=1)

        num_classes = int(torch.max(torch.cat([y_true, y_pred])).item()) + 1

        # confusion_matrix[gercek_sinif][tahmin_sinif] = kac ornek
        confusion = torch.zeros((num_classes, num_classes), dtype=torch.long)
        for true_label, pred_label in zip(y_true.tolist(), y_pred.tolist()):
            confusion[true_label][pred_label] += 1

        result = {}

        if "accuracy" in metrics:
            result["accuracy"] = (y_pred == y_true).float().mean().item()

        if "confusion_matrix" in metrics:
            result["confusion_matrix"] = confusion.tolist()

        if "precision" in metrics or "recall" in metrics or "f1" in metrics:
            per_class_precision, per_class_recall, per_class_f1, support = self._per_class_prf(confusion)
            # "micro" ortalamada precision = recall = f1 = accuracy'e esittir
            # (bkz. _average), bu yuzden confusion'un izini (dogru tahmin sayisi) ayrica veriyoruz
            total_correct = int(torch.trace(confusion).item())

            if "precision" in metrics:
                result["precision"] = self._average(per_class_precision, support, average, total_correct)
            if "recall" in metrics:
                result["recall"] = self._average(per_class_recall, support, average, total_correct)
            if "f1" in metrics:
                result["f1"] = self._average(per_class_f1, support, average, total_correct)

        result["num_samples"] = int(y_true.shape[0])
        result["num_classes"] = num_classes

        return {"data": result, "meta": result}

    def _per_class_prf(self, confusion: torch.Tensor):
        """Her sinif icin ayri ayri precision/recall/f1 ve destek (support) hesaplar."""
        num_classes = confusion.shape[0]
        precision = []
        recall = []
        f1 = []
        support = []

        for c in range(num_classes):
            true_positive = confusion[c][c].item()
            predicted_positive = confusion[:, c].sum().item()
            actual_positive = confusion[c, :].sum().item()

            p = true_positive / predicted_positive if predicted_positive else 0.0
            r = true_positive / actual_positive if actual_positive else 0.0
            f = 2 * p * r / (p + r) if (p + r) else 0.0

            precision.append(p)
            recall.append(r)
            f1.append(f)
            support.append(actual_positive)

        return precision, recall, f1, support

    def _average(self, per_class_values, support, average, total_correct):
        if average == "macro":
            return sum(per_class_values) / len(per_class_values)

        total_support = sum(support)
        if not total_support:
            return 0.0

        if average == "weighted":
            return sum(v * s for v, s in zip(per_class_values, support)) / total_support

        # "micro": tum siniflarin dogru/yanlis tahminleri tek havuzda toplanip
        # tek bir oran hesaplanir - tek-etiketli (multi-class, multi-not-multi-label)
        # classification'da bu deger dogru tahmin sayisi / toplam ornek sayisina,
        # yani accuracy'e esittir
        return total_correct / total_support


class ComputeRegressionMetricsBlock(Block):
    """
    Egitilmis bir regression modelinin performansini olcer.

    inputs:
    - model : egitilmis nn.Module (mlp_learner ciktisi, task_type="regression")
    - X     : feature tensor, sekli [N, input_size]
    - y     : gercek degerler, sekli [N, 1] (to_tensor'de squeeze=False ile uretilmis olmali)

    parameters:
    - metrics : hesaplanacak metrik isimlerinin listesi, varsayilan ["mse", "rmse", "mae", "r2"]
    """

    name = "compute_regression_metrics"

    VALID_METRICS = ("mse", "rmse", "mae", "r2")

    def validate(self, inputs: dict):
        for key in ("model", "X", "y"):
            if key not in inputs or inputs[key] is None:
                raise ValueError(f"{self.name}: '{key}' girdisi eksik")

        model = inputs["model"]
        X = inputs["X"]
        y = inputs["y"]

        if not isinstance(model, torch.nn.Module):
            raise ValueError(f"{self.name}: 'model' egitilmis bir nn.Module olmali")
        if not isinstance(X, torch.Tensor) or not isinstance(y, torch.Tensor):
            raise ValueError(f"{self.name}: 'X' ve 'y' birer tensor olmali, once to_tensor calistirin")
        if X.shape[0] != y.shape[0]:
            raise ValueError(
                f"{self.name}: X ve y satir sayilari eslesmiyor (X: {X.shape[0]}, y: {y.shape[0]})"
            )

        metrics = self.params.get("metrics", list(self.VALID_METRICS))
        invalid = [m for m in metrics if m not in self.VALID_METRICS]
        if invalid:
            raise ValueError(f"{self.name}: gecersiz metrik(ler) -> {invalid}, secenekler: {self.VALID_METRICS}")

    def run(self, inputs: dict) -> dict:
        model = inputs["model"]
        X = inputs["X"]
        y_true = inputs["y"]

        metrics = self.params.get("metrics", list(self.VALID_METRICS))

        model.eval()
        with torch.no_grad():
            y_pred = model(X)

        # ONEMLI: y_true [N, 1] iken model ciktisi da [N, 1] olmali (bkz.
        # to_tensor.py docstring'indeki squeeze uyarisi). Sekiller
        # uyusmuyorsa sessizce yanlis broadcast yapmak yerine hata veriyoruz.
        if y_pred.shape != y_true.shape:
            raise ValueError(
                f"{self.name}: model ciktisi ile 'y' sekli uyusmuyor "
                f"(model: {list(y_pred.shape)}, y: {list(y_true.shape)}) - "
                f"y'yi uretirken to_tensor'de squeeze=False kullanildigindan emin olun"
            )

        errors = y_pred - y_true
        mse = torch.mean(errors ** 2).item()

        result = {}
        if "mse" in metrics:
            result["mse"] = mse
        if "rmse" in metrics:
            result["rmse"] = mse ** 0.5
        if "mae" in metrics:
            result["mae"] = torch.mean(torch.abs(errors)).item()
        if "r2" in metrics:
            ss_res = torch.sum(errors ** 2)
            ss_tot = torch.sum((y_true - torch.mean(y_true)) ** 2)
            result["r2"] = (1 - ss_res / ss_tot).item() if ss_tot != 0 else 0.0

        result["num_samples"] = int(y_true.shape[0])

        return {"data": result, "meta": result}
