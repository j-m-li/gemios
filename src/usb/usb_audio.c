/*
 * This is free and unencumbered software released into the public domain.
 * GEMIOS Preemptive Real-Time Operating System
 *
 * USB Audio Class 1.0 (UAC 1.0) Driver - C-Media 0d8c:0014 Audio Adapter
 */

#include "usb_audio.h"
#include "usb_core.h"
#include "xhci.h"
#include "heap.h"
#include "string.h"
#include "timer.h"
#include "vga.h"
#include "fat.h"
#include "task.h"

static usb_audio_device_t g_audio_devs[USB_AUDIO_MAX_DEVICES];
static size_t g_num_audio_devs = 0;

/* 256-point Sine Table for pure tone generation (16-bit amplitude) */
static const int16_t g_sine_lut[256] = {
    0, 804, 1607, 2410, 3211, 4011, 4807, 5601, 6392, 7179, 7961, 8739, 9511, 10278, 11038, 11792,
    12539, 13278, 14009, 14732, 15446, 16150, 16845, 17530, 18204, 18867, 19519, 20159, 20787, 21402, 22004, 22594,
    23169, 23731, 24278, 24811, 25329, 25831, 26318, 26789, 27244, 27683, 28105, 28510, 28897, 29268, 29621, 29955,
    30272, 30571, 30851, 31113, 31356, 31580, 31785, 31970, 32137, 32284, 32412, 32520, 32609, 32678, 32727, 32757,
    32767, 32757, 32727, 32678, 32609, 32520, 32412, 32284, 32137, 31970, 31785, 31580, 31356, 31113, 30851, 30571,
    30272, 29955, 29621, 29268, 28897, 28510, 28105, 27683, 27244, 26789, 26318, 25831, 25329, 24811, 24278, 23731,
    23169, 22594, 22004, 21402, 20787, 20159, 19519, 18867, 18204, 17530, 16845, 16150, 15446, 14732, 14009, 13278,
    12539, 11792, 11038, 10278, 9511, 8739, 7961, 7179, 6392, 5601, 4807, 4011, 3211, 2410, 1607, 804,
    0, -804, -1607, -2410, -3211, -4011, -4807, -5601, -6392, -7179, -7961, -8739, -9511, -10278, -11038, -11792,
    -12539, -13278, -14009, -14732, -15446, -16150, -16845, -17530, -18204, -18867, -19519, -20159, -20787, -21402, -22004, -22594,
    -23169, -23731, -24278, -24811, -25329, -25831, -26318, -26789, -27244, -27683, -28105, -28510, -28897, -29268, -29621, -29955,
    -30272, -30571, -30851, -31113, -31356, -31580, -31785, -31970, -32137, -32284, -32412, -32520, -32609, -32678, -32727, -32757,
    -32767, -32757, -32727, -32678, -32609, -32520, -32412, -32284, -32137, -31970, -31785, -31580, -31356, -31113, -30851, -30571,
    -30272, -29955, -29621, -29268, -28897, -28510, -28105, -27683, -27244, -26789, -26318, -25831, -25329, -24811, -24278, -23731,
    -23169, -22594, -22004, -21402, -20787, -20159, -19519, -18867, -18204, -17530, -16845, -16150, -15446, -14732, -14009, -13278,
    -12539, -11792, -11038, -10278, -9511, -8739, -7961, -7179, -6392, -5601, -4807, -4011, -3211, -2410, -1607, -804
};

void usb_audio_init(void) {
    memset(g_audio_devs, 0, sizeof(g_audio_devs));
    g_num_audio_devs = 0;
}

size_t usb_audio_get_device_count(void) {
    return g_num_audio_devs;
}

usb_audio_device_t *usb_audio_get_device(size_t index) {
    if (index < g_num_audio_devs && g_audio_devs[index].initialized) {
        return &g_audio_devs[index];
    }
    return NULL;
}

usb_audio_device_t *usb_audio_get_default(void) {
    if (g_num_audio_devs > 0 && g_audio_devs[0].initialized) {
        return &g_audio_devs[0];
    }
    return NULL;
}

int usb_audio_init_device(usb_device_t *dev, usb_interface_t *iface) {
    usb_audio_device_t *audio;
    size_t i;
    uint8_t *p;
    uint8_t *end;
    uint8_t cur_iface_num = 0;
    uint8_t cur_alt_num = 0;
    uint8_t feature_unit_id = 9; /* Default for C-Media 0d8c:0014 */
    uint8_t out_ep_addr = 0;
    uint8_t out_ep_dci = 0;
    uint16_t out_ep_max_packet = 192;
    uint8_t as_out_iface_num = 1;
    uint8_t as_out_alt_setting = 1;
    int res;

    if (!dev || !iface) return -1;
    kprintf("[Audio] Initializing USB Audio device on Slot %u (Iface %u Class=0x%02x Sub=0x%02x)...\n",
            dev->slot_id, iface->interface_number, iface->interface_class, iface->interface_subclass);

    /* Check if slot is already registered */
    for (i = 0; i < g_num_audio_devs; i++) {
        if (g_audio_devs[i].initialized && g_audio_devs[i].slot_id == dev->slot_id) {
            return 0; /* Already registered */
        }
    }

    if (g_num_audio_devs >= USB_AUDIO_MAX_DEVICES) return -1;
    audio = &g_audio_devs[g_num_audio_devs];
    memset(audio, 0, sizeof(usb_audio_device_t));

    audio->dev = dev;
    audio->slot_id = dev->slot_id;
    audio->is_cmedia = (dev->dev_desc.idVendor == USB_VID_CMEDIA && dev->dev_desc.idProduct == USB_PID_CMEDIA_AUDIO_ADAPTER);

    if (audio->is_cmedia) {
        strncpy(dev->name, "C-Media USB Audio Adapter (0d8c:0014)", sizeof(dev->name) - 1);
    } else {
        strncpy(dev->name, "USB Audio Device (UAC 1.0)", sizeof(dev->name) - 1);
    }

    /* Parse raw config descriptor to locate AudioControl Feature Unit & AudioStreaming OUT Isoch endpoint */
    if (dev->raw_config_desc && dev->raw_config_len > 0) {
        p = dev->raw_config_desc;
        end = p + dev->raw_config_len;

        while (p < end) {
            uint8_t len = p[0];
            uint8_t type = p[1];

            if (len == 0 || p + len > end) break;

            if (type == USB_DESC_INTERFACE) {
                usb_interface_descriptor_t *id = (usb_interface_descriptor_t*)p;
                cur_iface_num = id->bInterfaceNumber;
                cur_alt_num = id->bAlternateSetting;
            } else if (type == 0x24) { /* CS_INTERFACE (Audio Class-Specific Interface) */
                uint8_t subtype = p[2];
                if (subtype == UAC_AC_FEATURE_UNIT) {
                    uint8_t uid = p[3];
                    /* Prefer Unit 9 for playback, or first Feature Unit encountered */
                    if (feature_unit_id == 0 || uid == 9) {
                        feature_unit_id = uid;
                    }
                }
            } else if (type == USB_DESC_ENDPOINT) {
                usb_endpoint_descriptor_t *ep = (usb_endpoint_descriptor_t*)p;
                if ((ep->bEndpointAddress & USB_DIR_IN) == 0 && (ep->bmAttributes & 0x03) == 1) {
                    /* Isochronous OUT endpoint found - bind interface & alt setting */
                    out_ep_addr = ep->bEndpointAddress;
                    out_ep_max_packet = ep->wMaxPacketSize;
                    out_ep_dci = (out_ep_addr & 0x0F) * 2;
                    as_out_iface_num = cur_iface_num;
                    as_out_alt_setting = (cur_alt_num > 0) ? cur_alt_num : 1;
                }
            }

            p += len;
        }
    }

    if (out_ep_addr == 0) {
        /* Default for C-Media 0d8c:0014: EP 1 OUT, DCI 2, MaxPacket 200 */
        out_ep_addr = 0x01;
        out_ep_dci = 2;
        out_ep_max_packet = 200;
        as_out_iface_num = 1;
        as_out_alt_setting = 1;
    }
    if (feature_unit_id == 0) {
        feature_unit_id = 9;
    }

    audio->ac_iface = 0;
    audio->as_out_iface = as_out_iface_num;
    audio->as_out_alt = as_out_alt_setting;
    audio->as_out_ep_addr = out_ep_addr;
    audio->as_out_ep_dci = out_ep_dci;
    audio->as_out_max_packet = out_ep_max_packet;
    audio->feature_unit_id = feature_unit_id;
    audio->sample_rate = 48000;
    audio->channels = 2;
    audio->bits_per_sample = 16;
    audio->volume_percent = 90;
    audio->is_muted = false;
    audio->initialized = true;

    g_num_audio_devs++;

    /* Activate Playback Stream: SET_INTERFACE on Interface 1, Alternate Setting 1 */
    res = usb_control_msg(dev,
                          USB_REQ_TYPE_STANDARD | USB_DIR_OUT | USB_REQ_RECIPIENT_INTERFACE,
                          USB_REQ_SET_INTERFACE,
                          audio->as_out_alt,
                          audio->as_out_iface,
                          NULL, 0);

    /* Set Sampling Frequency on Endpoint (48000 Hz default) */
    usb_audio_set_sample_rate(audio, 48000);

    /* Set initial hardware volume & unmute on Unit 9 and detected FU */
    usb_audio_set_volume(audio, 90);
    usb_audio_set_mute(audio, false);

    kprint_color(0x0A, "[Audio] Bound %s on Slot %u (48kHz/16-bit Stereo, EP 0x%02x DCI %u)\n",
                 dev->name, audio->slot_id, audio->as_out_ep_addr, audio->as_out_ep_dci);

    return 0;
}

int usb_audio_set_sample_rate(usb_audio_device_t *audio, uint32_t sample_rate) {
    uint8_t freq_bytes[3];
    int res;

    if (!audio || !audio->dev || !audio->initialized) return -1;

    freq_bytes[0] = (uint8_t)(sample_rate & 0xFF);
    freq_bytes[1] = (uint8_t)((sample_rate >> 8) & 0xFF);
    freq_bytes[2] = (uint8_t)((sample_rate >> 16) & 0xFF);

    res = usb_control_msg(audio->dev,
                          USB_REQ_TYPE_CLASS | USB_DIR_OUT | USB_REQ_RECIPIENT_ENDPOINT,
                          UAC_REQ_SET_CUR,
                          (UAC_EP_CONTROL_SAMPLING_FREQ << 8) | 0,
                          audio->as_out_ep_addr,
                          freq_bytes, 3);
    if (res == 0) {
        audio->sample_rate = sample_rate;
    }
    return res;
}

int usb_audio_set_volume(usb_audio_device_t *audio, uint8_t vol_percent) {
    int16_t vol_db;
    uint8_t fu;

    if (!audio || !audio->dev || !audio->initialized) return -1;
    if (vol_percent > 100) vol_percent = 100;

    /* UAC 1.0 volume: 1/256 dB units, 0 dB = 0x0000, -6 dB = -1536 */
    if (vol_percent == 0) {
        vol_db = (int16_t)0x8000;
    } else {
        vol_db = (int16_t)(((int32_t)(vol_percent - 100) * 10240) / 100);
    }

    fu = audio->feature_unit_id ? audio->feature_unit_id : 9;

    /* Set Volume on Master (0), Left (1), Right (2) */
    usb_control_msg(audio->dev,
                    USB_REQ_TYPE_CLASS | USB_DIR_OUT | USB_REQ_RECIPIENT_INTERFACE,
                    UAC_REQ_SET_CUR,
                    (UAC_FU_CONTROL_VOLUME << 8) | 0,
                    (fu << 8) | audio->ac_iface,
                    &vol_db, 2);

    usb_control_msg(audio->dev,
                    USB_REQ_TYPE_CLASS | USB_DIR_OUT | USB_REQ_RECIPIENT_INTERFACE,
                    UAC_REQ_SET_CUR,
                    (UAC_FU_CONTROL_VOLUME << 8) | 1,
                    (fu << 8) | audio->ac_iface,
                    &vol_db, 2);

    usb_control_msg(audio->dev,
                    USB_REQ_TYPE_CLASS | USB_DIR_OUT | USB_REQ_RECIPIENT_INTERFACE,
                    UAC_REQ_SET_CUR,
                    (UAC_FU_CONTROL_VOLUME << 8) | 2,
                    (fu << 8) | audio->ac_iface,
                    &vol_db, 2);

    if (fu != 9) {
        usb_control_msg(audio->dev,
                        USB_REQ_TYPE_CLASS | USB_DIR_OUT | USB_REQ_RECIPIENT_INTERFACE,
                        UAC_REQ_SET_CUR,
                        (UAC_FU_CONTROL_VOLUME << 8) | 1,
                        (9 << 8) | audio->ac_iface,
                        &vol_db, 2);
        usb_control_msg(audio->dev,
                        USB_REQ_TYPE_CLASS | USB_DIR_OUT | USB_REQ_RECIPIENT_INTERFACE,
                        UAC_REQ_SET_CUR,
                        (UAC_FU_CONTROL_VOLUME << 8) | 2,
                        (9 << 8) | audio->ac_iface,
                        &vol_db, 2);
    }

    audio->volume_percent = vol_percent;
    return 0;
}

int usb_audio_set_mute(usb_audio_device_t *audio, bool mute) {
    uint8_t m_val;
    uint8_t fu;

    if (!audio || !audio->dev || !audio->initialized) return -1;
    m_val = mute ? 1 : 0;
    fu = audio->feature_unit_id ? audio->feature_unit_id : 9;

    usb_control_msg(audio->dev,
                    USB_REQ_TYPE_CLASS | USB_DIR_OUT | USB_REQ_RECIPIENT_INTERFACE,
                    UAC_REQ_SET_CUR,
                    (UAC_FU_CONTROL_MUTE << 8) | 0,
                    (fu << 8) | audio->ac_iface,
                    &m_val, 1);

    usb_control_msg(audio->dev,
                    USB_REQ_TYPE_CLASS | USB_DIR_OUT | USB_REQ_RECIPIENT_INTERFACE,
                    UAC_REQ_SET_CUR,
                    (UAC_FU_CONTROL_MUTE << 8) | 1,
                    (fu << 8) | audio->ac_iface,
                    &m_val, 1);

    usb_control_msg(audio->dev,
                    USB_REQ_TYPE_CLASS | USB_DIR_OUT | USB_REQ_RECIPIENT_INTERFACE,
                    UAC_REQ_SET_CUR,
                    (UAC_FU_CONTROL_MUTE << 8) | 2,
                    (fu << 8) | audio->ac_iface,
                    &m_val, 1);

    if (fu != 9) {
        usb_control_msg(audio->dev,
                        USB_REQ_TYPE_CLASS | USB_DIR_OUT | USB_REQ_RECIPIENT_INTERFACE,
                        UAC_REQ_SET_CUR,
                        (UAC_FU_CONTROL_MUTE << 8) | 0,
                        (9 << 8) | audio->ac_iface,
                        &m_val, 1);
    }

    audio->is_muted = mute;
    return 0;
}

#define ISOCH_DMA_BUFFERS 4096
#define ISOCH_FRAME_MAX   256

static uint8_t g_isoch_pool[ISOCH_DMA_BUFFERS][ISOCH_FRAME_MAX];
static uint32_t g_isoch_head = 0;
static uint32_t g_stream_t_start = 0;

typedef struct {
    char path[256];
    char dev_name[16];
    volatile bool in_use;
} audio_bg_ctx_t;

static audio_bg_ctx_t g_audio_bg_ctx;

static void usb_audio_pump(usb_audio_device_t *audio) {
    xhci_controller_t *ctrl;
    size_t bytes_per_ms;
    uint32_t elapsed_ms;
    uint32_t target_frames;
    bool doorbell = false;

    if (!audio || !audio->initialized || !audio->tb.active) return;
    ctrl = xhci_get_controller();
    if (!ctrl) return;

    bytes_per_ms = (size_t)audio->sample_rate * audio->channels * (audio->bits_per_sample / 8) / 1000;
    if (bytes_per_ms == 0 || bytes_per_ms > ISOCH_FRAME_MAX) bytes_per_ms = 192;

    elapsed_ms = rtos_get_ticks() - g_stream_t_start;
    target_frames = elapsed_ms + 128; /* Maintain 128 ms lookahead depth */

    xhci_poll();

    while (g_isoch_head < target_frames && (audio->tb.queued_periods > 0 || audio->tb.stopping)) {
        uint8_t *dma_buf = g_isoch_pool[g_isoch_head % ISOCH_DMA_BUFFERS];
        bool sia = (g_isoch_head == 0);
        bool ioc = ((g_isoch_head % 16) == 0);

        if (audio->tb.queued_periods > 0) {
            uint8_t p_idx = audio->tb.play_period;
            size_t avail = audio->tb.period_bytes[p_idx] - audio->tb.period_play_pos[p_idx];
            size_t chunk = (avail < bytes_per_ms) ? avail : bytes_per_ms;

            memset(dma_buf, 0, ISOCH_FRAME_MAX);
            memcpy(dma_buf, &audio->tb.buffer[p_idx][audio->tb.period_play_pos[p_idx]], chunk);
            audio->tb.period_play_pos[p_idx] += chunk;
            audio->tb.total_played_bytes += (uint32_t)chunk;

            if (audio->tb.period_play_pos[p_idx] >= audio->tb.period_bytes[p_idx]) {
                /* Period fully consumed by DMA! Advance to next period */
                audio->tb.period_play_pos[p_idx] = 0;
                audio->tb.period_bytes[p_idx] = 0;
                audio->tb.play_period = (p_idx + 1) % AUDIO_NUM_PERIODS;
                audio->tb.queued_periods--;
            }

            xhci_isoch_transfer(ctrl, audio->slot_id, audio->as_out_ep_dci, dma_buf, (uint32_t)bytes_per_ms, sia, ioc);
            g_isoch_head++;
            doorbell = true;
        } else if (audio->tb.stopping) {
            break;
        } else {
            /* Underrun condition - fill with silence frame to prevent stall */
            audio->tb.underruns++;
            memset(dma_buf, 0, ISOCH_FRAME_MAX);
            xhci_isoch_transfer(ctrl, audio->slot_id, audio->as_out_ep_dci, dma_buf, (uint32_t)bytes_per_ms, sia, ioc);
            g_isoch_head++;
            doorbell = true;
            break;
        }
    }

    if (doorbell) {
        xhci_ring_doorbell(ctrl, audio->slot_id, audio->as_out_ep_dci);
    }
}

int usb_audio_stream_start(usb_audio_device_t *audio, uint32_t sample_rate, uint8_t channels, uint8_t bits) {
    if (!audio || !audio->initialized) return -1;

    if (sample_rate != audio->sample_rate && sample_rate > 0) {
        usb_audio_set_sample_rate(audio, sample_rate);
    }
    if (channels > 0) audio->channels = channels;
    if (bits > 0) audio->bits_per_sample = bits;

    memset(&audio->tb, 0, sizeof(audio_triple_buffer_t));
    audio->tb.active = true;
    audio->tb.stopping = false;
    audio->stream_active = true;
    g_isoch_head = 0;
    g_stream_t_start = rtos_get_ticks();
    return 0;
}

int usb_audio_stream_write(usb_audio_device_t *audio, const void *pcm_data, size_t len, uint32_t timeout_ms) {
    const uint8_t *src;
    size_t remaining;
    uint32_t t0;

    if (!audio || !audio->initialized || !pcm_data || len == 0) return -1;
    if (!audio->tb.active) {
        usb_audio_stream_start(audio, audio->sample_rate, audio->channels, audio->bits_per_sample);
    }

    src = (const uint8_t*)pcm_data;
    remaining = len;
    t0 = rtos_get_ticks();

    while (remaining > 0) {
        uint8_t w_idx;
        size_t space;
        size_t to_copy;

        /* If all periods are full, pump DMA and wait for buffer space */
        while (audio->tb.queued_periods >= AUDIO_NUM_PERIODS) {
            usb_audio_pump(audio);
            if (timeout_ms > 0 && (rtos_get_ticks() - t0 >= timeout_ms)) {
                return (int)(len - remaining);
            }
            rtos_sleep_ms(2);
        }

        w_idx = audio->tb.write_period;
        space = AUDIO_PERIOD_MAX_BYTES - audio->tb.write_offset;
        to_copy = (remaining < space) ? remaining : space;

        memcpy(&audio->tb.buffer[w_idx][audio->tb.write_offset], src, to_copy);
        audio->tb.write_offset += to_copy;
        src += to_copy;
        remaining -= to_copy;

        /* If period filled, commit period to play queue */
        if (audio->tb.write_offset >= AUDIO_PERIOD_MAX_BYTES) {
            audio->tb.period_bytes[w_idx] = audio->tb.write_offset;
            audio->tb.period_play_pos[w_idx] = 0;
            audio->tb.write_period = (w_idx + 1) % AUDIO_NUM_PERIODS;
            audio->tb.write_offset = 0;
            audio->tb.queued_periods++;
        }

        /* Feed newly available frames to hardware */
        usb_audio_pump(audio);
    }

    return (int)len;
}

int usb_audio_stream_flush(usb_audio_device_t *audio) {
    if (!audio || !audio->initialized) return -1;
    if (audio->tb.write_offset > 0) {
        uint8_t w_idx = audio->tb.write_period;
        audio->tb.period_bytes[w_idx] = audio->tb.write_offset;
        audio->tb.period_play_pos[w_idx] = 0;
        audio->tb.write_period = (w_idx + 1) % AUDIO_NUM_PERIODS;
        audio->tb.write_offset = 0;
        audio->tb.queued_periods++;
    }
    return 0;
}

int usb_audio_stream_drain(usb_audio_device_t *audio) {
    xhci_controller_t *ctrl;
    int z;
    size_t bytes_per_ms;

    if (!audio || !audio->initialized || !audio->tb.active) return 0;
    ctrl = xhci_get_controller();
    if (!ctrl) return 0;

    usb_audio_stream_flush(audio);

    bytes_per_ms = (size_t)audio->sample_rate * audio->channels * (audio->bits_per_sample / 8) / 1000;
    if (bytes_per_ms == 0 || bytes_per_ms > ISOCH_FRAME_MAX) bytes_per_ms = 192;

    /* Pump until all periods have been transferred to xHCI */
    while (audio->tb.queued_periods > 0) {
        usb_audio_pump(audio);
        rtos_sleep_ms(2);
    }

    /* Send smooth zero termination frames */
    for (z = 0; z < 8; z++) {
        uint8_t *dma_buf = g_isoch_pool[g_isoch_head % ISOCH_DMA_BUFFERS];
        memset(dma_buf, 0, ISOCH_FRAME_MAX);
        xhci_isoch_transfer(ctrl, audio->slot_id, audio->as_out_ep_dci, dma_buf, (uint32_t)bytes_per_ms, false, (z == 7));
        g_isoch_head++;
    }
    xhci_ring_doorbell(ctrl, audio->slot_id, audio->as_out_ep_dci);

    /* Wait for hardware pipeline to finish playing buffered audio */
    while (rtos_get_ticks() - g_stream_t_start < g_isoch_head) {
        xhci_poll();
        rtos_sleep_ms(4);
    }

    audio->tb.active = false;
    audio->stream_active = false;
    return 0;
}

void usb_audio_stream_stop(usb_audio_device_t *audio) {
    if (!audio) return;
    audio->tb.active = false;
    audio->tb.stopping = true;
    audio->tb.queued_periods = 0;
    audio->stream_active = false;
}

bool usb_audio_is_playing(usb_audio_device_t *audio) {
    if (!audio) return false;
    return audio->tb.active || (audio->tb.queued_periods > 0);
}

int usb_audio_play_pcm(usb_audio_device_t *audio, const void *pcm_data, size_t len, uint32_t sample_rate, uint8_t channels, uint8_t bits) {
    xhci_controller_t *ctrl;
    size_t bytes_per_sec;
    size_t bytes_per_ms;
    size_t offset;
    uint32_t buf_idx = 0;
    uint32_t t_start;
    uint32_t start_frame;
    int z;

    if (!audio || !audio->initialized || !pcm_data || len == 0) return -1;
    ctrl = xhci_get_controller();
    if (!ctrl) return -1;

    if (sample_rate != audio->sample_rate && sample_rate > 0) {
        usb_audio_set_sample_rate(audio, sample_rate);
    }

    /* 48000 Hz, 16-bit (2 bytes), stereo (2 channels) -> 192 bytes/ms */
    bytes_per_sec = (size_t)audio->sample_rate * channels * (bits / 8);
    bytes_per_ms = bytes_per_sec / 1000;
    if (bytes_per_ms == 0 || bytes_per_ms > ISOCH_FRAME_MAX) bytes_per_ms = 192;

    /* Synchronize with hardware xHCI microframe clock, schedule start 16ms in advance */
    start_frame = (xhci_get_current_frame(ctrl) + 16) & 0x7FF;

    t_start = rtos_get_ticks();
    offset = 0;
    while (offset < len) {
        uint32_t elapsed_ms = rtos_get_ticks() - t_start;
        uint32_t target_frames = elapsed_ms + 1024; /* Maintain 1024ms deep lookahead buffer */
        bool doorbell_needed = false;

        xhci_poll();

        /* Enqueue frames until target buffer depth is reached */
        while (buf_idx < target_frames && offset < len) {
            size_t chunk = len - offset;
            uint8_t *dma_buf;
            uint32_t frame_id = (start_frame + buf_idx) & 0x7FF;
            bool ioc = ((buf_idx % 16) == 0) || (offset + chunk >= len);

            if (chunk > bytes_per_ms) chunk = bytes_per_ms;

            dma_buf = g_isoch_pool[buf_idx % ISOCH_DMA_BUFFERS];
            memset(dma_buf, 0, ISOCH_FRAME_MAX);
            memcpy(dma_buf, (const uint8_t*)pcm_data + offset, chunk);

            xhci_isoch_transfer_frame(ctrl, audio->slot_id, audio->as_out_ep_dci, dma_buf, (uint32_t)bytes_per_ms, frame_id, false, ioc);

            buf_idx++;
            offset += chunk;
            doorbell_needed = true;
        }

        if (doorbell_needed) {
            xhci_ring_doorbell(ctrl, audio->slot_id, audio->as_out_ep_dci);
        }

        if (offset < len) {
            rtos_sleep_ms(10);
        }
    }

    /* Send smooth zero termination frames */
    for (z = 0; z < 8; z++) {
        uint8_t *dma_buf = g_isoch_pool[buf_idx % ISOCH_DMA_BUFFERS];
        uint32_t frame_id = (start_frame + buf_idx) & 0x7FF;
        memset(dma_buf, 0, ISOCH_FRAME_MAX);
        xhci_isoch_transfer_frame(ctrl, audio->slot_id, audio->as_out_ep_dci, dma_buf, (uint32_t)bytes_per_ms, frame_id, false, (z == 7));
        buf_idx++;
    }
    xhci_ring_doorbell(ctrl, audio->slot_id, audio->as_out_ep_dci);

    /* Wait for the hardware to finish playing buffered audio */
    while (rtos_get_ticks() - t_start < buf_idx) {
        xhci_poll();
        rtos_sleep_ms(10);
    }

    rtos_sleep_ms(10);
    xhci_poll();
    return 0;
}

int usb_audio_play_tone(uint32_t freq_hz, uint32_t duration_ms) {
    usb_audio_device_t *audio;
    size_t num_samples;
    size_t total_bytes;
    int16_t *buf;
    uint32_t phase;
    uint32_t phase_step;
    size_t attack_samples;
    size_t release_samples;
    size_t s;
    int res;

    audio = usb_audio_get_default();
    if (!audio) {
        kprint_color(0x4F, "No USB Audio device found.\n");
        return -1;
    }

    if (freq_hz == 0 || freq_hz > 20000) freq_hz = 440;
    if (duration_ms == 0) duration_ms = 300;
    if (duration_ms > 30000) duration_ms = 30000;

    num_samples = ((size_t)audio->sample_rate * duration_ms) / 1000;
    total_bytes = num_samples * 2 * sizeof(int16_t); /* Stereo 16-bit */

    buf = (int16_t*)kmalloc(total_bytes);
    if (!buf) {
        kprint_color(0x4F, "Failed to allocate audio buffer (%u bytes).\n", (uint32_t)total_bytes);
        return -1;
    }

    phase = 0;
    phase_step = (uint32_t)(((uint64_t)freq_hz * 16777216ULL) / (uint64_t)audio->sample_rate);

    attack_samples = ((size_t)audio->sample_rate * 15) / 1000; /* 15ms attack */
    release_samples = ((size_t)audio->sample_rate * 20) / 1000; /* 20ms release */
    if (attack_samples + release_samples > num_samples) {
        attack_samples = num_samples / 2;
        release_samples = num_samples / 2;
    }

    for (s = 0; s < num_samples; s++) {
        uint8_t lut_idx = (uint8_t)((phase >> 16) & 0xFF);
        int32_t raw = g_sine_lut[lut_idx];
        int32_t amp = (raw * (int32_t)audio->volume_percent) / 100;
        int16_t sample_val;

        /* Apply smooth ADSR envelope */
        if (s < attack_samples) {
            amp = (amp * (int32_t)s) / (int32_t)attack_samples;
        } else if (s >= num_samples - release_samples) {
            size_t rel_idx = num_samples - 1 - s;
            amp = (amp * (int32_t)rel_idx) / (int32_t)release_samples;
        }

        sample_val = (int16_t)amp;
        buf[s * 2 + 0] = sample_val; /* Left */
        buf[s * 2 + 1] = sample_val; /* Right */

        phase += phase_step;
    }

    res = usb_audio_play_pcm(audio, buf, total_bytes, audio->sample_rate, 2, 16);
    kfree(buf);
    return res;
}

int usb_audio_play_wav(const void *wav_data, size_t wav_len) {
    const uint8_t *p = (const uint8_t*)wav_data;
    uint32_t sample_rate;
    uint16_t channels;
    uint16_t bits_per_sample;
    uint16_t audio_format;
    const uint8_t *pcm_data;
    uint32_t pcm_len;
    size_t offset;
    usb_audio_device_t *audio;

    if (!wav_data || wav_len < 44) {
        kprint_color(0x4F, "WAV buffer too small (%u bytes).\n", (uint32_t)wav_len);
        return -1;
    }

    if (memcmp(p, "RIFF", 4) != 0 || memcmp(p + 8, "WAVE", 4) != 0) {
        kprint_color(0x4F, "Invalid WAV header (missing RIFF/WAVE signature).\n");
        return -1;
    }

    audio = usb_audio_get_default();
    if (!audio) {
        kprint_color(0x4F, "No USB Audio device found.\n");
        return -1;
    }

    offset = 12;
    audio_format = 0;
    channels = 0;
    sample_rate = 0;
    bits_per_sample = 0;
    pcm_data = NULL;
    pcm_len = 0;

    while (offset + 8 <= wav_len) {
        char chunk_id[5];
        uint32_t chunk_sz;
        memcpy(chunk_id, p + offset, 4);
        chunk_id[4] = '\0';
        chunk_sz = (uint32_t)p[offset + 4] | ((uint32_t)p[offset + 5] << 8) |
                   ((uint32_t)p[offset + 6] << 16) | ((uint32_t)p[offset + 7] << 24);
        offset += 8;

        if (strcmp(chunk_id, "fmt ") == 0 && chunk_sz >= 16) {
            audio_format = (uint16_t)p[offset] | ((uint16_t)p[offset + 1] << 8);
            channels = (uint16_t)p[offset + 2] | ((uint16_t)p[offset + 3] << 8);
            sample_rate = (uint32_t)p[offset + 4] | ((uint32_t)p[offset + 5] << 8) |
                          ((uint32_t)p[offset + 6] << 16) | ((uint32_t)p[offset + 7] << 24);
            bits_per_sample = (uint16_t)p[offset + 14] | ((uint16_t)p[offset + 15] << 8);
        } else if (strcmp(chunk_id, "data") == 0) {
            pcm_data = p + offset;
            pcm_len = (chunk_sz <= (wav_len - offset)) ? chunk_sz : (uint32_t)(wav_len - offset);
            break;
        }

        offset += chunk_sz;
        if (chunk_sz & 1) offset++; /* Pad byte */
    }

    if (!pcm_data || pcm_len == 0) {
        kprint_color(0x4F, "WAV 'data' chunk not found.\n");
        return -1;
    }

    if (audio_format != 1) {
        kprint_color(0x4F, "Unsupported WAV compression format (only uncompressed PCM supported).\n");
        return -1;
    }

    if (channels == 0 || (bits_per_sample != 8 && bits_per_sample != 16 && bits_per_sample != 24)) {
        kprint_color(0x4F, "Unsupported WAV format (%u channels, %u-bit).\n", channels, bits_per_sample);
        return -1;
    }

    /* Fast path: Already 48000 Hz Stereo 16-bit PCM -> direct play */
    if (sample_rate == 48000 && channels == 2 && bits_per_sample == 16) {
        kprintf("[Audio] Playing WAV: 48000 Hz Stereo 16-bit (%u ms)\n", (uint32_t)(pcm_len / 192));
        return usb_audio_play_pcm(audio, pcm_data, pcm_len, 48000, 2, 16);
    }

    {
        size_t in_frame_sz = (size_t)channels * (bits_per_sample / 8);
        size_t num_in_frames = pcm_len / in_frame_sz;
        size_t num_out_frames;
        size_t out_bytes;
        int16_t *out_buf;
        size_t i;
        int res;

        if (num_in_frames == 0) return 0;

        /* Target: 48000 Hz, Stereo (2 channels), 16-bit signed PCM */
        num_out_frames = (size_t)(((uint64_t)num_in_frames * 48000 + (sample_rate / 2)) / sample_rate);
        out_bytes = num_out_frames * 2 * sizeof(int16_t);

        out_buf = (int16_t*)kmalloc(out_bytes);
        if (!out_buf) {
            kprint_color(0x4F, "Out of memory allocating %u bytes for audio playback.\n", (uint32_t)out_bytes);
            return -1;
        }

        kprintf("[Audio] Playing WAV: %u Hz %s %u-bit -> 48000 Hz Stereo 16-bit (%u ms)\n",
                sample_rate, (channels == 1) ? "Mono" : "Stereo", bits_per_sample,
                (uint32_t)((num_out_frames * 1000) / 48000));

        /* Resample / expand channels / convert bits to 48kHz 16-bit Stereo */
        for (i = 0; i < num_out_frames; i++) {
            uint64_t src_pos_fp = ((uint64_t)i * sample_rate * 65536ULL) / 48000ULL;
            size_t idx0 = (size_t)(src_pos_fp >> 16);
            size_t idx1 = (idx0 + 1 < num_in_frames) ? (idx0 + 1) : idx0;
            uint32_t frac = (uint32_t)(src_pos_fp & 0xFFFF);
            int32_t s0_l, s0_r, s1_l, s1_r, out_l, out_r;

            /* Extract Frame 0 */
            if (bits_per_sample == 8) {
                const uint8_t *f0 = pcm_data + (idx0 * in_frame_sz);
                s0_l = ((int32_t)f0[0] - 128) << 8;
                s0_r = (channels >= 2) ? (((int32_t)f0[1] - 128) << 8) : s0_l;
            } else if (bits_per_sample == 16) {
                const uint8_t *f0 = pcm_data + (idx0 * in_frame_sz);
                s0_l = (int16_t)((uint16_t)f0[0] | ((uint16_t)f0[1] << 8));
                s0_r = (channels >= 2) ? (int16_t)((uint16_t)f0[2] | ((uint16_t)f0[3] << 8)) : s0_l;
            } else if (bits_per_sample == 24) {
                const uint8_t *f0 = pcm_data + (idx0 * in_frame_sz);
                s0_l = (int32_t)(((uint32_t)f0[0] << 8) | ((uint32_t)f0[1] << 16) | ((uint32_t)f0[2] << 24)) >> 16;
                s0_r = (channels >= 2) ? ((int32_t)(((uint32_t)f0[3] << 8) | ((uint32_t)f0[4] << 16) | ((uint32_t)f0[5] << 24)) >> 16) : s0_l;
            }

            /* Extract Frame 1 */
            if (bits_per_sample == 8) {
                const uint8_t *f1 = pcm_data + (idx1 * in_frame_sz);
                s1_l = ((int32_t)f1[0] - 128) << 8;
                s1_r = (channels >= 2) ? (((int32_t)f1[1] - 128) << 8) : s1_l;
            } else if (bits_per_sample == 16) {
                const uint8_t *f1 = pcm_data + (idx1 * in_frame_sz);
                s1_l = (int16_t)((uint16_t)f1[0] | ((uint16_t)f1[1] << 8));
                s1_r = (channels >= 2) ? (int16_t)((uint16_t)f1[2] | ((uint16_t)f1[3] << 8)) : s1_l;
            } else if (bits_per_sample == 24) {
                const uint8_t *f1 = pcm_data + (idx1 * in_frame_sz);
                s1_l = (int32_t)(((uint32_t)f1[0] << 8) | ((uint32_t)f1[1] << 16) | ((uint32_t)f1[2] << 24)) >> 16;
                s1_r = (channels >= 2) ? ((int32_t)(((uint32_t)f1[3] << 8) | ((uint32_t)f1[4] << 16) | ((uint32_t)f1[5] << 24)) >> 16) : s1_l;
            }

            /* Linear interpolation */
            out_l = (s0_l * (int32_t)(65536 - frac) + s1_l * (int32_t)frac) >> 16;
            out_r = (s0_r * (int32_t)(65536 - frac) + s1_r * (int32_t)frac) >> 16;

            /* Soft clamp to 16-bit range */
            if (out_l > 32767) out_l = 32767;
            else if (out_l < -32768) out_l = -32768;
            if (out_r > 32767) out_r = 32767;
            else if (out_r < -32768) out_r = -32768;

            out_buf[i * 2 + 0] = (int16_t)out_l;
            out_buf[i * 2 + 1] = (int16_t)out_r;
        }

        res = usb_audio_play_pcm(audio, out_buf, out_bytes, 48000, 2, 16);
        kfree(out_buf);
        return res;
    }
}

int usb_audio_play_file_dev(const char *dev_name, const char *path) {
    block_dev_t *bdev;
    fat_fs_t fs;
    char full_path[256];
    uint32_t file_size = 0;
    uint8_t attr = 0;
    uint8_t *buf;
    size_t bytes_read = 0;
    const char *candidates[4];
    size_t c;
    int res;

    if (!path || path[0] == '\0') return -1;

    if (path[0] != '/') {
        snprintf(full_path, sizeof(full_path), "/%s", path);
    } else {
        strncpy(full_path, path, sizeof(full_path) - 1);
        full_path[sizeof(full_path) - 1] = '\0';
    }

    candidates[0] = (dev_name && dev_name[0]) ? dev_name : "usb0";
    candidates[1] = "usb0";
    candidates[2] = "usb1";
    candidates[3] = "ata0";

    bdev = NULL;
    for (c = 0; c < 4; c++) {
        bdev = blockdev_get(candidates[c]);
        if (bdev && fat_mount(bdev, &fs) == 0) {
            if (fat_stat(&fs, full_path, &file_size, &attr) == 0 ||
                (full_path[0] == '/' && fat_stat(&fs, full_path + 1, &file_size, &attr) == 0)) {
                break; /* Found valid filesystem and file! */
            }
        }
        bdev = NULL;
    }

    if (!bdev) {
        kprint_color(0x4F, "File '%s' not found on any mounted drive.\n", full_path);
        return -1;
    }

    if (attr & FAT_ATTR_DIRECTORY) {
        kprint_color(0x4F, "'%s' is a directory.\n", full_path);
        return -1;
    }

    if (file_size < 44 || file_size > 16 * 1024 * 1024) {
        kprint_color(0x4F, "Invalid file size (%u bytes).\n", file_size);
        return -1;
    }

    buf = (uint8_t*)kmalloc(file_size + 64);
    if (!buf) {
        kprint_color(0x4F, "Out of memory allocating %u bytes for audio file.\n", file_size);
        return -1;
    }

    if (fat_read_file(&fs, full_path, buf, file_size, &bytes_read) != 0) {
        if (full_path[0] != '/' || fat_read_file(&fs, full_path + 1, buf, file_size, &bytes_read) != 0) {
            kfree(buf);
            kprint_color(0x4F, "Failed to read file '%s'.\n", full_path);
            return -1;
        }
    }

    res = usb_audio_play_wav(buf, bytes_read);
    kfree(buf);
    return res;
}

int usb_audio_play_file(const char *path) {
    return usb_audio_play_file_dev("usb0", path);
}

static void audio_bg_task(void *arg) {
    audio_bg_ctx_t *ctx = (audio_bg_ctx_t*)arg;
    if (ctx && ctx->in_use) {
        usb_audio_play_file_dev(ctx->dev_name, ctx->path);
        ctx->in_use = false;
    }
    rtos_task_exit();
}

int usb_audio_play_file_dev_async(const char *dev_name, const char *path) {
    if (g_audio_bg_ctx.in_use) {
        kprint_color(0x0E, "[Audio] Background playback already active. Stopping previous stream...\n");
        usb_audio_stop_all();
        rtos_sleep_ms(50);
    }
    strncpy(g_audio_bg_ctx.path, path ? path : "", sizeof(g_audio_bg_ctx.path) - 1);
    g_audio_bg_ctx.path[sizeof(g_audio_bg_ctx.path) - 1] = '\0';
    strncpy(g_audio_bg_ctx.dev_name, dev_name ? dev_name : "usb0", sizeof(g_audio_bg_ctx.dev_name) - 1);
    g_audio_bg_ctx.dev_name[sizeof(g_audio_bg_ctx.dev_name) - 1] = '\0';
    g_audio_bg_ctx.in_use = true;

    rtos_task_create("audio_play", audio_bg_task, &g_audio_bg_ctx, RTOS_PRIORITY_HIGH, 16384);
    return 0;
}

int usb_audio_play_file_async(const char *path) {
    return usb_audio_play_file_dev_async("usb0", path);
}

void usb_audio_stop_all(void) {
    size_t i;
    for (i = 0; i < g_num_audio_devs; i++) {
        if (g_audio_devs[i].initialized) {
            usb_audio_stream_stop(&g_audio_devs[i]);
        }
    }
    g_audio_bg_ctx.in_use = false;
}
