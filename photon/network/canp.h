#ifndef CAN_PROTO_H
#define CAN_PROTO_H

#include <stddef.h>
#include <stdint.h>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
typedef SOCKET canpSocket_t;
struct iovec {
  void* iov_base;
  size_t iov_len;
};
#else
#include <sys/uio.h>
typedef int canpSocket_t;
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define CANP_MAGIC 0x43414E31u /* "CAN1" */
#define CANP_VERSION 3u
#define CANP_MAX_BATCH 64u

#ifdef _MSC_VER
#define CANP_PACKED_BEGIN __pragma(pack(push, 1))
#define CANP_PACKED_END __pragma(pack(pop))
#define CANP_PACKED
#else
#define CANP_PACKED_BEGIN
#define CANP_PACKED_END
#define CANP_PACKED __attribute__((packed))
#endif

#define CANP_HEADER_SIZE 20u
#define CANP_PACKET_SIZE 29u

typedef enum {
  CANP_READ_OK = 1,
  CANP_READ_CLOSED = 0,
  CANP_READ_SOCKET_ERROR = -1,
  CANP_READ_BAD_MAGIC = -2,
  CANP_READ_BAD_VERSION = -3,
  CANP_READ_BAD_COUNT = -4
} canpReadStatus_t;

/* we assume single threaded            */
/* these functions are not thread safe  */
CANP_PACKED_BEGIN
typedef struct CANP_PACKED {
  uint32_t magic;
  uint16_t version;
  uint16_t count;
  uint32_t seq;
  uint64_t timestamp;
} canpHeader_t;

typedef struct CANP_PACKED {
  uint32_t can_id;
  uint8_t dlc;
  uint8_t data[8];
  uint16_t δt[8];
} canpPacket_t;
CANP_PACKED_END

#ifdef __cplusplus
static_assert(sizeof(canpHeader_t) == CANP_HEADER_SIZE);
static_assert(sizeof(canpPacket_t) == CANP_PACKET_SIZE);
#else
_Static_assert(sizeof(canpHeader_t) == CANP_HEADER_SIZE, "unexpected CANP header size");
_Static_assert(sizeof(canpPacket_t) == CANP_PACKET_SIZE, "unexpected CANP packet size");
#endif

typedef struct {
  uint32_t seq;
  uint64_t timestamp;
  uint16_t count;
  canpPacket_t packets[CANP_MAX_BATCH];
} canpBatch_t;

int canpWrite(canpSocket_t fd, struct iovec* iov, int iovcnt);
int canpRead(canpSocket_t fd, void* buf, size_t n);

int canpWriteBatch(canpSocket_t fd, canpBatch_t* batch);
int canpReadBatch(canpSocket_t fd, canpBatch_t* batch);

int canpRelayBatch(canpSocket_t in_fd, canpSocket_t out_fd);
void canpPrintBatch(canpBatch_t* batch);

uint32_t canpGetId(const canpPacket_t* p);
canpPacket_t canpMakePacket(uint32_t canId, uint8_t dlc, const uint8_t data[8],
                            const uint16_t δt[8]);

#ifdef __cplusplus
}
#endif

#endif
