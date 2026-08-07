import requests
import time
from datetime import datetime

# Configurações
IP_ESP32 = "http://192.168.100.49"
INTERVALO = 300  # 300 segundos = 5 minutos

# IDs das telas conforme definido no seu código Arduino (rota /select)
TELAS = [
    {"nome": "Creeper", "url": f"{IP_ESP32}/select?id=-1"},
    {"nome": "WiFi QR Code", "url": f"{IP_ESP32}/select?id=-2"},
    {"nome": "PIX QR Code", "url": f"{IP_ESP32}/select?id=-3"},
    {"nome": "Meteorologia", "url": f"{IP_ESP32}/select?id=-4"}
]

def alternar_telas():
    print(f"--- Iniciando Automação Creeper Auth v6.2 ---")
    print(f"Alvo: {IP_ESP32}")
    
    while True:
        for tela in TELAS:
            agora = datetime.now().strftime("%H:%M:%S")
            try:
                # Envia o comando para o ESP32 mudar a tela
                resposta = requests.get(tela["url"], timeout=10)
                
                if resposta.status_code == 200:
                    print(f"[{agora}] Tela alterada para: {tela['nome']}")
                else:
                    print(f"[{agora}] Erro: ESP32 retornou status {resposta.status_code}")
                    
            except requests.exceptions.RequestException as e:
                print(f"[{agora}] Falha ao conectar no ESP32: {e}")
            
            # Aguarda 5 minutos antes de passar para a próxima tela da lista
            time.sleep(INTERVALO)

if __name__ == "__main__":
    alternar_telas()
