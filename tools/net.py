# Консоль для ручной проверки манипулятора с ноутбука (на робот это не идёт).
# 1. Подключить ноутбук к Wi-Fi ROBOT_ARM (пароль 12345678)
# 2. python net.py
# 3. Набрать команду (PING, STATUS, LOAD) и нажать Enter.
#    Чтобы отправить с переводом строки, набрать буквально: PING\n
import socket
import threading

HOST, PORT = "192.168.4.1", 4210
KNOWN = {"PONG", "IDLE", "BUSY", "STARTED", "DONE"}

sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
sock.bind(("0.0.0.0", 0))   # свой адрес и любой свободный порт, ответ придёт сюда


def receiver():
    while True:
        try:
            data, addr = sock.recvfrom(1024)
        except ConnectionResetError:   # бывает на Windows, если пакет не дошёл
            continue
        raw = data.decode("utf-8", errors="replace")
        tag = "" if raw.strip() in KNOWN else "  (неизвестный ответ)"
        # repr показывает \n и \r, если манипулятор их присылает
        print(f"<- {raw!r} от {addr[0]}:{addr[1]}{tag}")


threading.Thread(target=receiver, daemon=True).start()

while True:
    line = input().replace("\\n", "\n")
    if line:
        sock.sendto(line.encode("utf-8"), (HOST, PORT))
        print(f"-> {line!r}")
