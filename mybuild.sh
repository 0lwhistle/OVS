source /root/esp-idf/esp-idf-v6.0.1/export.sh
rm ./build -rf
idf.py fullclean
idf.py build
