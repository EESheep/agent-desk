"""Capture the device's rendered LVGL buffer over its USB debug channel."""
import argparse
import struct
import time
import zlib
from pathlib import Path
import serial

def png_rgb565(width,height,rows):
    pixels=bytearray()
    for y in range(height):
        pixels.append(0)
        for (value,) in struct.iter_unpack('<H',rows[y]):
            pixels.extend((((value>>11)&31)*255//31,((value>>5)&63)*255//63,(value&31)*255//31))
    def chunk(kind,data):
        return struct.pack('>I',len(data))+kind+data+struct.pack('>I',zlib.crc32(kind+data)&0xffffffff)
    return b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',width,height,8,2,0,0,0))+chunk(b'IDAT',zlib.compress(pixels))+chunk(b'IEND',b'')

def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--port',default='COM4')
    parser.add_argument('--page',type=int,choices=range(4),default=0)
    parser.add_argument('--output',required=True)
    args=parser.parse_args()
    s=serial.Serial(); s.port=args.port; s.baudrate=115200; s.timeout=.5; s.dtr=s.rts=False; s.open()
    if hasattr(s,'set_buffer_size'): s.set_buffer_size(rx_size=1048576,tx_size=4096)
    rows={}; width=height=0
    with s:
        s.write(f'\nPAGE {args.page}\n'.encode())
        deadline=time.monotonic()+5
        while time.monotonic()<deadline:
            if b'PAGE_READY' in s.readline(): break
        s.write(b'SCREEN\n')
        deadline=time.monotonic()+45
        raw=bytearray()
        while time.monotonic()<deadline:
            # Bulk reads avoid overflowing the Windows RX queue with per-byte readline calls.
            raw.extend(s.read(s.in_waiting or 1))
            if b'SCREEN_END\n' in raw[-4096:] or b'SCREEN_END\r\n' in raw[-4096:]: break
        for line in raw.decode('ascii',errors='replace').splitlines():
            line=line.strip()
            if line.startswith('SCREEN_BEGIN '): _,w,h=line.split(); width,height=int(w),int(h)
            elif line.startswith('ROW '):
                try:
                    _,y,data=line.split(); row=bytes.fromhex(data)
                    if len(row)==width*2: rows[int(y)]=row
                except ValueError: pass
            elif line=='SCREEN_END': break
    if width!=600 or height!=450 or set(rows)!=set(range(height)):
        raise SystemExit(f'Incomplete screenshot: {len(rows)}/{height} rows')
    Path(args.output).write_bytes(png_rgb565(width,height,rows))
    print('Captured',args.output)

if __name__=='__main__': main()
