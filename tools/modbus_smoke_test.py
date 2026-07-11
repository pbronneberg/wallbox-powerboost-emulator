#!/usr/bin/env python3
"""USB-RS485 Modbus RTU smoke test for wallbox-powerboost-emulator."""

from __future__ import annotations

import argparse
import struct
import sys
import time
from dataclasses import dataclass
from typing import Iterable

try:
    import serial
except ImportError as exc:  # pragma: no cover - exercised by users without setup
    raise SystemExit("pyserial is required. Run `python -m pip install -r requirements.txt`.") from exc


@dataclass(frozen=True)
class ReadCheck:
    label: str
    function: int
    start: int
    quantity: int
    scale: float = 1.0
    signed: bool = False
    unit: str = ""


CHECKS = (
    ReadCheck("voltage_l_n", 0x03, 0x0000, 2, 10.0, False, "V"),
    ReadCheck("current", 0x03, 0x0002, 2, 1000.0, False, "A"),
    ReadCheck("active_power", 0x03, 0x0004, 2, 10.0, True, "W"),
    ReadCheck("apparent_power", 0x03, 0x0006, 2, 10.0, False, "VA"),
    ReadCheck("product_id", 0x03, 0x000B, 1, 1.0, False, ""),
    ReadCheck("power_factor", 0x04, 0x000E, 1, 1000.0, True, ""),
    ReadCheck("frequency", 0x04, 0x000F, 1, 10.0, False, "Hz"),
    ReadCheck("import_energy", 0x04, 0x0010, 2, 1000.0, False, "kWh"),
    ReadCheck("export_energy", 0x04, 0x0020, 2, 1000.0, False, "kWh"),
)


def crc16_modbus(data: bytes) -> int:
    crc = 0xFFFF
    for byte in data:
        crc ^= byte
        for _ in range(8):
            if crc & 0x0001:
                crc = (crc >> 1) ^ 0xA001
            else:
                crc >>= 1
    return crc & 0xFFFF


def append_crc(frame: bytes) -> bytes:
    crc = crc16_modbus(frame)
    return frame + bytes((crc & 0xFF, crc >> 8))


def verify_crc(frame: bytes) -> bool:
    if len(frame) < 4:
        return False
    expected = frame[-2] | (frame[-1] << 8)
    return crc16_modbus(frame[:-2]) == expected


def hex_bytes(data: Iterable[int]) -> str:
    return " ".join(f"{byte:02X}" for byte in data)


def read_exact(port: serial.Serial, size: int, deadline: float) -> bytes:
    data = bytearray()
    while len(data) < size and time.monotonic() < deadline:
        chunk = port.read(size - len(data))
        if chunk:
            data.extend(chunk)
    return bytes(data)


def request_response(port: serial.Serial, request: bytes, min_response_size: int, timeout: float) -> bytes:
    port.reset_input_buffer()
    port.write(request)
    port.flush()
    deadline = time.monotonic() + timeout
    response = read_exact(port, min_response_size, deadline)
    if len(response) >= 2 and response[1] & 0x80:
        response += read_exact(port, 5 - len(response), deadline)
        return response
    if len(response) >= 3:
        expected = 3 + response[2] + 2
        response += read_exact(port, expected - len(response), deadline)
    return response


def decode_words(payload: bytes) -> list[int]:
    if len(payload) % 2:
        raise ValueError("Modbus register payload has odd byte count")
    return [struct.unpack(">H", payload[index : index + 2])[0] for index in range(0, len(payload), 2)]


def decode_value(words: list[int], signed: bool) -> int:
    if len(words) == 1:
        value = words[0]
        if signed and value & 0x8000:
            value -= 0x10000
        return value
    if len(words) == 2:
        raw = words[0] | (words[1] << 16)
        if signed and raw & 0x80000000:
            raw -= 0x100000000
        return raw
    raise ValueError(f"expected one or two registers, got {len(words)}")


def read_registers(
    port: serial.Serial,
    slave: int,
    check: ReadCheck,
    timeout: float,
    measurements: dict[str, float] | None = None,
) -> bool:
    pdu = bytes((slave, check.function)) + struct.pack(">HH", check.start, check.quantity)
    request = append_crc(pdu)
    response = request_response(port, request, 5, timeout)
    if not response:
        print(f"FAIL {check.label}: no response")
        return False
    if not verify_crc(response):
        print(f"FAIL {check.label}: bad CRC response={hex_bytes(response)}")
        return False
    if response[0] != slave:
        print(f"FAIL {check.label}: unexpected slave {response[0]} response={hex_bytes(response)}")
        return False
    if response[1] == (check.function | 0x80):
        print(f"FAIL {check.label}: exception 0x{response[2]:02X} response={hex_bytes(response)}")
        return False
    if response[1] != check.function:
        print(f"FAIL {check.label}: unexpected function 0x{response[1]:02X} response={hex_bytes(response)}")
        return False

    byte_count = response[2]
    payload = response[3 : 3 + byte_count]
    words = decode_words(payload)
    raw = decode_value(words, check.signed)
    value = raw / check.scale
    if measurements is not None:
        measurements[check.label] = value
    suffix = f" {check.unit}" if check.unit else ""
    print(
        f"OK   {check.label:<15} raw={raw:<10} value={value:g}{suffix:<4} "
        f"regs=[{', '.join(f'0x{word:04X}' for word in words)}]"
    )
    return True


def evaluate_solar_threshold(measurements: dict[str, float], threshold_a: float) -> tuple[bool, str]:
    voltage = measurements.get("voltage_l_n")
    current = measurements.get("current")
    active_power = measurements.get("active_power")
    if voltage is None or current is None or active_power is None:
        return False, "missing voltage/current/active_power readings"

    export_w = max(-active_power, 0.0)
    required_export_w = voltage * threshold_a
    current_ok = current >= threshold_a
    export_ok = export_w >= required_export_w
    passed = current_ok and export_ok
    summary = (
        f"threshold={threshold_a:g}A current={current:g}A export={export_w:g}W "
        f"required_export={required_export_w:g}W current_ok={current_ok} export_ok={export_ok}"
    )
    return passed, summary


def verify_export_energy_motion(
    port: serial.Serial,
    slave: int,
    timeout: float,
    measurements: dict[str, float],
    interval_s: float,
) -> tuple[bool, str]:
    initial_kwh = measurements.get("export_energy")
    active_power_w = measurements.get("active_power")
    if initial_kwh is None or active_power_w is None:
        return False, "missing initial export_energy/active_power readings"
    if active_power_w >= 0.0:
        return False, f"active_power={active_power_w:g}W is not export"

    time.sleep(interval_s)
    follow_up: dict[str, float] = {}
    export_check = next(check for check in CHECKS if check.label == "export_energy")
    if not read_registers(port, slave, export_check, timeout, follow_up):
        return False, "follow-up export-energy read failed"

    final_kwh = follow_up["export_energy"]
    delta_kwh = final_kwh - initial_kwh
    expected_kwh = (-active_power_w * interval_s) / 3_600_000.0
    passed = delta_kwh >= 0.001
    return passed, (
        f"interval={interval_s:g}s initial={initial_kwh:.3f}kWh final={final_kwh:.3f}kWh "
        f"delta={delta_kwh:.3f}kWh expected_approx={expected_kwh:.3f}kWh"
    )


def run_diagnostics_echo(port: serial.Serial, slave: int, timeout: float) -> bool:
    payload = bytes.fromhex("12 34")
    pdu = bytes((slave, 0x08)) + struct.pack(">H", 0x0000) + payload
    request = append_crc(pdu)
    response = request_response(port, request, len(request), timeout)
    if not response:
        print("FAIL fc08_echo: no response")
        return False
    if not verify_crc(response):
        print(f"FAIL fc08_echo: bad CRC response={hex_bytes(response)}")
        return False
    if response != request:
        print(f"FAIL fc08_echo: response differs request={hex_bytes(request)} response={hex_bytes(response)}")
        return False
    print("OK   fc08_echo       return-query-data echoed")
    return True


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--port",
        required=True,
        help="Serial port for the USB-RS485 adapter, for example /dev/ttyUSB0 or /dev/cu.usbserial-0001.",
    )
    parser.add_argument("--slave", type=int, default=1, help="Modbus slave address. Default: 1.")
    parser.add_argument("--baud", type=int, default=9600, help="Modbus baud rate. Default: 9600.")
    parser.add_argument("--parity", choices=("N", "E"), default="N", help="Serial parity: N or E. Default: N.")
    parser.add_argument("--stop-bits", type=int, choices=(1, 2), default=1, help="Serial stop bits. Default: 1.")
    parser.add_argument("--timeout", type=float, default=1.0, help="Response timeout in seconds. Default: 1.0.")
    parser.add_argument("--skip-fc08", action="store_true", help="Skip FC08 diagnostics echo check.")
    parser.add_argument(
        "--solar-threshold-a",
        type=float,
        action="append",
        default=[],
        help=(
            "Evaluate a solar-sufficient threshold in amps using live Modbus reads. "
            "Pass multiple times for multiple thresholds, for example --solar-threshold-a 1.5 --solar-threshold-a 6.0."
        ),
    )
    parser.add_argument(
        "--verify-export-motion-seconds",
        type=float,
        metavar="SECONDS",
        help=(
            "During live export, wait this many seconds and require the EM112 PF.B exported-energy "
            "register to advance by at least 0.001 kWh. A value around 10 seconds is practical."
        ),
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    if not 1 <= args.slave <= 247:
        print("--slave must be between 1 and 247", file=sys.stderr)
        return 2
    if args.verify_export_motion_seconds is not None and args.verify_export_motion_seconds <= 0:
        print("--verify-export-motion-seconds must be greater than zero", file=sys.stderr)
        return 2

    parity = serial.PARITY_EVEN if args.parity == "E" else serial.PARITY_NONE
    stop_bits = serial.STOPBITS_TWO if args.stop_bits == 2 else serial.STOPBITS_ONE

    with serial.Serial(
        port=args.port,
        baudrate=args.baud,
        bytesize=serial.EIGHTBITS,
        parity=parity,
        stopbits=stop_bits,
        timeout=0.05,
        write_timeout=args.timeout,
    ) as port:
        passed = 0
        total = 0
        measurements: dict[str, float] = {}
        for check in CHECKS:
            total += 1
            passed += int(read_registers(port, args.slave, check, args.timeout, measurements))
        if not args.skip_fc08:
            total += 1
            passed += int(run_diagnostics_echo(port, args.slave, args.timeout))

        for threshold in args.solar_threshold_a:
            total += 1
            ok, summary = evaluate_solar_threshold(measurements, threshold)
            if ok:
                print(f"OK   solar_threshold threshold={threshold:g}A {summary}")
                passed += 1
            else:
                print(f"FAIL solar_threshold threshold={threshold:g}A {summary}")

        if args.verify_export_motion_seconds is not None:
            total += 1
            ok, summary = verify_export_energy_motion(
                port,
                args.slave,
                args.timeout,
                measurements,
                args.verify_export_motion_seconds,
            )
            if ok:
                print(f"OK   export_motion   {summary}")
                passed += 1
            else:
                print(f"FAIL export_motion   {summary}")

    print(f"\n{passed}/{total} Modbus smoke checks passed")
    return 0 if passed == total else 1


if __name__ == "__main__":
    raise SystemExit(main())
