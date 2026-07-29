from typing import Any
import pandas as pd
from blocks.base import Block

# Kullanicinin verdigi oranin dogru olup olmadigini kontrol ediyor
def _validate_ratio(
    ratio: Any,
    parameter_name: str
) -> float:
    """
    Bölme oraninin gecerli olup olmadigini kontrol eder.

    Gecerli oran: 0 ile 1 arasinda olmalidir.
    Ornegin: 0.8

    """

    if not isinstance(ratio, (int, float)):
        raise TypeError(
            f"'{parameter_name}' sayisal bir deger olmalidir."
        )
    
    ratio = float(ratio)

    if ratio <= 0 or ratio >= 1:
        raise ValueError(
            f"'{parameter_name}' 0 ile 1 arasinda olmalidir."
        )

    return ratio

# Data setinin satirlarini karistiriyor.
def _shuffle_dataframe(
    data: pd.DataFrame,
    shuffle: bool,
    random_seed: int
) -> pd.DataFrame:
    """
    İstenirse DataFrame satirlarini karistirir.

    """
    # data.sample()--> DataFrame'den rastgele satirlari secer.
    # frac=1 --> Datanın tamamını sec demektir(random).
    # random_state=random_seed --> Karistirmanin tekrar uretilebilir olmasini saglar.
    # reset_index(drop=True) --> Karistirma sonrasi indexler dagilabilir.
    if shuffle:
        return data.sample(
            frac=1,
            random_state=random_seed
        ).reset_index(drop=True)

    return data.reset_index(drop=True)


class TrainTestSplitBlock(Block):
    """
    Veri setini train ve test olmak uzere iki parcaya ayirir.

    Parametreler:
    - train_ratio: Train verisinin orani, varsayilan 0.8
    - shuffle: Satirlar karistirilsin mi, varsayilan True
    - random_seed: Tekrarlanabilirlik icin seed, varsayilan 42

    Ciktilar:
    - train
    - test
    """
    # Blok adı
    name = "train_test_split"

    #Bu func islem baslamadan once kontrolleri yaapr.
    # super().validate(inputs) --> Base classtaki validate() cagirir.
    # Data var mı ? Dataa empty mi ?
    def validate(self, inputs: dict) -> None:
        super().validate(inputs)

        train_ratio = self.params.get("train_ratio", 0.8)
        _validate_ratio(train_ratio, "train_ratio")

        shuffle = self.params.get("shuffle", True)

        if not isinstance(shuffle, bool):
            raise TypeError(
                f"{self.name}: 'shuffle' True veya False olmalidir."
            )

        random_seed = self.params.get("random_seed", 42)

        if not isinstance(random_seed, int):
            raise TypeError(
                f"{self.name}: 'random_seed' tam sayi olmalidir."
            )

    # Asil split burada yapilir
    def run(self, inputs: dict) -> dict:
        # SessionStore'daki orjinal veriyi degistirmemek icin kopya aliyoruz.
        data = self.get_data_copy(inputs)

        train_ratio = float(
            self.params.get("train_ratio", 0.8)
        )
        shuffle = self.params.get("shuffle", True)
        random_seed = self.params.get("random_seed", 42)

        data = _shuffle_dataframe(
            data=data,
            shuffle=shuffle,
            random_seed=random_seed
        )

        total_row_count = len(data)
        split_index = int(total_row_count * train_ratio)

        # iloc --> Satir pozisyonuna gore secim yapar.
        # :split_index --> Baslangictan split noktasina kadar alir.
        
        train_data = data.iloc[:split_index].copy()
        test_data = data.iloc[split_index:].copy()

        return {
            "outputs": {
                "train": train_data,
                "test": test_data
            },
            # Metadata --> Her output icin ayri aciklayici infolar doner.
            "meta": {
                "train": {
                    "output_type": "dataset",
                    "title": "Train Dataset",
                    "row_count": len(train_data),
                    "column_count": len(train_data.columns),
                    "ratio": train_ratio,
                    "shuffle": shuffle,
                    "random_seed": random_seed
                },
                "test": {
                    "output_type": "dataset",
                    "title": "Test Dataset",
                    "row_count": len(test_data),
                    "column_count": len(test_data.columns),
                    "ratio": round(1 - train_ratio, 10),
                    "shuffle": shuffle,
                    "random_seed": random_seed
                }
            }
        }


class TrainValidationTestSplitBlock(Block):
    """
    Veri setini train, validation ve test olarak üç parçaya ayirir.

    Parametreler:
    - train_ratio: Varsayilan 0.7
    - validation_ratio: Varsayilan 0.15
    - test_ratio: Varsayilan 0.15
    - shuffle: Varsayilan True
    - random_seed: Varsayilan 42

    Ciktilar:
    - train
    - validation
    - test
    """

    name = "train_validation_test_split"

    def validate(self, inputs: dict) -> None:
        super().validate(inputs)

        train_ratio = _validate_ratio(
            self.params.get("train_ratio", 0.7),
            "train_ratio"
        )
        validation_ratio = _validate_ratio(
            self.params.get("validation_ratio", 0.15),
            "validation_ratio"
        )
        test_ratio = _validate_ratio(
            self.params.get("test_ratio", 0.15),
            "test_ratio"
        )

        ratio_sum = (
            train_ratio
            + validation_ratio
            + test_ratio
        )

        if abs(ratio_sum - 1.0) > 1e-9:
            raise ValueError(
                f"{self.name}: train, validation ve test "
                f"oranlarinin toplami 1 olmalidir. "
                f"Gelen toplam: {ratio_sum}"
            )

        shuffle = self.params.get("shuffle", True)

        if not isinstance(shuffle, bool):
            raise TypeError(
                f"{self.name}: 'shuffle' True veya False olmalidir."
            )

        random_seed = self.params.get("random_seed", 42)

        if not isinstance(random_seed, int):
            raise TypeError(
                f"{self.name}: 'random_seed' tam sayi olmalidir."
            )

    def run(self, inputs: dict) -> dict:
        data = self.get_data_copy(inputs)

        train_ratio = float(
            self.params.get("train_ratio", 0.7)
        )
        validation_ratio = float(
            self.params.get("validation_ratio", 0.15)
        )
        test_ratio = float(
            self.params.get("test_ratio", 0.15)
        )
        shuffle = self.params.get("shuffle", True)
        random_seed = self.params.get("random_seed", 42)

        data = _shuffle_dataframe(
            data=data,
            shuffle=shuffle,
            random_seed=random_seed
        )

        total_row_count = len(data)

        train_end = int(total_row_count * train_ratio)
        validation_end = train_end + int(
            total_row_count * validation_ratio
        )

        # Verileri split 
        train_data = data.iloc[:train_end].copy()
        validation_data = data.iloc[
            train_end:validation_end
        ].copy()
        test_data = data.iloc[validation_end:].copy()

        return {
            "outputs": {
                "train": train_data,
                "validation": validation_data,
                "test": test_data
            },
            "meta": {
                "train": {
                    "output_type": "dataset",
                    "title": "Train Dataset",
                    "row_count": len(train_data),
                    "column_count": len(train_data.columns),
                    "ratio": train_ratio,
                    "shuffle": shuffle,
                    "random_seed": random_seed
                },
                "validation": {
                    "output_type": "dataset",
                    "title": "Validation Dataset",
                    "row_count": len(validation_data),
                    "column_count": len(
                        validation_data.columns
                    ),
                    "ratio": validation_ratio,
                    "shuffle": shuffle,
                    "random_seed": random_seed
                },
                "test": {
                    "output_type": "dataset",
                    "title": "Test Dataset",
                    "row_count": len(test_data),
                    "column_count": len(test_data.columns),
                    "ratio": test_ratio,
                    "shuffle": shuffle,
                    "random_seed": random_seed
                }
            }
        }
