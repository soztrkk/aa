from typing import Any
import pandas as pd
import plotly.express as px
import plotly.graph_objects as go


def _validate_dataframe(data: pd.DataFrame) -> None: 
    """Verinin geçerli bir pandas DataFrame olup olmadığını kontrol eder."""

    if not isinstance(data, pd.DataFrame):
        raise TypeError("Girdi pandas DataFrame olmalıdır.")

    if data.empty:
        raise ValueError("Gönderilen veri seti boş.")


def _validate_column(data: pd.DataFrame, column: str) -> None:
    """Sütunun veri setinde bulunup bulunmadığını kontrol eder."""

    if column not in data.columns:
        raise ValueError(f"'{column}' isimli sütun veri setinde bulunamadı.")


def _figure_result(
    figure: go.Figure,
    title: str,
    chart_type: str
) -> dict[str, Any]:
    """Grafik sonucunu standart bir formatta döndürür."""

    return {
        "output_type": "chart",
        "chart_type": chart_type,
        "title": title,
        "figure": figure,
        "figure_json": figure.to_json()
    }


def data_preview(
    data: pd.DataFrame,
    row_count: int = 5,
    preview_type: str = "head"
) -> dict[str, Any]:
    """
    Veri setinin ilk veya son satırlarını döndürür.

    preview_type:
        - head
        - tail
    """

    _validate_dataframe(data)

    if row_count <= 0:
        raise ValueError("Satır sayısı 0'dan büyük olmalıdır.")

    if preview_type == "head":
        preview_data = data.head(row_count)
    elif preview_type == "tail":
        preview_data = data.tail(row_count)
    else:
        raise ValueError("preview_type yalnızca 'head' veya 'tail' olabilir.")

    return {
        "output_type": "table",
        "title": "Data Preview",
        "columns": preview_data.columns.tolist(),
        "records": preview_data.to_dict(orient="records"),
        "row_count": len(preview_data)
    }


def dataset_summary(data: pd.DataFrame) -> dict[str, Any]:
    """Veri setinin genel bilgilerini döndürür."""

    _validate_dataframe(data)

    column_details = []

    for column in data.columns:
        column_details.append({
            "column": column,
            "data_type": str(data[column].dtype),
            "missing_count": int(data[column].isna().sum()),
            #isna()-> bir hücrenin boş olup olmadığını kontrol eder.
            #sum()-> True değerlerini sayar.
            "unique_count": int(data[column].nunique())
            #nunique()-> bir columnda kaç farklı değer var?
        })

    return {
        "output_type": "summary",
        "title": "Dataset Summary",
        "row_count": len(data),
        "column_count": len(data.columns),
        "duplicate_count": int(data.duplicated().sum()),
        "total_missing_values": int(data.isna().sum().sum()),
        "columns": column_details
    }


def describe_statistics(data: pd.DataFrame) -> dict[str, Any]:
    """Sayısal sütunların istatistiksel özetini döndürür."""

    _validate_dataframe(data)

    numeric_data = data.select_dtypes(include="number")

    if numeric_data.empty:
        raise ValueError("Veri setinde sayısal sütun bulunamadı.")

    statistics = numeric_data.describe().reset_index()
    #describe()-> sayısal satırlar için temel isstatistikleri çıkarır.

    return {
        "output_type": "table",
        "title": "Descriptive Statistics",
        "columns": statistics.columns.tolist(),
        "records": statistics.to_dict(orient="records")
    }

def missing_values_report(data: pd.DataFrame) -> dict[str, Any]:
    """Sütunlardaki eksik değerleri tablo olarak raporlar."""

    _validate_dataframe(data)

    missing_counts = data.isna().sum()

    report_data = []

    for column in data.columns:
        missing_count = int(missing_counts[column])

        if missing_count > 0:
            missing_percentage = round(
                (missing_count / len(data)) * 100,2
            )

            report_data.append({
                "column": column,
                "missing_count": missing_count,
                "missing_percentage": missing_percentage
            })

    report_data.sort(
        key=lambda item: item["missing_count"],
        reverse=True
    )

    if not report_data:
        return {
            "output_type": "message",
            "title": "Missing Values Report",
            "message": "Veri setinde eksik değer bulunmamaktadır."
        }

    return {
        "output_type": "table",
        "title": "Missing Values Report",
        "columns": [
            "column",
            "missing_count",
            "missing_percentage"
        ],
        "records": report_data,
        "total_missing_values": int(missing_counts.sum()),
        "affected_column_count": len(report_data)
    }

def duplicate_rows_report(
    data: pd.DataFrame,
    max_rows: int = 50
) -> dict[str, Any]:
    """Tekrar eden satırları tablo olarak raporlar."""

    _validate_dataframe(data)

    if max_rows <= 0:
        raise ValueError("Gösterilecek maksimum satır sayısı 0'dan büyük olmalıdır.")

    duplicate_mask = data.duplicated(keep=False)
    duplicate_rows = data[duplicate_mask]

    if duplicate_rows.empty:
        return {
            "output_type": "message",
            "title": "Duplicate Rows Report",
            "message": "Veri setinde tekrar eden satır bulunmamaktadır.",
            "duplicate_row_count": 0
        }

    displayed_rows = duplicate_rows.head(max_rows).copy()

    return {
        "output_type": "table",
        "title": "Duplicate Rows Report",
        "columns": displayed_rows.columns.tolist(),
        "records": displayed_rows.to_dict(orient="records"),
        "duplicate_row_count": int(data.duplicated().sum()),
        "matched_row_count": len(duplicate_rows),
        "displayed_row_count": len(displayed_rows),
        "is_truncated": len(duplicate_rows) > max_rows
    }

def data_types_summary(data: pd.DataFrame) -> dict[str, Any]:
    """Veri setindeki sütun tiplerinin özetini döndürür."""

    _validate_dataframe(data)

    numeric_columns = []
    categorical_columns = []
    datetime_columns = []
    boolean_columns = []
    other_columns = []

    for column in data.columns:

        dtype = data[column].dtype

        if pd.api.types.is_numeric_dtype(dtype):
            numeric_columns.append(column)

        elif pd.api.types.is_datetime64_any_dtype(dtype):
            datetime_columns.append(column)

        elif pd.api.types.is_bool_dtype(dtype):
            boolean_columns.append(column)

        elif pd.api.types.is_object_dtype(dtype):
            categorical_columns.append(column)

        else:
            other_columns.append(column)

    return {
        "output_type": "summary",
        "title": "Data Types Summary",

        "total_columns": len(data.columns),

        "numeric": {
            "count": len(numeric_columns),
            "columns": numeric_columns
        },

        "categorical": {
            "count": len(categorical_columns),
            "columns": categorical_columns
        },

        "datetime": {
            "count": len(datetime_columns),
            "columns": datetime_columns
        },

        "boolean": {
            "count": len(boolean_columns),
            "columns": boolean_columns
        },

        "other": {
            "count": len(other_columns),
            "columns": other_columns
        }
    }

def plot_histogram(
    data: pd.DataFrame,
    column: str,
    bins: int = 20 # Histogram kaç parçaya bölünsün?
) -> dict[str, Any]:
    """Seçilen sayısal sütun için histogram oluşturur."""

    _validate_dataframe(data)
    _validate_column(data, column)

    if not pd.api.types.is_numeric_dtype(data[column]):
        raise TypeError("Histogram için sayısal bir sütun seçilmelidir.")

    figure = px.histogram(
        data,
        x=column,
        nbins=bins, # Kutu sayısını ayarlar.
        title=f"{column} Distribution"
    )

    return _figure_result(
        figure=figure,
        title=f"{column} Distribution",
        chart_type="histogram"
    )


def plot_bar_chart(
    data: pd.DataFrame,
    column: str,
    top_n: int = 20
) -> dict[str, Any]:
    """Seçilen sütundaki değerlerin frekanslarını gösterir."""

    _validate_dataframe(data)
    _validate_column(data, column)

    value_counts = (
        data[column]
        .fillna("Missing")
        .astype(str)
        .value_counts()
        .head(top_n)
        .reset_index()
    )

    value_counts.columns = [column, "count"]

    figure = px.bar(
        value_counts,
        x=column,
        y="count",
        title=f"{column} Value Counts"
    )

    return _figure_result(
        figure=figure,
        title=f"{column} Value Counts",
        chart_type="bar"
    )


def plot_boxplot(
    data: pd.DataFrame,
    column: str,
    category_column: str | None = None
) -> dict[str, Any]:
    """Sayısal sütun için boxplot oluşturur."""

    _validate_dataframe(data)
    _validate_column(data, column)

    if not pd.api.types.is_numeric_dtype(data[column]):
        raise TypeError("Boxplot için sayısal bir sütun seçilmelidir.")

    if category_column is not None:
        _validate_column(data, category_column)

    figure = px.box(
        data,
        x=category_column,
        y=column,
        title=f"{column} Boxplot"
    )

    return _figure_result(
        figure=figure,
        title=f"{column} Boxplot",
        chart_type="boxplot"
    )


def plot_scatter(
    data: pd.DataFrame,
    x_column: str,
    y_column: str,
    color_column: str | None = None
) -> dict[str, Any]:
    """İki sayısal sütun arasında scatter plot oluşturur."""

    _validate_dataframe(data)
    _validate_column(data, x_column)
    _validate_column(data, y_column)

    if color_column is not None:
        _validate_column(data, color_column)

    if not pd.api.types.is_numeric_dtype(data[x_column]):
        raise TypeError("X ekseni için sayısal sütun seçilmelidir.")

    if not pd.api.types.is_numeric_dtype(data[y_column]):
        raise TypeError("Y ekseni için sayısal sütun seçilmelidir.")

    figure = px.scatter(
        data,
        x=x_column,
        y=y_column,
        color=color_column,
        title=f"{x_column} - {y_column} Relationship"
    )

    return _figure_result(
        figure=figure,
        title=f"{x_column} - {y_column} Relationship",
        chart_type="scatter"
    )


def plot_correlation_heatmap(
    data: pd.DataFrame
) -> dict[str, Any]:
    """Sayısal sütunların korelasyon matrisini oluşturur."""

    _validate_dataframe(data)

    numeric_data = data.select_dtypes(include="number")

    if numeric_data.shape[1] < 2:
        raise ValueError(
            "Korelasyon grafiği için en az iki sayısal sütun gereklidir."
        )

    correlation = numeric_data.corr()

    figure = px.imshow(
        correlation,
        text_auto=".2f",
        aspect="auto",
        title="Correlation Heatmap"
    )

    return _figure_result(
        figure=figure,
        title="Correlation Heatmap",
        chart_type="correlation_heatmap"
    )


def plot_missing_values(data: pd.DataFrame) -> dict[str, Any]:
    """Sütunlardaki eksik değer sayılarını grafik olarak gösterir."""

    _validate_dataframe(data)

    missing_data = data.isna().sum().reset_index()
    missing_data.columns = ["column", "missing_count"]
    missing_data = missing_data[missing_data["missing_count"] > 0]

    if missing_data.empty:
        return {
            "output_type": "message",
            "title": "Missing Values",
            "message": "Veri setinde eksik değer bulunmamaktadır."
        }

    missing_data = missing_data.sort_values(
        by="missing_count",
        ascending=False
    )

    figure = px.bar(
        missing_data,
        x="column",
        y="missing_count",
        title="Missing Values by Column"
    )

    return _figure_result(
        figure=figure,
        title="Missing Values by Column",
        chart_type="missing_values"
    )