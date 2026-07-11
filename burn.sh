set -e

source /root/esp-idf/esp-idf-v6.0.1/export.sh

idf.py flash -p /dev/ttyACM0 -b 3000000 monitor
