# Zephyr socket speed benchmarking suite

This repo hosts code used in benchmarking the bandwidth of the network socket
IO of different socket implementations in Zephyr. More prominently, I use it to
verify the correctness of my W5500 offload socket implementation.

In the `main()` inside `src/main.c`, there are a few tests to be enabled:
 - `loopback_tcp_client()` - create one or more TCP socket connecting to
   `192.168.11.1:4242`, transmit data and check the consistency of what the
   server echos back.
 - `loopback_tcp_server()` - create a TCP socket listening at
   `192.168.11.2:4242`, echo back whatever the server sends to it. The maximal
   number of clients to accept is defined by `SOCKET_CNT`.
 - `loopback_tcp_server_async()` - The same, but instead of using a
   thread-based implementation, it uses `poll`. This is to test the correctness
   of `poll` (plus the `echo_async` example in Zephyr is somewhat broken).
 - `loopback_udp_client()` - create one of more UDP socket connecting to
   `192.168.11.1:4242`, transmit data and check the consistency of what the
   server echos back.
 - `loopback_udp_server()` - create a UDP socket listening at
   `192.168.11.2:4242`, echo back whatever the server sends to it.


I assume the person testing the code will be connecting their microcontroller
to a PC that has a `python3` installation. I prepared 4 python scripts as the
peer to the microcontroller. They are
 - `scripts/server.py` - create a TCP server listening at `192.168.11.1:4242`,
   echo back everything the client sends to it.
 - `scripts/server_udp.py` - the same but with UDP socket.
 - `scripts/client.py` - create a TCP client socket connecting to
   `192.168.11.2:4242`, send data to it, and receive whatever the server echos
   back
 - `scripts/client_udp.py` - the same but with UDP socket.

All of these above four scripts will report the speed. Note that speed here is
the full-duplex data rate.

## To compile

I'm using a Raspberry Pi Pico, so I have the `rpi_pico.conf` and
`rpi_pico.overlay` configured in the way [my W5500 is
connected](https://github.com/TerryGeng/PoePico).
Please change it to the way your board is arranged.

In `prj.conf`, I set the IP address to suit my network. I disabled IPv6 because
sometimes the network management does weird things.
For reasons I don't quite understand, enabling `NET_SHELL` sometimes drives the
microcontroller into hard fault (not for this test program but for some Zephyr
examples) so I prefer to keep it off.

Then, execute
```
west build -p -b rpi_pico ./echo_test_suite
west flash
```
and you are good to go.

## My benchmark result

Please note that the exact speed also depends on my benchmarking code
implementation and specific switches (like using DMA or not). My benchmarking
code might not be the optimal implementation.

### `loopback_tcp_client`

Note that number of clients can be changed by `SOCKET_CNT`

- Zephyr socket
```
❯ python echo_client/server.py
2024-12-26 14:43:52,518: Listening at *:4242
2024-12-26 14:43:52,908: Socket 4: connection from ('192.168.11.2', 41273)
2024-12-26 14:43:57,926: Socket 4: data exchange rate: 94.5 kB/s
2024-12-26 14:44:02,938: Socket 4: data exchange rate: 95.6 kB/s
2024-12-26 14:44:07,940: Socket 4: data exchange rate: 92.0 kB/s
2024-12-26 14:44:12,945: Socket 4: data exchange rate: 93.9 kB/s
2024-12-26 14:44:17,952: Socket 4: data exchange rate: 92.8 kB/s
```

- Offload socket
```
❯ python echo_client/server.py
2024-12-26 14:32:43,780: Listening at *:4242
2024-12-26 14:32:49,840: Socket 4: connection from ('192.168.11.2', 50001)
2024-12-26 14:32:54,842: Socket 4: data exchange rate: 316.1 kB/s
2024-12-26 14:32:59,842: Socket 4: data exchange rate: 315.5 kB/s
2024-12-26 14:33:04,846: Socket 4: data exchange rate: 314.4 kB/s
2024-12-26 14:33:09,847: Socket 4: data exchange rate: 311.9 kB/s
2024-12-26 14:33:14,852: Socket 4: data exchange rate: 314.5 kB/s
```

### `loopback_tcp_server`

- Zephyr socket
```
❯ python echo_server/client.py
2024-12-26 14:42:58,131: Data exchange rate: 87.4 kB/s
2024-12-26 14:43:01,148: Data exchange rate: 87.5 kB/s
2024-12-26 14:43:04,172: Data exchange rate: 86.9 kB/s
2024-12-26 14:43:07,179: Data exchange rate: 87.4 kB/s
2024-12-26 14:43:10,206: Data exchange rate: 87.0 kB/s
```

- Offload socket
```
❯ python client.py
2024-12-26 14:29:42,271: Data exchange rate: 431.1 kB/s
2024-12-26 14:29:43,133: Data exchange rate: 432.1 kB/s
2024-12-26 14:29:44,014: Data exchange rate: 425.6 kB/s
2024-12-26 14:29:44,887: Data exchange rate: 427.6 kB/s
2024-12-26 14:29:45,735: Data exchange rate: 439.7 kB/s
```

### `loopback_udp_client`

- Zephyr socket
```
❯ python echo_client/server_udp.py
2024-12-26 14:40:37,800: Listening at *:4242
2024-12-26 14:40:37,804: Client ('192.168.11.2', 35780) incoming
2024-12-26 14:40:42,808: Client ('192.168.11.2', 35780) data exchange rate: 162.8 kB/s
2024-12-26 14:40:47,812: Client ('192.168.11.2', 35780) data exchange rate: 164.1 kB/s
2024-12-26 14:40:52,813: Client ('192.168.11.2', 35780) data exchange rate: 159.4 kB/s
2024-12-26 14:40:57,817: Client ('192.168.11.2', 35780) data exchange rate: 159.5 kB/s
2024-12-26 14:41:02,823: Client ('192.168.11.2', 35780) data exchange rate: 158.9 kB/s
```

- Offload socket
```
❯ python echo_client/server_udp.py
2024-12-26 14:35:36,308: Listening at *:4242
2024-12-26 14:35:36,310: Client ('192.168.11.2', 50001) incoming
2024-12-26 14:35:41,314: Client ('192.168.11.2', 50001) data exchange rate: 444.7 kB/s
2024-12-26 14:35:46,314: Client ('192.168.11.2', 50001) data exchange rate: 447.7 kB/s
2024-12-26 14:35:51,315: Client ('192.168.11.2', 50001) data exchange rate: 437.0 kB/s
2024-12-26 14:35:56,316: Client ('192.168.11.2', 50001) data exchange rate: 438.6 kB/s
2024-12-26 14:36:01,317: Client ('192.168.11.2', 50001) data exchange rate: 449.6 kB/s
```

### `loopback_udp_server`

- Zephyr socket
```
❯ python echo_server/client_udp.py
2024-12-26 14:39:04,214: Connecting to 192.168.11.2:4242
2024-12-26 14:39:06,451: Data exchange rate: 84.5 kB/s
2024-12-26 14:39:08,676: Data exchange rate: 84.4 kB/s
2024-12-26 14:39:10,897: Data exchange rate: 84.6 kB/s
2024-12-26 14:39:13,124: Data exchange rate: 84.3 kB/s
2024-12-26 14:39:15,351: Data exchange rate: 84.3 kB/s
```

- Offload socket
```
❯ python echo_server/client_udp.py
2024-12-26 14:36:50,985: Connecting to 192.168.11.2:4242
2024-12-26 14:36:51,876: Data exchange rate: 211.1 kB/s
2024-12-26 14:36:52,777: Data exchange rate: 208.7 kB/s
2024-12-26 14:36:53,685: Data exchange rate: 206.8 kB/s
2024-12-26 14:36:54,595: Data exchange rate: 206.3 kB/s
2024-12-26 14:36:55,509: Data exchange rate: 205.6 kB/s
2024-12-26 14:36:56,415: Data exchange rate: 207.6 kB/s
```


