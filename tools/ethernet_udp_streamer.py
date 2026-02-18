# ethernet_udp_streamer.py
# Sends a WAV file to Mira over Ethernet UDP.
# This is to test the Intel HDA and Intel E1000 drivers.

import socket, time
from tkinter import Tk, filedialog
from pydub import AudioSegment

IP = "127.0.0.1"
PORT = 2026

root = Tk()
root.withdraw()

path = filedialog.askopenfilename(filetypes=[("WAV", "*.wav")])
if not path: 
    exit()

raw = AudioSegment.from_wav(path).set_frame_rate(48000).set_sample_width(2).set_channels(1).raw_data
sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)

for i in range(0, len(raw), 1024): # Mira's drivers can't handle fragmentation,
                                   # so this should stay under 1500 bytes.
    sock.sendto(raw[i:i+1024], (IP, PORT))
    time.sleep(0.001)

sock.sendto(b"EOF", (IP, PORT))
sock.close()