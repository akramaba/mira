// * Specification: https://www.nvmexpress.org/wp-content/uploads/NVM_Express_1_2_1_Gold_20160603.pdf

#ifndef NVME_H
#define NVME_H

#include <stdint.h>
#include <stddef.h>

// * PCI * //

#define MK_NVME_PCI_CONFIG_ADDR 0x0CF8
#define MK_NVME_PCI_CONFIG_DATA 0x0CFC

#define MK_NVME_PCI_CLASS_STORAGE 0x01
#define MK_NVME_PCI_SUBCLASS_NVME 0x08

#define MK_NVME_PCI_CMD_MEM_SPACE (1 << 1)
#define MK_NVME_PCI_CMD_BUS_MASTER (1 << 2)

// * Controller Registers * //
// Offsets relative to BAR0 MMIO base.

#define MK_NVME_REG_CAP 0x0000 // Controller Capabilities (64-bit)
#define MK_NVME_REG_INTMS 0x000C // Interrupt Mask Set
#define MK_NVME_REG_CC 0x0014 // Controller Configuration
#define MK_NVME_REG_CSTS 0x001C // Controller Status
#define MK_NVME_REG_AQA 0x0024 // Admin Queue Attributes
#define MK_NVME_REG_ASQ 0x0028 // Admin Submission Queue Base Address (64-bit)
#define MK_NVME_REG_ACQ 0x0030 // Admin Completion Queue Base Address (64-bit)

// * CAP Fields * //

#define MK_NVME_CAP_DSTRD_SHIFT 32 // Doorbell Stride shift
#define MK_NVME_CAP_DSTRD_MASK 0xFUll // Doorbell Stride mask (4 bits)

// * CC Fields * //

#define MK_NVME_CC_EN (1 << 0)
#define MK_NVME_CC_CSS_NVM (0 << 4)
#define MK_NVME_CC_MPS_SHIFT 7 // Memory Page Size shift
#define MK_NVME_CC_AMS_RR (0 << 11) // Arbitration: Round Robin
#define MK_NVME_CC_SHN_NONE (0 << 14) // Shutdown Notification: None
#define MK_NVME_CC_IOSQES_SHIFT 16 // I/O SQ Entry Size shift (log2)
#define MK_NVME_CC_IOCQES_SHIFT 20 // I/O CQ Entry Size shift (log2)

// * CSTS Fields * //

#define MK_NVME_CSTS_RDY (1 << 0)
#define MK_NVME_CSTS_CFS (1 << 1) // Controller Fatal Status

// * Admin Opcodes * //

#define MK_NVME_ADMIN_CREATE_IO_SQ 0x01
#define MK_NVME_ADMIN_CREATE_IO_CQ 0x05
#define MK_NVME_ADMIN_IDENTIFY 0x06

// * NVM I/O Opcodes * //

#define MK_NVME_IO_WRITE 0x01
#define MK_NVME_IO_READ 0x02

// * Identify CNS Values * //

#define MK_NVME_IDENTIFY_NS 0x00
#define MK_NVME_IDENTIFY_CTRL 0x01

// * Queue Configuration * //

#define MK_NVME_ADMIN_QUEUE_SIZE 32
#define MK_NVME_IO_QUEUE_SIZE 64
#define MK_NVME_MAX_NAMESPACES 4
#define MK_NVME_MAX_TIMEOUT_MS 10000

#define MK_NVME_PAGE_SIZE 4096

// Maximum PRP list entries per transfer.
// Supports up to 512 KiB transfers (128 pages).
#define MK_NVME_MAX_PRP_LIST 128

// * Submission Queue Entry * //
// 64 bytes per NVMe spec.

typedef struct __attribute__((packed)) {
    // Command Dword 0
    uint8_t opcode;
    uint8_t flags; // Fused operation (bits 1:0), reserved
    uint16_t cid; // Command Identifier

    // Namespace Identifier
    uint32_t nsid;

    // Reserved
    uint64_t reserved;

    // Metadata Pointer
    uint64_t mptr;

    // PRP Entry 1
    uint64_t prp1;

    // PRP Entry 2
    uint64_t prp2;

    // Command-specific Dwords 10-15
    uint32_t cdw10;
    uint32_t cdw11;
    uint32_t cdw12;
    uint32_t cdw13;
    uint32_t cdw14;
    uint32_t cdw15;
} mk_nvme_sq_entry_t;

// * Completion Queue Entry * //
// 16 bytes per NVMe spec.

typedef struct __attribute__((packed)) {
    uint32_t dw0; // Command-specific
    uint32_t dw1; // Reserved
    uint16_t sq_head; // SQ Head Pointer
    uint16_t sq_id; // SQ Identifier
    uint16_t cid; // Command Identifier
    uint16_t status; // Status Field (bit 0 = Phase Tag)
} mk_nvme_cq_entry_t;

// * Identify Controller * //
// Some fields are unused.

typedef struct __attribute__((packed)) {
    uint16_t vid; // PCI Vendor ID
    uint16_t ssvid; // PCI Subsystem Vendor ID
    char sn[20]; // Serial Number
    char mn[40]; // Model Number
    char fr[8]; // Firmware Revision
    uint8_t rab; // Recommended Arbitration Burst
    uint8_t ieee[3]; // IEEE OUI Identifier
    uint8_t cmic; // Controller Multi-Path I/O and Namespace Sharing Capabilities
    uint8_t mdts; // Maximum Data Transfer Size (in units of minimum page size)
    uint16_t cntlid; // Controller ID
    uint32_t ver; // Version
    uint8_t reserved1[428]; // Padding to offset 512
    uint8_t sqes; // Submission Queue Entry Size
    uint8_t cqes; // Completion Queue Entry Size
    uint16_t maxcmd; // Maximum Outstanding Commands
    uint32_t nn; // Number of Namespaces
    uint8_t reserved2[3576]; // Pad to 4096
} mk_nvme_id_ctrl_t;

// * LBA Format * //

typedef struct __attribute__((packed)) {
    uint16_t ms; // Metadata Size
    uint8_t lbads; // LBA Data Size (log2)
    uint8_t rp; // Relative Performance
} mk_nvme_lbaf_t;

// * Identify Namespace * //
// Some fields are unused.

typedef struct __attribute__((packed)) {
    uint64_t nsze; // Namespace Size (total blocks)
    uint64_t ncap; // Namespace Capacity
    uint64_t nuse; // Namespace Utilization
    uint8_t nsfeat; // Namespace Features
    uint8_t nlbaf; // Number of LBA Formats (0-based)
    uint8_t flbas; // Formatted LBA Size
    uint8_t mc; // Metadata Capabilities
    uint8_t dpc; // End-to-end Data Protection Capabilities
    uint8_t dps; // End-to-end Data Protection Type Settings
    uint8_t nmic; // Namespace Multi-path I/O and Namespace Sharing Capabilities
    uint8_t rescap; // Reservation Capabilities
    uint8_t fpi; // Format Progress Indicator
    uint8_t dlfeat; // Deallocate Logical Block Features
    uint16_t nawun; // Namespace Atomic Write Unit Normal
    uint16_t nawupf; // Namespace Atomic Write Unit Power Fail
    uint16_t nacwu; // Namespace Atomic Compare & Write Unit
    uint16_t nabsn; // Namespace Atomic Boundary Size Normal
    uint16_t nabo; // Namespace Atomic Boundary Offset
    uint16_t nabspf; // Namespace Atomic Boundary Size Power Fail
    uint16_t noiob; // Namespace Optimal I/O Boundary
    uint8_t nvmcap[16]; // NVM Capacity
    uint8_t reserved1[64]; // Padding to offset 128
    mk_nvme_lbaf_t lbaf[16]; // LBA Format Support (up to 16 formats)
    uint8_t reserved2[3904]; // Pad to 4096
} mk_nvme_id_ns_t;

// * Namespace Handle * //

typedef struct {
    uint32_t nsid;
    uint32_t block_size;
    uint64_t block_count;
    uint8_t active;
} mk_nvme_ns_t;

// * Driver State * //

typedef struct {
    volatile uint8_t *mmio;

    // Doorbell Stride
    uint32_t db_stride;

    // Admin Submission Queue
    mk_nvme_sq_entry_t *admin_sq;
    uint16_t admin_sq_tail;
    uint16_t admin_sq_size;

    // Admin Completion Queue
    volatile mk_nvme_cq_entry_t *admin_cq;
    uint16_t admin_cq_head;
    uint16_t admin_cq_size;
    uint8_t admin_cq_phase;

    // I/O Submission Queue
    mk_nvme_sq_entry_t *io_sq;
    uint16_t io_sq_tail;
    uint16_t io_sq_size;

    // I/O Completion Queue
    volatile mk_nvme_cq_entry_t *io_cq;
    uint16_t io_cq_head;
    uint16_t io_cq_size;
    uint8_t io_cq_phase;

    // PRP list for multi-page transfers.
    uint64_t *prp_list;

    // Controller properties
    uint32_t max_transfer_blocks;
    uint32_t nn; // Number of Namespaces

    // Namespace table
    mk_nvme_ns_t namespaces[MK_NVME_MAX_NAMESPACES];

    // Identify buffer (4096-byte aligned)
    void *identify_buf;

    // Rolling command ID
    uint16_t next_cid;

    uint8_t initialized;
} mk_nvme_state_t;

// * Public API * //

// Function to initialize the NVMe driver.
int mk_nvme_init(void);

// Function to open a namespace.
mk_nvme_ns_t *mk_nvme_open(uint32_t nsid);

// Function to read from a namespace.
int mk_nvme_read(mk_nvme_ns_t *ns, uint64_t lba, uint32_t count, void *data);

// Function to write to a namespace.
int mk_nvme_write(mk_nvme_ns_t *ns, uint64_t lba, uint32_t count, const void *data);

#endif