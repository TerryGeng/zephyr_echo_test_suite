#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(net_echo_client_sample, LOG_LEVEL_DBG);

#include <zephyr/kernel.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <zephyr/device.h>
#include <fcntl.h>

#include <zephyr/posix/sys/eventfd.h>

#include <zephyr/misc/lorem_ipsum.h>

#include <zephyr/usb/usb_device.h>
#include <zephyr/usb/usbd.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/drivers/gpio.h>

#include <zephyr/net/net_if.h>
#include <zephyr/net/net_mgmt.h>
#include <zephyr/net/net_event.h>
#include <zephyr/net/conn_mgr_monitor.h>
#include <zephyr/net/socket.h>


#define LED0_NODE DT_ALIAS(led0)
static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(LED0_NODE, gpios);

K_SEM_DEFINE(run_app, 0, 1);

#define EVENT_MASK (NET_EVENT_L4_CONNECTED | \
		    NET_EVENT_L4_DISCONNECTED)

#define SOCKET_CNT 2

const char lorem_ipsum[] = LOREM_IPSUM;

const int ipsum_len = sizeof(lorem_ipsum);

struct echo_context {
    int sock;
    ssize_t tot_sendsize;
    ssize_t tot_recvsize;
    uint32_t sentsize_last_report_mib;
};

static struct echo_context echo_ctx[SOCKET_CNT];

uint8_t recvbuf[2048];

static struct net_mgmt_event_callback mgmt_cb;

#define THREAD_PRIORITY 8
#define NUM_OF_CLIENT 7
#define RECVBUF_SIZE 2048

static struct k_thread tcp_client_handler_thread[NUM_OF_CLIENT];

K_THREAD_STACK_ARRAY_DEFINE(tcp_client_handler_stack, NUM_OF_CLIENT, 1024);

static int client_fds[NUM_OF_CLIENT] = { -1, -1, -1, -1, -1, -1, -1 };

static uint8_t client_recvbuf[NUM_OF_CLIENT][RECVBUF_SIZE];

k_timepoint_t led_blink_timepoint;

static void loopback_tcp_client() {
    int ret = 0;
    struct sockaddr_in addr4;
    ssize_t sentsize = 0;
    ssize_t recvsize = 0;
    ssize_t sendbuf_offset;
    struct echo_context *ctx;

    uint8_t sockidx;

    for (sockidx = 0; sockidx < SOCKET_CNT; ++sockidx) {
        ctx = &echo_ctx[sockidx];

        ctx->tot_sendsize = 0;
        ctx->tot_recvsize = 0;
        ctx->sentsize_last_report_mib = 0;

        ctx->sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

        if (ctx->sock < 0) {
            LOG_ERR("Socket %d initialization failed: %d (%s)", sockidx, errno, strerror(errno));

            goto end;
        }

        addr4.sin_family = AF_INET;
        addr4.sin_port = htons(4242);
        inet_pton(AF_INET, CONFIG_NET_CONFIG_PEER_IPV4_ADDR, &addr4.sin_addr);

        ret = connect(ctx->sock, (const struct sockaddr *)&addr4, sizeof(struct sockaddr_in));

        if (ret < 0) {
            LOG_ERR("Socket %d connect failed: %d (%s)", sockidx, errno, strerror(errno));

            goto end;
        }
    }

    while (1) {
        k_usleep(1);
        for (sockidx = 0; sockidx < SOCKET_CNT; ++sockidx) {
            ctx = &echo_ctx[sockidx];

            while (sentsize < ipsum_len) {
                ret = send(ctx->sock, lorem_ipsum+sentsize, ipsum_len-sentsize, 0);

                if (ret < 0) {
                    if (errno == EAGAIN) {
                        continue;
                    }
                    LOG_ERR("Socket %d send error: %d (%s)", sockidx, errno, strerror(errno));
                    goto end;
                }

                sentsize += ret;

                while (1)
                {
                    memset(recvbuf, 0, sizeof(recvbuf));
                    ret = recv(ctx->sock, recvbuf, sizeof(recvbuf), MSG_DONTWAIT);

                    if (ret <= 0) break;

                    recvsize = ret;

                    for (uint16_t idx=0; idx<recvsize; idx++) {
                        sendbuf_offset = ctx->tot_recvsize + idx;
                        while (sendbuf_offset >= ipsum_len)
                            sendbuf_offset -= ipsum_len;

                        if (*(lorem_ipsum+sendbuf_offset) != recvbuf[idx]) {
                            LOG_ERR("Socket %d received data doesn't match sent data at position %d, expect (%d) %c, got %c\n",
                                    sockidx, idx, sendbuf_offset, *(lorem_ipsum+sendbuf_offset), recvbuf[idx]);
                            LOG_ERR("Actual buffer offset %d", ctx->tot_recvsize+idx);
                            LOG_ERR("Buffer expected: %.256s\n", lorem_ipsum+sendbuf_offset);
                            LOG_ERR("Buffer received: %.256s\n", recvbuf);

                            goto end;
                        }
                    }

                    ctx->tot_recvsize += recvsize;
                    ctx->tot_recvsize = ctx->tot_recvsize % ipsum_len;
                }
            }

            ctx->tot_sendsize += sentsize;
            sentsize = 0;

            uint32_t sendsize_in_mib = ctx->tot_sendsize / (1024*1024);
            if (sendsize_in_mib > ctx->sentsize_last_report_mib) {

                LOG_INF("Socket %d :Exchanged %d MiB", sockidx, sendsize_in_mib);
                ctx->sentsize_last_report_mib = sendsize_in_mib;
            }
            k_usleep(1);
        }
    }

end:
    for (sockidx = 0; sockidx < SOCKET_CNT; ++sockidx) {
        if (echo_ctx[sockidx].sock > 0) {
            if(close(echo_ctx[sockidx].sock) == 0) {
                LOG_INF("Socket %d: closed.", sockidx);
                echo_ctx[sockidx].sock = 0;
            } else {
                LOG_ERR("Socket %d: close failed: %d (%s).", sockidx, errno, strerror(errno));
            }
        }
    }
}


static void loopback_udp_client() {
    int ret = 0;
    struct sockaddr_in addr4;
    ssize_t sentsize = 0;
    ssize_t recvsize = 0;
    ssize_t sendbuf_offset;
    struct echo_context *ctx;

    uint8_t sockidx;

    for (sockidx = 0; sockidx < SOCKET_CNT; ++sockidx) {
        ctx = &echo_ctx[sockidx];

        ctx->tot_sendsize = 0;
        ctx->tot_recvsize = 0;
        ctx->sentsize_last_report_mib = 0;

        ctx->sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);

        if (ctx->sock < 0) {
            LOG_ERR("Socket %d initialization failed: %d (%s)", sockidx, errno, strerror(errno));

            goto end;
        }

        addr4.sin_family = AF_INET;
        addr4.sin_port = htons(4242);
        inet_pton(AF_INET, CONFIG_NET_CONFIG_PEER_IPV4_ADDR, &addr4.sin_addr);

        ret = connect(ctx->sock, (const struct sockaddr *)&addr4, sizeof(struct sockaddr_in));

        if (ret < 0) {
            LOG_ERR("Socket %d connect failed: %d (%s)", sockidx, errno, strerror(errno));

            goto end;
        }
    }

    while (1) {
		if (sys_timepoint_expired(led_blink_timepoint)) {
            gpio_pin_toggle_dt(&led);
            led_blink_timepoint = sys_timepoint_calc(K_MSEC(100));
        }

        for (sockidx = 0; sockidx < SOCKET_CNT; ++sockidx) {
            ctx = &echo_ctx[sockidx];

            while (sentsize < ipsum_len) {
                ret = send(ctx->sock, lorem_ipsum+sentsize, ipsum_len-sentsize, 0);

                if (ret < 0) {
                    LOG_ERR("Socket %d send error: %d (%s)", sockidx, errno, strerror(errno));
                    goto end;
                }

                sentsize += ret;

                while (1)
                {
                    memset(recvbuf, 0, sizeof(recvbuf));
                    ret = recv(ctx->sock, recvbuf, sizeof(recvbuf), MSG_DONTWAIT);

                    if (ret <= 0) break;

                    recvsize = ret;

                    for (uint16_t idx=0; idx<recvsize; idx++) {
                        sendbuf_offset = ctx->tot_recvsize + idx;
                        while (sendbuf_offset >= ipsum_len)
                            sendbuf_offset -= ipsum_len;

                        if (*(lorem_ipsum+sendbuf_offset) != recvbuf[idx]) {
                            LOG_ERR("Socket %d received data doesn't match sent data at position %d, expect (%d) %c, got %c\n",
                                    sockidx, idx, sendbuf_offset, *(lorem_ipsum+sendbuf_offset), recvbuf[idx]);
                            LOG_ERR("Actual buffer offset %d", ctx->tot_recvsize+idx);
                            LOG_ERR("Buffer expected: %.256s\n", lorem_ipsum+sendbuf_offset);
                            LOG_ERR("Buffer received: %.256s\n", recvbuf);

                            goto end;
                        }
                    }

                    ctx->tot_recvsize += recvsize;
                    ctx->tot_recvsize = ctx->tot_recvsize % ipsum_len;
                }
            }

            ctx->tot_sendsize += sentsize;
            sentsize = 0;

            uint32_t sendsize_in_mib = ctx->tot_sendsize / (1024*1024);
            if (sendsize_in_mib > ctx->sentsize_last_report_mib) {

                LOG_INF("Socket %d: Exchanged %d MiB", sockidx, sendsize_in_mib);
                ctx->sentsize_last_report_mib = sendsize_in_mib;
            }
            k_usleep(1);
        }
    }

end:
    for (sockidx = 0; sockidx < SOCKET_CNT; ++sockidx) {
        if (echo_ctx[sockidx].sock > 0) {
            if(close(echo_ctx[sockidx].sock) == 0) {
                LOG_INF("Socket %d: closed.", sockidx);
                echo_ctx[sockidx].sock = 0;
            } else {
                LOG_ERR("Socket %d: close failed: %d (%s).", sockidx, errno, strerror(errno));
            }
        }
    }
}



static void handle_tcp_client(void *ptr1, void *ptr2, void *ptr3)
{
    int ind = POINTER_TO_INT(ptr1);
    int client = client_fds[ind];

    uint8_t *recvbuf = &client_recvbuf[ind][0];

    int ret = 0;
    int sentsize = 0;
    int recvsize = 0;

    while (true) {
        recvsize = recv(client, recvbuf, RECVBUF_SIZE, MSG_DONTWAIT);

        if (recvsize < 0) {
            if (errno != EAGAIN) {
                    LOG_ERR("Socket fd %d recv error: %d (%s)", client, errno, strerror(errno));
                    goto end;
            }
            k_usleep(1);
            continue;
        }

        sentsize = 0;

        while (sentsize < recvsize) {
            ret = send(client, recvbuf+sentsize, recvsize-sentsize, 0);

            if (ret < 0) {
                if (errno == EAGAIN) {
                    continue;
                } else {
                    LOG_ERR("Socket fd %d send error: %d (%s)", client, errno, strerror(errno));
                    goto end;
                }
            }

            sentsize += ret;
        }

    }

end:
    close(client);
    client_fds[ind] = -1;
}

static void loopback_tcp_server() {
    int ret = 0;
    int sockidx = 0;

    struct echo_context *ctx = &echo_ctx[0];
	struct sockaddr_in addr4;

	(void)memset(&addr4, 0, sizeof(addr4));
	addr4.sin_family = AF_INET;
	addr4.sin_port = htons(4242);

    ctx->sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

    ret = fcntl(ctx->sock, F_SETFL, O_NONBLOCK);

    if (ret < 0) {
        LOG_ERR("Socket %d fcntl failed: %d (%s)", sockidx, errno, strerror(errno));
        goto end;
    }

    ret = bind(ctx->sock, (struct sockaddr *)&addr4, sizeof(addr4));

    if (ret < 0) {
        LOG_ERR("Socket %d bind failed: %d (%s)", sockidx, errno, strerror(errno));
        goto end;
    }

    ret = listen(ctx->sock, 2);

    if (ret < 0) {
        LOG_ERR("Socket %d listen failed: %d (%s)", sockidx, errno, strerror(errno));
        goto end;
    }

    LOG_INF("Listening to *:%d", 4242);

    int client;
    struct sockaddr_in peeraddr;
    ssize_t addrlen;

    uint8_t slot;


    while (true) {
        k_msleep(100);

		if (sys_timepoint_expired(led_blink_timepoint)) {
            gpio_pin_toggle_dt(&led);
            led_blink_timepoint = sys_timepoint_calc(K_MSEC(100));
        }

        client = accept(ctx->sock, (struct sockaddr *)&peeraddr, &addrlen);

        if (client < 0) {
            if (errno == EAGAIN)
                continue;
            else
                goto end;
        }

        LOG_INF("Incoming connection from: %d.%d.%d.%d:%d",
                ((uint8_t *)&peeraddr.sin_addr.s_addr)[0],
                ((uint8_t *)&peeraddr.sin_addr.s_addr)[1],
                ((uint8_t *)&peeraddr.sin_addr.s_addr)[2],
                ((uint8_t *)&peeraddr.sin_addr.s_addr)[3],
                ntohs(peeraddr.sin_port));

        for (slot = 0; slot < NUM_OF_CLIENT; ++slot) {
            if (client_fds[slot] < 0) {
                break;
            }
        }

        __ASSERT(slot < NUM_OF_CLIENT, "no more slots?");

        client_fds[slot] = client;

        k_thread_create(
                &tcp_client_handler_thread[slot],
                tcp_client_handler_stack[slot],
                K_THREAD_STACK_SIZEOF(tcp_client_handler_stack[slot]),
                handle_tcp_client,
                INT_TO_POINTER(slot), 0, 0,
                THREAD_PRIORITY,
                0,
                K_NO_WAIT);
    }

end:
    if (ctx->sock) {
        close(ctx->sock);
    }
}

static void loopback_tcp_server_async() {
    int ret = 0;
    int sockidx = 0;

    struct echo_context *ctx = &echo_ctx[0];
	struct sockaddr_in addr4;

	(void)memset(&addr4, 0, sizeof(addr4));
	addr4.sin_family = AF_INET;
	addr4.sin_port = htons(4242);

    ctx->sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

    ret = bind(ctx->sock, (struct sockaddr *)&addr4, sizeof(addr4));

    if (ret < 0) {
        LOG_ERR("Socket %d bind failed: %d (%s)", sockidx, errno, strerror(errno));
        goto end;
    }

    ret = listen(ctx->sock, 2);

    if (ret < 0) {
        LOG_ERR("Socket %d listen failed: %d (%s)", sockidx, errno, strerror(errno));
        goto end;
    }

    LOG_INF("Listening to *:%d", 4242);

    int client;
    struct sockaddr_in peeraddr;
    ssize_t addrlen;

    uint8_t slot;

    struct pollfd pollfds[1];

    while (true) {
        k_msleep(100);

		if (sys_timepoint_expired(led_blink_timepoint)) {
            gpio_pin_toggle_dt(&led);
            led_blink_timepoint = sys_timepoint_calc(K_MSEC(100));
        }

        pollfds[0].fd = ctx->sock;
        pollfds[0].events = POLLIN;

        ret = poll(pollfds, 1, 500);

        if (ret) {
            __ASSERT(pollfds[0].revents & POLLIN, "poll worked?");
            LOG_INF("poll returned %d", ret);

            client = accept(ctx->sock, (struct sockaddr *)&peeraddr, &addrlen);

            if (client < 0) {
                if (errno == EAGAIN)
                    continue;
                else
                    goto end;
            }

            LOG_INF("Incoming connection from: %d.%d.%d.%d:%d",
                    ((uint8_t *)&peeraddr.sin_addr.s_addr)[0],
                    ((uint8_t *)&peeraddr.sin_addr.s_addr)[1],
                    ((uint8_t *)&peeraddr.sin_addr.s_addr)[2],
                    ((uint8_t *)&peeraddr.sin_addr.s_addr)[3],
                    ntohs(peeraddr.sin_port));

            for (slot = 0; slot < NUM_OF_CLIENT; ++slot) {
                if (client_fds[slot] < 0) {
                    break;
                }
            }

            __ASSERT(slot < NUM_OF_CLIENT, "no more slots?");

            client_fds[slot] = client;

            k_thread_create(
                    &tcp_client_handler_thread[slot],
                    tcp_client_handler_stack[slot],
                    K_THREAD_STACK_SIZEOF(tcp_client_handler_stack[slot]),
                    handle_tcp_client,
                    INT_TO_POINTER(slot), 0, 0,
                    THREAD_PRIORITY,
                    0,
                    K_NO_WAIT);
        } else {
            LOG_INF("Poll got nothing");
        }
    }

end:
    if (ctx->sock) {
        close(ctx->sock);
    }
}


static void loopback_udp_server() {
    int ret = 0;
    struct echo_context *ctx = &echo_ctx[0];
	struct sockaddr_in addr4;

	(void)memset(&addr4, 0, sizeof(addr4));
	addr4.sin_family = AF_INET;
	addr4.sin_port = htons(4242);

    ctx->sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);

    LOG_INF("Listening to *:%d (UDP)", 4242);

    ret = fcntl(ctx->sock, F_SETFL, O_NONBLOCK);

    if (ret < 0) {
        LOG_ERR("Socket fcntl failed: %d (%s)", errno, strerror(errno));
        goto end;
    }

    ret = bind(ctx->sock, (struct sockaddr *)&addr4, sizeof(addr4));

    uint8_t recvbuf[RECVBUF_SIZE];

    int i = 0;
    int sentsize = 0;
    int recvsize = 0;
    struct sockaddr_in peeraddr;
    socklen_t peeraddr_len;
    struct sockaddr_in connected_peers[NUM_OF_CLIENT];
    int num_connected_clients = 0;

    while (true) {
		if (sys_timepoint_expired(led_blink_timepoint)) {
            gpio_pin_toggle_dt(&led);
            led_blink_timepoint = sys_timepoint_calc(K_MSEC(100));
        }

        recvsize = recvfrom(ctx->sock, recvbuf, RECVBUF_SIZE, MSG_DONTWAIT, (struct sockaddr *)&peeraddr, &peeraddr_len);

        if (recvsize < 0) {
            if (errno != EAGAIN) {
                    LOG_ERR("Socket fd %d recv error: %d (%s)", ctx->sock, errno, strerror(errno));
            }
            k_usleep(1);
            continue;
        }

        for (i = 0; i < num_connected_clients; ++i) {
            if (memcmp(&peeraddr, &connected_peers[i], sizeof(struct sockaddr_in)) == 0) {
                break;
            }
        }

        if (i < NUM_OF_CLIENT) {
            if (i == num_connected_clients) {
                memcpy(&connected_peers[i], &peeraddr, sizeof(struct sockaddr_in));
                num_connected_clients++;

                LOG_INF("Incoming connection from: %d.%d.%d.%d:%d",
                        ((uint8_t *)&peeraddr.sin_addr.s_addr)[0],
                        ((uint8_t *)&peeraddr.sin_addr.s_addr)[1],
                        ((uint8_t *)&peeraddr.sin_addr.s_addr)[2],
                        ((uint8_t *)&peeraddr.sin_addr.s_addr)[3],
                        ntohs(peeraddr.sin_port));

            }
        } else {
            LOG_ERR("Too many peers!");
            continue;
        }

        sentsize = 0;

        while (sentsize < recvsize) {
            ret = sendto(ctx->sock,
                    recvbuf+sentsize,
                    recvsize-sentsize,
                    0,
                    (struct sockaddr *)&peeraddr,
                    sizeof(struct sockaddr_in));

            if (ret < 0) {
                if (errno != EAGAIN) {
                    LOG_ERR("Socket fd %d send error: %d (%s)", ctx->sock, errno, strerror(errno));
                }
                continue;
            }

            sentsize += ret;
        }

    }

end:
    close(ctx->sock);
}


static void init_app(struct net_mgmt_event_callback *cb,
			  uint32_t mgmt_event, struct net_if *iface)
{
    k_sem_give(&run_app);
}


int main(void)
{
    if (!gpio_is_ready_dt(&led)) {
        return 0;
    }

    if (gpio_pin_configure_dt(&led, GPIO_OUTPUT_ACTIVE) < 0) {
        return 0;
    }

    led_blink_timepoint = sys_timepoint_calc(K_MSEC(100));

	if (IS_ENABLED(CONFIG_NET_CONNECTION_MANAGER)) {
		net_mgmt_init_event_callback(&mgmt_cb, init_app, EVENT_MASK);
		net_mgmt_add_event_callback(&mgmt_cb);

		conn_mgr_mon_resend_status();
	}

    k_sem_take(&run_app, K_FOREVER);

    while (true) {
        loopback_tcp_client();
        //loopback_tcp_server();
        //loopback_udp_client();
        //loopback_udp_server();
        //loopback_tcp_server_async();

        k_msleep(1000);

        LOG_INF("Restarting...");
    }

	return 0;
}
