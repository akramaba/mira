# image_to_mi.py
# Converts a PNG image to a Mira .mi 
# image file with a black background.

from tkinter import filedialog, Tk
from PIL import Image
import os
import struct

root = Tk()
root.withdraw()

# 1. Select PNG File
png_path = filedialog.askopenfilename(
    title="Select a PNG file",
    filetypes=[("PNG files", "*.png")]
)

if png_path:
    # * Convert to Custom .mi (Mira Image) Format * #
    
    img = Image.open(png_path).convert("RGBA")
    
    mi_img = Image.new("RGB", img.size, (0, 0, 0))
    # This "pastes" the image onto a black background
    mi_img.paste(img, (0, 0), img)
    
    width, height = mi_img.size
    raw_pixels = mi_img.tobytes()
    
    # Create the final pixel data buffer
    # * The .mi format goes as (B, G, R, 0x00) per pixel
    pixel_data = bytearray(width * height * 4)
    for i in range(width * height):
        r = raw_pixels[i * 3 + 0]
        g = raw_pixels[i * 3 + 1]
        b = raw_pixels[i * 3 + 2]
        
        pixel_data[i * 4 + 0] = b  # Blue
        pixel_data[i * 4 + 1] = g  # Green
        pixel_data[i * 4 + 2] = r  # Red
        pixel_data[i * 4 + 3] = 0  # 0x00 (padding, unused by Mira)

    # Save the File
    
    # ? .mi files start with its width and height.
    # ? <II = LE unsigned int (width, height), 4 bytes each.
    mi_blob = struct.pack("<II", width, height) + pixel_data

    original_name = os.path.splitext(os.path.basename(png_path))[0]
    
    save_path = filedialog.asksaveasfilename(
        title="Save as Mira Image",
        initialfile=f"{original_name}.mi",
        defaultextension=".mi",
        filetypes=[("Mira Image files", "*.mi")]
    )

    if save_path:
        with open(save_path, "wb") as f:
            f.write(mi_blob)
        print(f"[mi] File saved to {save_path}")
    else:
        print("[mi] Save cancelled.")
else:
    print("[mi] No file selected.")