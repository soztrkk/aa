from blocks.base import Block
from blocks.graphs_and_views import (
    data_preview,
    dataset_summary,
    describe_statistics,
    missing_values_report,
    duplicate_rows_report,
    data_types_summary,
    plot_histogram,
    plot_bar_chart,
    plot_boxplot,
    plot_scatter,
    plot_correlation_heatmap,
    plot_missing_values,
)


class DataPreviewBlock(Block):
    name = "data_preview"

    def run(self, inputs: dict) -> dict:
        data = inputs["data"]

        result = data_preview(
            data=data,
            row_count=self.params.get("row_count", 5),
            preview_type=self.params.get("preview_type", "head"),
        )

        return {"data": data, "meta": result}


class DatasetSummaryBlock(Block):
    name = "dataset_summary"

    def run(self, inputs: dict) -> dict:
        data = inputs["data"]
        result = dataset_summary(data)

        return {"data": data, "meta": result}


class DescribeStatisticsBlock(Block):
    name = "describe_statistics"

    def run(self, inputs: dict) -> dict:
        data = inputs["data"]
        result = describe_statistics(data)

        return {"data": data, "meta": result}


class MissingValuesReportBlock(Block):
    name = "missing_values_report"

    def run(self, inputs: dict) -> dict:
        data = inputs["data"]
        result = missing_values_report(data)

        return {"data": data, "meta": result}


class DuplicateRowsReportBlock(Block):
    name = "duplicate_rows_report"

    def run(self, inputs: dict) -> dict:
        data = inputs["data"]

        result = duplicate_rows_report(
            data=data,
            max_rows=self.params.get("max_rows", 50),
        )

        return {"data": data, "meta": result}


class DataTypesSummaryBlock(Block):
    name = "data_types_summary"

    def run(self, inputs: dict) -> dict:
        data = inputs["data"]
        result = data_types_summary(data)

        return {"data": data, "meta": result}


class PlotHistogramBlock(Block):
    name = "plot_histogram"

    def validate(self, inputs: dict):
        super().validate(inputs)

        if "column" not in self.params:
            raise ValueError(
                f"{self.name}: 'column' parametresi zorunludur."
            )

    def run(self, inputs: dict) -> dict:
        data = inputs["data"]

        result = plot_histogram(
            data=data,
            column=self.params["column"],
            bins=self.params.get("bins", 20),
        )

        return {"data": data, "meta": result}


class PlotBarChartBlock(Block):
    name = "plot_bar_chart"

    def validate(self, inputs: dict):
        super().validate(inputs)

        if "column" not in self.params:
            raise ValueError(
                f"{self.name}: 'column' parametresi zorunludur."
            )

    def run(self, inputs: dict) -> dict:
        data = inputs["data"]

        result = plot_bar_chart(
            data=data,
            column=self.params["column"],
            top_n=self.params.get("top_n", 20),
        )

        return {"data": data, "meta": result}


class PlotBoxplotBlock(Block):
    name = "plot_boxplot"

    def validate(self, inputs: dict):
        super().validate(inputs)

        if "column" not in self.params:
            raise ValueError(
                f"{self.name}: 'column' parametresi zorunludur."
            )

    def run(self, inputs: dict) -> dict:
        data = inputs["data"]

        result = plot_boxplot(
            data=data,
            column=self.params["column"],
            category_column=self.params.get("category_column"),
        )

        return {"data": data, "meta": result}


class PlotScatterBlock(Block):
    name = "plot_scatter"

    def validate(self, inputs: dict):
        super().validate(inputs)

        if "x_column" not in self.params:
            raise ValueError(
                f"{self.name}: 'x_column' parametresi zorunludur."
            )

        if "y_column" not in self.params:
            raise ValueError(
                f"{self.name}: 'y_column' parametresi zorunludur."
            )

    def run(self, inputs: dict) -> dict:
        data = inputs["data"]

        result = plot_scatter(
            data=data,
            x_column=self.params["x_column"],
            y_column=self.params["y_column"],
            color_column=self.params.get("color_column"),
        )

        return {"data": data, "meta": result}


class PlotCorrelationHeatmapBlock(Block):
    name = "plot_correlation_heatmap"

    def run(self, inputs: dict) -> dict:
        data = inputs["data"]
        result = plot_correlation_heatmap(data)

        return {"data": data, "meta": result}


class PlotMissingValuesBlock(Block):
    name = "plot_missing_values"

    def run(self, inputs: dict) -> dict:
        data = inputs["data"]
        result = plot_missing_values(data)

        return {"data": data, "meta": result}
