# Bu dosyada Block sinifindan tureyen bir alt sinif tanimlandigi icin
from blocks.base import Block
import pandas as pd


# LoadCSVBlock, Block sinifindan turetilmis somut (concrete) bir sinif
class LoadCSVBlock(Block):
    """
    CSV dosyasindan veri yukleyen blok.
    Pipeline'in genelde ilk blogu budur, bu yuzden 'inputs' icinde
    onceki bloktan gelen bir 'data' beklemez, bunun yerine dosya yolu
    (file_path) parametre olarak gelir.
    """

    # Bu sinifin ismi, C tarafindan gelen JSON'daki "block" alaniyla eslesecek
    name = "load_csv"

    # Bu blok icin ozel validate metodu, base class'takini TAMAMEN override eder
    def validate(self, inputs: dict):
        """
        Bu blok base class'taki validate'i KULLANMAZ (override eder),
        cunku bu blokta henuz elimizde 'data' yok, ilk once biz veriyi
        dosyadan okuyacagiz. Bu yuzden base'deki 'data eksikse hata ver'
        kontrolu burada anlamsiz ve yanlis olur.

        Bunun yerine, gerekli parametrelerin (file_path) var olup
        olmadigini kontrol ediyoruz.
        """
        # params dict'inden file_path degerini al, yoksa None doner
        file_path = self.params.get("file_path")

        # file_path hic verilmemisse (None, bos string vs.) hata firlat
        if not file_path:
            raise ValueError(f"{self.name}: 'file_path' parametresi eksik")

        # file_path bir string degilse (yanlislikla sayi/liste vs. gelirse) hata firlat
        if not isinstance(file_path, str):
            raise ValueError(f"{self.name}: 'file_path' bir string olmali")

        # dosya uzantisi .csv degilse hata firlat, kucuk/buyuk harf duyarliligi olmasin diye lower() kullanildi
        if not file_path.lower().endswith(".csv"):
            raise ValueError(f"{self.name}: dosya uzantisi .csv olmali, gelen: {file_path}")

    # Asil isi yapan metod, CSV dosyasini okuyup DataFrame olarak doner
    def run(self, inputs: dict) -> dict:
        """
        CSV dosyasini okur ve DataFrame olarak dondurur.

        params icinden okunabilecek opsiyonel ayarlar:
        - file_path   : okunacak dosyanin yolu (zorunlu)
        - separator   : kolon ayirici karakter (varsayilan ",")
        - encoding    : dosya encoding'i (varsayilan "utf-8")
        - header_row  : hangi satirin header (kolon ismi) oldugu (varsayilan 0)
        """
        # zorunlu parametre: dosya yolu
        file_path = self.params.get("file_path")
        # opsiyonel parametre: ayirici karakter, verilmezse virgul kullanilir
        separator = self.params.get("separator", ",")
        # opsiyonel parametre: dosya encoding'i, verilmezse utf-8 kullanilir
        encoding = self.params.get("encoding", "utf-8")
        # opsiyonel parametre: header'in hangi satirda oldugu, verilmezse ilk satir (0) kullanilir
        header_row = self.params.get("header_row", 0)

        # dosya okuma islemini try-except icine aliyoruz, cunku cesitli hatalar olusabilir
        try:
            # pandas'in read_csv fonksiyonu ile dosyayi oku, parametreleri ilet
            df = pd.read_csv(
                file_path,
                sep=separator,
                encoding=encoding,
                header=header_row,
            )
        # dosya path'te bulunamazsa bu hata firlar, biz onu daha anlasilir bir mesaja ceviriyoruz
        except FileNotFoundError:
            raise ValueError(f"{self.name}: dosya bulunamadi -> {file_path}")
        # dosya var ama icinde hic veri yoksa (tamamen bos dosya) bu hata firlar
        except pd.errors.EmptyDataError:
            raise ValueError(f"{self.name}: dosya bos -> {file_path}")
        # yukaridaki iki durumun disinda kalan tum diger hatalari genel olarak yakala
        except Exception as e:
            raise ValueError(f"{self.name}: dosya okunurken hata olustu -> {str(e)}")

        # meta bilgisi olusturuluyor, bu bilgi C tarafina/arayuze donecek
        # boylece butun veriyi (satir satir) tekrar gondermeye gerek kalmiyor
        meta = {
            # df.shape bir tuple doner (satir_sayisi, kolon_sayisi), JSON'a cevrilebilmesi icin list() ile sariyoruz
            "shape": list(df.shape),
            # kolon isimlerini bir Python listesine ceviriyoruz
            "columns": df.columns.tolist(),
            # her kolonun veri tipini (dtype) string'e cevirip dict olarak topluyoruz
            "dtypes": {col: str(dtype) for col, dtype in df.dtypes.items()},
            # her kolondaki eksik (NaN) deger sayisini hesaplayip dict'e ceviriyoruz
            "missing_values": df.isnull().sum().to_dict(),
        }

        # olusan DataFrame ve meta bilgisini bir dict icinde geri donduruyoruz
        return {"data": df, "meta": meta}

    # Bu blok icin ozel validate_output metodu, base class'takini genisletir (extend eder)
    def validate_output(self, result: dict):
        """
        Base class'taki validate_output'u genisletiyoruz.
        Once base'in kontrolunu (bos mu) calistiriyoruz,
        sonra bu bloga ozel bir kontrol daha ekliyoruz:
        en az bir kolon olmali (0 kolonlu bir CSV anlamsizdir).
        """
        # once base class'in kendi kontrolunu calistir (veri bos mu kontrolu)
        super().validate_output(result)

        # NOT: bu noktada result artik normalize edilmis (outputs/meta formatinda),
        # eski "data" anahtari yok - "outputs" uzerinden okunmali
        df = result["outputs"]["output"]
        # eger df gecerliyse ve hic kolonu yoksa (0 kolon) hata firlat
        if df is not None and len(df.columns) == 0:
            raise ValueError(f"{self.name}: yuklenen veride hic kolon yok")