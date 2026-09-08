# CYD Life Morphing

An ESP-IDF application for the Cheap Yellow Display (CYD), an ESP32 board with an ILI9341 display. It uses the [DGX graphics component](https://components.espressif.com/components/jef-sure/dgx) to animate Conway's Game of Life transitions.

## What It Does

The application:

- Initializes the CYD's ILI9341 display over SPI and creates a 16-bit virtual screen.
- Creates a 9x9 starting Life pattern: a four-cell T shape centered in the grid.
- Calculates each following generation using the standard Conway's Game of Life rules with fixed, non-wrapping grid edges.
- Interpolates active cells into movement segments, then renders a one-second transition between generations.
- Transfers the virtual screen to the physical display for each animation frame.

## Hardware

The target is an ESP32-based Cheap Yellow Display with an ILI9341 panel.

| Signal | GPIO |
| --- | ---: |
| SPI MOSI | 13 |
| SPI MISO | 12 |
| SPI clock | 14 |
| TFT chip select | 15 |
| TFT data/command | 2 |
| TFT backlight | 21 |
| TFT reset | Not connected |

The display runs on `SPI2_HOST` at 40 MHz. Its orientation is configured as right-to-left and top-to-bottom.

## Prerequisites

- ESP-IDF 4.1 or newer. The development container uses an ESP-IDF image.
- An ESP32 CYD connected over USB.
- Python dependencies installed by the ESP-IDF environment.

The project declares `jef-sure/dgx` in [main/idf_component.yml](main/idf_component.yml). ESP-IDF Component Manager downloads it during configuration/build.

## Build And Flash

Run these commands from the project root in an ESP-IDF shell or the development container:

```sh
idf.py set-target esp32
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor
```

Replace `/dev/ttyUSB0` with the serial device used by your board. Exit the monitor with `Ctrl+]`.

For a fresh configuration, [sdkconfig.defaults](sdkconfig.defaults) enables DGX SPI support, the ILI9341 driver, and the DGX virtual-screen feature.

## Current Build Status

`idf.py build` completes successfully as of September 8, 2026 and produces `build/cyd-life-morphing.bin`.

The renderer is still a work in progress: it computes per-pixel colors without drawing them and currently emits warnings for that unused color and unused grid offsets. These are application implementation tasks, not ESP-IDF or DGX setup failures.

## Formatting

The development container installs `clang-format`. Project rules are in [.clang-format](.clang-format), and C/C++ files are configured to use the `xaver.clang-format` VS Code extension.

Format the current file with `Shift+Alt+F`, or format from the terminal:

```sh
clang-format -i main/main.c
```
