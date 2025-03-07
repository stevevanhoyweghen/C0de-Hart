#!/usr/bin/env python3
"""
Used with Python 3.12 on Linux Mint
Test program to validate the OSC communication with C0de-hart-Meta-Human-breathing.ino
"""
import readchar
import time
import os
from threading import Thread
from pythonosc import udp_client
from pythonosc.dispatcher import Dispatcher
from pythonosc import osc_server

# Function to handle received OSC messages
def message_handler(address, *args):
    print(f"rcv {address}, {args}")

# Function to run the OSC server to catch incomming messages
def run_server():
    dispatcher = Dispatcher()
    dispatcher.map("/keep_alive", message_handler)
    dispatcher.map("/button", message_handler)

    server = osc_server.ThreadingOSCUDPServer((ip, port), dispatcher)
    print("Serving on {}".format(server.server_address))

    server.serve_forever()

# Function to periodically send OSC messages
def send_messages():
    client = udp_client.SimpleUDPClient(remoteIp, remotePort)
    i = 0
    while True:
        i = i + 1 % 100000
        client.send_message("/keep_alive", i)
        print(f"xmt /keep_alive,{i}")
        time.sleep(15)  # Wait for . second before sending the next set of messages

# Read key and send OSC messages
def read_user_input():
    client = udp_client.SimpleUDPClient(remoteIp, remotePort)
    while True:
        command = readchar.readkey()
        if command == 'x':
            os._exit(0);
        #elif '0' <= command <= '9':
        else:
            address = "/command"
            #client.send_message(address, 888, 123.456, "The quick brown fox jumps over the lazy dog")
            client.send_message(address, command)
            #print(f"Sent OSC message to {address} with message {command}")
            print(f"xmt {address}, {command}")

if __name__ == "__main__":

    #ip = "192.168.0.201"
    ip = "192.168.188.30"
    port = 4401

    #remoteIp = "192.168.0.97"
    remoteIp = "192.168.188.201"
    remotePort = 4400

    # Create UDP client
    client = udp_client.SimpleUDPClient(ip, port)

    # Create and start the server thread
    server_thread = Thread(target=run_server)
    server_thread.daemon = True
    server_thread.start()

    # Create and start the client thread
    client_thread = Thread(target=send_messages)
    client_thread.daemon = True
    client_thread.start()

    # Create and start the user input thread
#    input_thread = Thread(target=read_user_input, args=(client,))
    input_thread = Thread(target=read_user_input)
    input_thread.daemon = True
    input_thread.start()

    # Keep the main thread alive to allow the server and input threads to run
    while True:
        time.sleep(1)
