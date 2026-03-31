#!/bin/bash
# flash-box3.sh — Build + Flash KXKM BMU firmware on ESP32-S3-BOX-3
# Usage: ./scripts/flash-box3.sh [--remote]
#   --remote : build on kxkm@kxkm-ai, then flash locally
#   (default) : build + flash locally
#
# Prerequisites:
#   - ESP-IDF v5.3 installed
#   - BOX-3 connected via USB (usually /dev/ttyACM0)

set -e
cd "$(git rev-parse --show-toplevel)/firmware-idf"

REMOTE_HOST="kxkm@kxkm-ai"
REMOTE_DIR="/home/kxkm/KXKM_Batterie_Parallelator/firmware-idf"
PORT="${BMU_PORT:-/dev/ttyACM0}"

# ── Source ESP-IDF ──────────────────────────────────────────────────────
source_idf() {
    if [ -n "$IDF_PATH" ] && [ -f "$IDF_PATH/export.sh" ]; then
        . "$IDF_PATH/export.sh" > /dev/null 2>&1
    elif [ -f "$HOME/esp/esp-idf/export.sh" ]; then
        . "$HOME/esp/esp-idf/export.sh" > /dev/null 2>&1
    else
        echo "ERROR: ESP-IDF not found. Set IDF_PATH or install ESP-IDF."
        exit 1
    fi
}

# ── Apply sdkconfig defaults + LVGL fonts ──────────────────────────────
apply_defaults() {
    echo "=== Applying sdkconfig defaults ==="

    # Create sdkconfig.defaults.extra for LVGL fonts if not exists
    if [ ! -f sdkconfig.defaults.extra ]; then
        cat > sdkconfig.defaults.extra << 'EOF'
# LVGL Montserrat fonts (required by bmu_display UI)
CONFIG_LV_FONT_MONTSERRAT_10=y
CONFIG_LV_FONT_MONTSERRAT_12=y
CONFIG_LV_FONT_MONTSERRAT_14=y
CONFIG_LV_FONT_MONTSERRAT_16=y
CONFIG_LV_FONT_MONTSERRAT_20=y

# LVGL memory
CONFIG_LV_MEM_CUSTOM=y
CONFIG_LV_MEM_SIZE_KILOBYTES=64

# LVGL refresh
CONFIG_LV_DISP_DEF_REFR_PERIOD=10
EOF
        echo "Created sdkconfig.defaults.extra with LVGL font config"
    fi
}

# ── Build ──────────────────────────────────────────────────────────────
build_local() {
    source_idf
    apply_defaults

    echo "=== Setting target ESP32-S3 ==="
    idf.py set-target esp32s3

    echo "=== Building ==="
    idf.py build

    echo "=== Binary size ==="
    idf.py size

    echo "=== Build complete ==="
    ls -la build/kxkm-bmu.bin
}

# ── Build on remote kxkm-ai ───────────────────────────────────────────
build_remote() {
    echo "=== Syncing source to $REMOTE_HOST ==="
    rsync -avz --delete \
        --exclude='build/' \
        --exclude='managed_components/' \
        --exclude='sdkconfig' \
        --exclude='sdkconfig.old' \
        . "$REMOTE_HOST:$REMOTE_DIR/"

    echo "=== Building on $REMOTE_HOST ==="
    ssh "$REMOTE_HOST" "cd $REMOTE_DIR && \
        . \$HOME/esp/esp-idf/export.sh && \
        idf.py set-target esp32s3 && \
        idf.py build && \
        idf.py size"

    echo "=== Downloading binary ==="
    mkdir -p build
    scp "$REMOTE_HOST:$REMOTE_DIR/build/kxkm-bmu.bin" build/
    scp "$REMOTE_HOST:$REMOTE_DIR/build/partition_table/partition-table.bin" build/ 2>/dev/null || true
    scp "$REMOTE_HOST:$REMOTE_DIR/build/bootloader/bootloader.bin" build/ 2>/dev/null || true
    scp "$REMOTE_HOST:$REMOTE_DIR/build/storage.bin" build/ 2>/dev/null || true

    echo "=== Binary downloaded ==="
    ls -la build/kxkm-bmu.bin
}

# ── Flash ──────────────────────────────────────────────────────────────
flash_local() {
    source_idf
    echo "=== Flashing to $PORT ==="
    idf.py -p "$PORT" flash
    echo "=== Flash complete ==="
}

# ── Monitor ────────────────────────────────────────────────────────────
monitor() {
    source_idf
    echo "=== Starting monitor on $PORT (Ctrl+] to exit) ==="
    idf.py -p "$PORT" monitor
}

# ── Menuconfig (interactive) ───────────────────────────────────────────
menuconfig_local() {
    source_idf
    echo "=== Opening menuconfig ==="
    echo "Configure:"
    echo "  BMU Protection Config → thresholds"
    echo "  BMU WiFi → SSID/password"
    echo "  BMU MQTT → broker URI/credentials"
    echo "  BMU InfluxDB → URL/org/bucket/token"
    echo "  BMU Web Server → admin token"
    echo "  BMU VE.Direct → enable/UART pins"
    echo "  BMU Display → backlight/dim timeout"
    echo "  Component config → LVGL → Font → Montserrat 10/12/14/16/20"
    idf.py menuconfig
}

# ── Main ───────────────────────────────────────────────────────────────
case "${1:-all}" in
    --remote)
        build_remote
        flash_local
        monitor
        ;;
    build)
        build_local
        ;;
    flash)
        flash_local
        ;;
    monitor)
        monitor
        ;;
    menuconfig)
        menuconfig_local
        ;;
    all)
        build_local
        flash_local
        monitor
        ;;
    *)
        echo "Usage: $0 [--remote|build|flash|monitor|menuconfig|all]"
        echo ""
        echo "  all        Build + flash + monitor (default)"
        echo "  --remote   Build on kxkm-ai, flash + monitor locally"
        echo "  build      Build only"
        echo "  flash      Flash only (requires prior build)"
        echo "  monitor    Serial monitor only"
        echo "  menuconfig Open ESP-IDF menuconfig"
        exit 1
        ;;
esac
