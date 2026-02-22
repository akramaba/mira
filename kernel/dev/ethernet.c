#include "../inc/ethernet.h"
#include "../inc/util.h"
#include "../inc/mem.h"

// Singleton driver state. Mira uses Intel E1000 (8254x).
static mk_eth_state_t eth;

// * General Helpers * //

static inline uint16_t mk_eth_htons(uint16_t v) {
    return (v >> 8) | (v << 8);
}

static inline uint32_t mk_eth_htonl(uint32_t v) {
    return ((v >> 24) & 0xFF)
         | ((v >> 8)  & 0xFF00)
         | ((v << 8)  & 0xFF0000)
         | ((v << 24) & 0xFF000000);
}

static inline uint16_t mk_eth_ntohs(uint16_t v) {
    return mk_eth_htons(v);
}

static inline uint32_t mk_eth_ntohl(uint32_t v) {
    return mk_eth_htonl(v);
}

// * MMIO Access Helpers * //

static inline uint32_t mk_eth_reg32(uint32_t off) {
    return *(volatile uint32_t *)(eth.mmio + off);
}

static inline void mk_eth_write32(uint32_t off, uint32_t val) {
    *(volatile uint32_t *)(eth.mmio + off) = val;
}

// * PCI Configuration Helpers * //
// TODO: Have a driver helper header file since
// these are used by other drivers (most helpers).

static uint32_t mk_eth_pci_read32(uint8_t bus, uint8_t dev, uint8_t func, uint8_t reg) {
    uint32_t addr = (1u << 31)
                  | ((uint32_t)bus << 16)
                  | ((uint32_t)dev << 11)
                  | ((uint32_t)func << 8)
                  | (reg & 0xFC);
    mk_util_outl(MK_ETH_PCI_CONFIG_ADDR, addr);
    return mk_util_inl(MK_ETH_PCI_CONFIG_DATA);
}

static void mk_eth_pci_write32(uint8_t bus, uint8_t dev, uint8_t func, uint8_t reg, uint32_t val) {
    uint32_t addr = (1u << 31)
                  | ((uint32_t)bus << 16)
                  | ((uint32_t)dev << 11)
                  | ((uint32_t)func << 8)
                  | (reg & 0xFC);
    mk_util_outl(MK_ETH_PCI_CONFIG_ADDR, addr);
    mk_util_outl(MK_ETH_PCI_CONFIG_DATA, val);
}

static uint16_t mk_eth_pci_read16(uint8_t bus, uint8_t dev, uint8_t func, uint8_t reg) {
    uint32_t dword = mk_eth_pci_read32(bus, dev, func, reg & 0xFC);
    return (uint16_t)(dword >> ((reg & 2) * 8));
}

static void mk_eth_pci_write16(uint8_t bus, uint8_t dev, uint8_t func, uint8_t reg, uint16_t val) {
    uint32_t dword = mk_eth_pci_read32(bus, dev, func, reg & 0xFC);
    int shift = (reg & 2) * 8;

    dword &= ~(0xFFFF << shift);
    dword |= ((uint32_t)val << shift);

    mk_eth_pci_write32(bus, dev, func, reg & 0xFC, dword);
}

// * Scan * //

static uint8_t mk_eth_pci_bus;
static uint8_t mk_eth_pci_dev;
static uint8_t mk_eth_pci_func;

// Mira Kernel Ethernet PCI Find E1000
// Currently called once to find the E1000 device.
static int mk_eth_pci_find_e1000(void) {
    for (uint16_t bus = 0; bus < 256; bus++) {
        for (uint8_t dev = 0; dev < 32; dev++) {
            for (uint8_t func = 0; func < 8; func++) {
                uint32_t id = mk_eth_pci_read32(bus, dev, func, 0x00);

                if (id == 0xFFFFFFFF || id == 0x00000000) [[likely]] {
                    continue;
                }

                uint16_t vendor = id & 0xFFFF;
                uint16_t device = (id >> 16) & 0xFFFF;

                // Stop at the first E1000 device.
                if (vendor == MK_ETH_PCI_VENDOR_INTEL && device == MK_ETH_PCI_DEVICE_E1000) [[unlikely]] {
                    mk_eth_pci_bus = bus;
                    mk_eth_pci_dev = dev;
                    mk_eth_pci_func = func;

                    return 0;
                }

                // If not multifunction, remaining functions are invalid per PCI spec.
                if (func == 0) [[likely]] {
                    uint8_t header_type = (mk_eth_pci_read32(bus, dev, 0, 0x0C) >> 16) & 0xFF;
                    if (!(header_type & 0x80)) [[likely]] {
                        break;
                    }
                }
            }
        }
    }

    return -1;
}

// * Memory Helpers * //

// Mira Kernel Ethernet Alloc Aligned
// Mira malloc is not cache-line aware yet, so
// manual alignment is needed for DMA engines.
static void *mk_eth_alloc_aligned(size_t size, size_t align) {
    void *raw = mk_malloc(size + align);

    if (!raw) [[unlikely]] {
        return NULL;
    }

    uintptr_t addr = (uintptr_t)raw;
    uintptr_t aligned = (addr + align - 1) & ~(align - 1);

    return (void *)aligned;
}

// * E1000 Reset * //

// Mira Kernel Ethernet Reset
// Hardware state machine needs clock cycles to latch the reset bit.
static int mk_eth_reset(void) {
    mk_eth_write32(MK_ETH_REG_CTRL, mk_eth_reg32(MK_ETH_REG_CTRL) | MK_ETH_CTRL_RST);

    mk_util_port_delay(10);

    // Poll until reset clears.
    for (int i = 0; i < MK_ETH_MAX_TIMEOUT_MS; i++) {
        if (!(mk_eth_reg32(MK_ETH_REG_CTRL) & MK_ETH_CTRL_RST)) [[unlikely]] {
            break;
        }

        mk_util_port_delay(1);
    }

    if (mk_eth_reg32(MK_ETH_REG_CTRL) & MK_ETH_CTRL_RST) [[unlikely]] {
        return -1;
    }

    mk_util_port_delay(20);

    // Disable all interrupts. Polled mode only.
    mk_eth_write32(MK_ETH_REG_IMC, 0xFFFFFFFF);

    // Clear any pending interrupt causes.
    mk_eth_reg32(MK_ETH_REG_ICR);

    return 0;
}

// * MAC Address * //

// Mira Kernel Ethernet EEPROM Read
// Reads a 16-bit word from the EEPROM.
static uint16_t mk_eth_eeprom_read(uint8_t addr) {
    mk_eth_write32(MK_ETH_REG_EERD, ((uint32_t)addr << MK_ETH_EERD_ADDR_SHIFT) | MK_ETH_EERD_START);

    for (int i = 0; i < MK_ETH_MAX_TIMEOUT_MS; i++) {
        uint32_t val = mk_eth_reg32(MK_ETH_REG_EERD);

        if (val & MK_ETH_EERD_DONE) [[unlikely]] {
            return (uint16_t)(val >> MK_ETH_EERD_DATA_SHIFT);
        }

        mk_util_port_delay(1);
    }

    return 0;
}

// Mira Kernel Ethernet Read MAC
// Reads from RAL/RAH first, falls back to EEPROM.
static void mk_eth_read_mac(void) {
    uint32_t ral = mk_eth_reg32(MK_ETH_REG_RAL);
    uint32_t rah = mk_eth_reg32(MK_ETH_REG_RAH);

    // RAH bit 31 (AV) indicates a valid address.
    if (rah & (1u << 31)) {
        eth.mac[0] = (ral >> 0) & 0xFF;
        eth.mac[1] = (ral >> 8) & 0xFF;
        eth.mac[2] = (ral >> 16) & 0xFF;
        eth.mac[3] = (ral >> 24) & 0xFF;
        eth.mac[4] = (rah >> 0) & 0xFF;
        eth.mac[5] = (rah >> 8) & 0xFF;

        return;
    }

    // EEPROM fallback.

    uint16_t w0 = mk_eth_eeprom_read(0);
    uint16_t w1 = mk_eth_eeprom_read(1);
    uint16_t w2 = mk_eth_eeprom_read(2);

    eth.mac[0] = w0 & 0xFF;
    eth.mac[1] = (w0 >> 8) & 0xFF;
    eth.mac[2] = w1 & 0xFF;
    eth.mac[3] = (w1 >> 8) & 0xFF;
    eth.mac[4] = w2 & 0xFF;
    eth.mac[5] = (w2 >> 8) & 0xFF;

    // Write back to RAL/RAH so hardware filters correctly.

    mk_eth_write32(
        MK_ETH_REG_RAL,
        (uint32_t)eth.mac[0] | ((uint32_t)eth.mac[1] << 8) | ((uint32_t)eth.mac[2] << 16) | ((uint32_t)eth.mac[3] << 24)
    );

    mk_eth_write32(
        MK_ETH_REG_RAH,
        (uint32_t)eth.mac[4] | ((uint32_t)eth.mac[5] << 8) | (1u << 31)
    );
}

// * RX Init * //

// Mira Kernel Ethernet RX Init
// Initializes the RX descriptors and buffers.
static int mk_eth_rx_init(void) {
    eth.rx_descs = (mk_eth_rx_desc_t *)mk_eth_alloc_aligned(MK_ETH_RX_DESC_COUNT * sizeof(mk_eth_rx_desc_t), 128);

    if (!eth.rx_descs) [[unlikely]] {
        return -1;
    }

    mk_memset(eth.rx_descs, 0, MK_ETH_RX_DESC_COUNT * sizeof(mk_eth_rx_desc_t));

    for (int i = 0; i < MK_ETH_RX_DESC_COUNT; i++) {
        eth.rx_bufs[i] = (uint8_t *)mk_eth_alloc_aligned(MK_ETH_RX_BUF_SIZE, 16);

        if (!eth.rx_bufs[i]) [[unlikely]] {
            return -1;
        }

        mk_memset(eth.rx_bufs[i], 0, MK_ETH_RX_BUF_SIZE);

        eth.rx_descs[i].addr = (uint64_t)(uintptr_t)eth.rx_bufs[i];
    }

    uintptr_t rx_phys = (uintptr_t)eth.rx_descs;

    mk_eth_write32(MK_ETH_REG_RDBAL, (uint32_t)(rx_phys & 0xFFFFFFFF));
    mk_eth_write32(MK_ETH_REG_RDBAH, (uint32_t)(rx_phys >> 32));
    mk_eth_write32(MK_ETH_REG_RDLEN, MK_ETH_RX_DESC_COUNT * sizeof(mk_eth_rx_desc_t));
    mk_eth_write32(MK_ETH_REG_RDH, 0);
    mk_eth_write32(MK_ETH_REG_RDT, MK_ETH_RX_DESC_COUNT - 1);

    eth.rx_cur = 0;

    // Clear multicast table.
    for (int i = 0; i < 128; i++) {
        mk_eth_write32(MK_ETH_REG_MTA + (i * 4), 0);
    }

    mk_eth_write32(MK_ETH_REG_RCTL, MK_ETH_RCTL_EN | MK_ETH_RCTL_BAM | MK_ETH_RCTL_BSIZE_2048 | MK_ETH_RCTL_SECRC);

    return 0;
}

// * TX Init * //

// Mira Kernel Ethernet TX Init
// Initializes the TX descriptors and buffers.
static int mk_eth_tx_init(void) {
    eth.tx_descs = (mk_eth_tx_desc_t *)mk_eth_alloc_aligned(MK_ETH_TX_DESC_COUNT * sizeof(mk_eth_tx_desc_t), 128);

    if (!eth.tx_descs) [[unlikely]] {
        return -1;
    }

    mk_memset(eth.tx_descs, 0, MK_ETH_TX_DESC_COUNT * sizeof(mk_eth_tx_desc_t));

    for (int i = 0; i < MK_ETH_TX_DESC_COUNT; i++) {
        eth.tx_bufs[i] = (uint8_t *)mk_eth_alloc_aligned(MK_ETH_TX_BUF_SIZE, 16);

        if (!eth.tx_bufs[i]) [[unlikely]] {
            return -1;
        }

        mk_memset(eth.tx_bufs[i], 0, MK_ETH_TX_BUF_SIZE);
    }

    uintptr_t tx_phys = (uintptr_t)eth.tx_descs;

    mk_eth_write32(MK_ETH_REG_TDBAL, (uint32_t)(tx_phys & 0xFFFFFFFF));
    mk_eth_write32(MK_ETH_REG_TDBAH, (uint32_t)(tx_phys >> 32));
    mk_eth_write32(MK_ETH_REG_TDLEN, MK_ETH_TX_DESC_COUNT * sizeof(mk_eth_tx_desc_t));
    mk_eth_write32(MK_ETH_REG_TDH, 0);
    mk_eth_write32(MK_ETH_REG_TDT, 0);

    eth.tx_cur = 0;

    mk_eth_write32(MK_ETH_REG_TIPG, MK_ETH_TIPG_DEFAULT);

    mk_eth_write32(
        MK_ETH_REG_TCTL,
        MK_ETH_TCTL_EN | MK_ETH_TCTL_PSP | (0x0F << MK_ETH_TCTL_CT_SHIFT) | (0x3F << MK_ETH_TCTL_COLD_SHIFT)
    );

    return 0;
}

// * Link * //

// Mira Kernel Ethernet Link Up
// Sets up the link for operation.
static int mk_eth_link_up(void) {
    uint32_t ctrl = mk_eth_reg32(MK_ETH_REG_CTRL);
    ctrl |= MK_ETH_CTRL_SLU | MK_ETH_CTRL_ASDE;
    ctrl &= ~MK_ETH_CTRL_PHY_RST;

    mk_eth_write32(MK_ETH_REG_CTRL, ctrl);

    for (int i = 0; i < MK_ETH_MAX_TIMEOUT_MS; i++) {
        if (mk_eth_reg32(MK_ETH_REG_STATUS) & MK_ETH_STATUS_LU) [[unlikely]] {
            return 0;
        }

        mk_util_port_delay(1);
    }

    return -1;
}

// * IP Helpers * //

// Mira Kernel Ethernet Parse IP
// Returns IP in network byte order.
static uint32_t mk_eth_parse_ip(const char *str) {
    uint8_t parts[4];
    parts[0] = 0;
    parts[1] = 0;
    parts[2] = 0;
    parts[3] = 0;

    int idx = 0;

    for (int i = 0; str[i] != '\0' && idx < 4; i++) {
        if (str[i] == '.') {
            idx++;
        } else {
            parts[idx] = parts[idx] * 10 + (str[i] - '0');
        }
    }

    uint32_t result;
    uint8_t *p = (uint8_t *)&result;
    p[0] = parts[0];
    p[1] = parts[1];
    p[2] = parts[2];
    p[3] = parts[3];

    return result;
}

// * Checksum * //

// Mira Kernel Ethernet IP Checksum
static uint16_t mk_eth_ip_checksum(const void *data, uint16_t len) {
    const uint16_t *words = (const uint16_t *)data;
    uint32_t sum = 0;

    for (uint16_t i = 0; i < len / 2; i++) {
        sum += words[i];
    }

    // Fold carries.
    while (sum >> 16) {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }

    return (uint16_t)(~sum);
}

// * Raw TX * //

// Mira Kernel Ethernet Transmit
// Sends a raw frame through the TX ring.
static int mk_eth_transmit(const void *frame, uint16_t len) {
    uint16_t cur = eth.tx_cur;

    mk_memcpy(eth.tx_bufs[cur], frame, len);

    eth.tx_descs[cur].addr = (uint64_t)(uintptr_t)eth.tx_bufs[cur];
    eth.tx_descs[cur].length = len;
    eth.tx_descs[cur].cso = 0;
    eth.tx_descs[cur].cmd = MK_ETH_TDESC_CMD_EOP | MK_ETH_TDESC_CMD_IFCS | MK_ETH_TDESC_CMD_RS;
    eth.tx_descs[cur].sta = 0;
    eth.tx_descs[cur].css = 0;
    eth.tx_descs[cur].special = 0;

    eth.tx_cur = (cur + 1) % MK_ETH_TX_DESC_COUNT;
    mk_eth_write32(MK_ETH_REG_TDT, eth.tx_cur);

    for (int i = 0; i < MK_ETH_MAX_TIMEOUT_MS; i++) {
        if (eth.tx_descs[cur].sta & MK_ETH_TDESC_STA_DD) [[unlikely]] {
            return 0;
        }

        mk_util_port_delay(1);
    }

    return -1;
}

// * ARP * //

// Mira Kernel Ethernet ARP Lookup
// Looks up an IP in the ARP cache.
static int mk_eth_arp_lookup(uint32_t ip, uint8_t *mac_out) {
    for (int i = 0; i < MK_ETH_ARP_CACHE_SIZE; i++) {
        if (eth.arp_cache[i].valid && eth.arp_cache[i].ip == ip) {
            mk_memcpy(mac_out, eth.arp_cache[i].mac, 6);
            return 0;
        }
    }

    return -1;
}

// Mira Kernel Ethernet ARP Cache Add
// Adds an IP and MAC to the ARP cache.
// Useful speed when talking to the same IP multiple times.
static void mk_eth_arp_cache_add(uint32_t ip, const uint8_t *mac) {
    // Update existing entry.
    for (int i = 0; i < MK_ETH_ARP_CACHE_SIZE; i++) {
        if (eth.arp_cache[i].valid && eth.arp_cache[i].ip == ip) {
            mk_memcpy(eth.arp_cache[i].mac, mac, 6);
            return;
        }
    }

    // First free slot.
    for (int i = 0; i < MK_ETH_ARP_CACHE_SIZE; i++) {
        if (!eth.arp_cache[i].valid) {
            eth.arp_cache[i].ip = ip;
            mk_memcpy(eth.arp_cache[i].mac, mac, 6);
            eth.arp_cache[i].valid = 1;

            return;
        }
    }

    // Cache full. Evict first entry.
    eth.arp_cache[0].ip = ip;
    mk_memcpy(eth.arp_cache[0].mac, mac, 6);
}

// Mira Kernel Ethernet ARP Send Request
static int mk_eth_arp_send_request(uint32_t target_ip) {
    uint8_t frame[14 + 28];
    mk_eth_frame_hdr_t *hdr = (mk_eth_frame_hdr_t *)frame;
    mk_eth_arp_packet_t *arp = (mk_eth_arp_packet_t *)(frame + 14);

    // Broadcast.
    mk_memset(hdr->dst, 0xFF, 6);
    mk_memcpy(hdr->src, eth.mac, 6);
    hdr->ethertype = mk_eth_htons(MK_ETH_ETHERTYPE_ARP);

    arp->htype = mk_eth_htons(MK_ETH_ARP_HTYPE_ETH);
    arp->ptype = mk_eth_htons(MK_ETH_ARP_PTYPE_IPV4);
    arp->hlen = MK_ETH_ARP_HLEN;
    arp->plen = MK_ETH_ARP_PLEN;
    arp->opcode = mk_eth_htons(MK_ETH_ARP_OP_REQUEST);
    mk_memcpy(arp->sha, eth.mac, 6);
    arp->spa = eth.ip;
    mk_memset(arp->tha, 0x00, 6);
    arp->tpa = target_ip;

    return mk_eth_transmit(frame, sizeof(frame));
}

// Mira Kernel Ethernet ARP Process
// Handles an incoming ARP packet. Caches the sender
// mapping, and replies to requests targeting our IP.
static void mk_eth_arp_process(const uint8_t *buf, uint16_t len) {
    if (len < sizeof(mk_eth_arp_packet_t)) [[unlikely]] {
        return;
    }

    const mk_eth_arp_packet_t *arp = (const mk_eth_arp_packet_t *)buf;

    if (
        mk_eth_ntohs(arp->htype) != MK_ETH_ARP_HTYPE_ETH ||
        mk_eth_ntohs(arp->ptype) != MK_ETH_ARP_PTYPE_IPV4
    ) {
        return;
    }

    // Always cache the sender!
    mk_eth_arp_cache_add(arp->spa, arp->sha);

    uint16_t op = mk_eth_ntohs(arp->opcode);

    if (op == MK_ETH_ARP_OP_REQUEST && arp->tpa == eth.ip) {
        // Build and send reply.

        uint8_t frame[14 + 28];
        mk_eth_frame_hdr_t *hdr = (mk_eth_frame_hdr_t *)frame;
        mk_eth_arp_packet_t *reply = (mk_eth_arp_packet_t *)(frame + 14);

        mk_memcpy(hdr->dst, arp->sha, 6);
        mk_memcpy(hdr->src, eth.mac, 6);
        hdr->ethertype = mk_eth_htons(MK_ETH_ETHERTYPE_ARP);

        reply->htype = mk_eth_htons(MK_ETH_ARP_HTYPE_ETH);
        reply->ptype = mk_eth_htons(MK_ETH_ARP_PTYPE_IPV4);
        reply->hlen = MK_ETH_ARP_HLEN;
        reply->plen = MK_ETH_ARP_PLEN;
        reply->opcode = mk_eth_htons(MK_ETH_ARP_OP_REPLY);
        mk_memcpy(reply->sha, eth.mac, 6);
        reply->spa = eth.ip;
        mk_memcpy(reply->tha, arp->sha, 6);
        reply->tpa = arp->spa;

        mk_eth_transmit(frame, sizeof(frame));
    }
}

// Mira Kernel Ethernet RX Recycle
// Returns a descriptor back to hardware.
static void mk_eth_rx_recycle(uint16_t idx) {
    eth.rx_descs[idx].status = 0;
    mk_eth_write32(MK_ETH_REG_RDT, idx);
}

// Mira Kernel Ethernet RX Drain ARP
// Processes pending RX descriptors, handling ARP internally.
// Non-ARP packets are dropped during drain.
static void mk_eth_rx_drain_arp(void) {
    for (int limit = 0; limit < MK_ETH_RX_DESC_COUNT; limit++) {
        if (!(eth.rx_descs[eth.rx_cur].status & MK_ETH_RDESC_STA_DD)) {
            break;
        }

        uint8_t *buf = eth.rx_bufs[eth.rx_cur];
        uint16_t len = eth.rx_descs[eth.rx_cur].length;

        if (len >= 14) {
            mk_eth_frame_hdr_t *hdr = (mk_eth_frame_hdr_t *)buf;

            if (mk_eth_ntohs(hdr->ethertype) == MK_ETH_ETHERTYPE_ARP && len >= 14 + sizeof(mk_eth_arp_packet_t)) {
                mk_eth_arp_process(buf + 14, len - 14);
            }
        }

        uint16_t old = eth.rx_cur;
        eth.rx_cur = (eth.rx_cur + 1) % MK_ETH_RX_DESC_COUNT;
        mk_eth_rx_recycle(old);
    }
}

// Mira Kernel Ethernet ARP Resolve
// Resolves an IP to a MAC address. Checks cache first,
// then sends ARP requests with polling.
static int mk_eth_arp_resolve(uint32_t ip, uint8_t *mac_out) {
    if (mk_eth_arp_lookup(ip, mac_out) == 0) {
        return 0;
    }

    for (int attempt = 0; attempt < 3; attempt++) {
        if (mk_eth_arp_send_request(ip) < 0) [[unlikely]] {
            continue;
        }

        for (int i = 0; i < MK_ETH_MAX_TIMEOUT_MS; i++) {
            mk_eth_rx_drain_arp();

            if (mk_eth_arp_lookup(ip, mac_out) == 0) {
                return 0;
            }

            mk_util_port_delay(1);
        }
    }

    return -1;
}

// * Routing * //

// Mira Kernel Ethernet Resolve Next Hop
// Determines the L2 destination for a given IP.
// Local IPs are ARPed directly; remote IPs go through the gateway.
static int mk_eth_resolve_next_hop(uint32_t dst_ip, uint8_t *mac_out) {
    uint32_t next_hop;

    if ((dst_ip & eth.subnet) == (eth.ip & eth.subnet)) {
        next_hop = dst_ip;
    } else {
        next_hop = eth.gateway;
    }

    return mk_eth_arp_resolve(next_hop, mac_out);
}

// * Public API * //

// Mira Kernel Ethernet Init
int mk_eth_init(void) {
    mk_memset(&eth, 0, sizeof(eth));

    if (mk_eth_pci_find_e1000() < 0) [[unlikely]] {
        return -1;
    }

    // BAR0 at PCI register 0x10.
    uint32_t bar0 = mk_eth_pci_read32(mk_eth_pci_bus, mk_eth_pci_dev, mk_eth_pci_func, 0x10);

    if (bar0 & 1) [[unlikely]] {
        return -1; // IO space, not MMIO.
    }

    uint64_t mmio_base = bar0 & 0xFFFFFFF0;

    // 64-bit BAR.
    if (((bar0 >> 1) & 0x3) == 0x2) {
        uint32_t bar1 = mk_eth_pci_read32(mk_eth_pci_bus, mk_eth_pci_dev, mk_eth_pci_func, 0x14);
        mmio_base |= ((uint64_t)bar1 << 32);
    }

    eth.mmio = (volatile uint8_t *)mmio_base;

    // Enable bus mastering and memory space.
    uint16_t cmd = mk_eth_pci_read16(mk_eth_pci_bus, mk_eth_pci_dev, mk_eth_pci_func, 0x04);
    cmd |= MK_ETH_PCI_CMD_MEM_SPACE | MK_ETH_PCI_CMD_BUS_MASTER;
    mk_eth_pci_write16(mk_eth_pci_bus, mk_eth_pci_dev, mk_eth_pci_func, 0x04, cmd);

    if (mk_eth_reset() < 0) [[unlikely]] {
        return -1;
    }

    mk_eth_read_mac();

    // QEMU static network configuration.
    // A TODO is to get this using DHCP.
    eth.ip = MK_ETH_IP(10, 0, 2, 15);
    eth.gateway = MK_ETH_IP(10, 0, 2, 2);
    eth.subnet = MK_ETH_IP(255, 255, 255, 0);
    
    eth.next_ephemeral_port = 49152;

    if (mk_eth_rx_init() < 0 || mk_eth_tx_init() < 0) [[unlikely]] {
        return -1;
    }

    if (mk_eth_link_up() < 0) [[unlikely]] {
        return -1;
    }

    eth.initialized = 1;

    return 0;
}

// Mira Kernel Ethernet Socket
mk_eth_socket_t *mk_eth_socket(void) {
    if (!eth.initialized) [[unlikely]] {
        return NULL;
    }

    for (int i = 0; i < MK_ETH_MAX_SOCKETS; i++) {
        if (!eth.sockets[i].in_use) {
            eth.sockets[i].in_use = 1;
            eth.sockets[i].src_port = eth.next_ephemeral_port++;

            return &eth.sockets[i];
        }
    }

    return NULL;
}

// Mira Kernel Ethernet Send
// Sends an Ethernet + IPv4 + UDP frame.
int mk_eth_send(mk_eth_socket_t *sock, const char *ip, uint16_t port, const void *data, uint32_t len) {
    if (!eth.initialized || !sock || !ip || !data) [[unlikely]] {
        return -1;
    }

    uint32_t dst_ip = mk_eth_parse_ip(ip);

    uint16_t udp_len = 8 + (uint16_t)len;
    uint16_t ip_total_len = 20 + udp_len;
    uint16_t frame_len = 14 + ip_total_len;

    if (frame_len > MK_ETH_TX_BUF_SIZE) [[unlikely]] {
        return -1;
    }

    // Resolve L2 destination.
    uint8_t dst_mac[6];
    if (mk_eth_resolve_next_hop(dst_ip, dst_mac) < 0) [[unlikely]] {
        return -1;
    }

    // Build directly in the TX buffer.
    uint16_t cur = eth.tx_cur;
    uint8_t *buf = eth.tx_bufs[cur];

    // Ethernet header.
    mk_eth_frame_hdr_t *eth_hdr = (mk_eth_frame_hdr_t *)buf;
    mk_memcpy(eth_hdr->dst, dst_mac, 6);
    mk_memcpy(eth_hdr->src, eth.mac, 6);
    eth_hdr->ethertype = mk_eth_htons(MK_ETH_ETHERTYPE_IPV4);

    // IPv4 header.
    mk_eth_ip_hdr_t *ip_hdr = (mk_eth_ip_hdr_t *)(buf + 14);
    ip_hdr->version_ihl = 0x45;
    ip_hdr->dscp_ecn = 0x00;
    ip_hdr->total_length = mk_eth_htons(ip_total_len);
    ip_hdr->identification = 0;
    ip_hdr->flags_fragment = mk_eth_htons(0x4000); // Don't Fragment.
    ip_hdr->ttl = MK_ETH_IP_TTL;
    ip_hdr->protocol = MK_ETH_IP_PROTO_UDP;
    ip_hdr->checksum = 0;
    ip_hdr->src_ip = eth.ip;
    ip_hdr->dst_ip = dst_ip;
    ip_hdr->checksum = mk_eth_ip_checksum(ip_hdr, 20);

    // UDP header.
    mk_eth_udp_hdr_t *udp_hdr = (mk_eth_udp_hdr_t *)(buf + 14 + 20);
    udp_hdr->src_port = mk_eth_htons(sock->src_port);
    udp_hdr->dst_port = mk_eth_htons(port);
    udp_hdr->length = mk_eth_htons(udp_len);
    udp_hdr->checksum = 0; // Optional for IPv4 UDP.

    // Payload.
    mk_memcpy(buf + 14 + 20 + 8, data, len);

    // Set up descriptor and transmit.
    eth.tx_descs[cur].addr = (uint64_t)(uintptr_t)buf;
    eth.tx_descs[cur].length = frame_len;
    eth.tx_descs[cur].cso = 0;
    eth.tx_descs[cur].cmd = MK_ETH_TDESC_CMD_EOP | MK_ETH_TDESC_CMD_IFCS | MK_ETH_TDESC_CMD_RS;
    eth.tx_descs[cur].sta = 0;
    eth.tx_descs[cur].css = 0;
    eth.tx_descs[cur].special = 0;

    eth.tx_cur = (cur + 1) % MK_ETH_TX_DESC_COUNT;
    mk_eth_write32(MK_ETH_REG_TDT, eth.tx_cur);

    // Poll for completion.
    for (int i = 0; i < MK_ETH_MAX_TIMEOUT_MS; i++) {
        if (eth.tx_descs[cur].sta & MK_ETH_TDESC_STA_DD) [[unlikely]] {
            return 0;
        }

        mk_util_port_delay(1);
    }

    return -1;
}

// Mira Kernel Ethernet Recv Reset
// Recycles the RX descriptor at the current RX cursor.
// Useful since this snippet repeats in mk_eth_recv.
static void mk_eth_recv_reset(void) {
    uint16_t old = eth.rx_cur;
    eth.rx_cur = (eth.rx_cur + 1) % MK_ETH_RX_DESC_COUNT;
    mk_eth_rx_recycle(old);
}


// Mira Kernel Ethernet Recv
// Zero-copy receive. Returns a pointer into the RX DMA buffer.
// The pointer is valid until the next mk_eth_recv call.
int mk_eth_recv(mk_eth_socket_t *sock, const void **data, uint16_t *len) {
    if (!eth.initialized || !sock) [[unlikely]] {
        return -1;
    }

    for (int limit = 0; limit < MK_ETH_RX_DESC_COUNT; limit++) {
        if (!(eth.rx_descs[eth.rx_cur].status & MK_ETH_RDESC_STA_DD)) {
            return -1;
        }

        uint8_t *buf = eth.rx_bufs[eth.rx_cur];
        uint16_t pkt_len = eth.rx_descs[eth.rx_cur].length;

        if (pkt_len < 14) [[unlikely]] {
            mk_eth_recv_reset();
            continue;
        }

        mk_eth_frame_hdr_t *eth_hdr = (mk_eth_frame_hdr_t *)buf;
        uint16_t ethertype = mk_eth_ntohs(eth_hdr->ethertype);

        // Handle ARP internally.
        if (ethertype == MK_ETH_ETHERTYPE_ARP) {
            if (pkt_len >= 14 + sizeof(mk_eth_arp_packet_t)) {
                mk_eth_arp_process(buf + 14, pkt_len - 14);
            }

            mk_eth_recv_reset();

            continue;
        }

        // Filter for IPv4 + UDP matching the socket port.
        if (ethertype != MK_ETH_ETHERTYPE_IPV4 || pkt_len < 14 + 20 + 8) {
            mk_eth_recv_reset();
            continue;
        }

        mk_eth_ip_hdr_t *ip_hdr = (mk_eth_ip_hdr_t *)(buf + 14);

        if (ip_hdr->protocol != MK_ETH_IP_PROTO_UDP) {
            mk_eth_recv_reset();
            continue;
        }

        uint8_t ip_hdr_len = (ip_hdr->version_ihl & 0x0F) * 4;
        mk_eth_udp_hdr_t *udp_hdr = (mk_eth_udp_hdr_t *)(buf + 14 + ip_hdr_len);

        if (mk_eth_ntohs(udp_hdr->dst_port) != sock->src_port) {
            mk_eth_recv_reset();
            continue;
        }

        // Match found.
        uint16_t udp_payload_offset = 14 + ip_hdr_len + 8;
        uint16_t udp_total_len = mk_eth_ntohs(udp_hdr->length);
        uint16_t payload_len = udp_total_len - 8;

        *data = buf + udp_payload_offset;
        *len = payload_len;

        // Advance cursor but defer recycle until next recv call.
        // The caller reads from the buffer before that happens.
        mk_eth_recv_reset();

        return 0;
    }

    return -1;
}