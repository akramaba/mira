// * Specification: https://www.intel.com/content/dam/www/public/us/en/documents/product-specifications/high-definition-audio-specification.pdf

#ifndef SOUND_H
#define SOUND_H

#include <stdint.h>
#include <stddef.h>

// * PCI * //

#define MK_SND_PCI_CONFIG_ADDR 0x0CF8
#define MK_SND_PCI_CONFIG_DATA 0x0CFC

// Scan for Class 04h / Subclass 03h to find the HDA controller
#define MK_SND_PCI_CLASS_MULTIMEDIA 0x04
#define MK_SND_PCI_SUBCLASS_HDA     0x03

#define MK_SND_PCI_CMD_IO_SPACE (1 << 0)
#define MK_SND_PCI_CMD_MEM_SPACE (1 << 1)
#define MK_SND_PCI_CMD_BUS_MASTER (1 << 2)

// * HDA Registers  * //
// Offsets are relative to the BAR0 MMIO base.

#define MK_SND_HDA_REG_GCAP 0x00
#define MK_SND_HDA_REG_VMIN 0x02
#define MK_SND_HDA_REG_VMAJ 0x03
#define MK_SND_HDA_REG_OUTPAY 0x04
#define MK_SND_HDA_REG_INPAY 0x06
#define MK_SND_HDA_REG_GCTL 0x08
#define MK_SND_HDA_REG_WAKEEN 0x0C
#define MK_SND_HDA_REG_STATESTS 0x0E

#define MK_SND_HDA_REG_INTCTL 0x20
#define MK_SND_HDA_REG_INTSTS 0x24

#define MK_SND_HDA_REG_WALCLK 0x30 // Ticks at 24MHz. Useful if Mira ever needs it.
#define MK_SND_HDA_REG_SSYNC 0x38

// GCTL bits
#define MK_SND_HDA_GCTL_CRST (1 << 0) // 0->1 transition requires polling until hardware acknowledges.
#define MK_SND_HDA_GCTL_UNSOL (1 << 8)

// INTCTL bits
#define MK_SND_HDA_INTCTL_GIE (1u << 31)
#define MK_SND_HDA_INTCTL_CIE (1u << 30)

// * CORB (Command Outbound Ring Buffer) * //
// Ring buffer for sending commands to the codec.
// Driver writes to tail, hardware reads from head.

#define MK_SND_HDA_REG_CORBLBASE 0x40
#define MK_SND_HDA_REG_CORBUBASE 0x44
#define MK_SND_HDA_REG_CORBWP 0x48
#define MK_SND_HDA_REG_CORBRP 0x4A
#define MK_SND_HDA_REG_CORBCTL 0x4C
#define MK_SND_HDA_REG_CORBSTS 0x4D
#define MK_SND_HDA_REG_CORBSIZE 0x4E

#define MK_SND_HDA_CORBCTL_RUN (1 << 1)
#define MK_SND_HDA_CORBRP_RST (1 << 15) // Write 1 to reset read pointer.

// * RIRB (Response Inbound Ring Buffer) * //
// Ring buffer for receiving responses.

#define MK_SND_HDA_REG_RIRBLBASE 0x50
#define MK_SND_HDA_REG_RIRBUBASE 0x54
#define MK_SND_HDA_REG_RIRBWP 0x58
#define MK_SND_HDA_REG_RINTCNT 0x5A // Interrupt after N responses.
#define MK_SND_HDA_REG_RIRBCTL 0x5C
#define MK_SND_HDA_REG_RIRBSTS 0x5D
#define MK_SND_HDA_REG_RIRBSIZE 0x5E

#define MK_SND_HDA_RIRBCTL_RUN (1 << 1)
#define MK_SND_HDA_RIRBCTL_INT (1 << 0)
#define MK_SND_HDA_RIRBSTS_RINTFL (1 << 0)

// * Stream Descriptor Registers * //

#define MK_SND_HDA_SD_CTL 0x00
#define MK_SND_HDA_SD_STS 0x03
#define MK_SND_HDA_SD_LPIB 0x04 // Hardware updates this with current DMA position.
#define MK_SND_HDA_SD_CBL 0x08 // Cyclic Buffer Length (DMA buffer size).
#define MK_SND_HDA_SD_LVI 0x0C // Last Valid Index (BDL entry count - 1).
#define MK_SND_HDA_SD_FIFOS 0x10
#define MK_SND_HDA_SD_FMT 0x12
#define MK_SND_HDA_SD_BDLPL 0x18 // 128-byte aligned.
#define MK_SND_HDA_SD_BDLPU 0x1C

// SD CTL
#define MK_SND_HDA_SDCTL_RUN (1 << 1) // DMA Engine Start.
#define MK_SND_HDA_SDCTL_IOCE (1 << 2) // Global switch for IOC in BDL entries.
#define MK_SND_HDA_SDCTL_SRST (1 << 0) // Immediate stream reset (check for 0 bit after set).
#define MK_SND_HDA_SDCTL_STRM_SHIFT 20

// SD STS
#define MK_SND_HDA_SDSTS_BCIS (1 << 2) // Buffer Complete (IOC fired).
#define MK_SND_HDA_SDSTS_FIFOE (1 << 3) // FIFO Underrun (DMA too slow).
#define MK_SND_HDA_SDSTS_DESE (1 << 4)

// * Stream Format Encoding * //
// Works on QEMU. Uses complex bit-packing to give us 48kHz 16-bit mono.

#define MK_SND_HDA_FMT_48KHZ_16BIT_MONO 0x0010

// * Codec Verb Helpers * //
// Verb formats vary based on payload size per the spec.

// 12-bit
#define MK_SND_HDA_VERB_GET_PARAM 0xF00
#define MK_SND_HDA_VERB_GET_CONN_LIST 0xF02
#define MK_SND_HDA_VERB_GET_CONN_SELECT 0xF01
#define MK_SND_HDA_VERB_GET_PIN_CTRL 0xF07
#define MK_SND_HDA_VERB_GET_EAPD_BTL 0xF0C
#define MK_SND_HDA_VERB_GET_POWER_STATE 0xF05
#define MK_SND_HDA_VERB_GET_CONV_CTRL 0xF06

// 4-bit
#define MK_SND_HDA_VERB_GET_AMP_GAIN_MUTE 0xB

// SET verbs
#define MK_SND_HDA_VERB_SET_PIN_CTRL 0x707
#define MK_SND_HDA_VERB_SET_EAPD_BTL 0x70C
#define MK_SND_HDA_VERB_SET_POWER_STATE 0x705
#define MK_SND_HDA_VERB_SET_CONV_CTRL 0x706
#define MK_SND_HDA_VERB_SET_CONN_SELECT 0x701
#define MK_SND_HDA_VERB_SET_AMP_GAIN_MUTE 0x3

#define MK_SND_HDA_VERB_SET_CONV_FORMAT 0x2

#define MK_SND_HDA_VERB_GET_CONFIG_DEFAULT 0xF1C // For identifying Pin complex type (Jack/Internal/N/A)

// * Codec Parameters * //
// Used during initial node enumeration.

#define MK_SND_HDA_PARAM_VENDOR_ID 0x00
#define MK_SND_HDA_PARAM_REVISION_ID 0x02
#define MK_SND_HDA_PARAM_SUBNODE_COUNT 0x04
#define MK_SND_HDA_PARAM_FN_GROUP_TYPE 0x05
#define MK_SND_HDA_PARAM_AUDIO_WIDGET_CAP 0x09
#define MK_SND_HDA_PARAM_PIN_CAP 0x0C
#define MK_SND_HDA_PARAM_CONN_LIST_LEN 0x0E
#define MK_SND_HDA_PARAM_OUT_AMP_CAP 0x12

// Widget types (bits 23:20 of Audio Widget Caps)
#define MK_SND_HDA_WIDGET_AUD_OUT 0x0
#define MK_SND_HDA_WIDGET_AUD_IN 0x1
#define MK_SND_HDA_WIDGET_AUD_MIX 0x2
#define MK_SND_HDA_WIDGET_AUD_SEL 0x3
#define MK_SND_HDA_WIDGET_PIN 0x4
#define MK_SND_HDA_WIDGET_POWER 0x5
#define MK_SND_HDA_WIDGET_BEEP 0x7

// Pin Control
#define MK_SND_HDA_PIN_CTRL_OUT_EN (1 << 6)
#define MK_SND_HDA_PIN_CTRL_IN_EN (1 << 5)
#define MK_SND_HDA_PIN_CTRL_HP_EN (1 << 7)

// External Amplifier Power Down (EAPD)
// Often required on laptops to hear anything.
#define MK_SND_HDA_EAPD_BTL_ENABLE (1 << 1)

// * BDL Entry (Scatter/Gather) * //
// Hardware fetches these to know where to pull audio data from RAM.

typedef struct __attribute__((packed)) {
    uint64_t address; // Physical address, but Mira identity maps.
    uint32_t length;
    uint32_t ioc; // Interrupt On Completion (bit 0).
} mk_snd_hda_bdl_entry_t;

// * RIRB Entry * //

typedef struct __attribute__((packed)) {
    uint32_t response;
    uint32_t resp_ex; // Unsolicited response flags in bit 4.
} mk_snd_hda_rirb_entry_t;

// * Driver State * //

typedef struct {
    volatile uint8_t *mmio;

    // CORB
    uint32_t *corb;
    uint16_t corb_entries;

    // RIRB
    mk_snd_hda_rirb_entry_t *rirb;
    uint16_t rirb_entries;
    uint16_t rirb_rp; // Driver's shadow read pointer.

    // Node Graph
    // We grab the first output pin in the first group.
    uint8_t codec_addr; // From testing, this is usually 0, sometimes 2.
    uint8_t afg_nid; // Root of the function group.
    uint8_t dac_nid;
    uint8_t pin_nid;
    uint8_t dac_conn_idx; // Mux index if DAC isn't 0th connection.

    // Stream
    uint8_t stream_index; // Offset into SD registers.
    uint8_t stream_tag; // Tag sent in packets (>0).
    volatile uint8_t *sd_base;

    // DMA / Buffers
    uint8_t *dma_buf;
    uint32_t dma_buf_size;
    mk_snd_hda_bdl_entry_t *bdl; // 128-byte aligned.

    uint8_t initialized;
} mk_snd_hda_state_t;

// Function to initialize the sound card
int mk_snd_init(void);

// Function to play sound from a buffer
int mk_snd_play(const void *data, uint32_t size);

#endif