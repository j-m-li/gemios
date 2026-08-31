/*
 * This is free and unencumbered software released into the public domain.
 * GEMIOS Preemptive Real-Time Operating System
 *
 * USB Audio Class 1.0 (UAC 1.0) Driver - C-Media 0d8c:0014 Support
 */

#ifndef GEMIOS_USB_AUDIO_H
#define GEMIOS_USB_AUDIO_H

#include "types.h"
#include "usb_core.h"

/* USB Audio Subclasses */
#define USB_AUDIO_SUBCLASS_UNDEFINED       0x00
#define USB_AUDIO_SUBCLASS_AUDIOCONTROL    0x01
#define USB_AUDIO_SUBCLASS_AUDIOSTREAMING  0x02
#define USB_AUDIO_SUBCLASS_MIDISTREAMING   0x03

/* Audio Class-Specific AC Interface Descriptor Subtypes */
#define UAC_AC_HEADER                      0x01
#define UAC_AC_INPUT_TERMINAL              0x02
#define UAC_AC_OUTPUT_TERMINAL             0x03
#define UAC_AC_MIXER_UNIT                  0x04
#define UAC_AC_SELECTOR_UNIT               0x05
#define UAC_AC_FEATURE_UNIT                0x06
#define UAC_AC_PROCESSING_UNIT             0x07
#define UAC_AC_EXTENSION_UNIT              0x08

/* Audio Class-Specific AS Interface Descriptor Subtypes */
#define UAC_AS_GENERAL                     0x01
#define UAC_AS_FORMAT_TYPE                 0x02
#define UAC_AS_FORMAT_SPECIFIC             0x03

/* Audio Class-Specific Endpoint Descriptor Subtypes */
#define UAC_EP_GENERAL                     0x01

/* UAC 1.0 Class-Specific Request Codes */
#define UAC_REQ_SET_CUR                    0x01
#define UAC_REQ_GET_CUR                    0x81
#define UAC_REQ_SET_MIN                    0x02
#define UAC_REQ_GET_MIN                    0x82
#define UAC_REQ_SET_MAX                    0x03
#define UAC_REQ_GET_MAX                    0x83
#define UAC_REQ_SET_RES                    0x04
#define UAC_REQ_GET_RES                    0x84

/* Feature Unit Control Selectors */
#define UAC_FU_CONTROL_MUTE                0x01
#define UAC_FU_CONTROL_VOLUME              0x02
#define UAC_FU_CONTROL_BASS                0x03
#define UAC_FU_CONTROL_MID                 0x04
#define UAC_FU_CONTROL_TREBLE              0x05
#define UAC_FU_CONTROL_GRAPHIC_EQ          0x06
#define UAC_FU_CONTROL_AGC                 0x07
#define UAC_FU_CONTROL_DELAY               0x08
#define UAC_FU_CONTROL_BASS_BOOST          0x09
#define UAC_FU_CONTROL_LOUDNESS            0x0A

/* Endpoint Control Selectors */
#define UAC_EP_CONTROL_SAMPLING_FREQ       0x01
#define UAC_EP_CONTROL_PITCH               0x02

/* Known Vendor/Product IDs */
#define USB_VID_CMEDIA                     0x0d8c
#define USB_PID_CMEDIA_AUDIO_ADAPTER       0x0014

#define USB_AUDIO_MAX_DEVICES              4

#define AUDIO_NUM_PERIODS                  3       /* Triple Buffering (3 periods) */
#define AUDIO_PERIOD_MS                    40      /* 40 ms duration per period buffer */
#define AUDIO_PERIOD_MAX_BYTES             (AUDIO_PERIOD_MS * 192) /* 7680 bytes (48kHz 16-bit Stereo) */

typedef struct {
    uint8_t buffer[AUDIO_NUM_PERIODS][AUDIO_PERIOD_MAX_BYTES];
    size_t period_bytes[AUDIO_NUM_PERIODS];
    size_t period_play_pos[AUDIO_NUM_PERIODS];
    uint8_t write_period;       /* Period index being filled by producer */
    uint8_t play_period;        /* Period index currently being sent to xHCI */
    size_t write_offset;        /* Bytes written in current write_period */
    volatile uint32_t queued_periods; /* Number of periods ready for playback (0..3) */
    volatile bool active;       /* Stream is actively playing */
    volatile bool stopping;     /* Stream is draining remaining periods */
    uint32_t total_played_bytes;
    uint32_t underruns;         /* Number of buffer underruns */
} audio_triple_buffer_t;

typedef struct {
    usb_device_t *dev;
    uint8_t slot_id;
    uint8_t ac_iface;          /* Audio Control Interface number (usually 0) */
    uint8_t as_out_iface;      /* Audio Streaming Playback Interface number (usually 1) */
    uint8_t as_out_alt;        /* Alternate setting with endpoint (usually 1) */
    uint8_t as_out_ep_addr;    /* Endpoint address (e.g. 0x01) */
    uint8_t as_out_ep_dci;     /* xHCI DCI (e.g. 2) */
    uint16_t as_out_max_packet;/* Max packet size (e.g. 200) */
    uint8_t feature_unit_id;   /* Feature Unit ID for master volume/mute (e.g. 9 for 0d8c:0014) */
    uint32_t sample_rate;      /* 48000 Hz default, or 44100 Hz */
    uint8_t channels;          /* 2 (Stereo) */
    uint8_t bits_per_sample;   /* 16-bit */
    uint8_t volume_percent;    /* 0 - 100% */
    bool is_muted;
    bool is_cmedia;
    bool stream_active;
    bool initialized;
    audio_triple_buffer_t tb;  /* Triple buffer subsystem */

    /* Capture (Microphone) properties */
    bool has_capture;
    uint8_t as_in_iface;       /* Audio Streaming Capture Interface number (e.g. 2) */
    uint8_t as_in_alt;         /* Alternate setting with endpoint (e.g. 1) */
    uint8_t as_in_ep_addr;     /* Endpoint address (e.g. 0x82) */
    uint8_t as_in_ep_dci;      /* xHCI DCI (e.g. 5) */
    uint16_t as_in_max_packet; /* Max packet size (e.g. 100) */
    uint8_t mic_feature_unit_id; /* Feature Unit ID for Mic volume/mute (e.g. 10 for 0d8c:0014) */
    uint32_t in_sample_rate;   /* 48000 Hz default */
    uint8_t in_channels;       /* 1 (Mono) */
    uint8_t in_bits_per_sample;/* 16-bit */
    uint8_t mic_volume_percent;/* 0 - 100% */
    bool mic_is_muted;
} usb_audio_device_t;

void usb_audio_init(void);
int usb_audio_init_device(usb_device_t *dev, usb_interface_t *iface);
usb_audio_device_t *usb_audio_get_default(void);
size_t usb_audio_get_device_count(void);
usb_audio_device_t *usb_audio_get_device(size_t index);

int usb_audio_set_volume(usb_audio_device_t *audio, uint8_t vol_percent);
int usb_audio_set_mute(usb_audio_device_t *audio, bool mute);
int usb_audio_set_sample_rate(usb_audio_device_t *audio, uint32_t sample_rate);

/* Microphone Controls */
int usb_audio_set_mic_volume(usb_audio_device_t *audio, uint8_t vol_percent);
int usb_audio_set_mic_mute(usb_audio_device_t *audio, bool mute);
int usb_audio_set_mic_sample_rate(usb_audio_device_t *audio, uint32_t sample_rate);

/* Triple Buffering Streaming API */
int usb_audio_stream_start(usb_audio_device_t *audio, uint32_t sample_rate, uint8_t channels, uint8_t bits);
int usb_audio_stream_write(usb_audio_device_t *audio, const void *pcm_data, size_t len, uint32_t timeout_ms);
int usb_audio_stream_flush(usb_audio_device_t *audio);
int usb_audio_stream_drain(usb_audio_device_t *audio);
void usb_audio_stream_stop(usb_audio_device_t *audio);
bool usb_audio_is_playing(usb_audio_device_t *audio);

/* High-level Playback API */
int usb_audio_play_pcm(usb_audio_device_t *audio, const void *pcm_data, size_t len, uint32_t sample_rate, uint8_t channels, uint8_t bits);
int usb_audio_play_tone(uint32_t freq_hz, uint32_t duration_ms);
int usb_audio_play_wav(const void *wav_data, size_t wav_len);
int usb_audio_play_file(const char *path);
int usb_audio_play_file_dev(const char *dev_name, const char *path);
int usb_audio_play_file_async(const char *path);
int usb_audio_play_file_dev_async(const char *dev_name, const char *path);
void usb_audio_stop_all(void);

/* Audio Recording API */
int usb_audio_record_pcm(usb_audio_device_t *audio, void *pcm_data, size_t max_bytes, uint32_t duration_ms, size_t *out_bytes);
int usb_audio_record_wav_file(const char *dev_name, const char *path, uint32_t duration_sec);

#endif /* GEMIOS_USB_AUDIO_H */
