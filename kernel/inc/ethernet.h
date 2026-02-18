// * Specification: https://pdos.csail.mit.edu/6.828/2025/readings/8254x_GBe_SDM.pdf

#ifndef ETHERNET_H
#define ETHERNET_H

#include <stdint.h>
#include <stddef.h>

// * PCI * //

#define MK_ETH_PCI_CONFIG_ADDR 0x0CF8
#define MK_ETH_PCI_CONFIG_DATA 0x0CFC

#define MK_ETH_PCI_VENDOR_INTEL 0x8086
#define MK_ETH_PCI_DEVICE_E1000 0x100E

#define MK_ETH_PCI_CMD_IO_SPACE (1 << 0)
#define MK_ETH_PCI_CMD_MEM_SPACE (1 << 1)
#define MK_ETH_PCI_CMD_BUS_MASTER (1 << 2)

// * E1000 Registers * //
// Offsets are relative to the BAR0 MMIO base.

#define MK_ETH_REG_CTRL 0x0000
#define MK_ETH_REG_STATUS 0x0008
#define MK_ETH_REG_EERD 0x0014
#define MK_ETH_REG_ICR 0x00C0
#define MK_ETH_REG_IMS 0x00D0
#define MK_ETH_REG_IMC 0x00D8
#define MK_ETH_REG_RCTL 0x0100
#define MK_ETH_REG_TCTL 0x0400
#define MK_ETH_REG_TIPG 0x0410

#define MK_ETH_REG_RDBAL 0x2800
#define MK_ETH_REG_RDBAH 0x2804
#define MK_ETH_REG_RDLEN 0x2808
#define MK_ETH_REG_RDH 0x2810
#define MK_ETH_REG_RDT 0x2818

#define MK_ETH_REG_TDBAL 0x3800
#define MK_ETH_REG_TDBAH 0x3804
#define MK_ETH_REG_TDLEN 0x3808
#define MK_ETH_REG_TDH 0x3810
#define MK_ETH_REG_TDT 0x3818

#define MK_ETH_REG_RAL 0x5400
#define MK_ETH_REG_RAH 0x5404
#define MK_ETH_REG_MTA 0x5200

// CTRL bits
#define MK_ETH_CTRL_FD (1 << 0)
#define MK_ETH_CTRL_ASDE (1 << 5)
#define MK_ETH_CTRL_SLU (1 << 6)
#define MK_ETH_CTRL_RST (1 << 26)
#define MK_ETH_CTRL_PHY_RST (1u << 31)

// STATUS bits
#define MK_ETH_STATUS_FD (1 << 0)
#define MK_ETH_STATUS_LU (1 << 1)

// RCTL bits
#define MK_ETH_RCTL_EN (1 << 1)
#define MK_ETH_RCTL_SBP (1 << 2)
#define MK_ETH_RCTL_UPE (1 << 3)
#define MK_ETH_RCTL_MPE (1 << 4)
#define MK_ETH_RCTL_BAM (1 << 15)
#define MK_ETH_RCTL_BSIZE_2048 (0 << 16)
#define MK_ETH_RCTL_BSIZE_1024 (1 << 16)
#define MK_ETH_RCTL_BSIZE_512 (2 << 16)
#define MK_ETH_RCTL_BSIZE_256 (3 << 16)
#define MK_ETH_RCTL_SECRC (1 << 26)

// TCTL bits
#define MK_ETH_TCTL_EN (1 << 1)
#define MK_ETH_TCTL_PSP (1 << 3)
#define MK_ETH_TCTL_CT_SHIFT 4
#define MK_ETH_TCTL_COLD_SHIFT 12

// TIPG values
#define MK_ETH_TIPG_DEFAULT (10 | (8 << 10) | (6 << 20))

// TX Descriptor CMD bits
#define MK_ETH_TDESC_CMD_EOP (1 << 0)
#define MK_ETH_TDESC_CMD_IFCS (1 << 1)
#define MK_ETH_TDESC_CMD_RS (1 << 3)

// TX Descriptor STA bits
#define MK_ETH_TDESC_STA_DD (1 << 0)

// RX Descriptor Status bits
#define MK_ETH_RDESC_STA_DD (1 << 0)
#define MK_ETH_RDESC_STA_EOP (1 << 1)

// EEPROM Read
#define MK_ETH_EERD_START (1 << 0)
#define MK_ETH_EERD_DONE (1 << 4)
#define MK_ETH_EERD_ADDR_SHIFT 8
#define MK_ETH_EERD_DATA_SHIFT 16

// * Ring Configuration * //

#define MK_ETH_TX_DESC_COUNT 32
#define MK_ETH_RX_DESC_COUNT 32
#define MK_ETH_RX_BUF_SIZE 2048
#define MK_ETH_TX_BUF_SIZE 2048

// * Protocol Constants * //

#define MK_ETH_ETHERTYPE_IPV4 0x0800
#define MK_ETH_ETHERTYPE_ARP 0x0806

#define MK_ETH_ARP_HTYPE_ETH 0x0001
#define MK_ETH_ARP_PTYPE_IPV4 0x0800
#define MK_ETH_ARP_HLEN 6
#define MK_ETH_ARP_PLEN 4
#define MK_ETH_ARP_OP_REQUEST 1
#define MK_ETH_ARP_OP_REPLY 2

#define MK_ETH_IP_PROTO_UDP 17
#define MK_ETH_IP_TTL 64

#define MK_ETH_ARP_CACHE_SIZE 16
#define MK_ETH_MAX_SOCKETS 8
#define MK_ETH_MAX_TIMEOUT_MS 1000

// Mira Kernel Ethernet IP
// On little-endian x86, this places octets in wire order.
#define MK_ETH_IP(m, i, r, a) \
    ((uint32_t)(m) | ((uint32_t)(i) << 8) | ((uint32_t)(r) << 16) | ((uint32_t)(a) << 24))

// * Hardware Descriptors * //

typedef struct __attribute__((packed)) {
    volatile uint64_t addr;
    volatile uint16_t length;
    volatile uint8_t cso;
    volatile uint8_t cmd;
    volatile uint8_t sta;
    volatile uint8_t css;
    volatile uint16_t special;
} mk_eth_tx_desc_t;

typedef struct __attribute__((packed)) {
    volatile uint64_t addr;
    volatile uint16_t length;
    volatile uint16_t checksum;
    volatile uint8_t status;
    volatile uint8_t errors;
    volatile uint16_t special;
} mk_eth_rx_desc_t;

// * Protocol Headers * //

typedef struct __attribute__((packed)) {
    uint8_t dst[6];
    uint8_t src[6];
    uint16_t ethertype;
} mk_eth_frame_hdr_t;

typedef struct __attribute__((packed)) {
    uint16_t htype;
    uint16_t ptype;
    uint8_t hlen;
    uint8_t plen;
    uint16_t opcode;
    uint8_t sha[6];
    uint32_t spa;
    uint8_t tha[6];
    uint32_t tpa;
} mk_eth_arp_packet_t;

typedef struct __attribute__((packed)) {
    uint8_t version_ihl;
    uint8_t dscp_ecn;
    uint16_t total_length;
    uint16_t identification;
    uint16_t flags_fragment;
    uint8_t ttl;
    uint8_t protocol;
    uint16_t checksum;
    uint32_t src_ip;
    uint32_t dst_ip;
} mk_eth_ip_hdr_t;

typedef struct __attribute__((packed)) {
    uint16_t src_port;
    uint16_t dst_port;
    uint16_t length;
    uint16_t checksum;
} mk_eth_udp_hdr_t;

// * ARP Cache Entry * //

typedef struct {
    uint32_t ip;
    uint8_t mac[6];
    uint8_t valid;
} mk_eth_arp_entry_t;

// * Socket * //

typedef struct {
    uint16_t src_port;
    uint8_t in_use;
} mk_eth_socket_t;

// * Driver State * //

typedef struct {
    volatile uint8_t *mmio;

    // Network config
    uint8_t mac[6];
    uint32_t ip;
    uint32_t gateway;
    uint32_t subnet;

    // TX ring
    mk_eth_tx_desc_t *tx_descs;
    uint8_t *tx_bufs[MK_ETH_TX_DESC_COUNT];
    uint16_t tx_cur;

    // RX ring
    mk_eth_rx_desc_t *rx_descs;
    uint8_t *rx_bufs[MK_ETH_RX_DESC_COUNT];
    uint16_t rx_cur;

    // ARP
    mk_eth_arp_entry_t arp_cache[MK_ETH_ARP_CACHE_SIZE];

    // Sockets
    mk_eth_socket_t sockets[MK_ETH_MAX_SOCKETS];
    uint16_t next_ephemeral_port;

    uint8_t initialized;
} mk_eth_state_t;

// * Public API * //

// Function to initialize the driver.
int mk_eth_init(void);

// Function to create a new socket.
mk_eth_socket_t *mk_eth_socket(void);

// Function to send a UDP packet over a socket.
int mk_eth_send(mk_eth_socket_t *sock, const char *ip, uint16_t port, const void *data, uint32_t len);

// Function to receive a UDP packet on a socket.
int mk_eth_recv(mk_eth_socket_t *sock, const void **data, uint16_t *len);

#endif
