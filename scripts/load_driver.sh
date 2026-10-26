#!/usr/bin/env bash
# load_driver.sh - Insert kernel character driver module and create device nodes
set -euo pipefail

MODULE_NAME="chardev"
DEVICE_NAME="chardev0"
MODULE_PATH="$(dirname "$0")/../driver/${MODULE_NAME}.ko"

if [[ ! -f "$MODULE_PATH" ]]; then
    echo "[-] Error: Module file '$MODULE_PATH' not found. Please compile with make in driver/ first." >&2
    exit 1
fi

echo "[*] Loading module $MODULE_NAME..."
sudo insmod "$MODULE_PATH"

MAJOR=$(awk "\$2==\"$MODULE_NAME\" {print \$1}" /proc/devices)
if [[ -z "$MAJOR" ]]; then
    echo "[-] Failed to detect major number for $MODULE_NAME in /proc/devices" >&2
    exit 1
fi

echo "[+] Registered major number: $MAJOR"
DEVICE_NODE="/dev/$DEVICE_NAME"

if [[ -e "$DEVICE_NODE" ]]; then
    sudo rm -f "$DEVICE_NODE"
fi

echo "[*] Creating device node $DEVICE_NODE (major=$MAJOR, minor=0)..."
sudo mknod "$DEVICE_NODE" c "$MAJOR" 0
sudo chmod 666 "$DEVICE_NODE"

echo "[+] Driver successfully loaded and ready at $DEVICE_NODE"
