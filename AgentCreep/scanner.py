import os
import json
import socket
import time
import re

# --- CONFIGURAÇÃO ---
ROTEADOR_IP = "192.168.100.1"
ROTEADOR_USER = b"root\n"
ROTEADOR_PASS = b"admin\n"
DB_FILE = "dispositivos_conhecidos.json"
LOG_FILE = "agente_log.txt"

def log_status(mensagem):
    timestamp = time.strftime("%d/%m/%Y %H:%M:%S")
    with open(LOG_FILE, "a", encoding="utf-8") as f:
        f.write(f"[{timestamp}] {mensagem}\n")
    print(f"[{timestamp}] {mensagem}", flush=True)

def carregar_db():
    if os.path.exists(DB_FILE):
        with open(DB_FILE, "r", encoding="utf-8") as f:
            return json.load(f)
    return {}

def salvar_db(db):
    with open(DB_FILE, "w", encoding="utf-8") as f:
        json.dump(db, f, indent=4, ensure_ascii=False)

def get_router_data():
    """ Captura IP, MAC e tenta buscar o Nome (Hostname) """
    encontrados = []
    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        s.settimeout(10)
        s.connect((ROTEADOR_IP, 23))
        time.sleep(1)
        s.sendall(ROTEADOR_USER)
        time.sleep(1)
        s.sendall(ROTEADOR_PASS)
        time.sleep(1)

        # Pegamos primeiro a tabela ARP/Neighbor (IP e MAC)
        s.sendall(b"display ip neigh\n\n\n\n")
        time.sleep(2)
        raw_neigh = s.recv(32768).decode('ascii', errors='ignore')

        # Pegamos a tabela DHCP para tentar achar o NOME do dispositivo
        s.sendall(b"display dhcp server user all\n\n\n\n")
        time.sleep(3)
        raw_dhcp = s.recv(32768).decode('ascii', errors='ignore')
        s.close()

        # 1. Extrair Nomes do DHCP (Dicionário {MAC: Nome})
        nomes_map = {}
        dhcp_matches = re.findall(r"(\d+\.\d+\.\d+\.\d+)\s+([\w-]+|--)\s+([0-9a-f:]{17})", raw_dhcp)
        for ip, nome, mac in dhcp_matches:
            nomes_map[mac.lower()] = nome if nome != "--" else "Desconhecido"

        # 2. Extrair IPs e MACs da tabela Neighbor
        neigh_matches = re.findall(r"([0-9a-fA-F:]{17})\s+([0-9a-fA-F.:]+)", raw_neigh)
        
        for mac, ip in neigh_matches:
            mac_l = mac.lower()
            nome = nomes_map.get(mac_l, "Desconhecido")
            if ip != "--" and ip != ROTEADOR_IP:
                encontrados.append({"mac": mac_l, "ip": ip, "nome": nome})

    except Exception as e:
        log_status(f"Erro no Roteador: {e}")
    return encontrados

def monitorar():
    db = carregar_db()
    log_status("=== MONITORAMENTO INICIADO (SCAN DIRETO) ===")

    while True:
        dispositivos_atuais = get_router_data()
        houve_mudanca = False

        for dev in dispositivos_atuais:
            mac = dev['mac']
            ip = dev['ip']
            nome = dev['nome']

            # Se o MAC não existe no banco, é um OBJETO NOVO
            if mac not in db:
                log_status(f"⚠️ NOVO DISPOSITIVO: [{nome}] | MAC: {mac} | IP: {ip}")
                db[mac] = {
                    "nome": nome,
                    "primeira_vez": time.strftime("%d/%m/%Y %H:%M:%S"),
                    "ultimo_ip": ip,
                    "historico_ips": [ip]
                }
                houve_mudanca = True
            else:
                # Se o MAC já existe, mas mudou o IP ou o Nome, atualizamos
                if ip not in db[mac]["historico_ips"]:
                    db[mac]["historico_ips"].append(ip)
                    db[mac]["ultimo_ip"] = ip
                    log_status(f"ℹ️ IP Alterado: {nome} ({mac}) agora está em {ip}")
                    houve_mudanca = True

        if houve_mudanca:
            salvar_db(db)
        
        time.sleep(30)

if __name__ == "__main__":
    monitorar()
