#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>

#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/tcp.h>

#include <net/if.h>
#include <sys/ioctl.h>

#include <linux/can.h>
#include <linux/can/raw.h>

#include <sys/time.h>

#define SERVER_IP "3.141.38.115"
#define SERVER_PORT 6100

#define FIFO_SIZE 16384

#define MAX_BATCH_FRAMES 256
#define FLUSH_INTERVAL_US 20000

typedef struct
{
    uint32_t can_id;
    uint8_t dlc;
    uint8_t data[8];
} __attribute__((packed)) can_packet_t;

typedef struct
{
    can_packet_t buf[FIFO_SIZE];

    uint32_t head;
    uint32_t tail;

    pthread_mutex_t lock;

} fifo_t;

static fifo_t fifo =
{
    .head = 0,
    .tail = 0,
    .lock = PTHREAD_MUTEX_INITIALIZER
};

/* ============================================================
   STATS
   ============================================================ */

static volatile uint64_t stat_can_rx = 0;
static volatile uint64_t stat_can_sent = 0;
static volatile uint64_t stat_tcp_sends = 0;
static volatile uint64_t stat_fifo_overwrites = 0;
static volatile uint64_t stat_reconnects = 0;

static volatile uint64_t stat_batch_sum = 0;
static volatile uint64_t stat_batch_count = 0;
static volatile uint64_t stat_batch_max = 0;

static volatile uint32_t stat_fifo_highwater = 0;

/* ============================================================
   TIME
   ============================================================ */

static uint64_t now_us(void)
{
    struct timeval tv;

    gettimeofday(&tv, NULL);

    return
        ((uint64_t)tv.tv_sec * 1000000ULL)
        +
        tv.tv_usec;
}

/* ============================================================
   FIFO
   ============================================================ */

static inline uint32_t fifo_depth_nolock(void)
{
    if (fifo.head >= fifo.tail)
        return fifo.head - fifo.tail;

    return FIFO_SIZE - fifo.tail + fifo.head;
}

static uint32_t fifo_depth(void)
{
    uint32_t d;

    pthread_mutex_lock(&fifo.lock);
    d = fifo_depth_nolock();
    pthread_mutex_unlock(&fifo.lock);

    return d;
}

static void fifo_push(const can_packet_t *pkt)
{
    pthread_mutex_lock(&fifo.lock);

    uint32_t next = (fifo.head + 1) % FIFO_SIZE;

    if (next == fifo.tail)
    {
        /* overwrite oldest */

        fifo.tail = (fifo.tail + 1) % FIFO_SIZE;

        stat_fifo_overwrites++;
    }

    fifo.buf[fifo.head] = *pkt;
    fifo.head = next;

    uint32_t depth = fifo_depth_nolock();

    if (depth > stat_fifo_highwater)
        stat_fifo_highwater = depth;

    pthread_mutex_unlock(&fifo.lock);
}

static int fifo_pop_many(
    can_packet_t *dst,
    int max_count)
{
    pthread_mutex_lock(&fifo.lock);

    int count = 0;

    while (count < max_count &&
           fifo.tail != fifo.head)
    {
        dst[count++] = fifo.buf[fifo.tail];

        fifo.tail =
            (fifo.tail + 1)
            % FIFO_SIZE;
    }

    pthread_mutex_unlock(&fifo.lock);

    return count;
}

/* ============================================================
   TCP
   ============================================================ */

static int connect_tcp(void)
{
    int sock;

    struct sockaddr_in server;

    while (1)
    {
        sock =
            socket(
                AF_INET,
                SOCK_STREAM,
                0);

        if (sock < 0)
        {
            perror("socket");
            sleep(1);
            continue;
        }

        int flag = 1;

        setsockopt(
            sock,
            IPPROTO_TCP,
            TCP_NODELAY,
            &flag,
            sizeof(flag));

        memset(&server, 0, sizeof(server));

        server.sin_family = AF_INET;
        server.sin_port = htons(SERVER_PORT);

        inet_pton(
            AF_INET,
            SERVER_IP,
            &server.sin_addr);

        if (connect(
                sock,
                (struct sockaddr *)&server,
                sizeof(server)) == 0)
        {
            printf("TCP connected\n");
            return sock;
        }

        close(sock);

        stat_reconnects++;

        sleep(1);
    }
}

static int send_all(
    int sock,
    const void *buf,
    size_t len)
{
    const uint8_t *ptr = buf;

    size_t sent = 0;

    while (sent < len)
    {
        ssize_t ret =
            send(
                sock,
                ptr + sent,
                len - sent,
                0);

        if (ret <= 0)
            return -1;

        sent += ret;
    }

    return 0;
}

/* ============================================================
   CAN THREAD
   ============================================================ */

static void *can_thread(void *arg)
{
    int s =
        socket(
            PF_CAN,
            SOCK_RAW,
            CAN_RAW);

    struct ifreq ifr;
    struct sockaddr_can addr;

    memset(&ifr, 0, sizeof(ifr));

    strcpy(ifr.ifr_name, "can0");

    ioctl(
        s,
        SIOCGIFINDEX,
        &ifr);

    addr.can_family = AF_CAN;
    addr.can_ifindex = ifr.ifr_ifindex;

    bind(
        s,
        (struct sockaddr *)&addr,
        sizeof(addr));

    struct can_frame frame;

    while (1)
    {
        if (read(
                s,
                &frame,
                sizeof(frame))
            > 0)
        {
            can_packet_t pkt;

            pkt.can_id = frame.can_id;
            pkt.dlc = frame.can_dlc;

            memcpy(
                pkt.data,
                frame.data,
                8);

            fifo_push(&pkt);

            stat_can_rx++;
        }
    }

    return NULL;
}

/* ============================================================
   DEBUG THREAD
   ============================================================ */

static void *debug_thread(void *arg)
{
    while (1)
    {
        sleep(1);

        uint64_t rx = stat_can_rx;
        uint64_t sent = stat_can_sent;
        uint64_t sends = stat_tcp_sends;
        uint64_t ovw = stat_fifo_overwrites;

        stat_can_rx = 0;
        stat_can_sent = 0;
        stat_tcp_sends = 0;
        stat_fifo_overwrites = 0;

        uint64_t avg_batch = 0;

        if (stat_batch_count)
        {
            avg_batch =
                stat_batch_sum /
                stat_batch_count;
        }

        printf("\n");
        printf("====================================================\n");
        printf("CAN RX:            %llu msgs/s\n",
               (unsigned long long)rx);

        printf("CAN SENT:          %llu msgs/s\n",
               (unsigned long long)sent);

        printf("TCP SEND CALLS:    %llu calls/s\n",
               (unsigned long long)sends);

        printf("FIFO DEPTH:        %u\n",
               fifo_depth());

        printf("FIFO HIGHWATER:    %u\n",
               stat_fifo_highwater);

        printf("FIFO OVERWRITES:   %llu\n",
               (unsigned long long)ovw);

        printf("AVG BATCH SIZE:    %llu\n",
               (unsigned long long)avg_batch);

        printf("MAX BATCH SIZE:    %llu\n",
               (unsigned long long)stat_batch_max);

        printf("RECONNECTS:        %llu\n",
               (unsigned long long)stat_reconnects);

        printf("====================================================\n");
        fflush(stdout);
    }

    return NULL;
}

/* ============================================================
   MAIN
   ============================================================ */

int main(void)
{
    printf(
        "sizeof(can_packet_t) = %zu\n",
        sizeof(can_packet_t));

    pthread_t can_tid;
    pthread_t dbg_tid;

    pthread_create(
        &can_tid,
        NULL,
        can_thread,
        NULL);

    pthread_create(
        &dbg_tid,
        NULL,
        debug_thread,
        NULL);

    int sock =
        connect_tcp();

    can_packet_t batch[MAX_BATCH_FRAMES];

    uint64_t last_flush =
        now_us();

    while (1)
    {
        uint32_t depth =
            fifo_depth();

        uint64_t now =
            now_us();

        uint64_t dt =
            now - last_flush;

        int should_send = 0;

        if (depth >= MAX_BATCH_FRAMES)
            should_send = 1;

        if (depth > 0 &&
            dt >= FLUSH_INTERVAL_US)
            should_send = 1;

        if (!should_send)
        {
            usleep(100);
            continue;
        }

        int count =
            fifo_pop_many(
                batch,
                MAX_BATCH_FRAMES);

        if (count <= 0)
            continue;

        if (count > (int)stat_batch_max)
            stat_batch_max = count;

        stat_batch_sum += count;
        stat_batch_count++;

        int bytes =
            count *
            sizeof(can_packet_t);

        if (send_all(
                sock,
                batch,
                bytes) < 0)
        {
            close(sock);

            sock =
                connect_tcp();

            continue;
        }

        stat_can_sent += count;
        stat_tcp_sends++;

        last_flush = now;
    }

    return 0;
}
