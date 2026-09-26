import serial
import struct
import time


# --- CONFIGURATION ---
COM_PORT = 'COM3'  
BAUD_RATE = 57600


# Matches Sender: < (Little Endian), i (Lat), i (Lon)``
STRUCT_FORMAT = '<i i'
PACKET_SIZE = struct.calcsize(STRUCT_FORMAT) # 8 bytes


try:
    ser = serial.Serial(COM_PORT, BAUD_RATE, timeout=2)
    print(f"Listening on {COM_PORT} @ {BAUD_RATE}...")
    ser.reset_input_buffer()
   
    # Start the local Pit Laptop clock
    start_time = int(time.time() * 1000)


    while True:
        # 1. Read exactly one 8-byte payload
        buffer = ser.read(PACKET_SIZE)
       
        # 2. Process if complete
        if len(buffer) == PACKET_SIZE:
            try:
                data = struct.unpack(STRUCT_FORMAT, buffer)
                latitude = data[0] / 10000000.0
                longitude = data[1] / 10000000.0
               
                # Timestamp generated locally!
                current_time = int(time.time() * 1000) - start_time
               
                print(f"Time: {current_time}ms | Lat: {latitude:.6f} | Lon: {longitude:.6f}")
            except struct.error:
                print("Packet corrupted during unpack.")
       
        elif len(buffer) > 0:
            # If the timeout hits mid-packet, flush the bad data
            print(f"Incomplete read: Expected {PACKET_SIZE}, got {len(buffer)}")
            ser.reset_input_buffer()


except serial.SerialException as e:
    print(f"Error opening serial port: {e}")
except KeyboardInterrupt:
    if 'ser' in locals() and ser.is_open:
        ser.close()
    print("\nReceiver stopped.")
