# serial_debugger.py
# A simple serial log receiver for MiraOS running in QEMU.
# The same logs also appear on the shell dashboard, but you can
# use this tool to see them quicker and with timestamps.

import socket
import time
from datetime import datetime

HOST = '127.0.0.1'
PORT = 6472

while True:
    try:
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
            print(f"Attempting to connect to QEMU...")
            s.connect((HOST, PORT))
            print("Connected to QEMU!")
            print("--- Starting Debug Log ---")
            
            buffer = ""
            
            while True:
                data = s.recv(1024)
                if not data:
                    break

                text = data.decode('utf-8', errors='ignore')
                buffer += text
                
                # Print complete lines from buffer
                while "\n" in buffer:
                    line, buffer = buffer.split("\n", 1)
                    timestamp = datetime.now().strftime("%H:%M:%S.%f")[:-3]
                    print(f"[{timestamp}] {line}")
            
            # Check for more data in buffer if it doesn't end with a newline
            if buffer:
                timestamp = datetime.now().strftime("%H:%M:%S.%f")[:-3]
                print(f"[{timestamp}] {buffer}", end='')

    except ConnectionRefusedError:
        time.sleep(1) # Will keep looping until it connects to Mira
    except (ConnectionResetError, BrokenPipeError):
        print("\n--- Log Stopped (QEMU likely exited) ---")
        time.sleep(1)
    except KeyboardInterrupt: # Ctrl+C to exit
        print("\n--- Debugger exited by user ---")
        break