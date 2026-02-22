#include "../inc/nvme.h"
#include "../inc/util.h"
#include "../inc/mem.h"

// Singleton driver state. Mira uses NVMe for block storage.
static mk_nvme_state_t nvme;

// * MMIO Access Helpers * //

static inline uint32_t mk_nvme_reg32(uint32_t off) {
    return *(volatile uint32_t *)(nvme.mmio + off);
}

static inline uint64_t mk_nvme_reg64(uint32_t off) {
    uint32_t lo = *(volatile uint32_t *)(nvme.mmio + off);
    uint32_t hi = *(volatile uint32_t *)(nvme.mmio + off + 4);
    return (uint64_t)lo | ((uint64_t)hi << 32);
}

static inline void mk_nvme_write32(uint32_t off, uint32_t val) {
    *(volatile uint32_t *)(nvme.mmio + off) = val;
}

static inline void mk_nvme_write64(uint32_t off, uint64_t val) {
    *(volatile uint32_t *)(nvme.mmio + off) = (uint32_t)(val & 0xFFFFFFFF);
    *(volatile uint32_t *)(nvme.mmio + off + 4) = (uint32_t)(val >> 32);
}

// * PCI Configuration Helpers * //

static uint32_t mk_nvme_pci_read32(uint8_t bus, uint8_t dev, uint8_t func, uint8_t reg) {
    uint32_t addr = (1u << 31)
                  | ((uint32_t)bus << 16)
                  | ((uint32_t)dev << 11)
                  | ((uint32_t)func << 8)
                  | (reg & 0xFC);
    mk_util_outl(MK_NVME_PCI_CONFIG_ADDR, addr);
    return mk_util_inl(MK_NVME_PCI_CONFIG_DATA);
}

static void mk_nvme_pci_write32(uint8_t bus, uint8_t dev, uint8_t func, uint8_t reg, uint32_t val) {
    uint32_t addr = (1u << 31)
                  | ((uint32_t)bus << 16)
                  | ((uint32_t)dev << 11)
                  | ((uint32_t)func << 8)
                  | (reg & 0xFC);
    mk_util_outl(MK_NVME_PCI_CONFIG_ADDR, addr);
    mk_util_outl(MK_NVME_PCI_CONFIG_DATA, val);
}

static uint16_t mk_nvme_pci_read16(uint8_t bus, uint8_t dev, uint8_t func, uint8_t reg) {
    uint32_t dword = mk_nvme_pci_read32(bus, dev, func, reg & 0xFC);
    return (uint16_t)(dword >> ((reg & 2) * 8));
}

static void mk_nvme_pci_write16(uint8_t bus, uint8_t dev, uint8_t func, uint8_t reg, uint16_t val) {
    uint32_t dword = mk_nvme_pci_read32(bus, dev, func, reg & 0xFC);
    int shift = (reg & 2) * 8;

    dword &= ~(0xFFFF << shift);
    dword |= ((uint32_t)val << shift);

    mk_nvme_pci_write32(bus, dev, func, reg & 0xFC, dword);
}

// * Scan * //

static uint8_t mk_nvme_pci_bus;
static uint8_t mk_nvme_pci_dev;
static uint8_t mk_nvme_pci_func;

// Mira Kernel NVMe PCI Find
// Scans PCI for an NVMe controller.
static int mk_nvme_pci_find(void) {
    for (uint16_t bus = 0; bus < 256; bus++) {
        for (uint8_t dev = 0; dev < 32; dev++) {
            for (uint8_t func = 0; func < 8; func++) {
                uint32_t id = mk_nvme_pci_read32(bus, dev, func, 0x00);

                if (id == 0xFFFFFFFF || id == 0x00000000) [[likely]] {
                    continue;
                }

                uint32_t class_reg = mk_nvme_pci_read32(bus, dev, func, 0x08);
                uint8_t base_class = (class_reg >> 24) & 0xFF;
                uint8_t sub_class = (class_reg >> 16) & 0xFF;

                // Stop at the first NVMe controller.
                if (base_class == MK_NVME_PCI_CLASS_STORAGE && sub_class == MK_NVME_PCI_SUBCLASS_NVME) [[unlikely]] {
                    mk_nvme_pci_bus = bus;
                    mk_nvme_pci_dev = dev;
                    mk_nvme_pci_func = func;

                    return 0;
                }

                // If not multifunction, remaining functions are invalid per PCI spec.
                if (func == 0) [[likely]] {
                    uint8_t header_type = (mk_nvme_pci_read32(bus, dev, 0, 0x0C) >> 16) & 0xFF;
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

// Mira Kernel NVMe Alloc Aligned
// Mira malloc is not cache-line aware yet, so
// manual alignment is needed for DMA engines.
static void *mk_nvme_alloc_aligned(size_t size, size_t align) {
    void *raw = mk_malloc(size + align);

    if (!raw) [[unlikely]] {
        return NULL;
    }

    uintptr_t addr = (uintptr_t)raw;
    uintptr_t aligned = (addr + align - 1) & ~(align - 1);

    return (void *)aligned;
}

// * Doorbell Helpers * //

// Mira Kernel NVMe SQ Tail Doorbell
static void mk_nvme_sq_doorbell(uint16_t qid, uint16_t val) {
    uint32_t off = 0x1000 + (2 * qid) * nvme.db_stride;
    mk_nvme_write32(off, val);
}

// Mira Kernel NVMe CQ Head Doorbell
static void mk_nvme_cq_doorbell(uint16_t qid, uint16_t val) {
    uint32_t off = 0x1000 + (2 * qid + 1) * nvme.db_stride;
    mk_nvme_write32(off, val);
}

// * Controller Disable/Enable * //

// Mira Kernel NVMe Disable
// Clears CC.EN and waits for CSTS.RDY to clear.
static int mk_nvme_disable(void) {
    uint32_t cc = mk_nvme_reg32(MK_NVME_REG_CC);
    cc &= ~MK_NVME_CC_EN;
    mk_nvme_write32(MK_NVME_REG_CC, cc);

    for (int i = 0; i < MK_NVME_MAX_TIMEOUT_MS; i++) {
        uint32_t csts = mk_nvme_reg32(MK_NVME_REG_CSTS);

        if (!(csts & MK_NVME_CSTS_RDY)) [[unlikely]] {
            return 0;
        }

        mk_util_port_delay(1);
    }

    return -1;
}

// Mira Kernel NVMe Enable
// Sets CC to enable with NVM command set and waits for CSTS.RDY.
static int mk_nvme_enable(void) {
    uint32_t cc = MK_NVME_CC_EN
                | MK_NVME_CC_CSS_NVM
                | MK_NVME_CC_AMS_RR
                | MK_NVME_CC_SHN_NONE
                | (0 << MK_NVME_CC_MPS_SHIFT) // MPS=0 -> 4 KiB pages
                | (6 << MK_NVME_CC_IOSQES_SHIFT) // 2^6 = 64 bytes per SQ entry
                | (4 << MK_NVME_CC_IOCQES_SHIFT); // 2^4 = 16 bytes per CQ entry

    mk_nvme_write32(MK_NVME_REG_CC, cc);

    for (int i = 0; i < MK_NVME_MAX_TIMEOUT_MS; i++) {
        uint32_t csts = mk_nvme_reg32(MK_NVME_REG_CSTS);

        if (csts & MK_NVME_CSTS_CFS) [[unlikely]] {
            return -1;
        }

        if (csts & MK_NVME_CSTS_RDY) [[unlikely]] {
            return 0;
        }

        mk_util_port_delay(1);
    }

    return -1;
}

// * Admin Queue Setup * //

// Mira Kernel NVMe Admin Queues Init
// Allocates and programs the Admin Submission and Completion Queues.
static int mk_nvme_admin_queues_init(void) {
    nvme.admin_sq_size = MK_NVME_ADMIN_QUEUE_SIZE;
    nvme.admin_cq_size = MK_NVME_ADMIN_QUEUE_SIZE;
    nvme.admin_sq_tail = 0;
    nvme.admin_cq_head = 0;
    nvme.admin_cq_phase = 1;

    size_t sq_bytes = nvme.admin_sq_size * sizeof(mk_nvme_sq_entry_t);
    size_t cq_bytes = nvme.admin_cq_size * sizeof(mk_nvme_cq_entry_t);

    nvme.admin_sq = (mk_nvme_sq_entry_t *)mk_nvme_alloc_aligned(sq_bytes, MK_NVME_PAGE_SIZE);

    if (!nvme.admin_sq) [[unlikely]] {
        return -1;
    }

    mk_memset(nvme.admin_sq, 0, sq_bytes);

    nvme.admin_cq = (volatile mk_nvme_cq_entry_t *)mk_nvme_alloc_aligned(cq_bytes, MK_NVME_PAGE_SIZE);

    if (!nvme.admin_cq) [[unlikely]] {
        return -1;
    }

    mk_memset((void *)nvme.admin_cq, 0, cq_bytes);

    // Bits [27:16] = ACQS (0-based), bits [11:0] = ASQS (0-based).
    uint32_t aqa = ((uint32_t)(nvme.admin_cq_size - 1) << 16) | ((uint32_t)(nvme.admin_sq_size - 1));
    mk_nvme_write32(MK_NVME_REG_AQA, aqa);

    // Program base addresses.
    mk_nvme_write64(MK_NVME_REG_ASQ, (uint64_t)(uintptr_t)nvme.admin_sq);
    mk_nvme_write64(MK_NVME_REG_ACQ, (uint64_t)(uintptr_t)nvme.admin_cq);

    return 0;
}

// * Admin Command Submit + Poll * //

// Mira Kernel NVMe Admin Submit
// Submits one command to the Admin SQ and polls CQ for completion.
static int mk_nvme_admin_submit(mk_nvme_sq_entry_t *cmd, uint32_t *result) {
    uint16_t tail = nvme.admin_sq_tail;

    cmd->cid = nvme.next_cid++;

    mk_memcpy(&nvme.admin_sq[tail], cmd, sizeof(mk_nvme_sq_entry_t));

    tail = (tail + 1) % nvme.admin_sq_size;
    nvme.admin_sq_tail = tail;

    mk_nvme_sq_doorbell(0, tail);

    // Poll Admin CQ for our completion.
    for (int i = 0; i < MK_NVME_MAX_TIMEOUT_MS; i++) {
        volatile mk_nvme_cq_entry_t *cqe = &nvme.admin_cq[nvme.admin_cq_head];
        uint16_t status = cqe->status;

        // Phase bit is bit 0 of the status word.
        uint8_t phase = status & 1;

        if (phase != nvme.admin_cq_phase) {
            mk_util_port_delay(1);
            continue;
        }

        // Status Code is in bits [15:1].
        uint16_t sc = (status >> 1) & 0x7FFF;

        if (result) {
            *result = cqe->dw0;
        }

        // Advance CQ head.
        nvme.admin_cq_head = (nvme.admin_cq_head + 1) % nvme.admin_cq_size;

        if (nvme.admin_cq_head == 0) {
            nvme.admin_cq_phase ^= 1;
        }

        mk_nvme_cq_doorbell(0, nvme.admin_cq_head);

        if (sc != 0) [[unlikely]] {
            return -1;
        }

        return 0;
    }

    return -1;
}

// * Identify Controller * //

// Mira Kernel NVMe Identify Controller
// Issues Identify command with CNS=1.
static int mk_nvme_identify_controller(void) {
    mk_nvme_sq_entry_t cmd;
    mk_memset(&cmd, 0, sizeof(cmd));

    cmd.opcode = MK_NVME_ADMIN_IDENTIFY;
    cmd.nsid = 0;
    cmd.prp1 = (uint64_t)(uintptr_t)nvme.identify_buf;
    cmd.prp2 = 0;
    cmd.cdw10 = MK_NVME_IDENTIFY_CTRL;

    if (mk_nvme_admin_submit(&cmd, NULL) < 0) [[unlikely]] {
        return -1;
    }

    mk_nvme_id_ctrl_t *id = (mk_nvme_id_ctrl_t *)nvme.identify_buf;

    nvme.nn = id->nn;

    if (nvme.nn > MK_NVME_MAX_NAMESPACES) {
        nvme.nn = MK_NVME_MAX_NAMESPACES;
    }

    // MDTS is in units of (2^MDTS * minimum page size).
    // MDTS=0 means no limit, so use the max possible.
    if (id->mdts == 0) {
        nvme.max_transfer_blocks = MK_NVME_MAX_PRP_LIST;
    } else {
        // Store the raw MDTS value temporarily, but compute the
        // actual max transfer blocks once the block size is known.
        nvme.max_transfer_blocks = id->mdts;
    }

    return 0;
}

// * I/O Queue Creation * //

// Mira Kernel NVMe Create IO CQ
// Admin command to create I/O Completion Queue (QID=1).
static int mk_nvme_create_io_cq(void) {
    nvme.io_cq_size = MK_NVME_IO_QUEUE_SIZE;
    nvme.io_cq_head = 0;
    nvme.io_cq_phase = 1;

    size_t cq_bytes = nvme.io_cq_size * sizeof(mk_nvme_cq_entry_t);

    nvme.io_cq = (volatile mk_nvme_cq_entry_t *)mk_nvme_alloc_aligned(cq_bytes, MK_NVME_PAGE_SIZE);

    if (!nvme.io_cq) [[unlikely]] {
        return -1;
    }

    mk_memset((void *)nvme.io_cq, 0, cq_bytes);

    mk_nvme_sq_entry_t cmd;
    mk_memset(&cmd, 0, sizeof(cmd));

    cmd.opcode = MK_NVME_ADMIN_CREATE_IO_CQ;
    cmd.prp1 = (uint64_t)(uintptr_t)nvme.io_cq;
    cmd.cdw10 = ((uint32_t)(nvme.io_cq_size - 1) << 16) | 1;
    cmd.cdw11 = 1; // Contiguous + No Interrupts

    return mk_nvme_admin_submit(&cmd, NULL);
}

// Mira Kernel NVMe Create IO SQ
// Admin command to create I/O Submission Queue (QID=1), paired with CQ 1.
static int mk_nvme_create_io_sq(void) {
    nvme.io_sq_size = MK_NVME_IO_QUEUE_SIZE;
    nvme.io_sq_tail = 0;

    size_t sq_bytes = nvme.io_sq_size * sizeof(mk_nvme_sq_entry_t);

    nvme.io_sq = (mk_nvme_sq_entry_t *)mk_nvme_alloc_aligned(sq_bytes, MK_NVME_PAGE_SIZE);

    if (!nvme.io_sq) [[unlikely]] {
        return -1;
    }

    mk_memset(nvme.io_sq, 0, sq_bytes);

    mk_nvme_sq_entry_t cmd;
    mk_memset(&cmd, 0, sizeof(cmd));

    cmd.opcode = MK_NVME_ADMIN_CREATE_IO_SQ;
    cmd.prp1 = (uint64_t)(uintptr_t)nvme.io_sq;
    cmd.cdw10 = ((uint32_t)(nvme.io_sq_size - 1) << 16) | 1;
    cmd.cdw11 = (1 << 16) | 1;

    return mk_nvme_admin_submit(&cmd, NULL);
}

// * I/O Command Submit + Poll * //

// Mira Kernel NVMe IO Submit
// Submits one command to the I/O SQ (QID=1) and polls for completion.
static int mk_nvme_io_submit(mk_nvme_sq_entry_t *cmd) {
    uint16_t tail = nvme.io_sq_tail;

    cmd->cid = nvme.next_cid++;

    mk_memcpy(&nvme.io_sq[tail], cmd, sizeof(mk_nvme_sq_entry_t));

    tail = (tail + 1) % nvme.io_sq_size;
    nvme.io_sq_tail = tail;

    mk_nvme_sq_doorbell(1, tail);

    // Poll I/O CQ for completion.
    for (int i = 0; i < MK_NVME_MAX_TIMEOUT_MS; i++) {
        volatile mk_nvme_cq_entry_t *cqe = &nvme.io_cq[nvme.io_cq_head];
        uint16_t status = cqe->status;

        uint8_t phase = status & 1;

        if (phase != nvme.io_cq_phase) {
            mk_util_port_delay(1);
            continue;
        }

        uint16_t sc = (status >> 1) & 0x7FFF;

        // Advance CQ head.
        nvme.io_cq_head = (nvme.io_cq_head + 1) % nvme.io_cq_size;

        if (nvme.io_cq_head == 0) {
            nvme.io_cq_phase ^= 1;
        }

        mk_nvme_cq_doorbell(1, nvme.io_cq_head);

        if (sc != 0) [[unlikely]] {
            return -1;
        }

        return 0;
    }

    return -1;
}

// * Identify Namespace * //

// Mira Kernel NVMe Identify Namespace
// Issues Identify command with CNS=0 for the given NSID.
static int mk_nvme_identify_namespace(uint32_t nsid, mk_nvme_ns_t *ns) {
    mk_nvme_sq_entry_t cmd;
    mk_memset(&cmd, 0, sizeof(cmd));

    cmd.opcode = MK_NVME_ADMIN_IDENTIFY;
    cmd.nsid = nsid;
    cmd.prp1 = (uint64_t)(uintptr_t)nvme.identify_buf;
    cmd.prp2 = 0;
    cmd.cdw10 = MK_NVME_IDENTIFY_NS;

    if (mk_nvme_admin_submit(&cmd, NULL) < 0) [[unlikely]] {
        return -1;
    }

    mk_nvme_id_ns_t *id = (mk_nvme_id_ns_t *)nvme.identify_buf;

    // Namespace size of 0 means the namespace is inactive.
    if (id->nsze == 0) [[unlikely]] {
        return -1;
    }

    // FLBAS bits [3:0] select the LBA format index.
    uint8_t lba_idx = id->flbas & 0x0F;

    if (lba_idx > id->nlbaf) [[unlikely]] {
        return -1;
    }

    uint8_t lba_shift = id->lbaf[lba_idx].lbads;

    // Block size must be at least 512 bytes (2^9).
    if (lba_shift < 9) [[unlikely]] {
        return -1;
    }

    ns->nsid = nsid;
    ns->block_size = 1u << lba_shift;
    ns->block_count = id->nsze;
    ns->active = 1;

    return 0;
}

// * PRP List Builder * //

// Mira Kernel NVMe Build PRP
// Sets up PRP1 and PRP2 for a transfer.
// For single-page transfers, only PRP1 is used.
// For two-page transfers, PRP2 is the second page address.
// For larger transfers, PRP2 points to a PRP list.
static int mk_nvme_build_prp(mk_nvme_sq_entry_t *cmd, uintptr_t buf, uint32_t len) {
    cmd->prp1 = (uint64_t)buf;

    uint32_t first_page_remain = MK_NVME_PAGE_SIZE - (buf & (MK_NVME_PAGE_SIZE - 1));

    // Transfer fits entirely in one page.
    if (len <= first_page_remain) {
        cmd->prp2 = 0;
        return 0;
    }

    uint32_t remaining = len - first_page_remain;
    uintptr_t next_addr = (buf + first_page_remain);

    // Transfer fits in exactly two pages.
    if (remaining <= MK_NVME_PAGE_SIZE) {
        cmd->prp2 = (uint64_t)next_addr;
        return 0;
    }

    // Multi-page, build PRP list.
    uint32_t num_entries = (remaining + MK_NVME_PAGE_SIZE - 1) / MK_NVME_PAGE_SIZE;

    if (num_entries > MK_NVME_MAX_PRP_LIST) [[unlikely]] {
        return -1;
    }

    for (uint32_t i = 0; i < num_entries; i++) {
        nvme.prp_list[i] = (uint64_t)next_addr;
        next_addr += MK_NVME_PAGE_SIZE;
    }

    cmd->prp2 = (uint64_t)(uintptr_t)nvme.prp_list;

    return 0;
}

// * Public API * //

// Mira Kernel NVMe Init
// Initialize the NVMe controller.
int mk_nvme_init(void) {
    mk_memset(&nvme, 0, sizeof(nvme));

    if (mk_nvme_pci_find() < 0) [[unlikely]] {
        return -1;
    }

    uint32_t bar0 = mk_nvme_pci_read32(mk_nvme_pci_bus, mk_nvme_pci_dev, mk_nvme_pci_func, 0x10);

    if (bar0 & 1) [[unlikely]] {
        return -1; // IO space, not MMIO.
    }

    uint64_t mmio_base = bar0 & 0xFFFFFFF0;

    // Confirm 64-bit BAR.
    if (((bar0 >> 1) & 0x3) == 0x2) {
        uint32_t bar1 = mk_nvme_pci_read32(mk_nvme_pci_bus, mk_nvme_pci_dev, mk_nvme_pci_func, 0x14);
        mmio_base |= ((uint64_t)bar1 << 32);
    }

    nvme.mmio = (volatile uint8_t *)mmio_base;

    // Enable bus mastering and memory space.
    uint16_t cmd = mk_nvme_pci_read16(mk_nvme_pci_bus, mk_nvme_pci_dev, mk_nvme_pci_func, 0x04);
    cmd |= MK_NVME_PCI_CMD_MEM_SPACE | MK_NVME_PCI_CMD_BUS_MASTER;
    mk_nvme_pci_write16(mk_nvme_pci_bus, mk_nvme_pci_dev, mk_nvme_pci_func, 0x04, cmd);

    // Read CAP register for doorbell stride.
    uint64_t cap = mk_nvme_reg64(MK_NVME_REG_CAP);
    uint32_t dstrd = (uint32_t)((cap >> MK_NVME_CAP_DSTRD_SHIFT) & MK_NVME_CAP_DSTRD_MASK);
    nvme.db_stride = 4 << dstrd;

    // Disable controller before configuration.
    if (mk_nvme_disable() < 0) [[unlikely]] {
        return -1;
    }

    // Mask all interrupts, as Mira doesn't do that for speed.
    mk_nvme_write32(MK_NVME_REG_INTMS, 0xFFFFFFFF);

    // Allocate the shared identify buffer (4 KiB & page-aligned).
    nvme.identify_buf = mk_nvme_alloc_aligned(MK_NVME_PAGE_SIZE, MK_NVME_PAGE_SIZE);

    if (!nvme.identify_buf) [[unlikely]] {
        return -1;
    }

    mk_memset(nvme.identify_buf, 0, MK_NVME_PAGE_SIZE);

    // Allocate PRP list for multi-page transfers.
    nvme.prp_list = (uint64_t *)mk_nvme_alloc_aligned(MK_NVME_MAX_PRP_LIST * sizeof(uint64_t), MK_NVME_PAGE_SIZE);

    if (!nvme.prp_list) [[unlikely]] {
        return -1;
    }

    mk_memset(nvme.prp_list, 0, MK_NVME_MAX_PRP_LIST * sizeof(uint64_t));
    nvme.next_cid = 1;

    // Initialize all other components.
    if (
        mk_nvme_admin_queues_init() < 0 ||
        mk_nvme_enable() < 0 ||
        mk_nvme_identify_controller() < 0 ||
        mk_nvme_create_io_cq() < 0 ||
        mk_nvme_create_io_sq() < 0
    ) [[unlikely]] {
        return -1;
    }

    nvme.initialized = 1;

    return 0;
}

// Mira Kernel NVMe Open
// Opens a namespace by NSID and returns a handle.
mk_nvme_ns_t *mk_nvme_open(uint32_t nsid) {
    if (!nvme.initialized) [[unlikely]] {
        return NULL;
    }

    if (nsid == 0 || nsid > nvme.nn) [[unlikely]] {
        return NULL;
    }

    uint32_t idx = nsid - 1;

    if (nvme.namespaces[idx].active) {
        return &nvme.namespaces[idx]; // Already opened.
    }

    if (mk_nvme_identify_namespace(nsid, &nvme.namespaces[idx]) < 0) [[unlikely]] {
        return NULL;
    }

    // Finalize MDTS in blocks now that the block size is known.
    // max_transfer_blocks was storing raw MDTS value from identify controller.
    if (nvme.max_transfer_blocks < 128) {
        uint32_t max_bytes = (1u << nvme.max_transfer_blocks) * MK_NVME_PAGE_SIZE;
        nvme.max_transfer_blocks = max_bytes / nvme.namespaces[idx].block_size;
    }

    return &nvme.namespaces[idx];
}

// Mira Kernel NVMe Read
// Zero-copy read. DMA writes directly into the caller's buffer.
int mk_nvme_read(mk_nvme_ns_t *ns, uint64_t lba, uint32_t count, void *data) {
    if (!nvme.initialized || !ns || !ns->active || !data || count == 0) [[unlikely]] {
        return -1;
    }

    if (lba + count > ns->block_count) [[unlikely]] {
        return -1;
    }

    uint32_t blocks_per_transfer = nvme.max_transfer_blocks;

    while (count > 0) {
        uint32_t chunk = count;

        if (chunk > blocks_per_transfer) {
            chunk = blocks_per_transfer;
        }

        uint32_t transfer_bytes = chunk * ns->block_size;

        mk_nvme_sq_entry_t cmd;
        mk_memset(&cmd, 0, sizeof(cmd));

        cmd.opcode = MK_NVME_IO_READ;
        cmd.nsid = ns->nsid;

        if (mk_nvme_build_prp(&cmd, (uintptr_t)data, transfer_bytes) < 0) [[unlikely]] {
            return -1;
        }

        // CDW10: Starting LBA (lower 32 bits)
        cmd.cdw10 = (uint32_t)(lba & 0xFFFFFFFF);

        // CDW11: Starting LBA (upper 32 bits)
        cmd.cdw11 = (uint32_t)(lba >> 32);

        // CDW12: [15:0] = Number of Logical Blocks (0-based)
        cmd.cdw12 = chunk - 1;

        if (mk_nvme_io_submit(&cmd) < 0) [[unlikely]] {
            return -1;
        }

        lba += chunk;
        count -= chunk;
        data = (uint8_t *)data + transfer_bytes;
    }

    return 0;
}

// Mira Kernel NVMe Write
// Zero-copy write. DMA reads directly from the caller's buffer.
int mk_nvme_write(mk_nvme_ns_t *ns, uint64_t lba, uint32_t count, const void *data) {
    if (!nvme.initialized || !ns || !ns->active || !data || count == 0) [[unlikely]] {
        return -1;
    }

    if (lba + count > ns->block_count) [[unlikely]] {
        return -1;
    }

    uint32_t blocks_per_transfer = nvme.max_transfer_blocks;

    while (count > 0) {
        uint32_t chunk = count;

        if (chunk > blocks_per_transfer) {
            chunk = blocks_per_transfer;
        }

        uint32_t transfer_bytes = chunk * ns->block_size;

        mk_nvme_sq_entry_t cmd;
        mk_memset(&cmd, 0, sizeof(cmd));

        cmd.opcode = MK_NVME_IO_WRITE;
        cmd.nsid = ns->nsid;

        if (mk_nvme_build_prp(&cmd, (uintptr_t)data, transfer_bytes) < 0) [[unlikely]] {
            return -1;
        }

        cmd.cdw10 = (uint32_t)(lba & 0xFFFFFFFF);
        cmd.cdw11 = (uint32_t)(lba >> 32);
        cmd.cdw12 = chunk - 1;

        if (mk_nvme_io_submit(&cmd) < 0) [[unlikely]] {
            return -1;
        }

        lba += chunk;
        count -= chunk;
        data = (const uint8_t *)data + transfer_bytes;
    }

    return 0;
}