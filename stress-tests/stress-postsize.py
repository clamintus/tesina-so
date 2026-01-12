#!/bin/python
import socket
import datetime
import time
import sys

USER = ( "utente256" + "6"*255 )[ :255 ]    # utente256666666...
PASS = USER

def interpret( code: bytes ):
    if code[ 0 ] == 0x64:
        return "OK"
    elif code[ 0 ] == 0x65:
        return f"Errore: { code[ 1 ]:02X}"
    else:
        return f"Boh: { code[ 0 ]:08b}"

s = socket.socket( socket.AF_INET, socket.SOCK_STREAM )
s.connect( (sys.argv[1], int( sys.argv[2] )) )

login_msg = bytearray( [ 0b10010000, len( USER ), len( PASS ) ] ) + USER.encode() + PASS.encode()

print( s.send( login_msg ) )

count = 14 # assumiamo che il titolo della board sia vuoto (altrimenti dovremmo aggiungere la sua lunghezza)
while count > 0:
    count -= len( s.recv( count ) )

oggetto = "Post da 64KB"
testo = "0" * 59999 + "1"

post = b"\x11\x22\x33\x44" + bytearray( [ len( USER ), len( oggetto ) ] ) + len( testo ).to_bytes( 2 ) + b"\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF" + USER.encode() + oggetto.encode() + testo.encode()
post_msg = bytearray( [ 0b10010011, len( USER ), len( PASS ) ] ) + USER.encode() + PASS.encode() + post

output = ""
start = datetime.datetime.now()
for i in range( 50 ):
    output += f"{i} "
    output += f"{s.send( post_msg )} "
    output += f"{interpret( s.recv( 2 ) )}\n"
end = datetime.datetime.now()

print( output )
print( end - start )
