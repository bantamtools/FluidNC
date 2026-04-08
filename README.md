# Bantam Tools FluidNC v2.5.0 - source release

Corresponding source for Bantam Tools FluidNC firmware v2.5.0, a modified version of
[FluidNC](https://github.com/bdring/FluidNC), distributed under the GNU General Public
License v3 (see `LICENSE` and `NOTICE`).

## Building

Requires [PlatformIO](https://platformio.org/). Clone with submodules:

    git clone --recurse-submodules https://github.com/bantamtools/FluidNC
    cd FluidNC
    git checkout v2.5.0
    git submodule update --init --recursive
    pio run -e wifi_s3            # firmware.bin
    pio run -e wifi_s3_usb-otg    # USB-OTG variant (where present in platformio.ini)
    pio run -e wifi_s3 -t buildfs # littlefs.bin from FluidNC/data

Some releases also define board-variant environments (for example `wifi_s3-1824` and
`wifi_s3-2436`), built the same way with `pio run -e <env>`. All environments are listed in
`platformio.ini`.

`build-release.py` assembles the release image set. Building from a source archive without
git metadata reports the version as `noGit`.

All library dependencies are pinned in `platformio.ini` and `.gitmodules` to public repositories.
Dependencies that the original release referenced by branch are pinned here to the exact
revisions every release build fetched: each such dependency's branch head predates v1.11.0 and
has not moved since.

## Flashing (ESP32-S3)

    esptool.py --chip esp32s3 write_flash 0x10000 firmware.bin 0x3D0000 littlefs.bin

This updates the application and filesystem built from this source. Use the `firmware.bin`
of the environment you built (for example `wifi_s3` or `wifi_s3_usb-otg`). For a full image
set including the bootloader and partition table, see the scripts in
`install_scripts/` and `build-release.py`, which give the exact offsets used for the released
images.

Partition layout: `min_littlefs.csv`.

## Web UI

`FluidNC/data/index.html.gz` is built from [bantamtools/ESP3D-WEBUI-BT](https://github.com/bantamtools/ESP3D-WEBUI-BT)
at commit `f5aa58a412f59a803a99631fd83a4c482b1aa1b9` (rebuilt and verified against the shipped file) with:

    git -c core.autocrlf=true clone https://github.com/bantamtools/ESP3D-WEBUI-BT && cd ESP3D-WEBUI-BT && git checkout f5aa58a412f59a803a99631fd83a4c482b1aa1b9 && npm ci && npx gulp

The source that is edited is `dist/` plus `gulpfile.js`. The `www/` directory holds the
original upstream ESP3D-WEBUI source that `dist/` was derived from.

## License

GNU General Public License v3 or later. Third-party libraries keep their own licenses
(arduino-esp32, WiFi and arduinoWebSockets: LGPL-2.1; tinyxml2: zlib; TMCStepper: MIT;
esp8266-oled-ssd1306: MIT-style; platform-espressif32: Apache-2.0 build tooling).
