#include "canp.h"

#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#include <winsock2.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <unistd.h>
#endif

static uint64_t canpHton64(uint64_t value) {
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
  uint32_t high = htonl((uint32_t)(value >> 32));
  uint32_t low = htonl((uint32_t)value);
  return ((uint64_t)low << 32) | high;
#else
  return value;
#endif
}

static uint64_t canpNtoh64(uint64_t value) {
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
  uint32_t high = ntohl((uint32_t)value);
  uint32_t low = ntohl((uint32_t)(value >> 32));
  return ((uint64_t)high << 32) | low;
#else
  return value;
#endif
}

static int canpSendBytes(canpSocket_t fd, const void* data, size_t size) {
  const char* bytes = (const char*)data;
  while (size > 0) {
#ifdef _WIN32
    int sent = send(fd, bytes, (int)size, 0);
    if (sent < 0 && WSAGetLastError() == WSAEINTR) continue;
#else
    int flags = 0;
#ifdef MSG_NOSIGNAL
    flags = MSG_NOSIGNAL;
#endif
    ssize_t sent = send(fd, bytes, size, flags);
    if (sent < 0 && errno == EINTR) continue;
#endif
    if (sent <= 0) return -1;
    bytes += sent;
    size -= (size_t)sent;
  }
  return 1;
}

canpPacket_t canpMakePacket(uint32_t canId, uint8_t dlc, const uint8_t data[8],
                            const uint16_t δt[8]) {
  canpPacket_t p;
  p.can_id = htonl(canId);
  p.dlc = dlc;
  for (int i = 0; i < 8; i++) p.data[i] = data[i];
  for (int i = 0; i < 8; i++) p.δt[i] = htons(δt[i]);
  return p;
};

uint32_t canpGetId(const canpPacket_t* p) { return ntohl(p->can_id); }

int canpWrite(canpSocket_t fd, struct iovec* iov, int iovcnt) {
  while (iovcnt > 0) {
#ifdef _WIN32
    int n = send(fd, (const char*)iov[0].iov_base, (int)iov[0].iov_len, 0);
#else
    ssize_t n = writev(fd, iov, iovcnt);
#endif
    if (n < 0) {
#ifdef _WIN32
      if (WSAGetLastError() == WSAEINTR) continue;
#else
      if (errno == EINTR) continue;
#endif
      return -1;
    }
    if (n == 0) return -1;
    while (iovcnt > 0 && (size_t)n >= iov[0].iov_len) {
      n -= iov[0].iov_len;
      iov++;
      iovcnt--;
    }
    if (iovcnt > 0) {
      iov[0].iov_base = (char*)iov[0].iov_base + n;
      iov[0].iov_len -= n;
    }
  }
  return 1;
}

int canpWriteBatch(canpSocket_t fd, canpBatch_t* batch) {
  if (batch->count == 0 || batch->count > CANP_MAX_BATCH) return -1;
  canpHeader_t hdr = {.magic = htonl(CANP_MAGIC),
                      .version = htons(CANP_VERSION),
                      .count = htons(batch->count),
                      .seq = htonl(batch->seq),
                      .timestamp = canpHton64(batch->timestamp)};
  struct iovec iov[2] = {
      {.iov_base = &hdr, .iov_len = sizeof hdr},
      {.iov_base = (void*)batch->packets, .iov_len = batch->count * sizeof batch->packets[0]}};
  return canpWrite(fd, iov, 2);
}

int canpWriteTimelineSeek(canpSocket_t fd, uint64_t timestamp) {
  const canpTimelineRequest_t request = {.magic = htonl(CANP_TIMELINE_MAGIC),
                                         .version = htons(CANP_TIMELINE_VERSION),
                                         .command = htons(CANP_TIMELINE_SEEK),
                                         .timestamp = canpHton64(timestamp)};
  return canpSendBytes(fd, &request, sizeof request);
}

int canpRead(canpSocket_t fd, void* buf, size_t len) {
  char* p = (char*)buf;
  size_t accum = 0;
  while (accum < len) {
#ifdef _WIN32
    int n = recv(fd, p + accum, (int)(len - accum), 0);
#else
    ssize_t n = read(fd, p + accum, len - accum);
#endif
    if (n == 0) return CANP_READ_CLOSED;
    if (n < 0) {
#ifdef _WIN32
      if (WSAGetLastError() == WSAEINTR) continue;
#else
      if (errno == EINTR) continue;
#endif
      return CANP_READ_SOCKET_ERROR;
    }
    accum += n;
  }
  return CANP_READ_OK;
}

int canpReadBatch(canpSocket_t fd, canpBatch_t* batch) {
  canpHeader_t hdr;
  int r = canpRead(fd, &hdr, sizeof hdr);
  if (r <= 0) return r;
  if (ntohl(hdr.magic) != CANP_MAGIC) return CANP_READ_BAD_MAGIC;
  if (ntohs(hdr.version) != CANP_VERSION) return CANP_READ_BAD_VERSION;
  uint16_t n = ntohs(hdr.count);
  if (n == 0 || n > CANP_MAX_BATCH) return CANP_READ_BAD_COUNT;
  r = canpRead(fd, batch->packets, n * sizeof batch->packets[0]);
  if (r <= 0) return r;
  batch->seq = ntohl(hdr.seq);
  batch->timestamp = canpNtoh64(hdr.timestamp);
  batch->count = n;
  return CANP_READ_OK;
}

int canpRelayBatch(canpSocket_t in_fd, canpSocket_t out_fd) {
  canpBatch_t batch;
  int r = canpReadBatch(in_fd, &batch);
  if (r <= 0) return r;
  return canpWriteBatch(out_fd, &batch);
}

static inline void dataToString(const uint8_t data[8], uint8_t dlc, char* s) {
  static const char hex[] = "0123456789ABCDEF";
  for (uint8_t i = 0; i < dlc; i++) {
    s[i * 2 + 0] = hex[data[i] >> 4];
    s[i * 2 + 1] = hex[data[i] & 0x0F];
  }
  s[dlc * 2] = '\0';
}

void canpPrintBatch(canpBatch_t* batch) {
  printf("[Ts|%" PRIu64 "][Seq|%" PRIu32 "]\n", batch->timestamp, batch->seq);
  for (uint16_t i = 0; i < batch->count; i++) {
    const canpPacket_t* p = &batch->packets[i];
    char s[17];
    dataToString(p->data, p->dlc, s);
    printf("\t[0x%03" PRIX32 "][%u][%s]\n", canpGetId(p), p->dlc, s);
  }
};
