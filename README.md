# wallbox-powerboost-emulator

Experimental ESPHome external component firmware for a Waveshare ESP32-S3-RS485-CAN Industrial Controller. It reads live grid data from an existing DSMRloggerAPI P1 reader over local HTTP and emulates a selectable Carlo Gavazzi Modbus RTU meter over RS485 for Wallbox Copper SB Power Boost / Eco-Smart / Solar Charging experiments.

This project is an unofficial emulator. Wallbox officially supports Wallbox-delivered meters. Use this only for experimental load-balancing and solar-charging behavior validation.

## What It Does

- Polls DSMRloggerAPI at `http://<DSMRLOGGER_HOST>/api/v1/sm/actual`.
- Parses the real DSMRloggerAPI `actual` array by field `name`.
- Emulates one explicitly selected profile: EM112 PF.B, EM330DINAV53HS1X, or EM530DINAV53XS1X.
- Serves Modbus RTU as a slave/server over RS485.
- Exposes live values and compact diagnostics to Home Assistant through the ESPHome native API.
- Provides local debug pages at `/debug` and `/debug.json`.
- Keeps the bridge operating without Home Assistant as long as Wi-Fi and DSMRloggerAPI remain reachable.

## What It Does Not Do

- No MID compliance.
- No fiscal, reimbursement, billing, or legal metrology use.
- No CAN bus use.
- No MQTT dependency.
- No WiFiManager.
- No support for variants other than the three exact profiles documented here.

## Hardware

- Waveshare ESP32-S3-RS485-CAN Industrial Controller.
- Existing DSMRloggerAPI P1 reader on the same local network.
- Wallbox Copper SB configured to use an external compatible energy meter.
- RS485 wiring between the Wallbox meter RS485 terminals and the Waveshare RS485 A/B terminals.

The Waveshare board has hardware/automatic RS485 direction control, so this component does not require an RS485 DE/RE GPIO by default. The default YAML uses `GPIO17` TX and `GPIO18` RX based on the Waveshare schematic/examples reviewed during planning, but the pins remain substitutions because board revisions and examples should be verified before installation.

The board has an onboard/reserved 120 ohm RS485 termination jumper. Enable termination only when this bridge is physically at an RS485 bus end.

## Safety

Work inside or near an EV charger should be done only with power isolated and by a qualified person where required. Keep communication wiring separate from mains wiring. If RS485 does not communicate, A/B naming may differ between vendors; try swapping A/B after confirming power is isolated and the installation is safe.

## Build Environment

Use the devcontainer for reproducible tooling on ARM64 Macs and x64 hosts.

In VS Code or another devcontainer-capable tool, reopen this repository in the container. The container installs Python, build tools, ESPHome, and PlatformIO dependencies from `requirements.txt`.

Manual setup outside the container:

```bash
python -m pip install -r requirements.txt
```

For USB-RS485 smoke testing from inside the devcontainer, pass the adapter through to the container. On Linux this is commonly `/dev/ttyUSB0`; on macOS Docker Desktop usually exposes USB serial devices differently, so running the smoke test on the host can be simpler.

## Build And Validate

```bash
make test
make config
make compile
```

Equivalent raw ESPHome commands:

```bash
esphome config wallbox-powerboost-emulator.yaml
esphome compile wallbox-powerboost-emulator.yaml
```

Full validation:

```bash
make check
```

Hardware smoke test with a USB-RS485 adapter:

```bash
make smoke PORT=/dev/ttyUSB0
```

On macOS the port often looks like `/dev/cu.usbserial-0001`:

```bash
make smoke PORT=/dev/cu.usbserial-0001
```

Override serial settings when your YAML is not at the defaults:

```bash
make smoke PORT=/dev/ttyUSB0 SLAVE=1 BAUD=9600 PARITY=N
```

## Flashing

Compile first, then flash with ESPHome using your preferred transport:

```bash
esphome run wallbox-powerboost-emulator.yaml
```

The fallback AP SSID is:

```text
EM112-Bridge-Fallback
```

Change `fallback_ap_password` before deploying.

## Configuration

Important YAML substitutions in `wallbox-powerboost-emulator.yaml`:

- `rs485_tx_pin`
- `rs485_rx_pin`
- `modbus_baud`
- `modbus_parity`
- `modbus_stop_bits`
- `modbus_slave_id`
- `meter_profile`: `em112_pfb`, `em330_av5`, or `em530_av5`.
- `dsmr_default_url`
- `fallback_ap_password`

Runtime Home Assistant controls are provided where practical:

- DSMRlogger URL.
- DSMR poll interval.
- Fail-safe import power.
- Stale timeout.
- Source phase mode: `total`, `l1`, `l2`, `l3`.
- Meter profile: `em112_pfb`, `em330_av5`, `em530_av5`.
- Debug logging.
- Strict Modbus exceptions.
- Simulation override switches: force `insufficient` or `sufficient` solar snapshots for Modbus debugging.

The default source phase is `l1`, which is intended for common single-phase Wallbox installations. Use `total` only if that matches how you want the Wallbox to interpret the installation.

The simulator overrides are off by default. With both switches off, the firmware uses live DSMR data. Change the meter profile while testing only, then power-cycle the Wallbox so it redetects the meter cleanly.

## DSMRloggerAPI

The component polls:

```text
http://<DSMRLOGGER_HOST>/api/v1/sm/actual
```

Test it in a browser before flashing:

```text
http://dsmr-api.local/api/v1/sm/actual
```

The response is expected to contain:

```json
{
  "actual": [
    { "name": "power_delivered", "value": 1.234, "unit": "kW" }
  ]
}
```

Fields are parsed by `name`, not array index. Missing, `null`, `"-"`, NaN, and non-numeric values are treated as unavailable.

## EM112 PF.B Register Mapping

Only this zero-based physical Modbus register subset is implemented:

| Register | Words | Description | Encoding |
| --- | ---: | --- | --- |
| `0x0000` | 2 | voltage L-N | `INT32`, V x 10 |
| `0x0002` | 2 | current | `INT32`, A x 1000 |
| `0x0004` | 2 | active power | signed `INT32`, W x 10 |
| `0x0006` | 2 | apparent power | `INT32`, VA x 10 |
| `0x0008` | 2 | reactive power | `INT32`, var x 10, default 0 |
| `0x000B` | 1 | Carlo Gavazzi product ID | `104` |
| `0x000E` | 1 | power factor | `INT16`, PF x 1000, default 1000 |
| `0x000F` | 1 | frequency | `INT16`, Hz x 10, default 500 |
| `0x0010` | 2 | imported energy | `INT32`, kWh x 1000 |
| `0x0020` | 2 | exported energy | `INT32`, kWh x 1000 |

32-bit values are packed LSW first, then MSW. Each 16-bit word is transmitted MSB first, then LSB, as required by Modbus RTU.

Examples:

- `230.6 V -> 2306`
- `6.321 A -> 6321`
- `-1450 W -> -14500`
- `12345.678 kWh -> 12345678`

## EM330 And EM530 Register Mapping

The additional profiles target exactly `EM330DINAV53HS1X` (product ID `332`) and `EM530DINAV53XS1X` (product ID `1744`). They use the official common three-phase physical register layout with L1 populated from the selected DSMR source and L2/L3 returned as zero for the present single-phase installation.

| Register | Words | Description | Encoding |
| --- | ---: | --- | --- |
| `0x0000` | 2 | L1-N voltage | `INT32`, V x 10 |
| `0x000C` | 2 | L1 RMS current | non-negative `INT32`, A x 1000 |
| `0x0012` | 2 | L1 active power | signed `INT32`, W x 10 |
| `0x0028` | 2 | system active power | signed `INT32`, W x 10 |
| `0x002E` | 1 | L1 power factor | signed `INT16`, PF x 1000 |
| `0x0031` | 1 | system power factor | signed `INT16`, PF x 1000 |
| `0x0033` | 1 | frequency | `INT16`, Hz x 10 |
| `0x0034` | 2 | imported energy, legacy | `INT32`, kWh x 10 |
| `0x004E` | 2 | exported energy, legacy | `INT32`, kWh x 10 |
| `0x0500` | 4 | imported energy | `INT64`, Wh |
| `0x051C` | 4 | exported energy | `INT64`, Wh |
| `0x1103` | 1 | measurement mode | EM330 `1` (B), EM530 `2` (bidirectional) |

For EM330/EM530, a one-word read at `0x000B` returns the product ID. When `0x000B` is included as the second word of a two-word read starting at `0x000A`, it instead returns the high word of L3-L1 voltage as specified by the protocol.

Phase current remains a positive RMS magnitude for both import and export. Direction is carried by the signed active-power registers: positive power is import and negative power is export.

## Modbus Behavior

- Wallbox is the Modbus master/client.
- ESP32 is the Modbus slave/server.
- Default slave address: `1`.
- Default serial format: `9600 8N1`.
- FC03 and FC04 are supported and treated the same for measurements.
- FC08 subfunction `0x0000` Return Query Data is echoed.
- FC06 writes are logged and rejected by exception by default.
- Broadcast address `0` is ignored except for diagnostics.
- Wrong slave IDs are ignored and counted.
- Unknown registers return `0` in courtesy mode.
- Strict mode returns exception `0x02` for illegal address.

## Home Assistant

Adopt the ESPHome node through the native API. The component exposes:

- Grid power, import/export power, selected voltage/current, energies, apparent/reactive power, PF, frequency.
- DSMR poll health and counters.
- Modbus request counters, CRC errors, unsupported functions, illegal addresses, exceptions, wrong slave IDs.
- Last request function, address, quantity, exception, frame hex, summary, and range.
- Binary states for DSMR connected, DSMR stale, Wallbox polling active, fail-safe active, and last CRC OK.

The full last-20 Modbus request history is intentionally not published as one Home Assistant state. Use `/debug` or `/debug.json` for that.

## Debug Pages

Open:

```text
http://wallbox-powerboost-emulator.local/debug
http://wallbox-powerboost-emulator.local/debug.json
```

These pages show the active exact meter profile, current DSMR values, stale/fail-safe state, Modbus counters, build identity, and the last 20 Modbus requests.

## Testing Before Connecting Wallbox

Use a USB-RS485 adapter and a Modbus client before connecting to the Wallbox.

Example checks:

- Read slave `1`, FC03, start `0x0000`, length `2` for voltage.
- Read start `0x0004`, length `2` for signed active power.
- Read start `0x000B`, length `1` for product ID `104`.
- Confirm `/debug` shows the requests.

The repository includes a small smoke-test client:

```bash
python tools/modbus_smoke_test.py --port /dev/ttyUSB0 --slave 1
```

Select the same profile that is active on the emulator:

```bash
make smoke PORT=/dev/ttyUSB0 SMOKE_ARGS='--profile em330_av5'
make smoke PORT=/dev/ttyUSB0 SMOKE_ARGS='--profile em530_av5'
```

To check the solar-threshold scenarios from the same fixture set, pass the extra threshold arguments through `make smoke`:

```bash
make smoke PORT=/dev/ttyUSB0 SMOKE_ARGS='--solar-threshold-a 1.5 --solar-threshold-a 6.0'
```

To verify the PF.B exported-energy counter is moving during live export, add a timed second read:

```bash
make smoke PORT=/dev/ttyUSB0 SMOKE_ARGS='--verify-export-motion-seconds 10'
```

It checks profile-specific FC03/FC04 reads, the exact product ID and bidirectional mode, and FC08 Return Query Data. It is intentionally separate from `make check` because it requires a flashed board and a physical USB-RS485 adapter.

The additional solar-threshold checks are based on the live Modbus reads: they verify that the measured current is at least the requested threshold and that export power is at least voltage times threshold. The 1.5 A case is the more permissive baseline; the 6.0 A case matches the commonly documented Wallbox smart/solar floor.

The export-motion check requires negative active power, waits for the requested interval, and verifies that exported energy advances by at least the EM112 PF.B serial resolution of 0.001 kWh. A live capture on 2026-07-11 advanced from 22300.852 to 22300.859 kWh over 10 seconds at approximately 2.63 kW export.

If the Wallbox polls unknown registers, enable debug logging and inspect `/debug`. Unknown registers return `0` unless strict mode is enabled.

## Wallbox Setup Notes

- Confirm the emulator profile selector and smoke-test `--profile` agree before restarting the Wallbox.
- Use the correct installation current in Wallbox.
- Update Wallbox firmware before testing.
- Confirm the Wallbox slave ID, baud rate, parity, and stop bits match the YAML substitutions.

## Troubleshooting

- No Modbus requests seen: check Wallbox meter profile, RS485 wiring, slave ID, baud, parity, and that the Wallbox is configured to use an external meter.
- CRC errors: check A/B polarity, cable quality, termination, baud, parity, and grounding.
- Wrong slave ID count increases: align Wallbox meter ID with `modbus_slave_id`.
- A/B swapped: isolate power where required and swap RS485 A/B.
- Wrong baud/parity: align `modbus_baud`, `modbus_parity`, and `modbus_stop_bits`.
- DSMR stale: open the DSMRloggerAPI URL in a browser and check Wi-Fi.
- Fail-safe active: DSMR data is older than the stale timeout, so the bridge reports high import power.
- Wallbox polls unknown registers: inspect `/debug`, keep courtesy mode enabled, and add registers only when supported by the official protocol for the selected exact profile.
- Profile change not detected: select the profile, confirm it in `/debug.json`, then power-cycle the Wallbox.

## References And Attribution

- DSMRloggerAPI by Willem Aandewiel: https://github.com/mrWheel/DSMRloggerAPI
- DSMRloggerAPI `actual` endpoint implementation: https://github.com/mrWheel/DSMRloggerAPI/blob/master/src/restAPI.cpp
- DSMRloggerAPI Home Assistant example: https://github.com/mrWheel/DSMRloggerAPI/blob/master/HA_Configuration.yaml
- Waveshare ESP32-S3-RS485-CAN wiki: https://www.waveshare.com/wiki/ESP32-S3-RS485-CAN
- Waveshare schematic: https://files.waveshare.com/wiki/ESP32-S3-RS485-CAN/ESP32-S3-RS485-CAN-Schematic.pdf
- ESPHome external components: https://esphome.io/components/external_components/
- Smart Stuff P1 Modbus Dongle EM330/EM340 mapping: https://docs.smart-stuff.nl/p1-modbus-dongle/firmware-v1-legacy/register-mapping/em330-em340-emulation.md
- Carlo Gavazzi EM330/EM340/ET330/ET340 communication protocol: https://www.gavazziautomation.com/fileadmin/images/PIM/OTHERSTUFF/COMPRO/EM330_EM340_ET330_ET340_CP.pdf
- Carlo Gavazzi EM530/EM540 communication protocol: https://gavazzi.se/wp-content/uploads/em500_cpp.pdf

The Smart Stuff page is cited as inspiration for clear open register-mapping documentation and Modbus emulator behavior. Carlo Gavazzi protocol documents are the source for the implemented register encodings.
