import sys
import serial
import serial.tools.list_ports

BAUD_RATE = 115200
ARQUIVO_SAIDA = "ensaio_eap52_esp32.csv"

def detectar_porta():
    for p in serial.tools.list_ports.comports():
        if "ttyUSB" in p.device or "ttyACM" in p.device:
            print(f"Porta detectada: {p.device}")
            return p.device
    print("ERRO: Nenhuma porta serial encontrada.")
    sys.exit(1)

def executar_ensaio():
    porta = detectar_porta()
    seqs_geradas = set()
    chaves_entregues = []
    seqs_entregues = set()
    eventos_offline = 0
    retransmissoes = 0
    cabecalho_gravado = False

    with serial.Serial(porta, BAUD_RATE, timeout=2) as ser, \
         open(ARQUIVO_SAIDA, mode="w", newline="", encoding="utf-8") as csv_file:

        print(f"Capturando ensaio EAP 5.2 em {ARQUIVO_SAIDA}... (Ctrl+C para encerrar)")
        try:
            while True:
                linha = ser.readline().decode("utf-8", errors="ignore").strip()
                if not linha:
                    continue
                if linha.startswith("#"):
                    print(linha)
                    continue
                if linha.startswith("seq_evento,"):
                    if not cabecalho_gravado:
                        print(linha)
                        csv_file.write(linha + "\n")
                        csv_file.flush()
                        cabecalho_gravado = True
                    continue
                if not linha[0].isdigit():
                    continue

                print(linha)
                csv_file.write(linha + "\n")
                csv_file.flush()

                partes = linha.split(",")
                if len(partes) >= 7:
                    seq_num = int(partes[0])
                    idem_key = partes[1]
                    acao = partes[2]
                    status = int(partes[4])

                    seqs_geradas.add(seq_num)
                    if acao == "ENQUEUE_OFFLINE":
                        eventos_offline += 1
                    elif acao == "DRAIN_RETRY" and (200 <= status <= 299 or status == 409):
                        retransmissoes += 1

                    if 200 <= status <= 299 or status == 409:
                        seqs_entregues.add(seq_num)
                        chaves_entregues.append(idem_key)

        except KeyboardInterrupt:
            perdidos = seqs_geradas - seqs_entregues
            duplicados = len(chaves_entregues) - len(set(chaves_entregues))
            print("\n=== AUDITORIA DE ACEITE EAP 5.2 (FALHA E RETOMADA DE REDE) ===")
            print(f"Total de leituras geradas          : {len(seqs_geradas)}")
            print(f"Leituras retidas na Flash (Offline): {eventos_offline}")
            print(f"Pacotes recuperados da Flash       : {retransmissoes}")
            print(f"Total entregue com sucesso na API  : {len(seqs_entregues)}")
            print(f"Pacotes perdidos                   : {len(perdidos)} (Meta: 0)")
            print(f"Pacotes duplicados                 : {duplicados} (Meta: 0)")
            status_final = "APROVADO" if (len(perdidos) == 0 and duplicados == 0 and eventos_offline > 0) else "PENDENTE"
            print(f"RESULTADO DO CRITÉRIO DE ACEITE    : {status_final}")

if __name__ == "__main__":
    executar_ensaio()
