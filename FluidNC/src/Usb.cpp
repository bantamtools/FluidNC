// Copyright (c) 2025 -  Windell Oskay, Bantam Tools
// Copyright (c) 2023 -  Matt Staniszewski
// Copyright (c) 2021 -  Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

/*
 * USB Serial driver that accesses the ESP32 hardware via the Arduino Serial interface
 */

#include "Usb.h"
#include "Protocol.h"  //  g_usb_tx_drops / g_usb_last_tx_ms
#include "UsbRxDrain.h"

#include <soc/soc_caps.h>
#if ARDUINO_USB_MODE && defined(SOC_USB_SERIAL_JTAG_SUPPORTED) && SOC_USB_SERIAL_JTAG_SUPPORTED
#    define FLUIDNC_USB_SERIAL_JTAG_DRAIN 1
#    include <hal/usb_serial_jtag_ll.h>
#    include <freertos/FreeRTOS.h>
#    include <freertos/task.h>
#    include <esp_timer.h>
#endif

Usb::Usb() : _usb_num(0) {}

void Usb::begin() {
    Serial.setRxBufferSize(2048);  // Increase from default 256 to 2048 bytes
#if ARDUINO_USB_MODE
    // HWCDC (ROM JTAG) has setTxBufferSize
    Serial.setTxBufferSize(2048);  // Also increase TX buffer for symmetry
#endif
    // USBCDC (TinyUSB) doesn't have setTxBufferSize method
    Serial.begin(115200);
#ifdef FLUIDNC_USB_SERIAL_JTAG_DRAIN
    //  Discard OUT data stranded across HWCDC::begin(), which cleared the
    // RECV_PKT status that announced it, plus the host backlog queued behind it (see
    // UsbRxDrain.h). This must run after begin(): a drain before it would let the
    // host's retried packet land and have its RECV_PKT wiped by begin(), stranding it
    // again. The HWCDC ISR is kept off the FIFO by masking the RECV_PKT interrupt
    // enable (begin() is the only other code that sets it), which holds whichever core
    // the ISR was allocated on. The int_ena read-modify-writes are done in a critical
    // section so they cannot interleave with the ISR's own IN_EMPTY updates on this
    // core. Re-enabling lets any RECV_PKT left raw-pending fire the ISR normally.
    static portMUX_TYPE ena_mux = portMUX_INITIALIZER_UNLOCKED;
    uint8_t             scratch[USB_SERIAL_JTAG_PACKET_SZ_BYTES];
    portENTER_CRITICAL(&ena_mux);
    usb_serial_jtag_ll_disable_intr_mask(USB_SERIAL_JTAG_INTR_SERIAL_OUT_RECV_PKT);
    portEXIT_CRITICAL(&ena_mux);
    _rx_drained_at_begin = drain_stranded_rx(
        [] { usb_serial_jtag_ll_clr_intsts_mask(USB_SERIAL_JTAG_INTR_SERIAL_OUT_RECV_PKT); },
        [] { return usb_serial_jtag_ll_rxfifo_data_available() != 0; },
        [&scratch] { return usb_serial_jtag_ll_read_rxfifo(scratch, sizeof(scratch)); },
        [] { return uint32_t(esp_timer_get_time()); },
        [] { vTaskDelay(1); },
        30 * 1000,    // quiet window: the host has stopped sending stale data
        500 * 1000);  // cap: a host that never stops cannot hold boot longer
    portENTER_CRITICAL(&ena_mux);
    usb_serial_jtag_ll_ena_intr_mask(USB_SERIAL_JTAG_INTR_SERIAL_OUT_RECV_PKT);
    portEXIT_CRITICAL(&ena_mux);
#endif
}

int Usb::read() {
    if (_pushback != -1) {
        int ret   = _pushback;
        _pushback = -1;
        return ret;
    }
    return Serial.read();
}

size_t Usb::write(uint8_t c) {
    size_t n = Serial.write(c);
    //  a short return == HWCDC tx_timeout (host stopped reading / ring full).
    // Sustained drops + a large "USB last tx age" at the moment of mute point at a
    // USB-Serial-JTAG peripheral drop rather than a software task stall.
    if (n < 1) {
        ++g_usb_tx_drops;
    } else {
        g_usb_last_tx_ms = millis();
    }
    return n;
}

size_t Usb::write(const uint8_t* buffer, size_t length) {
    size_t n = Serial.write(buffer, length);
    if (n < length) {
        ++g_usb_tx_drops;
    } else {
        g_usb_last_tx_ms = millis();
    }
    return n;
}

size_t Usb::timedReadBytes(char* buffer, size_t len, TickType_t timeout) {

    Serial.setTimeout(timeout);
    size_t res = Serial.readBytes(buffer, len);

    // If res < 0, no bytes were read

    return res < 0 ? 0 : res;
}

void Usb::config_message(const char* prefix, const char* usage) {
    log_info(prefix << usage);
}

int Usb::rx_buffer_available(void) {
    return 2048 - available();  // Updated for increased buffer size
}

int Usb::peek() {
    if (_pushback != -1) {
        return _pushback;
    }
    int ch = Serial.read();
    if (ch == -1) {
        return -1;
    }
    _pushback = ch;
    return ch;
}

int Usb::available() {
    return Serial.available() + (_pushback != -1);
}

void Usb::flushRx() {
    _pushback = -1;
    // Don't use Serial.flush(), which clears TX instead of RX
    while(Serial.available()) {
        Serial.read();
    }
}
