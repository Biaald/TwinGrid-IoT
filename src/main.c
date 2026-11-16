import network
import ntptime
import urequests
import ujson
import utime
import machine
import os
import gc

# ==================== CONFIGURAÇÕES ====================
WIFI_SSID = "sua-rede-wifi"
WIFI_PASS = "sua-senha-wifi"

API_URL = "https://httpbin.org/post"
API_TOKEN = "Bearer energy"
DEVICE_ID = "esp32-energytrack-01"

INTERVALO_ENVIO_MS = 10000        # 10 segundos entre envios
INTERVALO_RESYNC_NTP_S = 21600    # Ressincroniza NTP a cada 6 horas
CSV_FILENAME = "ensaio_envio.csv"

wlan = None

# ==================== INICIALIZAÇÃO DE REDE ====================
def inicializar_radio():
    global wlan
    if wlan is None:
        wlan = network.WLAN(network.STA_IF)
        wlan.active(True)
        utime.sleep_ms(200)
        try:
            # Limita o pico de corrente TX em 15 dBm para proteger a porta USB
            wlan.config(txpower=15)
        except Exception:
            pass

def conectar_wifi(timeout_s=15):
    inicializar_radio()
    if wlan.isconnected():
        return True
    print(f"# [BOOT] Conectando ao Wi-Fi '{WIFI_SSID}'...")
    try:
        wlan.disconnect()
    except Exception:
        pass
    utime.sleep_ms(200)
    wlan.connect(WIFI_SSID, WIFI_PASS)
    inicio = utime.time()
    while not wlan.isconnected():
        if (utime.time() - inicio) >= timeout_s:
            print(f"# [ERRO] Timeout no Wi-Fi. Status: {wlan.status()}")
            return False
        utime.sleep_ms(500)
    print(f"# [OK] Wi-Fi conectado! IP: {wlan.ifconfig()[0]}")
    return True

def sincronizar_ntp(tentativas=4):
    ntptime.host = "a.st1.ntp.br"
    print("# [BOOT] Sincronizando relogio via NTP (a.st1.ntp.br)...")
    for i in range(tentativas):
        try:
            ntptime.settime()
            print(f"# [OK] Horario UTC sincronizado: {obter_timestamp_iso8601()}")
            return True
        except Exception as e:
            print(f"# [AVISO] Tentativa NTP {i+1} falhou ({type(e).__name__})")
            ntptime.host = "pool.ntp.org"
            utime.sleep(1)
    return False

def obter_timestamp_iso8601():
    tm = utime.gmtime()
    return "{:04d}-{:02d}-{:02d}T{:02d}:{:02d}:{:02d}Z".format(
        tm[0], tm[1], tm[2], tm[3], tm[4], tm[5]
    )

# ==================== LEITURA DOS DADOS PROCESSADOS ====================
def ler_dados_processados():
    return {
        "voltage_v": 220.4,
        "current_a": 4.85,
        "active_power_w": 1047.5,
        "energy_kwh": 18.420,
        "power_factor": 0.98,
        "frequency_hz": 60.0
    }

# ==================== LOG CSV DE ENSAIO ====================
def inicializar_csv():
    cabecalho = "seq,timestamp_utc,rssi_dbm,http_status,latencia_ms,sucesso,erro\n"
    try:
        os.stat(CSV_FILENAME)
    except OSError:
        with open(CSV_FILENAME, "w") as f:
            f.write(cabecalho)
    print(cabecalho.strip())

def registrar_linha_csv(seq, ts, rssi, status_code, latencia_ms, sucesso, erro=""):
    linha = "{},{},{},{},{},{},{}\n".format(
        seq, ts, rssi, status_code, latencia_ms, int(sucesso), erro
    )
    print(linha.strip())
    try:
        with open(CSV_FILENAME, "a") as f:
            f.write(linha)
    except Exception:
        pass

# ==================== ENVIO HTTPS POST ====================
def enviar_telemetria(seq):
    if not conectar_wifi():
        registrar_linha_csv(seq, obter_timestamp_iso8601(), -100, 0, 0, False, "WIFI_DISCONNECTED")
        return

    rssi = wlan.status("rssi")
    ts_utc = obter_timestamp_iso8601()
    medidas = ler_dados_processados()

    payload = {
        "device_id": DEVICE_ID,
        "seq": seq,
        "timestamp": ts_utc,
        "metrics": medidas
    }

    headers = {
        "Content-Type": "application/json",
        "Authorization": API_TOKEN
    }

    gc.collect()
    t_inicio = utime.ticks_ms()
    status_code = 0
    sucesso = False
    erro_msg = "OK"

    try:
        resposta = urequests.post(
            API_URL,
            data=ujson.dumps(payload),
            headers=headers
        )
        latencia = utime.ticks_diff(utime.ticks_ms(), t_inicio)
        status_code = resposta.status_code
        resposta.close()
        sucesso = (status_code >= 200 and status_code <= 299)
        if not sucesso:
            erro_msg = "HTTP_" + str(status_code)
    except Exception as e:
        latencia = utime.ticks_diff(utime.ticks_ms(), t_inicio)
        erro_msg = type(e).__name__

    registrar_linha_csv(seq, ts_utc, rssi, status_code, latencia, sucesso, erro_msg)

# ==================== LOOP PRINCIPAL ====================
def main():
    print("# [BOOT] Aguardando 3s de seguranca antes de iniciar o radio...")
    utime.sleep(3)

    if conectar_wifi():
        sincronizar_ntp()
    inicializar_csv()
    ultimo_sync_ntp = utime.time()
    seq = 1

    while True:
        ciclo_inicio = utime.ticks_ms()

        if (utime.time() - ultimo_sync_ntp) >= INTERVALO_RESYNC_NTP_S:
            if conectar_wifi() and sincronizar_ntp():
                ultimo_sync_ntp = utime.time()

        enviar_telemetria(seq)
        seq += 1

        decorrido = utime.ticks_diff(utime.ticks_ms(), ciclo_inicio)
        espera = INTERVALO_ENVIO_MS - decorrido
        if espera >= 10:
            utime.sleep_ms(espera)

if __name__ == "__main__":
    main()
