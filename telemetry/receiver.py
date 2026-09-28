"""Base-station receiver: LoRa serial radio -> WebSocket.

Reads GPS packets from the base-station LoRa radio over USB serial and
re-broadcasts them as JSON to any connected dashboard over WebSocket.

Packet format (little-endian, 8 bytes, sent ~1 Hz from the car):
    int32 latitude  * 1e7
    int32 longitude * 1e7

Packets have no sync header, so they are framed by the idle gap between
them: the radio delivers each packet as one burst, and any bytes left over
when the line goes quiet are discarded. This keeps the reader aligned even
if a byte is dropped over the air.
"""

import argparse
import asyncio
import json
import math
import struct
import time

import serial
import websockets

# --- CONFIGURATION (overridable from the command line) ---
COM_PORT = '/dev/tty.usbmodem1101'
BAUD_RATE = 57600
WS_PORT = 8765

STRUCT_FORMAT = '<i i'
PACKET_SIZE = struct.calcsize(STRUCT_FORMAT)  # 8 bytes

MAX_SPEED_MPH = 60        # Baja car can't do 60+ mph; faster jumps are corrupt
MAX_REJECTS = 3           # after this many rejects in a row, trust the new position

clients = set()


async def ws_handler(ws):
    clients.add(ws)
    print(f"Dashboard connected ({len(clients)} client(s))")
    try:
        await ws.wait_closed()
    finally:
        clients.discard(ws)
        print(f"Dashboard disconnected ({len(clients)} client(s))")


async def broadcast(msg):
    for ws in clients.copy():
        try:
            await ws.send(msg)
        except websockets.ConnectionClosed:
            clients.discard(ws)


def implied_speed_mph(lat1, lon1, lat2, lon2, dt):
    # Equirectangular approximation — plenty accurate over a 1 s hop
    dlat = (lat2 - lat1) * 111320
    dlon = (lon2 - lon1) * 111320 * abs(math.cos(math.radians(lat2)))
    return math.hypot(dlat, dlon) / dt * 2.23694


async def serial_reader(port, baud):
    ser = serial.Serial(port, baud, timeout=0.1)
    print(f"Listening on {port} @ {baud}...")
    ser.reset_input_buffer()
    start_time = time.time()

    last_lat = None
    last_lon = None
    last_time = None
    rejects = 0
    pending = b''

    while True:
        # Blocking read runs in a worker thread so the WebSocket server stays responsive
        chunk = await asyncio.to_thread(ser.read, PACKET_SIZE - len(pending))

        if not chunk:
            # Line went idle: anything half-received is a broken packet
            if pending:
                print(f"Incomplete packet: expected {PACKET_SIZE}, got {len(pending)} — discarded")
                pending = b''
            continue

        pending += chunk
        if len(pending) < PACKET_SIZE:
            continue

        packet, pending = pending[:PACKET_SIZE], b''
        lat_raw, lon_raw = struct.unpack(STRUCT_FORMAT, packet)
        latitude = lat_raw / 1e7
        longitude = lon_raw / 1e7

        # Sanity check: coordinates must be on Earth
        if not (-90 <= latitude <= 90 and -180 <= longitude <= 180):
            print(f"Bad coords: {latitude}, {longitude} — skipping (likely misaligned)")
            ser.reset_input_buffer()
            continue

        # Sanity check: reject impossible jumps from the last good fix. If several
        # in a row get rejected, the *reference* is probably the bad one, so reset it.
        now = time.time()
        if last_lat is not None and rejects < MAX_REJECTS:
            dt = now - last_time
            if dt > 0:
                speed_mph = implied_speed_mph(last_lat, last_lon, latitude, longitude, dt)
                if speed_mph > MAX_SPEED_MPH:
                    rejects += 1
                    print(f"Impossible speed: {speed_mph:.0f} mph — skipping")
                    continue
        rejects = 0

        last_lat = latitude
        last_lon = longitude
        last_time = now

        elapsed_ms = int((now - start_time) * 1000)
        print(f"Time: {elapsed_ms}ms | Lat: {latitude:.6f} | Lon: {longitude:.6f}")

        await broadcast(json.dumps({
            "lat": latitude,
            "lon": longitude,
            "timestamp": now * 1000,
        }))


async def main(port, baud, ws_port):
    async with websockets.serve(ws_handler, "0.0.0.0", ws_port):
        print(f"WebSocket server on ws://localhost:{ws_port}")
        await serial_reader(port, baud)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--port", default=COM_PORT, help="LoRa radio serial port")
    parser.add_argument("--baud", type=int, default=BAUD_RATE)
    parser.add_argument("--ws-port", type=int, default=WS_PORT)
    args = parser.parse_args()

    try:
        asyncio.run(main(args.port, args.baud, args.ws_port))
    except KeyboardInterrupt:
        print("\nReceiver stopped.")
    except serial.SerialException as e:
        print(f"Serial error: {e}")
