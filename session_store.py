# session_store.py

class SessionStore:
    """
    Veriyi node_id + output_slot kombinasyonuyla saklar.
    Rastgele/sayacli id uretimi YOK artik - anahtar tamamen
    C tarafinin belirledigi node_id'ye dayanir. Bu sayede:
    - Bir node tekrar calistiginda eski ciktisi otomatik olarak
      uzerine yazilir (overwrite), ayri bir silme islemine gerek kalmaz.
    - Bir node'un birden fazla ciktisi (train/test gibi) ayni node_id
      altinda farkli slot isimleriyle saklanabilir.
    """

    def __init__(self):
        # key formati: "node_id:slot_name" -> obje
        self._store = {}

    def set(self, node_id: str, slot: str, obj) -> str:
        """Belirli bir node+slot icin veriyi kaydeder (varsa uzerine yazar)."""
        key = f"{node_id}:{slot}"
        self._store[key] = obj
        return key

    def get(self, ref: str):
        """ref formati 'node_id:slot' seklinde bir referans string'idir."""
        if ref not in self._store:
            raise ValueError(f"SessionStore: unknown ref -> {ref}")
        return self._store[ref]

    def clear_node(self, node_id: str):
        """
        Bir node'a ait TUM slotlari siler.
        Bu, hem 'node tekrar calisti, eski ciktilarini temizle'
        hem de 'kullanici node'u pipeline'dan sildi' senaryolarinda kullanilir.
        Onemli: bir node onceden 2 cikis uretmisken (train/test) sonradan
        tek cikis uretir hale gelirse, eski fazladan slot burada temizlenir.
        """
        prefix = f"{node_id}:"
        keys_to_delete = [k for k in self._store if k.startswith(prefix)]
        for k in keys_to_delete:
            del self._store[k]

    def delete_node(self, node_id: str):
        """Kullanici bir node'u pipeline'dan tamamen sildiginde cagrilir."""
        self.clear_node(node_id)