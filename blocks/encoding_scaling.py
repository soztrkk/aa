from blocks.base import Block
import pandas as pd

class EncodeCategoricalBlock(Block):
    """
    Parametre listesi 
    - method: "label" / "onehot" / "ordinal"
    - columns: hangi kolonlara uygulanacağı (verilmezse otomatik kategorik kolonlar)
    - category_order (dict, kolon adı → sıralı kategori listesi) — label ve ordinal için kullanılır, kullanıcı vermezse... (bir karar noktası aşağıda)
    - possible_categories (dict, kolon adı → kategori listesi) — onehot için, sıra önemli değil ama hangi kolonların oluşacağını belirler
    - drop_first — sadece onehot için
    """
    
    name = "encode_categorical"

    VALID_METHODS = ("label", "onehot", "ordinal")

    def validate(self, inputs: dict):
        super().validate(inputs)
        df = inputs["data"]

        method = self.params.get("method", "label")
        if method not in self.VALID_METHODS:
            raise ValueError(
                f"{self.name}: invalid method '{method}', must be one of {self.VALID_METHODS}"
            )

        columns = self._resolve_columns(df)
        if self.params.get("columns") is not None:
            missing_cols = [c for c in columns if c not in df.columns]
            if missing_cols:
                raise ValueError(f"{self.name}: columns not found in data -> {missing_cols}")

        if method in ("label", "ordinal"):
            category_order = self.params.get("category_order", {})
            for col in columns:
                order = category_order.get(col)
                if method == "ordinal" and not order:
                    raise ValueError(
                        f"{self.name}: 'category_order' is required for column '{col}' when method is 'ordinal'"
                    )
                if order:
                    missing_values = set(df[col].dropna().unique()) - set(order)
                    if missing_values:
                        raise ValueError(
                            f"{self.name}: category_order for '{col}' is missing values -> {sorted(missing_values)}"
                        )

        if method == "onehot":
            possible_categories = self.params.get("possible_categories", {})
            for col in columns:
                categories = possible_categories.get(col)
                if categories:
                    missing_values = set(df[col].dropna().unique()) - set(categories)
                    if missing_values:
                        raise ValueError(
                            f"{self.name}: possible_categories for '{col}' is missing values -> {sorted(missing_values)}"
                        )

    def run(self, inputs: dict) -> dict:
        df = self.get_data_copy(inputs)

        method = self.params.get("method", "label")
        columns = self._resolve_columns(df)

        if method in ("label", "ordinal"):
            category_order = self.params.get("category_order", {})
            for col in columns:
                order = category_order.get(col)
                if not order:
                    order = sorted(df[col].dropna().unique())
                mapping = {value: code for code, value in enumerate(order)}
                df[col] = df[col].map(mapping)

        elif method == "onehot":
            possible_categories = self.params.get("possible_categories", {})
            drop_first = self.params.get("drop_first", False)
            for col in columns:
                categories = possible_categories.get(col)
                if categories:
                    df[col] = pd.Categorical(df[col], categories=categories)
            df = pd.get_dummies(df, columns=columns, drop_first=drop_first)

        meta = {
            "shape": list(df.shape),
            "columns": df.columns.tolist(),
            "method": method,
        }

        return {"data": df, "meta": meta}

    def _resolve_columns(self, df):
        columns = self.params.get("columns")
        if columns is None:
            return df.select_dtypes(include=["object", "category"]).columns.tolist()
        return columns

class ScaleFeaturesBlock(Block):
    """
    parameters:
    - method: "minmax" / "zscore" / "robust"
    - columns: hangi kolonlara uygulanacağı (verilmezse otomatik sayısal kolonlar)
    - feature_range: sadece minmax için, varsayılan (0, 1)

    Not: bu blok train-test split'ten once tum dataya uygulanmali.
    """
    name = "scale_features"

    VALID_METHODS = ("minmax", "zscore", "robust")

    def validate(self, inputs: dict):
        super().validate(inputs)
        df = inputs["data"]

        method = self.params.get("method", "minmax")
        if method not in self.VALID_METHODS:
            raise ValueError(
                f"{self.name}: invalid method '{method}', must be one of {self.VALID_METHODS}"
            )

        columns = self._resolve_columns(df)
        if self.params.get("columns") is not None:
            missing_cols = [c for c in columns if c not in df.columns]
            if missing_cols:
                raise ValueError(f"{self.name}: columns not found in data -> {missing_cols}")

        non_numeric_cols = [c for c in columns if not pd.api.types.is_numeric_dtype(df[c])]
        if non_numeric_cols:
            raise ValueError(f"{self.name}: columns must be numeric -> {non_numeric_cols}")

        if method == "minmax":
            feature_range = self.params.get("feature_range", (0, 1))
            if len(feature_range) != 2 or feature_range[0] >= feature_range[1]:
                raise ValueError(
                    f"{self.name}: 'feature_range' must be a (min, max) pair with min < max"
                )

    def run(self, inputs: dict) -> dict:
        df = self.get_data_copy(inputs)

        method = self.params.get("method", "minmax")
        columns = self._resolve_columns(df)

        if method == "minmax":
            range_min, range_max = self.params.get("feature_range", (0, 1))
            for col in columns:
                col_min = df[col].min()
                col_max = df[col].max()
                if col_max == col_min:
                    df[col] = range_min
                else:
                    df[col] = (df[col] - col_min) / (col_max - col_min) * (range_max - range_min) + range_min

        elif method == "zscore":
            for col in columns:
                mean = df[col].mean()
                std = df[col].std()
                df[col] = 0.0 if not std or pd.isna(std) else (df[col] - mean) / std

        elif method == "robust":
            for col in columns:
                median = df[col].median()
                q1 = df[col].quantile(0.25)
                q3 = df[col].quantile(0.75)
                iqr = q3 - q1
                df[col] = 0.0 if not iqr else (df[col] - median) / iqr

        meta = {
            "shape": list(df.shape),
            "columns": df.columns.tolist(),
            "method": method,
        }

        return {"data": df, "meta": meta}

    def _resolve_columns(self, df):
        columns = self.params.get("columns")
        if columns is None:
            return df.select_dtypes(include="number").columns.tolist()
        return columns

