# Bill of materials (BOM)

The selected assembly is a **USB-powered Waveshare LCD-7 touchscreen with an external SHT40 sensor**. Quantities below build one unit. See [verification](../docs/verification.md) for implementation and validation status.

## Electronics and cables

|        Qty | Component                                                                                    | Compatibility / included items                                                                                                                                                                                |
| ---------: | -------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
|          1 | [Waveshare ESP32-S3-Touch-LCD-7, SKU 27078](https://docs.waveshare.com/ESP32-S3-Touch-LCD-7) | 7-inch, 800×480, **with touch**, integrated ESP32-S3 and 8 MB PSRAM. Current documentation lists 16 MB flash; older 8 MB revisions also fit the firmware layout. Do not substitute 7B or non-touch SKU 30406. |
| 1 included | HY2.0 4-pin to 2.54 mm male harness, 10 cm                                                   | Manufacturer lists this I²C harness in the kit; confirm the seller's package contents. No separate harness purchase if supplied.                                                                              |
|          1 | [Adafruit SHT40 module, 4885](https://www.adafruit.com/product/4885)                         | Assembled sensor with STEMMA QT/Qwiic sockets, I²C address 0x44. Its QT cable is separate.                                                                                                                    |
|          1 | [Adafruit 4397, QT to female sockets, 150 mm](https://www.adafruit.com/product/4397)         | Joins the supplied male harness to the sensor without soldering. It is not a QT-to-QT cable.                                                                                                                  |
|          1 | Regulated USB supply, 5 V, at least 2 A                                                      | Reuse a suitable supply or buy one for the local socket. With a multiport supply, reserve its output capacity for PACE.                                                                                       |
|          1 | USB data/power cable, USB-C at the board                                                     | Match the supply and computer connector; a charge-only cable cannot flash firmware. Check wall-socket reach.                                                                                                  |

Set the sensor supply selector to **3.3 V** and verify every signal using [wiring](wiring.md). The board's HY2.0 socket and the sensor's JST-SH socket are different connectors. Record the received board/panel revision before integrating drivers.

## Sensor choice

**SHT40 is the reference sensor.** Its [manufacturer specification](https://sensirion.com/products/catalog/SHT40) gives ±0.2 °C typical temperature accuracy; enclosure heat still affects the installed result. The assembled Adafruit board provides sockets and fits the existing shared I²C bus. Leave the sensor heater off for room-temperature measurements.

## Mounting parts

Use **enclosure A**, generated from [the CAD source and assembly guide](enclosure.md). The parts below complete one assembly; small-pack leftovers are sufficient fastener spares.

|    Qty | Component                                                       | Exact specification / use                                                                                                                    |
| -----: | --------------------------------------------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------- |
| 1 each | Printed rear cover and wall plate, revision A                   | PETG; reserve 200 g of 1.75 mm filament including retry allowance.                                                                            |
|      5 | M3 × 25 socket-head screws                                      | DIN 912 / ISO 4762; four display mounts and one underside lock; 5.5 mm head diameter, 3 mm head height.                                      |
|      1 | M3 hex nut                                                      | DIN 934; 5.5 mm across flats, 2.4 mm high; captive lift-off lock.                                                                            |
|      2 | M2.5 × 8 socket-head screws                                     | DIN 912 / ISO 4762; sensor mounting; 4.5 mm head diameter, 2.5 mm head height.                                                               |
|      2 | M2.5 hex nuts                                                   | DIN 934; 5 mm across flats, 2 mm high; sensor cradle pockets.                                                                                |
| 2 each | Fischer DuoPower 6 × 30 anchors and 4.5 × 40 countersunk screws | Matched set, e.g. manufacturer pack 534997; reference for sound masonry/concrete and a 4 mm plate. See installation conditions in the guide. |
|      3 | Nylon cable ties, 100 × 2.5 mm                                  | USB strain relief, sensor lead and harness retention; trim tails.                                                                            |

The cover is specific to SKU 27078. Use the guide’s first-build fit and load checks.

## Spares and tools

Spares are optional: keep leftover fasteners and a known-good USB data cable; an extra sensor cable can help with fault isolation. The quantities above are sufficient for one assembly.

Have a multimeter, 2 mm and 2.5 mm hex keys, a driver matching the wall screws, a level and a drill with a 6 mm masonry bit. A 200 × 200 mm PETG-capable printer is sufficient; see the print guide. Assembly needs no soldering, separate controller, battery or debugger.

## Availability and substitutions

Espressif lists a [minimum 12-year commitment from 1 January 2021](https://www.espressif.com/en/products/longevity-commitment) for ESP32-S3. This covers the chip family, not Waveshare’s complete display. Recheck distributor stock, lifecycle notices and exact revisions before each hardware release as required by QR-10.

- **Display:** order SKU 27078 with touch. Similar names and the same ESP32 chip do not establish compatible pins, panel timing or dimensions. A replacement board needs its own tested profile.
- **Sensor:** another assembled SHT40 breakout is a candidate after checking supply, pull-ups, address, connector and fit. SHT41/SHT45 or DS18B20 modules need a reviewed driver/accuracy decision; do not silently substitute them.
- **Cable/power/mounting:** match electrical ratings, connector pitch, wiring and mechanical dimensions; no specific retailer is required.

Check local stock, shipping and taxes before ordering; retailer prices and availability change.
