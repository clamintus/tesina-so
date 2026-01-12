#!/bin/bash
import socket
import time
import sys

USER = "admin"
PASS = "admin"

do_wait_recv = True # imposta su False per un test più aggressivo

s = socket.socket( socket.AF_INET, socket.SOCK_STREAM )
s.connect( (sys.argv[1], int( sys.argv[2] )) )

login_msg = bytearray( [ 0b10010000, len( USER ), len( PASS ) ] ) + USER.encode() + PASS.encode()

print( s.send( login_msg ) )

post = b"\x11\x22\x33\x44" + bytearray( [ 3, 4 + len( sys.argv[3] ), 0, 10 ] ) + b"\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF" + b"abc" + b"STRE" + str( sys.argv[3] ).encode() + b"STRESSTEST"
post_msg = bytearray( [ 0b10010011, len( USER ), len( PASS ) ] ) + USER.encode() + PASS.encode() + post

for i in range( 1000 ):
    sc = s.send( post_msg )
    if do_wait_recv:
        r = s.recv( 1 )
    else:
        r = b'\x64'
    print( f"{str( i )} {sc} {r[0]:#x}" )
    #time.sleep(0.005)
if not do_wait_recv:
    time.sleep(10) # altrimenti la connessione cadrebbe quando il server sta ancora processando i primi post -> SIGPIPE al prossimo OK/NOT_OK -> sessione chiusa e post seguenti persi
