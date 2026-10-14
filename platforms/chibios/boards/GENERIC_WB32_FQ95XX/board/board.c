/*
    Copyright (C) 2022 Westberry Technology (ChangZhou) Corp., Ltd

    Licensed under the Apache License, Version 2.0 (the "License");
    you may not use this file except in compliance with the License.
    You may obtain a copy of the License at

        http://www.apache.org/licenses/LICENSE-2.0

    Unless required by applicable law or agreed to in writing, software
    distributed under the License is distributed on an "AS IS" BASIS,
    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
    See the License for the specific language governing permissions and
    limitations under the License.
*/

/*
 * This file has been automatically generated using ChibiStudio board
 * generator plugin. Do not edit manually.
 */

#include "hal.h"

/*===========================================================================*/
/* Driver local definitions.                                                 */
/*===========================================================================*/

/*===========================================================================*/
/* Driver exported variables.                                                */
/*===========================================================================*/

/*===========================================================================*/
/* Driver local variables and types.                                         */
/*===========================================================================*/

/*===========================================================================*/
/* Driver local functions.                                                   */
/*===========================================================================*/

static void wb32_gpio_init(void) {
#if WB32_HAS_GPIOA
    rccEnableAPB1(RCC_APB1ENR_GPIOAEN);
#endif

#if WB32_HAS_GPIOB
    rccEnableAPB1(RCC_APB1ENR_GPIOBEN);
#endif

#if WB32_HAS_GPIOC
    rccEnableAPB1(RCC_APB1ENR_GPIOCEN);
#endif

#if WB32_HAS_GPIOD
    rccEnableAPB1(RCC_APB1ENR_GPIODEN);
#endif
}

/*===========================================================================*/
/* Driver interrupt handlers.                                                */
/*===========================================================================*/

/*===========================================================================*/
/* Driver exported functions.                                                */
/*===========================================================================*/
/*
 * Early initialization code.
 * This initialization must be performed just after stack setup and before
 * any other initialization.
 */
void __early_init(void) {
    wb32_clock_init();
    wb32_gpio_init();
}

/**
 * @brief   Board-specific initialization code.
 * @note    You can add your board-specific code here.
 */
void boardInit(void) {}

/*
 * The WB32 USB peripheral must not be torn down and restarted.
 *
 * This override restores upstream's own board-level fix, which was added in
 * `b5a52b4cff` ("Prevent USB peripheral fault when restarting USB on WB32 MCUs
 * (#18058)") and removed again in `bd2e59af60` ("Fixed WB32 MCU remote wakeup
 * issue (#20863)"). Neither commit's intent is our concern here; what matters is
 * that the removal left the *weak* default in `tmk_core/protocol/chibios/usb_main.c`
 * linked for this board, and that default is
 * `usbDisconnectBus` -> `usbStop` -> endpoint stops -> `wait_ms(50)` -> endpoint
 * restarts -> `usbStart`/`usbConnectBus` -- exactly the sequence the added commit
 * said "breaks it" on WB32.
 *
 * This is not a workaround we invented: it is upstream's own text, put back, and
 * it is the discriminator for whether the intermittent USB faults seen on this
 * board are this erratum. See `docs/HARDWARE.md`, "The USB restart erratum", for
 * the `nm`/`objdump` evidence that the weak default was being linked and for the
 * status boundary (present, not yet established as the cause).
 */
void restart_usb_driver(USBDriver *usbp) {
    (void)usbp;
}
