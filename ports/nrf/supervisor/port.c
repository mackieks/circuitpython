/*
 * This file is part of the MicroPython project, http://micropython.org/
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2017 Scott Shawcroft for Adafruit Industries
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include <stdint.h>
#include "supervisor/port.h"
#include "boards/board.h"

#include "nrfx/hal/nrf_clock.h"
#include "nrfx/hal/nrf_power.h"
#include "nrfx/drivers/include/nrfx_power.h"
#include "nrfx/drivers/include/nrfx_rtc.h"

#include "nrf/cache.h"
#include "nrf/clocks.h"
#include "nrf/power.h"
#include "nrf/timers.h"

#include "shared-module/gamepad/__init__.h"
#include "common-hal/microcontroller/Pin.h"
#include "common-hal/_bleio/__init__.h"
#include "common-hal/analogio/AnalogIn.h"
#include "common-hal/busio/I2C.h"
#include "common-hal/busio/SPI.h"
#include "common-hal/busio/UART.h"
#include "common-hal/pulseio/PWMOut.h"
#include "common-hal/pulseio/PulseOut.h"
#include "common-hal/pulseio/PulseIn.h"
#include "common-hal/rtc/RTC.h"
#include "common-hal/neopixel_write/__init__.h"

#include "shared-bindings/rtc/__init__.h"

#ifdef CIRCUITPY_AUDIOBUSIO
#include "common-hal/audiobusio/I2SOut.h"
#endif

#ifdef CIRCUITPY_AUDIOPWMIO
#include "common-hal/audiopwmio/PWMAudioOut.h"
#endif

static void power_warning_handler(void) {
    reset_into_safe_mode(BROWNOUT);
}

const nrfx_rtc_t rtc_instance = NRFX_RTC_INSTANCE(2);

const nrfx_rtc_config_t rtc_config = {
    .prescaler = RTC_FREQ_TO_PRESCALER(0x8000),
    .reliable = 0,
    .tick_latency = 0,
    .interrupt_priority = 6
};

static volatile uint64_t overflowed_ticks = 0;

void rtc_handler(nrfx_rtc_int_type_t int_type) {
    if (int_type == NRFX_RTC_INT_OVERFLOW) {
        // Our RTC is 24 bits and we're clocking it at 32.768khz which is 32 (2 ** 5) subticks per
        // tick.
        overflowed_ticks += (1L<< (24 - 5));
    } else if (int_type == NRFX_RTC_INT_TICK && nrfx_rtc_counter_get(&rtc_instance) % 32 == 0) {
        // Do things common to all ports when the tick occurs
        supervisor_tick();
    } else if (int_type == NRFX_RTC_INT_COMPARE0) {
        nrfx_rtc_cc_set(&rtc_instance, 0, 0, false);
    }
}

void tick_init(void) {
    if (!nrf_clock_lf_is_running(NRF_CLOCK)) {
        nrf_clock_task_trigger(NRF_CLOCK, NRF_CLOCK_TASK_LFCLKSTART);
    }
    nrfx_rtc_counter_clear(&rtc_instance);
    nrfx_rtc_init(&rtc_instance, &rtc_config, rtc_handler);
    nrfx_rtc_enable(&rtc_instance);
    nrfx_rtc_overflow_enable(&rtc_instance, true);
}

safe_mode_t port_init(void) {
    nrf_peripherals_clocks_init();

    // If GPIO voltage is set wrong in UICR, this will fix it, and
    // will also do a reset to make the change take effect.
    nrf_peripherals_power_init();

    nrfx_power_pofwarn_config_t power_failure_config;
    power_failure_config.handler = power_warning_handler;
    power_failure_config.thr = NRF_POWER_POFTHR_V27;
    #if NRF_POWER_HAS_VDDH
    power_failure_config.thrvddh = NRF_POWER_POFTHRVDDH_V27;
    #endif
    nrfx_power_pof_init(&power_failure_config);
    nrfx_power_pof_enable(&power_failure_config);

    nrf_peripherals_enable_cache();

    // Configure millisecond timer initialization.
    tick_init();

#if CIRCUITPY_RTC
    common_hal_rtc_init();
#endif

#if CIRCUITPY_ANALOGIO
    analogin_init();
#endif

    return NO_SAFE_MODE;
}

void reset_port(void) {
#ifdef CIRCUITPY_GAMEPAD_TICKS
    gamepad_reset();
#endif

#if CIRCUITPY_BUSIO
    i2c_reset();
    spi_reset();
    uart_reset();
#endif

#if CIRCUITPY_NEOPIXEL_WRITE
    neopixel_write_reset();
#endif

#if CIRCUITPY_AUDIOBUSIO
    i2s_reset();
#endif

#if CIRCUITPY_AUDIOPWMIO
    audiopwmout_reset();
#endif


#if CIRCUITPY_PULSEIO
    pwmout_reset();
    pulseout_reset();
    pulsein_reset();
#endif

    timers_reset();

#if CIRCUITPY_BLEIO
    bleio_reset();
#endif

    reset_all_pins();
}

void reset_to_bootloader(void) {
    enum { DFU_MAGIC_SERIAL = 0x4e };

    NRF_POWER->GPREGRET = DFU_MAGIC_SERIAL;
    reset_cpu();
}

void reset_cpu(void) {
    NVIC_SystemReset();
}

// The uninitialized data section is placed directly after BSS, under the theory
// that Circuit Python has a lot more .data and .bss than the bootloader.  As a
// result, this section is less likely to be tampered with by the bootloader.
extern uint32_t _euninitialized;

uint32_t *port_heap_get_bottom(void) {
    return &_euninitialized;
}

uint32_t *port_heap_get_top(void) {
    return port_stack_get_top();
}

uint32_t *port_stack_get_limit(void) {
    return &_euninitialized;
}

uint32_t *port_stack_get_top(void) {
    return &_estack;
}

// Place the word in the uninitialized section so it won't get overwritten.
__attribute__((section(".uninitialized"))) uint32_t _saved_word;
void port_set_saved_word(uint32_t value) {
    _saved_word = value;
}

uint32_t port_get_saved_word(void) {
    return _saved_word;
}

uint64_t port_get_raw_ticks(uint8_t* subticks) {
    uint32_t rtc = nrfx_rtc_counter_get(&rtc_instance);
    if (subticks != NULL) {
        *subticks = (rtc % 32);
    }
    return overflowed_ticks + rtc / 32;
}

// Enable 1/1024 second tick.
void port_enable_tick(void) {
    nrfx_rtc_tick_enable(&rtc_instance, true);
}

// Disable 1/1024 second tick.
void port_disable_tick(void) {
    nrfx_rtc_tick_disable(&rtc_instance);
}

void port_interrupt_after_ticks(uint32_t ticks) {
    uint32_t current_ticks = nrfx_rtc_counter_get(&rtc_instance);
    uint32_t diff = 3;
    if (ticks > diff) {
        diff = ticks * 32;
    }
    if (diff > 0xffffff) {
        diff = 0xffffff;
    }
    nrfx_rtc_cc_set(&rtc_instance, 0, current_ticks + diff, true);
}

void port_sleep_until_interrupt(void) {
    // Clear the FPU interrupt because it can prevent us from sleeping.
    if (NVIC_GetPendingIRQ(FPU_IRQn)) {
        __set_FPSCR(__get_FPSCR()  & ~(0x9f));
        (void) __get_FPSCR();
        NVIC_ClearPendingIRQ(FPU_IRQn);
    }
    uint8_t sd_enabled;

    sd_softdevice_is_enabled(&sd_enabled);
    if (sd_enabled) {
        sd_app_evt_wait();
    } else {
        // Call wait for interrupt ourselves if the SD isn't enabled.
        __WFI();
    }
}


void HardFault_Handler(void) {
    reset_into_safe_mode(HARD_CRASH);
    while (true) {
        asm("nop;");
    }
}
