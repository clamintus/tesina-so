#!/bin/bash
import socket
import sys

for i in range( int( sys.argv[3] ) ):
    s = socket.socket( socket.AF_INET, socket.SOCK_STREAM )
    s.connect( ( sys.argv[1], int( sys.argv[2] ) ) )
    s.close()
    print( i )
