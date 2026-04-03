#!/usr/bin/env python3
"""mkboot.py - generate SavaOS 16-bit bootloader binary"""
import struct, sys

def build():
    buf = bytearray(512)
    code = bytearray()
    DRIVE_OFF=0x1F0; ML=0x100; MOK=0x11C; MERR=0x128
    code+=b'\xfa\x31\xc0\x8e\xd8\x8e\xc0\x8e\xd0\xbc\x00\x7c\xfb'
    code+=bytes([0x88,0x16,DRIVE_OFF&0xFF,(DRIVE_OFF>>8)&0xFF])
    code+=b'\xb8\x03\x00\xcd\x10\xb8\x00\x01\xb9\x07\x26\xcd\x10'
    code+=bytes([0xbe,ML&0xFF,(ML>>8)&0xFF]); c1=len(code); code+=b'\xe8\x00\x00'
    code+=b'\xb4\x02\xb0\x3f\xb5\x00\xb1\x01\xb6\x00'
    code+=bytes([0x8a,0x16,DRIVE_OFF&0xFF,(DRIVE_OFF>>8)&0xFF])
    code+=b'\xbb\x00\x10\xcd\x13'; jc=len(code); code+=b'\x72\x00'
    code+=bytes([0xbe,MOK&0xFF,(MOK>>8)&0xFF]); c2=len(code); code+=b'\xe8\x00\x00'
    code+=b'\xe4\x92\x0c\x02\x24\xfe\xe6\x92'
    GD=0x1E0
    code+=bytes([0x0f,0x01,0x16,GD&0xFF,(GD>>8)&0xFF])
    code+=b'\x66\x0f\x20\xc0\x66\x83\xc8\x01\x66\x0f\x22\xc0'
    code+=b'\x66\xea'+struct.pack('<IH',0x1000,0x0008)
    de=len(code); code[jc+1]=(de-(jc+2))&0xFF
    code+=bytes([0xbe,MERR&0xFF,(MERR>>8)&0xFF]); c3=len(code); code+=b'\xe8\x00\x00'
    code+=b'\xfa\xf4'
    ps=len(code)
    code+=b'\xac\x84\xc0\x74\x07\xb4\x0e\xbb\x07\x00\xcd\x10'; jb=len(code); code+=b'\xeb\x00\xc3'
    code[jb+1]=(ps-(jb+2))&0xFF
    for cp in [c1,c2,c3]:
        r=(ps-(cp+3))&0xFFFF; code[cp+1]=r&0xFF; code[cp+2]=(r>>8)&0xFF
    buf[0:len(code)]=code
    buf[ML:ML+28]=b'SavaOS v0.9 - Booting...\r\n\x00\x00'
    buf[MOK:MOK+12]=b'Kernel OK\r\n\x00'
    buf[MERR:MERR+14]=b'DISK ERROR!\r\n\x00'
    GO=0x1D0
    buf[GO:GO+8]=b'\x00'*8
    buf[GO+8:GO+16]=bytes([0xFF,0xFF,0,0,0,0x9A,0xCF,0])
    buf[GO+16:GO+24]=bytes([0xFF,0xFF,0,0,0,0x92,0xCF,0])
    buf[GD:GD+2]=struct.pack('<H',23)
    buf[GD+2:GD+6]=struct.pack('<I',0x7C00+GO)
    buf[DRIVE_OFF]=0x80; buf[510]=0x55; buf[511]=0xAA
    return bytes(buf)

out=sys.argv[1] if len(sys.argv)>1 else 'boot.bin'
b=build()
open(out,'wb').write(b)
print(f"Boot: {len(b)}B sig={hex(b[510])},{hex(b[511])}")
