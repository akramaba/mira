#include "../inc/sound.h"
#include "../inc/util.h"
#include "../inc/mem.h"

// Singleton driver state. Mira uses Intel High Definition Audio.
static mk_snd_hda_state_t hda;

// * Configuration * //

#define MK_SND_HDA_DMA_BUF_SIZE (64 * 1024) // Enough space for now, as the assets
                                            // system is still limited in space.
#define MK_SND_HDA_BDL_ENTRIES 1
#define MK_SND_HDA_MAX_TIMEOUT_MS 500

// * MMIO Access Helpers * //

static inline uint8_t mk_snd_hda_reg8(uint32_t off) {
    return *(volatile uint8_t *)(hda.mmio + off);
}

static inline uint16_t mk_snd_hda_reg16(uint32_t off) {
    return *(volatile uint16_t *)(hda.mmio + off);
}

static inline uint32_t mk_snd_hda_reg32(uint32_t off) {
    return *(volatile uint32_t *)(hda.mmio + off);
}

static inline void mk_snd_hda_write8(uint32_t off, uint8_t val) {
    *(volatile uint8_t *)(hda.mmio + off) = val;
}

static inline void mk_snd_hda_write16(uint32_t off, uint16_t val) {
    *(volatile uint16_t *)(hda.mmio + off) = val;
}

static inline void mk_snd_hda_write32(uint32_t off, uint32_t val) {
    *(volatile uint32_t *)(hda.mmio + off) = val;
}

// * Stream Descriptor Access Helpers * //

static inline uint8_t mk_snd_sd_reg8(uint32_t off) {
    return *(volatile uint8_t *)(hda.sd_base + off);
}

static inline uint16_t mk_snd_sd_reg16(uint32_t off) {
    return *(volatile uint16_t *)(hda.sd_base + off);
}

static inline uint32_t mk_snd_sd_reg32(uint32_t off) {
    return *(volatile uint32_t *)(hda.sd_base + off);
}

static inline void mk_snd_sd_write8(uint32_t off, uint8_t val) {
    *(volatile uint8_t *)(hda.sd_base + off) = val;
}

static inline void mk_snd_sd_write16(uint32_t off, uint16_t val) {
    *(volatile uint16_t *)(hda.sd_base + off) = val;
}

static inline void mk_snd_sd_write32(uint32_t off, uint32_t val) {
    *(volatile uint32_t *)(hda.sd_base + off) = val;
}

// * PCI Configuration Helpers * //

static uint32_t mk_snd_pci_read32(uint8_t bus, uint8_t dev, uint8_t func, uint8_t reg) {
    uint32_t addr = (1u << 31)
                  | ((uint32_t)bus << 16)
                  | ((uint32_t)dev << 11)
                  | ((uint32_t)func << 8)
                  | (reg & 0xFC);
    mk_util_outl(MK_SND_PCI_CONFIG_ADDR, addr);
    return mk_util_inl(MK_SND_PCI_CONFIG_DATA);
}

static void mk_snd_pci_write32(uint8_t bus, uint8_t dev, uint8_t func, uint8_t reg, uint32_t val) {
    uint32_t addr = (1u << 31)
                  | ((uint32_t)bus << 16)
                  | ((uint32_t)dev << 11)
                  | ((uint32_t)func << 8)
                  | (reg & 0xFC);
    mk_util_outl(MK_SND_PCI_CONFIG_ADDR, addr);
    mk_util_outl(MK_SND_PCI_CONFIG_DATA, val);
}

static uint16_t mk_snd_pci_read16(uint8_t bus, uint8_t dev, uint8_t func, uint8_t reg) {
    uint32_t dword = mk_snd_pci_read32(bus, dev, func, reg & 0xFC);
    return (uint16_t)(dword >> ((reg & 2) * 8));
}

static void mk_snd_pci_write16(uint8_t bus, uint8_t dev, uint8_t func, uint8_t reg, uint16_t val) {
    uint32_t dword = mk_snd_pci_read32(bus, dev, func, reg & 0xFC);
    int shift = (reg & 2) * 8;

    dword &= ~(0xFFFF << shift);
    dword |= ((uint32_t)val << shift);

    mk_snd_pci_write32(bus, dev, func, reg & 0xFC, dword);
}

// * Scan * //

static uint8_t mk_snd_pci_bus;
static uint8_t mk_snd_pci_dev;
static uint8_t mk_snd_pci_func;

// Mira Kernel Sound PCI Find HDA
// Currently called once to find the HDA device.
static int mk_snd_pci_find_hda(void) {
    for (uint16_t bus = 0; bus < 256; bus++) {
        for (uint8_t dev = 0; dev < 32; dev++) {
            for (uint8_t func = 0; func < 8; func++) {
                uint32_t id = mk_snd_pci_read32(bus, dev, func, 0x00);
                
                if (id == 0xFFFFFFFF || id == 0x00000000) [[likely]] {
                    continue;
                }

                uint32_t class_reg = mk_snd_pci_read32(bus, dev, func, 0x08);
                uint8_t base_class = (class_reg >> 24) & 0xFF;
                uint8_t sub_class = (class_reg >> 16) & 0xFF;

                // Stop at the first HDA controller.
                if (base_class == MK_SND_PCI_CLASS_MULTIMEDIA && sub_class == MK_SND_PCI_SUBCLASS_HDA) [[unlikely]] {
                    mk_snd_pci_bus = bus;
                    mk_snd_pci_dev = dev;
                    mk_snd_pci_func = func;

                    return 0;
                }

                // If not multifunction, remaining functions are invalid per PCI spec.
                if (func == 0) [[likely]] {
                    uint8_t header_type = (mk_snd_pci_read32(bus, dev, 0, 0x0C) >> 16) & 0xFF;
                    if (!(header_type & 0x80)) [[likely]] {
                        break; 
                    }
                }
            }
        }
    }

    return -1;
}

// Mira Kernel Sound Alloc Aligned
// Mira malloc is not cache-line aware yet, so 
// manual alignment is needed for DMA engines.
static void *mk_snd_alloc_aligned(size_t size, size_t align) {
    void *raw = mk_malloc(size + align);

    if (!raw) [[unlikely]] {
        return NULL;
    }

    uintptr_t addr = (uintptr_t)raw;
    uintptr_t aligned = (addr + align - 1) & ~(align - 1);

    return (void *)aligned;
}

// Mira Kernel Sound Stream Reset
// Hardware state machine needs clock cycles to latch the reset bit.
static int mk_snd_stream_reset(void) {
    uint8_t ctl = mk_snd_sd_reg8(MK_SND_HDA_SD_CTL);

    // Stop engine first. Modifying reset while RUN=1 is undefined behavior.
    if (ctl & MK_SND_HDA_SDCTL_RUN) [[unlikely]] {
        mk_snd_sd_write8(MK_SND_HDA_SD_CTL, ctl & ~MK_SND_HDA_SDCTL_RUN);

        for (int i = 0; i < MK_SND_HDA_MAX_TIMEOUT_MS; i++) {
            if (!(mk_snd_sd_reg8(MK_SND_HDA_SD_CTL) & MK_SND_HDA_SDCTL_RUN)) [[likely]] {
                break;
            }

            mk_util_port_delay(1);
        }
    }

    // Assert reset.
    mk_snd_sd_write8(MK_SND_HDA_SD_CTL, MK_SND_HDA_SDCTL_SRST);
    for (int i = 0; i < MK_SND_HDA_MAX_TIMEOUT_MS; i++) {
        if (mk_snd_sd_reg8(MK_SND_HDA_SD_CTL) & MK_SND_HDA_SDCTL_SRST) [[likely]] {
            break;
        }

        mk_util_port_delay(1);
    }

    if (!(mk_snd_sd_reg8(MK_SND_HDA_SD_CTL) & MK_SND_HDA_SDCTL_SRST)) [[unlikely]] {
        return -1; // Hardware stuck.
    }

    // Deassert reset.
    mk_snd_sd_write8(MK_SND_HDA_SD_CTL, 0);
    for (int i = 0; i < MK_SND_HDA_MAX_TIMEOUT_MS; i++) {
        if (!(mk_snd_sd_reg8(MK_SND_HDA_SD_CTL) & MK_SND_HDA_SDCTL_SRST)) [[likely]] {
            break;
        }

        mk_util_port_delay(1);
    }

    if (mk_snd_sd_reg8(MK_SND_HDA_SD_CTL) & MK_SND_HDA_SDCTL_SRST) [[unlikely]] {
        return -1; // Reset line is stuck high.
    }

    // Clear sticky status bits (write-1-to-clear) so everything starts fresh.
    mk_snd_sd_write8(MK_SND_HDA_SD_STS, MK_SND_HDA_SDSTS_BCIS | MK_SND_HDA_SDSTS_FIFOE | MK_SND_HDA_SDSTS_DESE);

    return 0;
}

// Mira Kernel Sound CORB & RIRB Init
// Prepare both Command and Response rings.
static int mk_snd_corb_rirb_init(void) {
    // Stop DMA before touching pointers.
    mk_snd_hda_write8(MK_SND_HDA_REG_CORBCTL, mk_snd_hda_reg8(MK_SND_HDA_REG_CORBCTL) & ~MK_SND_HDA_CORBCTL_RUN);
    for (int i = 0; i < MK_SND_HDA_MAX_TIMEOUT_MS; i++) {
        if (!(mk_snd_hda_reg8(MK_SND_HDA_REG_CORBCTL) & MK_SND_HDA_CORBCTL_RUN)) [[likely]] {
            break;
        }

        mk_util_port_delay(1);
    }

    // Negotiate size. QEMU supports 256.

    uint8_t corbsize_reg = mk_snd_hda_reg8(MK_SND_HDA_REG_CORBSIZE);
    uint8_t corbsize_cap = (corbsize_reg >> 4) & 0x0F;

    if (corbsize_cap & 0x04) {
        hda.corb_entries = 256;
        mk_snd_hda_write8(MK_SND_HDA_REG_CORBSIZE, (corbsize_reg & 0xFC) | 0x02);
    } else if (corbsize_cap & 0x02) {
        hda.corb_entries = 16;
        mk_snd_hda_write8(MK_SND_HDA_REG_CORBSIZE, (corbsize_reg & 0xFC) | 0x01);
    } else {
        hda.corb_entries = 2;
        mk_snd_hda_write8(MK_SND_HDA_REG_CORBSIZE, (corbsize_reg & 0xFC) | 0x00);
    }

    // 128-byte alignment required by HDA spec for all ring buffers.
    hda.corb = (uint32_t *)mk_snd_alloc_aligned(hda.corb_entries * sizeof(uint32_t), 128);
    if (!hda.corb) [[unlikely]] {
        return -1;
    }

    mk_memset(hda.corb, 0, hda.corb_entries * sizeof(uint32_t));

    uintptr_t corb_phys = (uintptr_t)hda.corb;
    mk_snd_hda_write32(MK_SND_HDA_REG_CORBLBASE, (uint32_t)(corb_phys & 0xFFFFFFFF));
    mk_snd_hda_write32(MK_SND_HDA_REG_CORBUBASE, (uint32_t)(corb_phys >> 32));

    // Reset Read Pointer. Must write 1 to bit 15.
    mk_snd_hda_write16(MK_SND_HDA_REG_CORBRP, MK_SND_HDA_CORBRP_RST);
    for (int i = 0; i < MK_SND_HDA_MAX_TIMEOUT_MS; i++) {
        if (!(mk_snd_hda_reg16(MK_SND_HDA_REG_CORBRP) & MK_SND_HDA_CORBRP_RST)) [[likely]] {
            break;
        }

        mk_util_port_delay(1);
    }

    // Clear reset bit.
    mk_snd_hda_write16(MK_SND_HDA_REG_CORBRP, 0);
    for (int i = 0; i < MK_SND_HDA_MAX_TIMEOUT_MS; i++) {
        if (!(mk_snd_hda_reg16(MK_SND_HDA_REG_CORBRP) & MK_SND_HDA_CORBRP_RST)) [[likely]] {
            break;
        }

        mk_util_port_delay(1);
    }

    mk_snd_hda_write16(MK_SND_HDA_REG_CORBWP, 0);

    mk_snd_hda_write8(MK_SND_HDA_REG_CORBCTL, MK_SND_HDA_CORBCTL_RUN);
    for (int i = 0; i < MK_SND_HDA_MAX_TIMEOUT_MS; i++) {
        if (mk_snd_hda_reg8(MK_SND_HDA_REG_CORBCTL) & MK_SND_HDA_CORBCTL_RUN) [[likely]] {
            break;
        }

        mk_util_port_delay(1);
    }

    // Setup RIRB

    mk_snd_hda_write8(MK_SND_HDA_REG_RIRBCTL, mk_snd_hda_reg8(MK_SND_HDA_REG_RIRBCTL) & ~MK_SND_HDA_RIRBCTL_RUN);
    for (int i = 0; i < MK_SND_HDA_MAX_TIMEOUT_MS; i++) {
        if (!(mk_snd_hda_reg8(MK_SND_HDA_REG_RIRBCTL) & MK_SND_HDA_RIRBCTL_RUN)) [[likely]] {
            break;
        }

        mk_util_port_delay(1);
    }

    uint8_t rirbsize_reg = mk_snd_hda_reg8(MK_SND_HDA_REG_RIRBSIZE);
    uint8_t rirbsize_cap = (rirbsize_reg >> 4) & 0x0F;

    if (rirbsize_cap & 0x04) {
        hda.rirb_entries = 256;
        mk_snd_hda_write8(MK_SND_HDA_REG_RIRBSIZE, (rirbsize_reg & 0xFC) | 0x02);
    } else if (rirbsize_cap & 0x02) {
        hda.rirb_entries = 16;
        mk_snd_hda_write8(MK_SND_HDA_REG_RIRBSIZE, (rirbsize_reg & 0xFC) | 0x01);
    } else {
        hda.rirb_entries = 2;
        mk_snd_hda_write8(MK_SND_HDA_REG_RIRBSIZE, (rirbsize_reg & 0xFC) | 0x00);
    }

    hda.rirb = (mk_snd_hda_rirb_entry_t *)mk_snd_alloc_aligned(hda.rirb_entries * sizeof(mk_snd_hda_rirb_entry_t), 128);
    if (!hda.rirb) [[unlikely]] {
        return -1;
    }

    mk_memset(hda.rirb, 0, hda.rirb_entries * sizeof(mk_snd_hda_rirb_entry_t));

    uintptr_t rirb_phys = (uintptr_t)hda.rirb;
    mk_snd_hda_write32(MK_SND_HDA_REG_RIRBLBASE, (uint32_t)(rirb_phys & 0xFFFFFFFF));
    mk_snd_hda_write32(MK_SND_HDA_REG_RIRBUBASE, (uint32_t)(rirb_phys >> 32));

    // Reset Write Pointer.
    mk_snd_hda_write16(MK_SND_HDA_REG_RIRBWP, (1 << 15));

    hda.rirb_rp = 0;

    // Interrupt after 1 response to keep latency low.
    mk_snd_hda_write16(MK_SND_HDA_REG_RINTCNT, 1);

    // Clear stale status.
    mk_snd_hda_write8(MK_SND_HDA_REG_RIRBSTS, MK_SND_HDA_RIRBSTS_RINTFL | (1 << 2));

    // INT bit required for RINTFL updates, even if Mira is polling.
    mk_snd_hda_write8(MK_SND_HDA_REG_RIRBCTL, MK_SND_HDA_RIRBCTL_RUN | MK_SND_HDA_RIRBCTL_INT);
    for (int i = 0; i < MK_SND_HDA_MAX_TIMEOUT_MS; i++) {
        if (mk_snd_hda_reg8(MK_SND_HDA_REG_RIRBCTL) & MK_SND_HDA_RIRBCTL_RUN) [[likely]] {
            break;
        }

        mk_util_port_delay(1);
    }

    return 0;
}

// * Verbs * //

// 12-bit ID, 8-bit Payload.
static uint32_t mk_snd_make_verb12(uint8_t codec, uint8_t nid, uint16_t verb, uint8_t param) {
    return ((uint32_t)codec << 28) | ((uint32_t)nid << 20) | ((uint32_t)(verb & 0xFFF) << 8) | param;
}

// 4-bit ID, 16-bit Payload.
static uint32_t mk_snd_make_verb4(uint8_t codec, uint8_t nid, uint8_t verb, uint16_t param) {
    return ((uint32_t)codec << 28) | ((uint32_t)nid << 20) | ((uint32_t)(verb & 0xF) << 16) | param;
}

// Mira Kernel Sound Codec Verb
// Polling RIRB. Interrupts would be better, but this is fine for Mira.
static int mk_snd_codec_verb(uint32_t verb, uint32_t *response) {
    uint16_t wp = mk_snd_hda_reg16(MK_SND_HDA_REG_CORBWP) & 0xFF; // Valid in lower 8 bits.
    uint16_t next_wp = (wp + 1) % hda.corb_entries;

    hda.corb[next_wp] = verb;
    mk_snd_hda_write16(MK_SND_HDA_REG_CORBWP, next_wp);

    // Poll for hardware to writeback response.
    for (int i = 0; i < MK_SND_HDA_MAX_TIMEOUT_MS; i++) {
        if (mk_snd_hda_reg8(MK_SND_HDA_REG_RIRBSTS) & MK_SND_HDA_RIRBSTS_RINTFL) [[likely]] {
            uint16_t rp = (hda.rirb_rp + 1) % hda.rirb_entries;

            if (response) {
                *response = hda.rirb[rp].response;
            }

            hda.rirb_rp = rp;

            // Clear the flag.
            mk_snd_hda_write8(MK_SND_HDA_REG_RIRBSTS, MK_SND_HDA_RIRBSTS_RINTFL);

            return 0;
        }

        mk_util_port_delay(1);
    }

    return -1; // Timeout.
}

// * Codec Wrappers * // 

static int mk_snd_codec_get_param(uint8_t nid, uint8_t param, uint32_t *out) {
    uint32_t verb = mk_snd_make_verb12(hda.codec_addr, nid, MK_SND_HDA_VERB_GET_PARAM, param);
    return mk_snd_codec_verb(verb, out);
}

static int mk_snd_codec_set_verb12(uint8_t nid, uint16_t verb_id, uint8_t payload) {
    uint32_t verb = mk_snd_make_verb12(hda.codec_addr, nid, verb_id, payload);
    return mk_snd_codec_verb(verb, NULL);
}

static int mk_snd_codec_set_verb4(uint8_t nid, uint8_t verb_id, uint16_t payload) {
    uint32_t verb = mk_snd_make_verb4(hda.codec_addr, nid, verb_id, payload);
    return mk_snd_codec_verb(verb, NULL);
}

// * Graph Walking * //

// Mira Kernel Sound Widget Type
static int mk_snd_widget_type(uint8_t nid) {
    uint32_t cap;

    if (mk_snd_codec_get_param(nid, MK_SND_HDA_PARAM_AUDIO_WIDGET_CAP, &cap) < 0) [[unlikely]] {
        return -1;
    }

    return (cap >> 20) & 0xF;
}

// Mira Kernel Sound Has Out Amp
static int mk_snd_has_out_amp(uint8_t nid) {
    uint32_t cap;

    if (mk_snd_codec_get_param(nid, MK_SND_HDA_PARAM_AUDIO_WIDGET_CAP, &cap) < 0) [[unlikely]] {
        return 0;
    }

    return (cap >> 2) & 1;
}

// Mira Kernel Sound Set Out Amp
static void mk_snd_set_out_amp(uint8_t nid, int gain_pct) {
    uint32_t amp_cap;

    if (mk_snd_codec_get_param(nid, MK_SND_HDA_PARAM_OUT_AMP_CAP, &amp_cap) < 0) [[unlikely]] {
        return;
    }

    uint8_t num_steps = (amp_cap >> 8) & 0x7F;
    uint8_t gain = (num_steps > 0) ? (uint8_t)((num_steps * gain_pct) / 100) : 0;

    // Payload: [15] Set Output; [14] Set Input; [13] Left; [12] Right; [6:0] Gain
    uint16_t payload = (1 << 15) | (1 << 13) | (1 << 12) | gain;
    mk_snd_codec_set_verb4(nid, MK_SND_HDA_VERB_SET_AMP_GAIN_MUTE, payload);
}

// Mira Kernel Sound Probe Codec
// Intel HDA codecs are structured as a directed graph of "widgets" (nodes).
// To play sound, Mira needs to find a valid path from a Digital-to-Analog Converter
// (DAC) to a Pin Complex (the physical Jack or Speaker). This function finds the
// Audio Function Group, identifies all available Audio Output widgets (DACs), and
// finds a Pin Complex that is connected to one of those DACs.
static int mk_snd_probe_codec(void) {
    uint32_t val;

    if (mk_snd_codec_get_param(0, MK_SND_HDA_PARAM_SUBNODE_COUNT, &val) < 0) [[unlikely]] {
        return -1;
    }

    uint8_t fg_start = (val >> 16) & 0xFF;
    uint8_t fg_count = val & 0xFF;

    hda.afg_nid = 0;

    // Find Audio Function Group (0x01).
    for (uint8_t i = 0; i < fg_count; i++) {
        uint8_t nid = fg_start + i;

        if (mk_snd_codec_get_param(nid, MK_SND_HDA_PARAM_FN_GROUP_TYPE, &val) < 0) {
            continue;
        }

        if ((val & 0xFF) == 0x01) {
            hda.afg_nid = nid;
            break;
        }
    }

    if (hda.afg_nid == 0) [[unlikely]] {
        return -1;
    }

    mk_snd_codec_set_verb12(hda.afg_nid, MK_SND_HDA_VERB_SET_POWER_STATE, 0x00); // D0 (Active).
    mk_util_port_delay(20); // Needs time to transition from D3 (Power Off) to D0 (Active).

    if (mk_snd_codec_get_param(hda.afg_nid, MK_SND_HDA_PARAM_SUBNODE_COUNT, &val) < 0) [[unlikely]] {
        return -1;
    }

    uint8_t w_start = (val >> 16) & 0xFF;
    uint8_t w_count = val & 0xFF;

    if (w_count == 0) [[unlikely]] {
        return -1;
    }

    // Power on everything.
    // Optimizing this to only power used paths is a TODO for later.

    uint8_t dac_nids[16];
    uint8_t num_dacs = 0;

    for (uint8_t i = 0; i < w_count; i++) {
        uint8_t nid = w_start + i;

        mk_snd_codec_set_verb12(nid, MK_SND_HDA_VERB_SET_POWER_STATE, 0x00);
        mk_util_port_delay(2);

        int wtype = mk_snd_widget_type(nid);

        if (wtype == MK_SND_HDA_WIDGET_AUD_OUT && num_dacs < 16) {
            dac_nids[num_dacs++] = nid;
        }
    }

    if (num_dacs == 0) [[unlikely]] {
        return -1;
    }

    // Find a pin complex that is actually connected to something.

    hda.dac_nid = 0;
    hda.pin_nid = 0;
    hda.dac_conn_idx = 0;

    for (uint8_t i = 0; i < w_count; i++) {
        uint8_t nid = w_start + i;

        if (mk_snd_widget_type(nid) != MK_SND_HDA_WIDGET_PIN) {
            continue;
        }

        uint32_t config;
        uint32_t cfg_verb = mk_snd_make_verb12(hda.codec_addr, nid, MK_SND_HDA_VERB_GET_CONFIG_DEFAULT, 0);

        if (mk_snd_codec_verb(cfg_verb, &config) < 0) {
            continue;
        }

        uint8_t device = (config >> 20) & 0xF;
        uint8_t connectivity = (config >> 30) & 0x3;

        // Skip NC pins and non-output devices (only LineOut/Speaker/HP).
        if (connectivity == 0x1 || device > 0x2) {
            continue;
        }

        // Skip pins without output capability (bit 4) or connection list.
        uint32_t pin_cap;
        if (
            mk_snd_codec_get_param(nid, MK_SND_HDA_PARAM_PIN_CAP, &pin_cap) < 0 ||
            !(pin_cap & (1 << 4)) ||
            mk_snd_codec_get_param(nid, MK_SND_HDA_PARAM_CONN_LIST_LEN, &val) < 0
        ) {
            continue;
        }

        uint8_t conn_len = val & 0x7F;
        if (conn_len == 0) {
            continue;
        }

        // Check if this pin can reach one of the DACs.
        for (uint8_t j = 0; j < conn_len; j += 4) {
            uint32_t entry;
            uint32_t cl_verb = mk_snd_make_verb12(hda.codec_addr, nid, MK_SND_HDA_VERB_GET_CONN_LIST, j);

            if (mk_snd_codec_verb(cl_verb, &entry) < 0) {
                break;
            }

            for (int k = 0; k < 4 && (j + k) < conn_len; k++) {
                uint8_t candidate_nid = (entry >> (k * 8)) & 0xFF;

                for (int d = 0; d < num_dacs; d++) {
                    if (candidate_nid == dac_nids[d] && candidate_nid != 0) {
                        hda.pin_nid = nid;
                        hda.dac_nid = dac_nids[d];
                        hda.dac_conn_idx = j + k; // Store which input on the Pin mux points to the DAC.

                        goto found_path;
                    }
                }
            }
        }
    }

found_path:
    // Fallback: If no valid path found, assume first DAC + first output pin.
    if (hda.pin_nid == 0 || hda.dac_nid == 0) [[unlikely]] {
        hda.dac_nid = dac_nids[0];
        hda.dac_conn_idx = 0;

        for (uint8_t i = 0; i < w_count; i++) {
            uint8_t nid = w_start + i;

            if (mk_snd_widget_type(nid) != MK_SND_HDA_WIDGET_PIN) {
                continue;
            }

            uint32_t pin_cap;

            if (mk_snd_codec_get_param(nid, MK_SND_HDA_PARAM_PIN_CAP, &pin_cap) < 0) {
                continue;
            }

            if (pin_cap & (1 << 4)) {
                hda.pin_nid = nid;
                break;
            }
        }
    }

    if (hda.pin_nid == 0 || hda.dac_nid == 0) [[unlikely]] {
        return -1;
    }

    return 0;
}

// Mira Kernel Sound Configure Output
// Sets the output format and volume.
static int mk_snd_configure_output(void) {
    if (mk_snd_codec_set_verb4(hda.dac_nid, MK_SND_HDA_VERB_SET_CONV_FORMAT, MK_SND_HDA_FMT_48KHZ_16BIT_MONO) < 0) [[unlikely]] {
        return -1;
    }

    // Assign Stream 1, Channel 0.
    if (mk_snd_codec_set_verb12(hda.dac_nid, MK_SND_HDA_VERB_SET_CONV_CTRL, (hda.stream_tag << 4) | 0x00) < 0) [[unlikely]] {
        return -1;
    }

    // Force 90% gain. Default is too quiet (or unknown).
    if (mk_snd_has_out_amp(hda.dac_nid)) {
        mk_snd_set_out_amp(hda.dac_nid, 90);
    }

    if (mk_snd_codec_set_verb12(hda.pin_nid, MK_SND_HDA_VERB_SET_CONN_SELECT, hda.dac_conn_idx) < 0) [[unlikely]] {
        return -1;
    }

    if (mk_snd_has_out_amp(hda.pin_nid)) {
        mk_snd_set_out_amp(hda.pin_nid, 90);
    }

    // External Amplifier Power Down (EAPD).
    // Required for internal speakers on many laptops (ThinkPads, Dell Latitude).
    uint32_t eapd_val;
    uint32_t eapd_verb = mk_snd_make_verb12(hda.codec_addr, hda.pin_nid, MK_SND_HDA_VERB_GET_EAPD_BTL, 0);

    if (mk_snd_codec_verb(eapd_verb, &eapd_val) == 0) {
        mk_snd_codec_set_verb12(hda.pin_nid, MK_SND_HDA_VERB_SET_EAPD_BTL, (eapd_val & 0xFF) | MK_SND_HDA_EAPD_BTL_ENABLE);
    }

    uint32_t cur_pin_ctrl;
    uint32_t pin_verb = mk_snd_make_verb12(hda.codec_addr, hda.pin_nid, MK_SND_HDA_VERB_GET_PIN_CTRL, 0);

    // Enable Output + HP Amp.
    if (mk_snd_codec_verb(pin_verb, &cur_pin_ctrl) == 0) {
        mk_snd_codec_set_verb12(
            hda.pin_nid,
            MK_SND_HDA_VERB_SET_PIN_CTRL,
            (cur_pin_ctrl & 0xFF) | MK_SND_HDA_PIN_CTRL_OUT_EN | MK_SND_HDA_PIN_CTRL_HP_EN
        );
    } else {
        mk_snd_codec_set_verb12(hda.pin_nid, MK_SND_HDA_VERB_SET_PIN_CTRL, MK_SND_HDA_PIN_CTRL_OUT_EN | MK_SND_HDA_PIN_CTRL_HP_EN);
    }

    return 0;
}

// * Stream Setup * //

// Mira Kernel Sound Stream Setup
// Sets up the stream for audio playback.
static int mk_snd_stream_setup(void) {
    uint16_t gcap = mk_snd_hda_reg16(MK_SND_HDA_REG_GCAP);
    uint8_t num_iss = (gcap >> 8) & 0x0F;
    uint8_t num_oss = (gcap >> 12) & 0x0F;

    if (num_oss == 0) [[unlikely]] {
        return -1;
    }

    // Output streams follow input streams in memory map.
    hda.stream_index = num_iss;
    hda.stream_tag = 1;

    // The spec says that stream descriptors start at 0x80.
    // Each block is 0x20 bytes. Skip input streams to find output.
    hda.sd_base = hda.mmio + 0x80 + (hda.stream_index * 0x20);

    if (mk_snd_stream_reset() < 0) [[unlikely]] {
        return -1;
    }

    hda.dma_buf_size = MK_SND_HDA_DMA_BUF_SIZE;
    hda.dma_buf = (uint8_t *)mk_snd_alloc_aligned(hda.dma_buf_size, 128);

    if (!hda.dma_buf) [[unlikely]] {
        return -1;
    }

    mk_memset(hda.dma_buf, 0, hda.dma_buf_size);

    hda.bdl = (mk_snd_hda_bdl_entry_t *)mk_snd_alloc_aligned(MK_SND_HDA_BDL_ENTRIES * sizeof(mk_snd_hda_bdl_entry_t), 128);

    if (!hda.bdl) [[unlikely]] {
        return -1;
    }

    mk_memset(hda.bdl, 0, MK_SND_HDA_BDL_ENTRIES * sizeof(mk_snd_hda_bdl_entry_t));

    hda.bdl[0].address = (uintptr_t)hda.dma_buf;
    hda.bdl[0].length = hda.dma_buf_size;
    hda.bdl[0].ioc = 1; // Interrupt on Completion.

    mk_snd_sd_write32(MK_SND_HDA_SD_BDLPL, (uint32_t)((uintptr_t)hda.bdl & 0xFFFFFFFF));
    mk_snd_sd_write32(MK_SND_HDA_SD_BDLPU, (uint32_t)((uintptr_t)hda.bdl >> 32));
    mk_snd_sd_write32(MK_SND_HDA_SD_CBL, hda.dma_buf_size);
    mk_snd_sd_write16(MK_SND_HDA_SD_LVI, MK_SND_HDA_BDL_ENTRIES - 1);
    mk_snd_sd_write16(MK_SND_HDA_SD_FMT, MK_SND_HDA_FMT_48KHZ_16BIT_MONO);

    // Stream tag goes in bits [7:4] of offset 0x2.
    mk_snd_sd_write8(MK_SND_HDA_SD_CTL + 2, (hda.stream_tag << 4));

    return 0;
}

// Mira Kernel Sound Controller Reset
// Resets the controller for playback.
static int mk_snd_controller_reset(void) {
    // Assert reset (0).
    mk_snd_hda_write32(MK_SND_HDA_REG_GCTL, mk_snd_hda_reg32(MK_SND_HDA_REG_GCTL) & ~MK_SND_HDA_GCTL_CRST);

    for (int i = 0; i < MK_SND_HDA_MAX_TIMEOUT_MS; i++) {
        if (!(mk_snd_hda_reg32(MK_SND_HDA_REG_GCTL) & MK_SND_HDA_GCTL_CRST)) [[likely]] {
            break;
        }

        mk_util_port_delay(1);
    }

    if (mk_snd_hda_reg32(MK_SND_HDA_REG_GCTL) & MK_SND_HDA_GCTL_CRST) [[unlikely]] {
        return -1; // Reset failed.
    }

    mk_util_port_delay(10);

    // Deassert reset (1).
    mk_snd_hda_write32(MK_SND_HDA_REG_GCTL, mk_snd_hda_reg32(MK_SND_HDA_REG_GCTL) | MK_SND_HDA_GCTL_CRST);

    for (int i = 0; i < MK_SND_HDA_MAX_TIMEOUT_MS; i++) {
        if (mk_snd_hda_reg32(MK_SND_HDA_REG_GCTL) & MK_SND_HDA_GCTL_CRST) [[likely]] {
            break;
        }

        mk_util_port_delay(1);
    }

    if (!(mk_snd_hda_reg32(MK_SND_HDA_REG_GCTL) & MK_SND_HDA_GCTL_CRST)) [[unlikely]] {
        return -1;
    }

    // Required for RINTFL updates.
    mk_snd_hda_write32(MK_SND_HDA_REG_INTCTL, MK_SND_HDA_INTCTL_GIE | MK_SND_HDA_INTCTL_CIE);

    // Wait for codec wake up (STATESTS).
    mk_util_port_delay(50);
    for (int i = 0; i < MK_SND_HDA_MAX_TIMEOUT_MS; i++) {
        if (mk_snd_hda_reg16(MK_SND_HDA_REG_STATESTS)) [[likely]] {
            break;
        }

        mk_util_port_delay(1);
    }

    uint16_t statests = mk_snd_hda_reg16(MK_SND_HDA_REG_STATESTS);

    if (statests == 0) [[unlikely]] {
        return -1;
    }

    // Find the first available codec.
    // QEMU should be 0 per testing.
    for (uint8_t i = 0; i < 15; i++) {
        if (statests & (1 << i)) {
            hda.codec_addr = i;
            break;
        }
    }

    // Clear STATESTS (write-1-to-clear).
    mk_snd_hda_write16(MK_SND_HDA_REG_STATESTS, statests);

    return 0;
}

// * Public API * //

// Mira Kernel Sound Init
// Initializes the sound card.
int mk_snd_init(void) {
    mk_memset(&hda, 0, sizeof(hda));

    if (mk_snd_pci_find_hda() < 0) [[unlikely]] {
        return -1;
    }

    // HDA is Type 00h (Normal) header, so BAR0 is at 0x10.
    uint32_t bar0 = mk_snd_pci_read32(mk_snd_pci_bus, mk_snd_pci_dev, mk_snd_pci_func, 0x10);

    if (bar0 & 1) [[unlikely]] {
        return -1; // IO Space not supported.
    }

    uint64_t mmio_base = bar0 & 0xFFFFFFF0;

    // Check for 64-bit BAR type (10b).
    if (((bar0 >> 1) & 0x3) == 0x2) {
        uint32_t bar1 = mk_snd_pci_read32(mk_snd_pci_bus, mk_snd_pci_dev, mk_snd_pci_func, 0x14);
        mmio_base |= ((uint64_t)bar1 << 32);
    }

    hda.mmio = (volatile uint8_t *)mmio_base;

    // Enable the master bus and memory space.
    uint16_t cmd = mk_snd_pci_read16(mk_snd_pci_bus, mk_snd_pci_dev, mk_snd_pci_func, 0x04);
    cmd |= MK_SND_PCI_CMD_MEM_SPACE | MK_SND_PCI_CMD_BUS_MASTER;
    mk_snd_pci_write16(mk_snd_pci_bus, mk_snd_pci_dev, mk_snd_pci_func, 0x04, cmd);

    if (
        mk_snd_controller_reset() < 0 ||
        mk_snd_corb_rirb_init() < 0 ||
        mk_snd_probe_codec() < 0 ||
        mk_snd_stream_setup() < 0 ||
        mk_snd_configure_output() < 0
    ) {
        return -1;
    }

    hda.initialized = 1;

    return 0;
}

// Mira Kernel Sound Play
// Plays audio data.
// TODO: Support streaming better, as this function resets the stream and 
// stops at the end per call. Both are currently commented out so that the
// audio testing with UDP packets sounds smooth (works, but still check this).
int mk_snd_play(const void *data, uint32_t size) {
    if (!hda.initialized || size == 0 || data == NULL) [[unlikely]] {
        return -1;
    }

    if (size > hda.dma_buf_size) {
        size = hda.dma_buf_size;
    }

    // if (mk_snd_stream_reset() < 0) [[unlikely]] {
    //     return -1;
    // }

    mk_memcpy(hda.dma_buf, data, size);

    // Silence the rest of the buffer.
    if (size < hda.dma_buf_size) {
        mk_memset(hda.dma_buf + size, 0, hda.dma_buf_size - size);
    }

    hda.bdl[0].length = size;
    hda.bdl[0].ioc = 1;

    // Program stream descriptor: BDL address, buffer size, format, and start DMA.
    mk_snd_sd_write32(MK_SND_HDA_SD_BDLPL, (uint32_t)((uintptr_t)hda.bdl & 0xFFFFFFFF));
    mk_snd_sd_write32(MK_SND_HDA_SD_BDLPU, (uint32_t)((uintptr_t)hda.bdl >> 32));
    mk_snd_sd_write32(MK_SND_HDA_SD_CBL, size);
    mk_snd_sd_write16(MK_SND_HDA_SD_LVI, 0);
    mk_snd_sd_write16(MK_SND_HDA_SD_FMT, MK_SND_HDA_FMT_48KHZ_16BIT_MONO);

    mk_snd_sd_write8(MK_SND_HDA_SD_CTL + 2, (hda.stream_tag << 4));
    mk_snd_sd_write8(MK_SND_HDA_SD_CTL, mk_snd_sd_reg8(MK_SND_HDA_SD_CTL) | MK_SND_HDA_SDCTL_RUN);

    // Blocking playback. A TODO is to get this setup with interrupts/scheduling.
    for (int i = 0; i < MK_SND_HDA_MAX_TIMEOUT_MS * 10; i++) {
        if (mk_snd_sd_reg8(MK_SND_HDA_SD_STS) & MK_SND_HDA_SDSTS_BCIS) [[likely]] {
            break;
        }

        mk_util_port_delay(1);
    }

    // Stop.
    // mk_snd_sd_write8(MK_SND_HDA_SD_CTL, mk_snd_sd_reg8(MK_SND_HDA_SD_CTL) & ~MK_SND_HDA_SDCTL_RUN);
    // for (int i = 0; i < MK_SND_HDA_MAX_TIMEOUT_MS; i++) {
    //     if (!(mk_snd_sd_reg8(MK_SND_HDA_SD_CTL) & MK_SND_HDA_SDCTL_RUN)) [[likely]] {
    //         break;
    //     }

    //     mk_util_port_delay(1);
    // }

    mk_snd_sd_write8(MK_SND_HDA_SD_STS, MK_SND_HDA_SDSTS_BCIS | MK_SND_HDA_SDSTS_FIFOE | MK_SND_HDA_SDSTS_DESE);

    return 0;
}