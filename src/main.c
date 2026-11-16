import network
import ntptime
import urequests
import ujson
import utime
import machine
import os
import gc
import ubinascii

# ==================== CONFIGURAÇÕES ====================
WIFI_SSID = "seu-wifi"
WIFI_PASS = "sua-senha-wifi"

# Para testar localmente com deduplicacao real, aponte para o servidor de teste no seu Linux
# Exemplo: "http://192.168.43.100:8000/api/v1/telemetry" ou "https://httpbin.org/post"
API_URL = "https://httpbin.org/post"
API_TOKEN = "Bearer SEU_TOKEN_AQUI"
DEVICE_ID = "esp32-energytrack-01"

INTERVALO_COLETA_MS = 10000       # Coleta a cada 10s
MAX_DRENO_POR_CICLO = 5           # Quantos pacotes da fila reenviar por ciclo quando online
CAPACIDADE_FILA = 200             # 200 slots * 256 bytes = 51.2 KB na Flash
TAMANHO_SLOT = 256

ARQUIVO_FILA = "fila_offline.bin"
ARQUIVO_META = "fila_meta.json"
CSV_FILENAME = "ensaio_eap52.csv"

wlan = None
BOOT_ID = ubinascii.hexlify(machine.unique_id()).decode() + "-" + str(utime.ticks_ms() & 0xFFFF)

# ==================== FILA CIRCULAR EM FLASH ====================
class FlashRingBuffer:
    def __init__(self, data_file=ARQUIVO_FILA, meta_file=ARQUIVO_META, capacity=CAPACIDADE_FILA, slot_size=TAMANHO_SLOT):
        self.data_file = data_file
        self.meta_file = meta_file
        self.capacity = capacity
        self.slot_size = slot_size
        self.head = 0
        self.tail = 0
        self.count = 0
        self._inicializar_arquivos()

    def _inicializar_arquivos(self):
        try:
            os.stat(self.data_file)
        except OSError:
            with open(self.data_file, "wb") as f:
                f.write(b"\x20" * self.slot_size)
        try:
            with open(self.meta_file, "r") as f:
                m = ujson.loads(f.read())
                self.head = m.get("head", 0)
                self.tail = m.get("tail", 0)
                self.count = m.get("count", 0)
        except Exception:
            self._salvar_meta()

    def _salvar_meta(self):
        with open(self.meta_file, "w") as f:
            f.write(ujson.dumps({"head": self.head, "tail": self.tail, "count": self.count}))

    def enqueue(self, pacote_dict):
        raw = ujson.dumps(pacote_dict).encode("utf-8")
        if len(raw) > self.slot_size:
            raw = raw[:self.slot_size]
        padded = raw + (b"\x20" * (self.slot_size - len(raw)))

        with open(self.data_file, "r+b") as f:
            f.seek(self.head * self.slot_size)
            f.write(padded)

        self.head = (self.head + 1) % self.capacity
        if self.count == self.capacity:
            # Fila cheia: avanca tail (sobrescreve o mais antigo)
            self.tail = (self.tail + 1) % self.capacity
        else:
            self.count += 1
        self._salvar_meta()

    def peek(self):
        if self.count == 0:
            return None
        with open(self.data_file, "rb") as f:
            f.seek(self.tail * self.slot_size)
            raw = f.read(self.slot_size).decode("utf-8").strip()
            return ujson.loads(raw)

    def pop(self):
        if self.count == 0:
            return
        self.tail = (self.tail + 1) % self.capacity
        self.count -= 1
        self._salvar_meta()

# ==================== REDE E RELÓGIO NTP ====================
def inicializar_radio():
    global wlan
    if wlan is None:
        wlan = network.WLAN(network.STA_IF)
        wlan.active(True)
        utime.sleep_ms(200)
        try:
            wlan.config(txpower=15)
        except Exception:
            pass

def conectar_wifi(timeout_s=8):
    inicializar_radio()
    if wlan.isconnected():
        return True
    try:
        wlan.disconnect()
    except Exception:
        pass
    utime.sleep_ms(150)
    wlan.connect(WIFI_SSID, WIFI_PASS)
    inicio = utime.time()
    while not wlan.isconnected():
        if (utime.time() - inicio) >= timeout_s:
            return False
        utime.sleep_ms(400)
    return True

def sincronizar_ntp():
    ntptime.host = "a.st1.ntp.br"
    for _ in range(3):
        try:
            ntptime.settime()
            return True
        except Exception:
            ntptime.host = "pool.ntp.org"
            utime.sleep(1)
    return False

def obter_timestamp_iso8601():
    tm = utime.gmtime()
    return "{:04d}-{:02d}-{:02d}T{:02d}:{:02d}:{:02d}Z".format(
        tm[0], tm[1], tm[2], tm[3], tm[4], tm[5]
    )

# ==================== LEITURA E MONTAGEM DO PACOTE ====================
def gerar_pacote_telemetria(seq):
    ts_utc = obter_timestamp_iso8601()
    idem_key = "{}-{}-{}".format(DEVICE_ID, BOOT_ID, seq)
    return {
        "idempotency_key": idem_key,
        "device_id": DEVICE_ID,
        "seq": seq,
        "timestamp": ts_utc,
        "metrics": {
            "v_v": 220.4,
            "i_a": 4.85,
            "p_w": 1047.5,
            "e_kwh": round(18.40 + (seq * 0.01), 3),
            "pf": 0.98
        }
    }

# ==================== LOG CSV DE AUDITORIA EAP 5.2 ====================
def inicializar_csv():
    cabecalho = "seq_evento,idempotency_key,acao,ocupacao_fila,http_status,latencia_ms,resultado\n"
    try:
        os.stat(CSV_FILENAME)
    except OSError:
        with open(CSV_FILENAME, "w") as f:
            f.write(cabecalho)
    print(cabecalho.strip())

def registrar_csv(seq_ev, idem_key, acao, fila_cnt, status_code, lat_ms, resultado):
    linha = "{},{},{},{},{},{},{}\n".format(
        seq_ev, idem_key, acao, fila_cnt, status_code, lat_ms, resultado
    )
    print(linha.strip())
    try:
        with open(CSV_FILENAME, "a") as f:
            f.write(linha)
    except Exception:
        pass

# ==================== TRANSMISSÃO IDEMPOTENTE ====================
def transmitir_pacote(pacote):
    headers = {
        "Content-Type": "application/json",
        "Authorization": API_TOKEN,
        "Idempotency-Key": pacote["idempotency_key"]
    }
    gc.collect()
    t0 = utime.ticks_ms()
    try:
        resp = urequests.post(API_URL, data=ujson.dumps(pacote), headers=headers)
        lat = utime.ticks_diff(utime.ticks_ms(), t0)
        code = resp.status_code
        resp.close()
        # 200/201 = Criado com sucesso | 409 = Conflito de Idempotencia (ja recebido antes)
        entregue = (200 <= code <= 299) or (code == 409)
        return entregue, code, lat, ("OK" if entregue else "HTTP_" + str(code))
    except Exception as e:
        lat = utime.ticks_diff(utime.ticks_ms(), t0)
        return False, 0, lat, type(e).__name__

# ==================== LOOP PRINCIPAL ====================
def main():
    print("# [BOOT] Aguardando 3s de seguranca antes de iniciar o radio...")
    utime.sleep(3)

    fila = FlashRingBuffer()
    ntp_sincronizado = False
    if conectar_wifi():
        ntp_sincronizado = sincronizar_ntp()

    inicializar_csv()
    seq = 1

    while True:
        ciclo_t0 = utime.ticks_ms()
        pacote_atual = gerar_pacote_telemetria(seq)

        # 1. Sempre persiste na fila circular primeiro (garante zero perda se cair no meio do envio)
        fila.enqueue(pacote_atual)

        if not conectar_wifi():
            # Modo Offline: mantem na Flash e registra o armazenamento local
            registrar_csv(seq, pacote_atual["idempotency_key"], "ENQUEUE_OFFLINE", fila.count, 0, 0, "BUFFERED_IN_FLASH")
        else:
            if not ntp_sincronizado:
                ntp_sincronizado = sincronizar_ntp()

            # 2. Modo Online: drena a fila em ordem FIFO (do mais antigo para o mais novo)
            enviados_neste_ciclo = 0
            while fila.count > 0 and enviados_neste_ciclo < MAX_DRENO_POR_CICLO:
                item = fila.peek()
                acao = "DRAIN_RETRY" if item["seq"] < seq else "SEND_LIVE"
                ok, code, lat, msg = transmitir_pacote(item)

                if ok:
                    fila.pop() # So remove da Flash apos confirmacao 2xx ou 409 da API
                    registrar_csv(item["seq"], item["idempotency_key"], acao, fila.count, code, lat, msg)
                    enviados_neste_ciclo += 1
                else:
                    # Falha durante a transmissao: interrompe o dreno e mantem na fila
                    registrar_csv(item["seq"], item["idempotency_key"], "SEND_FAIL", fila.count, code, lat, msg)
                    break

        seq += 1
        decorrido = utime.ticks_diff(utime.ticks_ms(), ciclo_t0)
        espera = INTERVALO_COLETA_MS - decorrido
        if espera >= 10:
            utime.sleep_ms(espera)

if __name__ == "__main__":
    main()
