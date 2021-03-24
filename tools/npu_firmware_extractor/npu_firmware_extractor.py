import argparse
import os
import pathlib
import re
import struct
import tempfile

from pathlib import Path
from ppadb.client import Client as AdbClient


def main(args):
    if not args.cortex_a ^ args.cortex_m:
        print("[-] Exactly one flag (--cortex-a/--cortex-m) should be set.")
        return

    boot_img_path = args.boot_img

    if boot_img_path is None:
        # Connection to the device using ADB ----------------------------------
        print("[+] Connection to the device using ADB.")
        host = '127.0.0.1'
        port = 5037
        client = AdbClient(host=host, port=port)
        if client is None:
            print(f"[-] Could not connect to the adb server {host}:{port}")
            return
        devices = client.devices()
        if not len(devices):
            print(f"[-] No device is connected to the host")
            return
        if args.serial:
            device = client.device(args.serial)
            if device is None:
                print(f"[-] Could not connect to device {args.serial}")
        else:
            devices = client.devices()
            if len(devices) > 1:
                print(f"[-] More than one device is connected to the host, "
                    "please provide a serial.")
                return
            device = devices[0]

        # Pulling the kernel from the device to the host ----------------------
        print("[+] Pulling the kernel from the device to the host.")
        input_file='/dev/block/by-name/boot'
        output_file='/data/local/tmp/boot.img'
        device.shell(f'su root sh -c "dd if={input_file} of={output_file}"')
        boot_img_path = Path(args.directory) / 'boot.img'
        device.pull(output_file, boot_img_path)

    # Extracting the firmware -------------------------------------------------
    print("[+] Extracting the firmware.")
    with open(boot_img_path, 'rb') as f:
        boot_img = f.read()
    regex = re.compile(rb'\[.*\d{4}\/\d{2}\/\d{2} \d{2}:\d{2}:\d{2}\]')
    try:
        sig = next(regex.finditer(boot_img))
    except:
        print("[-] Could not find the NPU firmware signature.")
        return
    if args.cortex_a:
        size = struct.unpack(
            '<I', boot_img[sig.start()-0x30:sig.start()-0x30+4])[0]
        npu_start = sig.start() - 0x28 - size
    else:
        npu_stack_addr = rb'\x00\x10\x07\x00'
        # Padding with \0 to prevent false positives
        regex = re.compile(rb'\x00\x00\x00\x00' + npu_stack_addr)
        for m in regex.finditer(boot_img):
            if m.start() > sig.end():
                break
            npu_start = m.start() + 4
    firmware = boot_img[npu_start:sig.end()]
    npu_path = Path(args.directory) / 'NPU.bin'
    with open(npu_path, 'wb') as f:
        f.write(firmware)

    print("[+] Done.")


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument(
        '-s', '--serial',
        help="Serial of the Samsung device to connect to. "
             "Can be omitted if only one device is connected to the host "
             "or if the boot image is passed directly using --boot-img.",
        default='', type=str)
    parser.add_argument(
        '-b', '--boot-img',
        help="Boot img file path. Must be omitted if you want to pull the "
             "kernel image from the phone.",
        type=str)
    parser.add_argument(
        '--cortex-a', action='store_true',
        help="Extract the ARM Cortex-A NPU firmware "
             "(newer SoC versions, e.g. S20).")
    parser.add_argument(
        '--cortex-m', action='store_true',
        help="Extract the ARM Cortex-M NPU firmware "
             "(older SoC versions, e.g. S10).")
    parser.add_argument(
        '-d', '--directory', help="Output directory path.", required=True,
        type=str)
    main(parser.parse_args())
