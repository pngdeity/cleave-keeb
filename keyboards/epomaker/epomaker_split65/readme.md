# EPOMAKER Split65

A 65% wireless split keyboard.

> **In this repository, read `docs/HARDWARE.md` and `AGENTS.md` first.** The
> commands and DFU instructions below are the upstream/vendor shape and are
> **not correct for this tree**: bare `make` and `make ...:flash` are forbidden
> here (use `./bin/make`; flashing is a human-only action), Esc-hold bootmagic is
> broken on our build, and the left half has no reset switch — it enters DFU via
> the spacebar-hole short. Details in the root `docs/`.

* Keyboard Maintainer: [yangzheng20003](https://github.com/yangzheng20003)
* Hardware Supported: EPOMAKER Split65 (WB32FQ95)
* Hardware Availability: [epomaker](https://www.epomaker.com)

Make example for this keyboard (after setting up your build environment):

    make epomaker/epomaker_split65:default

Flashing example for this keyboard:

    make epomaker/epomaker_split65:default:flash

See the [build environment setup](https://docs.qmk.fm/#/getting_started_build_tools) and the [make instructions](https://docs.qmk.fm/#/getting_started_make_guide) for more information. Brand new to QMK? Start with our [Complete Newbs Guide](https://docs.qmk.fm/#/newbs).

## Bootloader

Both halves run identical firmware; handedness is pin-based, not EEPROM-based,
so there are no left/right build variants. Each half is flashed separately and
enters the bootloader by a **different** method.

* **Left half**: hold the `Escape` key while plugging in the USB-C cable. This
  also erases persistent settings. Alternatively, use the `QK_BOOT` keycode, or
  the physical reset switch on the underside of the PCB.
* **Right half**: the `Escape` method does not work. Remove the `R_Shift`
  keycap, flip the hidden toggle switch to its lower position, then short the
  two holes under the spacebar switch with tweezers while plugging in the right
  half's USB-C cable. Flip the switch back afterwards.

Flashing requires a **wired USB connection**; the 2.4 GHz dongle and Bluetooth
cannot be used to flash.
