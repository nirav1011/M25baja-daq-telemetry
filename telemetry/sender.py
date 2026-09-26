import serial
import time
import struct
import random


# --- CONFIGURATION ---
COM_PORT = 'COM4'  
BAUD_RATE = 57600  


# --- PACKET STRUCTURE ---
# '<' = Little Endian
# 'i' = Signed Integer (4 bytes) -> Used for Latitude
# 'i' = Signed Integer (4 bytes) -> Used for Longitude
# Total Packet Size = 4 + 4 = 8 bytes
STRUCT_FORMAT = '<i i'


try:
    ser = serial.Serial(COM_PORT, BAUD_RATE, timeout=None)
    print(f"Connected to Sender on {COM_PORT}")
   
    # Starting coordinates (mocking the UCLA area)
    lat_float = 34.068921  
    lon_float = -118.445181


    while True:
        # Simulate movement by adding a small random drift to the floats
        # +/- 0.0001 degrees is roughly a 30ft movement in any direction
        lat_float += random.uniform(-0.001, 0.001)
        lon_float += random.uniform(-0.001, 0.001)


        # 1. Scale Floats to Integers (x10,000,000)
        lat_scaled = int(lat_float * 10000000)
        lon_scaled = int(lon_float * 10000000)


        # 2. Pack into Binary (8 bytes total)
        packet = struct.pack(STRUCT_FORMAT, lat_scaled, lon_scaled)
       
        # 3. Send over Serial
  # claude wanted to add this ser.write(b'\xAA\x55')
        ser.write(packet)
        ser.flush()
       
        print(f"Sent Packet ({len(packet)} bytes): {packet.hex()} | Lat: {lat_float:.6f}, Lon: {lon_float:.6f}")
   
        time.sleep(1)


except serial.SerialException as e:
    print(f"Error opening serial port: {e}")
except KeyboardInterrupt:
    if 'ser' in locals() and ser.is_open:
        ser.close()
    print("\nSender stopped.")
