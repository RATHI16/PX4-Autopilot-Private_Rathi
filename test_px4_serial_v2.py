#!/usr/bin/env python3
"""Hardware smoke test for the SAMV71 PX4 port."""
import pathlib
import serial
import subprocess
import time
from datetime import datetime

PORT = "/dev/ttyACM0"
BAUD = 115200
BUILD_TARGET = "microchip_samv71-xult-clickboards_default"
BOOT_CAPTURE_SECONDS = 6
LOG_ROOT = pathlib.Path("test_data/boot_logs")
LOG_ROOT.mkdir(parents=True, exist_ok=True)


def run_upload():
    print(f"\nFlashing target: {BUILD_TARGET}")
    subprocess.run(["make", BUILD_TARGET, "upload"], check=True)


def open_serial():
    print(f"Opening {PORT} @ {BAUD} baud")
    return serial.Serial(PORT, BAUD, timeout=0.5)


def drain_serial(port, duration_s):
    end_time = time.time() + duration_s
    buffer = []

    while time.time() < end_time:
        waiting = port.in_waiting

        if waiting:
            buffer.append(port.read(waiting).decode('utf-8', errors='ignore'))
        else:
            time.sleep(0.1)

    return "".join(buffer)


def send_command(port, command, wait_s=1.5):
    port.write((command + "\n").encode("utf-8"))
    time.sleep(wait_s)
    return port.read(port.in_waiting).decode('utf-8', errors='ignore')


def capture_boot_log(port):
    print("\nCapturing boot log...")
    port.reset_input_buffer()
    log = drain_serial(port, BOOT_CAPTURE_SECONDS)
    timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    log_file = LOG_ROOT / f"boot_{timestamp}.log"
    log_file.write_text(log)
    print(f"Boot log saved to {log_file}")
    return log


def fetch_param(port, name):
    response = send_command(port, f"param show {name}", wait_s=1.0)
    value = None

    for line in response.splitlines():
        if line.startswith(name):
            tokens = line.split()

            if len(tokens) >= 2:
                value = tokens[1]

    return value, response


def main():
    run_upload()
    ser = open_serial()

    boot_log = capture_boot_log(ser)
    if not boot_log:
        print("⚠️  No boot output captured - continuing anyway")

    ver_output = send_command(ser, "ver all", wait_s=2.5)
    print("\nVER ALL OUTPUT:\n", ver_output)

    param_name = "SYS_AUTOSTART"
    original_value, original_output = fetch_param(ser, param_name)

    if original_value is None:
        print(f"Failed to read {param_name}")
    else:
        print(f"\n{param_name} original value: {original_value}")
        temp_value = "60100" if original_value != "60100" else "0"
        send_command(ser, f"param set {param_name} {temp_value}")
        updated_value, _ = fetch_param(ser, param_name)
        print(f"{param_name} updated value: {updated_value}")
        send_command(ser, f"param set {param_name} {original_value}")
        restored_value, _ = fetch_param(ser, param_name)
        print(f"{param_name} restored value: {restored_value}")

    log_file = LOG_ROOT / "latest_ver_all.log"
    log_file.write_text(ver_output)
    print(f"\nVER output archived to {log_file}")

    ser.close()
    print("\nHardware smoke test complete.")


if __name__ == "__main__":
    main()
