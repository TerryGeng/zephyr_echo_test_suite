import socket
import logging
import time
import asyncio
from collections import deque

num_client = 1
port = 4242

backlog = 0

ctr = 0

peer_ip = '192.168.11.2'
peer_port = 4242

data = b"""Sockets are used nearly everywhere, but are one of the most severely misunderstood technologies around. This is a 10,000 foot overview of sockets. It's not really a tutorial - you'll still have work to do in getting things operational. It doesn't cover the fine points (and there are a lot of them), but I hope it will give you enough background to begin using them decently."""
b""" I'm only going to talk about INET (i.e. IPv4) sockets, but they account for at least 99% of the sockets in use. And I'll only talk about STREAM (i.e. TCP) sockets - unless you really know what you're doing (in which case this HOWTO isn't for you!), you'll get better behavior and performance from a STREAM socket than anything else. I will try to clear up the mystery of what a socket is, as well as some hints on how to work with blocking and non-blocking sockets. But I'll start by talking about blocking sockets. You'll need to know how they work before dealing with non-blocking sockets."""
b"""Part of the trouble with understanding these things is that "socket" can mean a number of subtly different things, depending on context. So first, let's make a distinction between a "client" socket - an endpoint of a conversation, and a "server" socket, which is more like a switchboard operator. The client application (your browser, for example) uses "client" sockets exclusively; the web server it's talking to uses both "server" sockets and "client" sockets.
"""


logging.basicConfig(level=logging.DEBUG,
                    format='%(asctime)s: '
                    '%(message)s')

async def write_to_server(writer):
    writer.write(data)
    await writer.drain()

    return len(data)

async def read_from_server(reader):
    recvbuf = await reader.read(1024)

    return recvbuf

async def run_client(client_id):
    logging.info(f"client {client_id}: Connecting to {peer_ip}:{peer_port}")
    reader, writer = await asyncio.open_connection(peer_ip, peer_port)

    backlog = 0
    last_report_time = time.time()
    recvd_size_queue = deque(maxlen=500)
    timestamp_queue  = deque(maxlen=500)

    while True:
        write_task = asyncio.create_task(write_to_server(writer))
        read_task = asyncio.create_task(read_from_server(reader))

        done, pending = await asyncio.wait([write_task, read_task], return_when=asyncio.FIRST_COMPLETED)

        while True:
            assert len(done) > 0, "done?"
            if write_task in done:
                backlog += write_task.result()
                write_task = asyncio.create_task(write_to_server(writer))

            if read_task in done:
                recvsize = len(read_task.result())
                backlog -= recvsize
                read_task = asyncio.create_task(read_from_server(reader))

                recvd_size_queue.append(recvsize)
                timestamp_queue.append(time.time())

                if time.time() - last_report_time > 2:
                    last_report_time = time.time()
                    speed = sum(recvd_size_queue) / (
                            timestamp_queue[-1] - timestamp_queue[0])

                    logging.info(f"client {client_id}: Data receiving rate: {speed/1000:.1f} kB/s")

            if backlog <= 2*4096:
                done, pending = await asyncio.wait([write_task, read_task], return_when=asyncio.FIRST_COMPLETED)
            else:
                done, pending = await asyncio.wait([read_task], return_when=asyncio.FIRST_COMPLETED)


async def main():
    tasks = []

    for i in range(num_client):
        logging.info(f"creating client {i}")
        task = asyncio.create_task(run_client(i))
        tasks.append(task)

    while tasks:
        done, pending = await asyncio.wait(tasks, return_when=asyncio.FIRST_COMPLETED)
        for d in done:
            if d.exception():
                logging.error(d.exception())
        tasks = pending


if __name__ == "__main__":
#    asyncio.run(main())

    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        s.connect((peer_ip, peer_port))

        s.settimeout(5)
        s.setblocking(False)

        ctr = 0

        recvd_size_queue = deque(maxlen=500)
        timestamp_queue  = deque(maxlen=500)

        recvd_size_queue.clear()
        timestamp_queue.clear()


        try:
            while True:
                tot_sendsize = 0

                while tot_sendsize < len(data):
                    if backlog <= 2048:
                        try:
                            sendsize = s.send(data[tot_sendsize:])
                            tot_sendsize += sendsize
                        except BlockingIOError:
                            continue

                        backlog += sendsize

                    while True:
                        try:
                            recvbuf = s.recv(1024)

                            backlog -= len(recvbuf)
                            recvd_size_queue.append(len(recvbuf))
                            timestamp_queue.append(time.time())

                            if len(recvbuf) == 0:
                                break

                            ctr += 1

                            if ctr and ctr % 500 == 0:
                                speed = sum(recvd_size_queue) / (
                                        timestamp_queue[-1] - timestamp_queue[0])

                                logging.info(f"Data exchange rate: {speed/1000:.1f} kB/s")

                        except BlockingIOError:
                            break

        except TimeoutError:
            s.close()
            logging.info("Timeout. Close connection.")
        finally:
            s.close()

