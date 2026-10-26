#!/usr/bin/env bash
# unload_driver.sh - Remove kernel character driver module and cleanup device node
set -euo pipefail

MODULE_NAME="chardev"
DEVICE_NAME="chardev0"
DEVICE_NODE="/dev/$DEVICE_NAME"

echo "[*] Cleaning up device node $DEVICE_NODE..."
if [[ -e "$DEVICE_NODE" ]]; then
    sudo rm -f "$DEVICE_NODE"
fi

echo "[*] Unloading module $MODULE_NAME..."
if lsmod | grep -q "$MODULE_NAME"; then
    sudo rmmod "$MODULE_NAME"
    echo "[+] Module $MODULE_NAME successfully unloaded."
else
    echo "[!] Module $MODULE_NAME is not currently loaded."
fi
