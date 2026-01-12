#!/bin/python
import socket
import os
import sys

s = []
n = int( sys.argv[3] )
for i in range( n ):
    s.append( socket.socket( socket.AF_INET, socket.SOCK_STREAM ) )

for i in range( len( s ) ):
    s[ i ].connect( ( sys.argv[1], int( sys.argv[2] ) ) )
    print( f"{i} {s[ i ].recv(1)[0]:#x}" )

input( f"{n} connessioni effettuate" )

for i in range( n-5, n ):
    s[ i ].close()
input( "Ultime socket disconnesse" )

for i in range( 5 ):
    s[ i ].close()
input( "Prime socket disconnesse" )

for i in range( 5 ):
    s[ i ] = socket.socket( socket.AF_INET, socket.SOCK_STREAM )
    s[ i ].connect( ( sys.argv[1], int( sys.argv[2] ) ) )
    print( f"{i} {s[ i ].recv(1)[0]:#x}" )
for i in range( n-5, n ):
    s[ i ] = socket.socket( socket.AF_INET, socket.SOCK_STREAM )
    s[ i ].connect( ( sys.argv[1], int( sys.argv[2] ) ) )
    print( f"{i} {s[ i ].recv(1)[0]:#x}" )
input()
