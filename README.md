# CrowPanel 7" Kids Games

Simple touchscreen games for toddlers (made for a 3-year-old), running on the
**Elecrow CrowPanel 7.0 (basic)** ESP32-S3 display. No reading needed: big
pictures, big touch targets, and a confetti party for every win.

## Games

A home screen with four big tiles. A house button (top left) in each game goes back home.

- **Maze**: drag the character to the pink star. The cat, car and dino take turns,
  gliding from square to square and leaving a rainbow trail. Mazes start tiny (3×2)
  and grow to 6×4. Movement is forgiving: the character follows the finger if it's
  up to two squares ahead along the path. Each win adds a gold star at the top.
- **Draw**: finger painting with 7 rainbow colors, dark, and an eraser. The
  round-arrow button clears the page.
- **Bubbles**: colorful bubbles float up; touch them to pop. Drawn off-screen
  first, so there's no flicker.
- **Memory**: flip two cards to find a match (star, cat, car, dino, heart, ball).
  Switches between 3 and 4 pairs.

## Hardware

This is for the **basic CrowPanel 7.0**, not the CrowPanel **ADVANCE** 7.0.
They look alike but use different modules and wiring.

| | Basic 7.0 (this project) | ADVANCE 7.0 |
|---|---|---|
| Module | ESP32-S3-WROOM-1-**N4R8** | ESP32-S3-WROOM-1-N16R8 |
| Flash | **4 MB** | 16 MB |
| PSRAM | 8 MB octal | 8 MB octal |

Not sure which one you have? With [esptool](https://github.com/espressif/esptool) installed, run:

```bash
python -m esptool --chip esp32s3 flash-id
```

If it says `Detected flash size: 4MB`, you have the basic board.

**Tested on:** a board revision with a PCA9557 I/O expander at I2C `0x18` (V3.x)
that holds the GT911 touch controller's reset line. The firmware pulses it at
startup, and also works without the expander (earlier revisions), but only the
V3 board has been tested.

If firmware built for a 16 MB board was flashed onto a 4 MB board, the serial log
shows a boot loop with:

```
Detected size(4096k) smaller than the size in the binary image header(16384k)
```

The screen stays black. Flashing this project fixes it.

## Build and flash

Needs [PlatformIO](https://platformio.org/) (CLI or the VS Code extension).

```bash
pio run -t upload
```

PlatformIO finds the board's serial port (CH340) automatically. If you have
several serial ports, add `--upload-port COM3` (or `/dev/ttyUSB0`). If the board
won't connect, hold **BOOT**, tap **RESET**, and release **BOOT**.

Serial monitor (115200 baud) shows startup info, including which I2C devices were found:

```bash
pio device monitor
```

**Windows:** if the build fails with "The filename or extension is too long",
move the project to a short path such as `C:\dev\crowpanel7-kids-games`.

## Credits

- Graphics: [LovyanGFX](https://github.com/lovyan03/LovyanGFX)
- Display pin and timing settings follow Elecrow's published LovyanGFX setup for
  the CrowPanel 7.0.

## License

MIT. See [LICENSE](LICENSE).
