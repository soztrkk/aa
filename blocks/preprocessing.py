from blocks.base import Block
import pandas as pd

# HandleMissingValuesBlock, eksik (NaN) verileri isleyen blok
class HandleMissingValuesBlock(Block):
    """
    Handles missing (NaN) values in the dataset using different strategies.

    Supported params:
    - strategy : one of "mean", "median", "mode", "constant", "drop_rows", "drop_columns"
                 default: "mean"
    - columns  : list of column names to apply the strategy to.
                 if not given, applies to all applicable columns.
    - fill_value : used only when strategy is "constant"
    """

    # bu blogun ismi, JSON mesajindaki "block" alaniyla eslesecek
    name = "handle_missing_values"

    # gecerli strateji degerlerinin tuple'i, hem validate'de kontrol icin hem dokumantasyon icin burada tanimli
    VALID_STRATEGIES = ("mean", "median", "mode", "constant", "drop_rows", "drop_columns")

    # bu bloga ozel validate metodu
    def validate(self, inputs: dict):
        # once base class'in ortak kontrollerini calistir (data var mi, bos mu)
        super().validate(inputs)

        # params'tan strategy degerini al, verilmemisse varsayilan "mean" kullan
        strategy = self.params.get("strategy", "mean")
        # eger strategy, izin verilen degerlerden biri degilse hata firlat
        if strategy not in self.VALID_STRATEGIES:
            raise ValueError(
                f"{self.name}: invalid strategy '{strategy}', "
                f"must be one of {self.VALID_STRATEGIES}"
            )

        # eger strategy "constant" ise, fill_value parametresi mutlaka verilmis olmali
        if strategy == "constant" and "fill_value" not in self.params:
            raise ValueError(f"{self.name}: 'fill_value' is required when strategy is 'constant'")

        # kullanici belirli kolonlar vermisse, bu kolonlarin gercekten veride olup olmadigini kontrol et
        columns = self.params.get("columns")
        if columns is not None:
            # inputs icindeki DataFrame'e eris
            df = inputs["data"]
            # verilen kolon isimlerinden hangileri DataFrame'de yok, bunlari bir listede topla
            missing_cols = [c for c in columns if c not in df.columns]
            # eger eksik kolon varsa hata firlat, hangi kolonlarin eksik oldugunu mesajda belirt
            if missing_cols:
                raise ValueError(f"{self.name}: columns not found in data -> {missing_cols}")

    # asil isi yapan metod
    def run(self, inputs: dict) -> dict:
        # her zaman kopya uzerinde calis, orijinal session store verisini mutasyona ugratma
        df = self.get_data_copy(inputs)

        # strategy parametresini al, verilmemisse "mean" kullan
        strategy = self.params.get("strategy", "mean")
        # eger belirli kolonlar verilmemisse, DataFrame'deki TUM kolonlari hedef al
        columns = self.params.get("columns", df.columns.tolist())

        # strateji "drop_rows" ise: belirtilen kolonlarda NaN olan HERHANGI bir satiri sil
        if strategy == "drop_rows":
            df = df.dropna(subset=columns)

        # strateji "drop_columns" ise: belirtilen kolonlari tamamen sil (NaN sayisina bakmadan)
        elif strategy == "drop_columns":
            df = df.drop(columns=columns)

        # strateji "mean" ise: her kolonun NaN degerlerini o kolonun ortalamasiyla doldur
        elif strategy == "mean":
            # belirtilen her kolon icin dongu
            for col in columns:
                # sadece sayisal (numeric) kolonlarda ortalama hesaplamak mantikli, bu yuzden kontrol ediyoruz
                if pd_is_numeric(df[col]):
                    # fillna, NaN degerleri verilen deger ile degistirir
                    df[col] = df[col].fillna(df[col].mean())

        # strateji "median" ise: her kolonun NaN degerlerini o kolonun medyaniyla doldur
        elif strategy == "median":
            for col in columns:
                if pd_is_numeric(df[col]):
                    df[col] = df[col].fillna(df[col].median())

        # strateji "mode" ise: her kolonun NaN degerlerini o kolonda en sik gorulen degerle doldur
        elif strategy == "mode":
            for col in columns:
                # mode() birden fazla deger donebilir (esitlik durumunda), bu yuzden bir Series doner
                mode_series = df[col].mode()
                # eger mode_series bos degilse (yani en az bir mode degeri varsa), ilk degeri kullan
                if not mode_series.empty:
                    df[col] = df[col].fillna(mode_series[0])

        # strateji "constant" ise: kullanicinin verdigi sabit bir degerle NaN'lari doldur
        elif strategy == "constant":
            # fill_value, validate asamasinda varligi zaten kontrol edilmisti
            fill_value = self.params.get("fill_value")
            for col in columns:
                df[col] = df[col].fillna(fill_value)

        # C tarafina donecek meta bilgisi: islem sonrasi kalan eksik veri sayilari
        meta = {
            # DataFrame'in yeni boyutu (satir, kolon)
            "shape": list(df.shape),
            # guncel kolon listesi (drop_columns kullanildiysa degismis olabilir)
            "columns": df.columns.tolist(),
            # her kolonda hala kac tane eksik (NaN) deger kaldigini hesapla
            "remaining_missing_values": df.isnull().sum().to_dict(),
        }

        # isli DataFrame ve meta bilgisini geri dondur
        return {"data": df, "meta": meta}


# Yardimci fonksiyon, bir pandas Series'in sayisal (numeric) tipte olup olmadigini kontrol eder
def pd_is_numeric(series) -> bool:
    """
    Small helper to check if a pandas Series has a numeric dtype.
    Kept as a plain function (not a method) since it does not depend
    on any block instance state.
    """
    # pandas'in tip kontrol araclarindan biri olan is_numeric_dtype fonksiyonunu kullaniyoruz
    import pandas as pd
    return pd.api.types.is_numeric_dtype(series)

class RemoveDuplicatesBlock(Block):
    """
    Supported params:
    - subset : column label or iterable of labels, optional
    - keep : Determines which duplicates (if any) to keep.
        ‘first’ : Drop duplicates except for the first occurrence.
        ‘last’ : Drop duplicates except for the last occurrence.
        False : Drop all duplicates.
    - ignore_index : bool, default False
        If True, the resulting axis will be labeled 0, 1, …, n - 1.
    """

    name = "remove_duplicates"

    def validate(self, inputs:dict):
         # once base class'in ortak kontrollerini calistir (data var mi, bos mu)
        super().validate(inputs)

        # eger kullanici belirli kolonlar vermisse, bu kolonlarin veride
        # gercekten var olup olmadigini kontrol et
        subset = self.params.get("subset")
        if subset is not None:
            df = inputs["data"]
            # subset tek bir string de olabilir, liste de olabilir, ikisini de destekle
            columns_to_check = [subset] if isinstance(subset, str) else subset
            missing_cols = [c for c in columns_to_check if c not in df.columns]
            if missing_cols:
                raise ValueError(f"{self.name}: columns not found in data -> {missing_cols}")


    def run(self, inputs: dict) -> dict:
        df = self.get_data_copy(inputs)

        subset = self.params.get("subset", None)
        keep = self.params.get("keep", False)
        ignore_index = self.params.get("ignore_index", False)

        df = df.drop_duplicates(subset=subset,keep=keep,ignore_index=ignore_index)

        meta = {
            # DataFrame'in yeni boyutu (satir, kolon)
            "shape": list(df.shape),
            # guncel kolon listesi (drop_columns kullanildiysa degismis olabilir)
            "columns": df.columns.tolist(),
            # her kolonda hala kac tane eksik (NaN) deger kaldigini hesapla
            "remaining_duplicate_values": int(df.duplicated().sum()),
        }

        return {"data": df, "meta": meta}

    def validate_output(self, result: dict):
        # once base class'in kontrolunu calistir (veri bos mu kontrolu)
        super().validate_output(result)

        # calisma sonrasi kontrol: hala duplicate satir kaldi mi
        # (normalde kalmamasi gerekir, ama beklenmedik bir durumu erken yakalamak icin)
        # NOT: bu noktada result artik normalize edilmis (outputs/meta formatinda),
        # eski "data" anahtari yok - "outputs" uzerinden okunmali
        df = result["outputs"]["output"]
        remaining = df.duplicated().sum()
        if remaining > 0:
            raise ValueError(f"{self.name}: islem sonrasi hala {remaining} duplicate satir kaldi")

class HandleOutliersBlock(Block):
    """
    Supported params:
    - columns : list of column names, optional. Verilmezse tum sayisal kolonlar kullanilir.
    - method : "iqr" veya "zscore", default "iqr"
    - iqr_multiplier : sadece method="iqr" iken kullanilir, default 1.5
    - zscore_threshold : sadece method="zscore" iken kullanilir, default 3.0
    - action : "remove", "cap", "impute_mean", "impute_median", default "remove"
    """

    name = "handle_outliers"

    VALID_METHODS = ("iqr", "zscore")
    VALID_ACTIONS = ("remove", "cap", "impute_mean", "impute_median")

    def validate(self, inputs: dict):
        # once base class'in ortak kontrollerini calistir (data var mi, bos mu)
        super().validate(inputs)

        method = self.params.get("method", "iqr")
        if method not in self.VALID_METHODS:
            raise ValueError(
                f"{self.name}: invalid method '{method}', must be one of {self.VALID_METHODS}"
            )

        action = self.params.get("action", "remove")
        if action not in self.VALID_ACTIONS:
            raise ValueError(
                f"{self.name}: invalid action '{action}', must be one of {self.VALID_ACTIONS}"
            )

        df = inputs["data"]
        columns = self.params.get("columns")
        if columns is not None:
            # verilen kolonlarin veride var olup olmadigini kontrol et
            missing_cols = [c for c in columns if c not in df.columns]
            if missing_cols:
                raise ValueError(f"{self.name}: columns not found in data -> {missing_cols}")

            # outlier hesabi sadece sayisal kolonlarda anlamli
            non_numeric_cols = [c for c in columns if not pd_is_numeric(df[c])]
            if non_numeric_cols:
                raise ValueError(
                    f"{self.name}: columns must be numeric -> {non_numeric_cols}"
                )

    def run(self, inputs: dict) -> dict:
        df = self.get_data_copy(inputs)

        # secilmis kolonlar varsa onlari, yoksa tum sayisal kolonlari hedef al
        columns = self.params.get("columns")
        if columns is None:
            columns = df.select_dtypes(include="number").columns.tolist()

        method = self.params.get("method", "iqr")
        action = self.params.get("action", "remove")

        if method == "iqr":
            multiplier = self.params.get("iqr_multiplier", 1.5)
            bounds = self._iqr_bounds(df, columns, multiplier)
        else:
            threshold = self.params.get("zscore_threshold", 3.0)
            bounds = self._zscore_bounds(df, columns, threshold)

        outlier_mask = self._outlier_mask(df, columns, bounds)
        outlier_count = int(outlier_mask.sum())

        if action == "remove":
            df = df[~outlier_mask]
        elif action == "cap":
            for col in columns:
                lower, upper = bounds[col]
                df[col] = df[col].clip(lower=lower, upper=upper)
        elif action in ("impute_mean", "impute_median"):
            for col in columns:
                lower, upper = bounds[col]
                col_outlier_mask = (df[col] < lower) | (df[col] > upper)
                non_outliers = df.loc[~col_outlier_mask, col]
                if action == "impute_mean":
                    replacement = non_outliers.mean()
                else:
                    replacement = non_outliers.median()
                df.loc[col_outlier_mask, col] = replacement

        meta = {
            "shape": list(df.shape),
            "columns": df.columns.tolist(),
            "method": method,
            "action": action,
            "outlier_count": outlier_count,
        }

        return {"data": df, "meta": meta}

    # yardimci metod: IQR yontemiyle her kolon icin (alt_sinir, ust_sinir) hesaplar
    def _iqr_bounds(self, df, columns, multiplier):
        bounds = {}
        for col in columns:
            Q1 = df[col].quantile(0.25)
            Q3 = df[col].quantile(0.75)
            IQR = Q3 - Q1
            bounds[col] = (Q1 - multiplier * IQR, Q3 + multiplier * IQR)
        return bounds

    # yardimci metod: z-score yontemiyle her kolon icin (alt_sinir, ust_sinir) hesaplar
    def _zscore_bounds(self, df, columns, threshold):
        bounds = {}
        for col in columns:
            mean = df[col].mean()
            std = df[col].std()
            # std 0 veya NaN ise (tum degerler ayni / tek satir) sinir hesaplanamaz,
            # bu kolonda outlier olmadigini varsay (min/max sinir olarak kullanilir)
            if not std or pd.isna(std):
                bounds[col] = (df[col].min(), df[col].max())
            else:
                bounds[col] = (mean - threshold * std, mean + threshold * std)
        return bounds

    # yardimci metod: verilen sinirlara gore satir bazinda outlier maskesi (True/False) uretir
    def _outlier_mask(self, df, columns, bounds):
        mask = pd.Series(False, index=df.index)
        for col in columns:
            lower, upper = bounds[col]
            mask = mask | (df[col] < lower) | (df[col] > upper)
        return mask
