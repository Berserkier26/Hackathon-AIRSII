import socket
import threading

port = 4210
host = "192.168.4.1"

sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
address = ( host, port )
sock.bind(( '0.0.0.0', 8080 ))

def receive_msg():
    while True:
        sock.sendto(input().encode('utf-8'), address)
    

def send_msg():
    payload = ""
    commands = [ "PONG", "IDLE", "BUSY", "STARTED", "DONE" ]
    while True:
       msg, addr = sock.recvfrom(1024)
       msg = msg.decode('utf-8')
       if (len(msg) == 0): continue
       payload += msg
       cmds = payload.split("\n")
       *full_cmds, last_cmd = cmds
       payload = last_cmd

       for cmd in full_cmds:
           if (cmd in commands): print(cmd)
           else: print(f"Unknown command: {cmd}")
           
       if (last_cmd in commands):
           payload = ""
           print(last_cmd)


recvt = threading.Thread(target = receive_msg)
sendt = threading.Thread(target = send_msg)
recvt.start()
sendt.start()

try:
    recvt.join()
    sendt.join()
except KeyboardInterrupt:
    sock.close()
    pass
