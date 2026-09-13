# CYD Life Morphing

CYD Life Morphing is a real-time Conway's Game of Life visualizer for the ESP32-based Cheap Yellow Display (CYD). Instead of replacing one generation with the next as a blocky grid, it interprets births and deaths as movement and renders each transition as a field of soft, traveling light.

The application is written in C with ESP-IDF and uses the [DGX graphics component](https://components.espressif.com/components/jef-sure/dgx) to drive the ILI9341 display.

## Highlights

- **Animated Life transitions** - each generation morphs into the next over one second instead of changing instantly.
- **Directional movement** - newly born cells collect short segments from neighboring live cells, creating visible trails between generations.
- **Additive radial glow** - moving, static, and fading cells contribute to a per-pixel luminance field with saturated additive blending.
- **Temporal smoothing** - double-buffered glow maps and smoothstep interpolation reduce flicker and abrupt intensity changes.
- **Six built-in patterns** - oscillators, gliders collision, and methuselahs (Gliders, Navy T-tetromino, Beacon, Toad, Pulsar, and R-pentomino).
- **Pattern switching** - one press of the CYD BOOT button selects the next seed; holding the button does not repeatedly switch patterns.
- **Memory-aware rendering** - initialization reduces cell size until the virtual screen and renderer buffers fit in available RAM.
- **Live performance reporting** - completed render-and-transfer frames and step counts are reported to the serial log once per second.
- **Optimized rendering pipeline** - direct virtual-screen buffer writes, circular-span bounding, and integer fixed-point math sustain ~25 FPS on the ESP32.

## How It Works

The simulation uses the standard Conway's Game of Life rules on a finite, non-wrapping grid:

- A live cell survives with two or three live neighbors.
- A dead cell becomes alive when it has exactly three live neighbors.
- Every other cell dies or remains dead.

For rendering, each cell is classified by its state in the current and next generations:

| Current | Next | Rendering behavior |
| --- | --- | --- |
| Dead | Dead | No contribution |
| Alive | Dead | Fades unless used as a source for a birth |
| Dead | Alive | Receives animated segments from live neighbors |
| Alive | Alive | Remains as a static glow source |

Segment endpoints advance at different rates to produce a short moving tail. The renderer accumulates radial contributions into an 8-bit glow map, blends it with the previous frame, converts the result to RGB565, and transfers the virtual screen to the display over SPI.

If a pattern becomes extinct or reaches a still life, the application starts that seed again.

## Built-in Patterns

Press the BOOT button to cycle through the 6 included patterns:

| Pattern | Grid | Type | Description |
| --- | :---: | :---: | --- |
| **Gliders** | 10x11 | Collision | 4 gliders flying inward from the corners, colliding in the center into 4 still blocks. |
| **Navy (T-Tetromino)** | 9x9 | Methuselah | Compact 4-cell T-shape that rapidly expands, oscillates, and stabilizes. |
| **Beacon** | 6x6 | Period-2 Oscillator | Two diagonally adjacent 2x2 blocks with touching corners alternately blinking on and off. |
| **Toad** | 6x4 | Period-2 Oscillator | Two offset 3-cell rows pulsating between horizontal and vertical forms. |
| **Pulsar** | 15x15 | Period-3 Oscillator | Large, highly symmetrical (8-fold D4 symmetry) pulsating flower pattern. |
| **R-pentomino** | 30x25 | Methuselah | Classic 5-cell seed evolving through a long, complex cascade of births and moving debris. |

## Controls

Press the CYD **BOOT** button on `GPIO 0` to cycle through the built-in patterns. The button uses a press/release state machine, so it must be released before another press is accepted.

The serial monitor reports initialization, allocation fallback, generation steps, pattern restarts, and measured FPS:

```text
I (615) cyd-life-morphing: life transformation initialized with max cell width 21
I (616) cyd-life-morphing: CYD display initialized: 320x240, cell 21px
I (617) cyd-life-morphing: Step #1
I (1645) cyd-life-morphing: FPS: 25.1
I (1646) cyd-life-morphing: Step #2
...
I (13670) cyd-life-morphing: Restarting pattern (still life) after 13 steps
```

## Hardware

The current pin configuration targets a classic ESP32 Cheap Yellow Display with a 320x240 ILI9341 panel.

| Function | GPIO |
| --- | ---: |
| TFT MOSI | 13 |
| TFT MISO | 12 |
| TFT clock | 14 |
| TFT chip select | 15 |
| TFT data/command | 2 |
| TFT backlight | 21 |
| BOOT button | 0 |
| TFT reset | Not connected |

The display uses `SPI2_HOST`, RGB565 color, and a 40 MHz SPI clock. Display orientation is configured in `cyd_init_display()`.

CYD boards exist in several revisions. Check your board's schematic before changing the display driver or pin assignments.

## Requirements

- ESP32-based CYD with an ILI9341 display
- USB data cable and access to the board's serial port
- ESP-IDF environment with `idf.py` available

The component manifest requires ESP-IDF 4.1 or newer. The [DGX component](https://components.espressif.com/components/jef-sure/dgx) is declared in [main/idf_component.yml](main/idf_component.yml) and is resolved by the ESP-IDF Component Manager.

## Build, Flash, and Monitor

From an ESP-IDF shell in the project root:

```sh
idf.py set-target esp32
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor
```

Replace `/dev/ttyUSB0` with the serial device for your board. On Linux, it is commonly `/dev/ttyUSB0` or `/dev/ttyACM0`. Exit the ESP-IDF monitor with `Ctrl+]`.

For subsequent builds, `idf.py build` is sufficient. To erase stored flash contents before reflashing:

```sh
idf.py -p /dev/ttyUSB0 erase-flash
idf.py -p /dev/ttyUSB0 flash monitor
```

The default configuration in [sdkconfig.defaults](sdkconfig.defaults) selects the ESP32 target and enables DGX SPI, ILI9341, and virtual-screen support.

## Project Structure

```text
.
|-- CMakeLists.txt             ESP-IDF project definition
|-- main/
|   |-- CMakeLists.txt         Main component definition
|   |-- idf_component.yml      DGX dependency declaration
|   `-- main.c                 Simulation, renderer, controls, and display setup
|-- sdkconfig.defaults         Reproducible DGX and target defaults
`-- LICENSE                    MIT license
```

The main renderer state is owned by `LifeTransformation`. It contains the active generations, grid geometry, radial lookup table, DGX virtual screen, double glow buffers, source-cell bitset, and the selected seed factory. Keeping these resources together makes pattern replacement and allocation cleanup explicit.

## Adding Patterns

Create a function that allocates a `LifeGeneration`, sets its live cells, and returns it. Then add the function to the null-terminated `life_types` array in [main/main.c](main/main.c):

```c
LifeGeneration *create_my_pattern(void)
{
	LifeGeneration *life = create_life(20, 15);
	if (life == NULL) {
		return NULL;
	}

	life->cells[CELL_OFFSET(10, 7, life->width)] = 1;
	return life;
}

life_creation_func_t life_types[] = {
	create_initial_gliders_life,
	create_initial_navy_life,
	create_my_pattern,
	NULL,
};
```

Keep pattern dimensions modest: the RGB565 virtual screen requires two bytes per pixel, and each glow buffer requires one byte per pixel. The application automatically retries with smaller cells if the initial allocation does not fit.

## Development

Format the source with the repository's ClangFormat configuration:

```sh
clang-format -i main/main.c
clang-format --dry-run --Werror main/main.c
```

Build warnings are treated seriously by the ESP-IDF toolchain. Run a full build before flashing changes:

```sh
idf.py build
```

## License

This project is available under the [MIT License](LICENSE).
