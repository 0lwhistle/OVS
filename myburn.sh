set -e

source /root/esp-idf/esp-idf-v6.0.1/export.sh

idf.py flash -p /dev/ttyUSB0 monitor
