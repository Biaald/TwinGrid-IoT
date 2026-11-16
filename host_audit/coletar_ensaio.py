import serial
import csv
import time

PORTA_SERIAL = "/dev/ttyACM0"  # No Linux: /dev/ttyACM0 | No Windows: COMx
BAUD_RATE = 115200
ARQUIVO_SAIDA = "ensaio_eap51_resultados.csv"

def executar_ensaio():
    total_envios = 0
    total_sucessos = 0
    latencias = []

    with serial.Serial(PORTA_SERIAL, BAUD_RATE, timeout=2) as ser, \
         open(ARQUIVO_SAIDA, mode="w", newline="", encoding="utf-8") as csv_file:
        
        print(f"Capturando ensaio de telemetria em {ARQUIVO_SAIDA}... (Ctrl+C para encerrar)")
        try:
            while True:
                linha = ser.readline().decode("utf-8", errors="ignore").strip()
                if not linha or (not linha[0].isdigit() and not linha.startswith("seq,")):
                    continue

                print(linha)
                csv_file.write(linha + "\n")
                csv_file.flush()

                if not linha.startswith("seq,"):
                    partes = linha.split(",")
                    if len(partes) >= 6:
                        total_envios += 1
                        latencia = int(partes[4])
                        sucesso = int(partes[5])
                        if sucesso == 1:
                            total_sucessos += 1
                            latencias.append(latencia)
        except KeyboardInterrupt:
            print("\n=== RESUMO DO ENSAIO (CRITÉRIO DE ACEITE EAP 5.1) ===")
            if total_envios > 0:
                taxa = (total_sucessos / total_envios) * 100.0
                lat_media = sum(latencias) / len(latencias) if latencias else 0
                print(f"Total de pacotes disparados : {total_envios}")
                print(f"Pacotes recebidos pela API  : {total_sucessos} ({taxa:.2f}%)")
                print(f"Latência média HTTPS POST   : {lat_media:.1f} ms")

if __name__ == "__main__":
    executar_ensaio()
