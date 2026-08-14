# Bu dosya, farkli modullerdeki tum blok siniflarini bir araya toplar
# ve tek bir BLOCK_REGISTRY sozlugu (dictionary) olusturur.
# Bu sozluk, bir blogun string ismini (C tarafindan gelen JSON mesajindan)
# ilgili Python sinifiyla eslestirir.
#
# Dispatcher, sadece bu dosyadan BLOCK_REGISTRY'yi import eder,
# hangi blogun hangi modulde oldugunu bilmesine gerek kalmaz.

# LoadCSVBlock sinifini data_loading.py modulunden import ediyoruz
from blocks.data_loading import LoadCSVBlock
# HandleMissingValuesBlock, RemoveDuplicatesBlock, HandleOutliersBlock siniflarini
# preprocessing.py modulunden import ediyoruz
from blocks.preprocessing import (
    HandleMissingValuesBlock,
    RemoveDuplicatesBlock,
    HandleOutliersBlock,
    DropColumnsBlock,
    RenameColumnsBlock,
    ConvertDtypeBlock
)
from blocks.encoding_scaling import EncodeCategoricalBlock, ScaleFeaturesBlock
from blocks.data_preparation import ToTensorBlock, CreateDataLoaderBlock
from blocks.mlp import MLPLearnerBlock, DeepMLPLearnerBlock
from blocks.view_blocks import (
    DataPreviewBlock,
    DatasetSummaryBlock,
    DescribeStatisticsBlock,
    MissingValuesReportBlock,
    DuplicateRowsReportBlock,
    DataTypesSummaryBlock,
    PlotHistogramBlock,
    PlotBarChartBlock,
    PlotBoxplotBlock,
    PlotScatterBlock,
    PlotCorrelationHeatmapBlock,
    PlotMissingValuesBlock,
)
from blocks.splitting import (
    TrainTestSplitBlock,
    TrainValidationTestSplitBlock,
)
from blocks.metrics import (
    ComputeClassificationMetricsBlock,
    ComputeRegressionMetricsBlock,
)
# TODO: blocks/logistic_regression.py henuz repoya eklenmedi (main'de dosya eksik),
# eklenince asagidaki iki satir tekrar acilmali
# from blocks.logistic_regression import (
#     LogisticRegressionLearnerBlock,
#     LogisticRegressionPredictorBlock,
# )
# asagidaki satirlar, ileride eklenecek bloklar icin simdilik yorum satiri halinde birakildi
# from blocks.splitting import TrainTestSplitBlock
# (yeni bloklar eklendikce buraya yeni import satirlari eklenecek)


# Registry: blok ismi (string) -> blok sinifi eslemesi
# Buradaki string degerler, C tarafinin JSON mesajinda "block" alaninda gonderdigi
# degerlerle BIREBIR ayni olmali
BLOCK_REGISTRY = {
    "load_csv": LoadCSVBlock,
    "handle_missing_values": HandleMissingValuesBlock,
    "remove_duplicates": RemoveDuplicatesBlock,
    "handle_outliers": HandleOutliersBlock,
    "drop_columns": DropColumnsBlock,
    "rename_columns":RenameColumnsBlock,
    "convert_dtype": ConvertDtypeBlock,
    "encode_categorical": EncodeCategoricalBlock,
    "scale_features": ScaleFeaturesBlock,
    "to_tensor": ToTensorBlock,
    "create_dataloader": CreateDataLoaderBlock,
    "mlp_learner": MLPLearnerBlock,
    "deep_mlp_learner": DeepMLPLearnerBlock,
    # "train_test_split": TrainTestSplitBlock,

    "data_preview": DataPreviewBlock,
    "dataset_summary": DatasetSummaryBlock,
    "describe_statistics": DescribeStatisticsBlock,
    "missing_values_report": MissingValuesReportBlock,
    "duplicate_rows_report": DuplicateRowsReportBlock,
    "data_types_summary": DataTypesSummaryBlock,
    "plot_histogram": PlotHistogramBlock,
    "plot_bar_chart": PlotBarChartBlock,
    "plot_boxplot": PlotBoxplotBlock,
    "plot_scatter": PlotScatterBlock,
    "plot_correlation_heatmap": PlotCorrelationHeatmapBlock,
    "plot_missing_values": PlotMissingValuesBlock,

    "train_test_split": TrainTestSplitBlock,
    "train_validation_test_split": TrainValidationTestSplitBlock,

    "compute_classification_metrics": ComputeClassificationMetricsBlock,
    "compute_regression_metrics": ComputeRegressionMetricsBlock,

    # "logistic_regression_learner": LogisticRegressionLearnerBlock,
    # "logistic_regression_predictor": LogisticRegressionPredictorBlock,
}

# Factory (uretici) fonksiyon: blok ismi ve parametreleri alip, hazir bir blok objesi doner
def create_block(block_name: str, params: dict):
    """
    Factory function: given a block name and its parameters,
    returns an instantiated block object ready to run.
    Raises a clear error if the block name is not registered.
    """
    # eger verilen blok ismi registry'de kayitli degilse, anlasilir bir hata firlat
    if block_name not in BLOCK_REGISTRY:
        raise ValueError(f"Unknown block name: {block_name}")

    # registry'den ilgili sinifi (henuz instantiate edilmemis halde) al
    block_class = BLOCK_REGISTRY[block_name]
    # sinifi, verilen parametrelerle instantiate edip (yeni bir obje olusturup) geri dondur
    return block_class(params)
