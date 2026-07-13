source /root/esp-idf/esp-idf-v6.0.1/export.sh

# 1. 构建 Vue 前端
echo "=== Building Vue frontend ==="
cd vue_web/vue-ui
npm install --silent
npm run build
cd ../..

# 2. 将 Vue 构建产物打包为 C 代码
echo "=== Packing web resources ==="
python tools/fs_to_c.py vue_web/vue-ui/dist modules/web/web_data

# 3. 编译固件
echo "=== Building firmware ==="
idf.py build

# 4. 签名固件（用于 OTA 升级）
echo "=== Signing firmware for OTA ==="
python3 tools/sign_firmware.py build/ovs.bin

