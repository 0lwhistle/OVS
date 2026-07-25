<template>
  <div class="ota-page">
    <!-- 动态粒子背景 -->
    <div class="bg-particles">
      <span v-for="n in 10" :key="n" class="particle" :style="particleStyle(n)"></span>
    </div>

    <!-- 头部 -->
    <div class="ota-header">
      <div class="header-icon">
        <svg viewBox="0 0 24 24" class="ota-svg" :class="{ active: uploading }">
          <path d="M12 2C6.48 2 2 6.48 2 12s4.48 10 10 10 10-4.48 10-10S17.52 2 12 2zm-2 15l-5-5 1.41-1.41L10 14.17l7.59-7.59L19 8l-9 9z"/>
        </svg>
      </div>
      <h2>OTA 升级</h2>
      <p>固件升级 / 网页资源更新</p>
    </div>

    <!-- Tab 切换 -->
    <div class="tab-bar">
      <button class="tab" :class="{ active: mode === 'firmware' }" @click="mode = 'firmware'">
        固件升级
      </button>
      <button class="tab" :class="{ active: mode === 'web' }" @click="mode = 'web'">
        网页升级
      </button>
    </div>

    <!-- 固件升级 -->
    <section v-if="mode === 'firmware'" class="card">
      <div class="card-title">固件升级</div>
      <p class="card-desc">上传 .bin 固件文件，支持断点续传</p>

      <!-- 拖拽区 -->
      <div
        class="drop-zone"
        :class="{ dragover: dragOver, hasfile: firmwareFile }"
        @dragover.prevent="dragOver = true"
        @dragleave="dragOver = false"
        @drop.prevent="onDropFirmware"
        @click="openFilePicker('firmware')"
      >
        <input
          ref="firmwareInput"
          type="file"
          accept=".bin"
          class="file-input-hidden"
          @change="onFirmwareSelected"
        />
        <template v-if="!firmwareFile">
          <div class="drop-icon">📦</div>
          <p>拖拽固件文件到此处，或点击选择</p>
          <span class="drop-hint">支持 .bin 格式</span>
        </template>
        <template v-else>
          <div class="file-info">
            <span class="file-name">{{ firmwareFile.name }}</span>
            <span class="file-size">{{ formatSize(firmwareFile.size) }}</span>
          </div>
          <button class="btn-clear" @click.stop="clearFile('firmware')">✕</button>
        </template>
      </div>

      <!-- 进度条 -->
      <div v-if="fwState !== 'idle'" class="progress-section">
        <div class="progress-bar">
          <div
            class="progress-fill"
            :class="{ done: fwState === 'done' || fwState === 'rebooting' }"
            :style="{ width: fwProgress + '%' }"
          ></div>
        </div>
        <p class="progress-label">{{ fwStatusText }}</p>
      </div>

      <!-- 上传按钮 -->
      <button
        v-if="firmwareFile && fwState === 'idle'"
        class="btn-primary"
        @click="uploadFirmware"
      >
        开始升级
      </button>
    </section>

    <!-- 网页升级 -->
    <section v-if="mode === 'web'" class="card">
      <div class="card-title">网页资源升级</div>
      <p class="card-desc">
        上传 .tar 网页包，更新 SPIFFS 中的前端资源<br/>
        <code>python tools/pack_web.py vue_web/vue-ui/dist</code>
      </p>

      <!-- 拖拽区 -->
      <div
        class="drop-zone"
        :class="{ dragover: dragOverWeb, hasfile: webFile }"
        @dragover.prevent="dragOverWeb = true"
        @dragleave="dragOverWeb = false"
        @drop.prevent="onDropWeb"
        @click="openFilePicker('web')"
      >
        <input
          ref="webInput"
          type="file"
          accept=".tar"
          class="file-input-hidden"
          @change="onWebSelected"
        />
        <template v-if="!webFile">
          <div class="drop-icon">🌐</div>
          <p>拖拽网页包到此处，或点击选择</p>
          <span class="drop-hint">支持 .tar 格式</span>
        </template>
        <template v-else>
          <div class="file-info">
            <span class="file-name">{{ webFile.name }}</span>
            <span class="file-size">{{ formatSize(webFile.size) }}</span>
          </div>
          <button class="btn-clear" @click.stop="clearFile('web')">✕</button>
        </template>
      </div>

      <!-- 进度条 -->
      <div v-if="webState !== 'idle'" class="progress-section">
        <div class="progress-bar">
          <div
            class="progress-fill web-fill"
            :class="{ done: webState === 'done' }"
            :style="{ width: webProgress + '%' }"
          ></div>
        </div>
        <p class="progress-label">{{ webStatusText }}</p>
      </div>

      <!-- 上传按钮 -->
      <button
        v-if="webFile && webState === 'idle'"
        class="btn-primary btn-web"
        @click="uploadWeb"
      >
        开始升级
      </button>
    </section>

    <!-- 提示 -->
    <div class="tips">
      <p>⚠️ 固件升级后设备会自动重启，升级期间请勿断电</p>
    </div>
  </div>
</template>

<script>
export default {
  data() {
    return {
      mode: 'firmware',

      // 固件
      firmwareFile: null,
      dragOver: false,
      fwState: 'idle',    // idle | uploading | writing | done | rebooting
      fwProgress: 0,
      fwXhr: null,

      // 网页
      webFile: null,
      dragOverWeb: false,
      webState: 'idle',   // idle | uploading | processing | done
      webProgress: 0,
      webXhr: null,
    }
  },
  computed: {
    fwStatusText() {
      switch (this.fwState) {
        case 'uploading': return `正在上传… ${this.fwProgress}%`
        case 'writing': return '正在写入 Flash…'
        case 'done': return '校验通过，即将重启…'
        case 'rebooting': return '设备重启中，请稍候…'
        default: return ''
      }
    },
    webStatusText() {
      switch (this.webState) {
        case 'uploading': return `正在上传… ${this.webProgress}%`
        case 'processing': return '正在解包写入 SPIFFS…'
        case 'done': return '网页升级完成，刷新页面查看'
        default: return ''
      }
    }
  },
  methods: {
    particleStyle(n) {
      const size = 2 + Math.random() * 3
      return {
        left: ((n * 37 + 13) % 100) + '%',
        top: ((n * 53 + 7) % 100) + '%',
        width: size + 'px', height: size + 'px',
        animationDelay: (n * 0.7) + 's',
        animationDuration: (4 + Math.random() * 4) + 's',
      }
    },
    formatSize(bytes) {
      if (bytes < 1024) return bytes + ' B'
      if (bytes < 1024 * 1024) return (bytes / 1024).toFixed(1) + ' KB'
      return (bytes / (1024 * 1024)).toFixed(1) + ' MB'
    },

    // ── 文件选择 ──
    openFilePicker(type) {
      if (type === 'firmware') this.$refs.firmwareInput.click()
      else this.$refs.webInput.click()
    },
    onDropFirmware(e) {
      this.dragOver = false
      const f = e.dataTransfer.files[0]
      if (f && f.name.endsWith('.bin')) this.firmwareFile = f
    },
    onFirmwareSelected(e) {
      const f = e.target.files[0]
      if (f) this.firmwareFile = f
    },
    onDropWeb(e) {
      this.dragOverWeb = false
      const f = e.dataTransfer.files[0]
      if (f && f.name.endsWith('.tar')) this.webFile = f
    },
    onWebSelected(e) {
      const f = e.target.files[0]
      if (f) this.webFile = f
    },
    clearFile(type) {
      if (type === 'firmware') {
        this.firmwareFile = null
        this.fwState = 'idle'
        this.fwProgress = 0
        if (this.$refs.firmwareInput) this.$refs.firmwareInput.value = ''
      } else {
        this.webFile = null
        this.webState = 'idle'
        this.webProgress = 0
        if (this.$refs.webInput) this.$refs.webInput.value = ''
      }
    },

    // ── 固件上传 ──
    uploadFirmware() {
      if (!this.firmwareFile || this.fwState !== 'idle') return

      this.fwState = 'uploading'
      this.fwProgress = 0

      const xhr = new XMLHttpRequest()
      this.fwXhr = xhr

      xhr.upload.onprogress = (e) => {
        if (e.lengthComputable) {
          this.fwProgress = Math.round((e.loaded / e.total) * 80)  // upload = 0-80%
        }
      }

      xhr.onload = () => {
        if (xhr.status === 200) {
          this.fwState = 'writing'
          this.fwProgress = 90
          this.pollOtaProgress()
        } else {
          alert('上传失败: HTTP ' + xhr.status)
          this.clearFile('firmware')
        }
      }

      xhr.onerror = () => {
        alert('网络错误，上传中断')
        this.clearFile('firmware')
      }

      xhr.open('POST', '/ota/update')
      xhr.send(this.firmwareFile)
    },

    async pollOtaProgress() {
      let attempts = 0
      const maxAttempts = 60  // 最多等 60 秒

      const poll = async () => {
        try {
          const res = await fetch('/ota/progress')
          const data = await res.json()
          if (data.total > 0) {
            const pct = Math.round((data.written / data.total) * 100)
            this.fwProgress = 90 + Math.round(pct * 0.1)  // 90-100%
          }

          if (data.written >= data.total && data.total > 0) {
            this.fwProgress = 100
            this.fwState = 'done'
            setTimeout(() => { this.fwState = 'rebooting' }, 2000)
            return
          }

          attempts++
          if (attempts < maxAttempts) {
            setTimeout(poll, 1000)
          } else {
            this.fwState = 'done'
          }
        } catch (e) {
          // 设备可能已重启，忽略
          this.fwState = 'done'
          setTimeout(() => { this.fwState = 'rebooting' }, 1000)
        }
      }

      setTimeout(poll, 500)
    },

    // ── 网页上传 ──
    uploadWeb() {
      if (!this.webFile || this.webState !== 'idle') return

      this.webState = 'uploading'
      this.webProgress = 0

      const xhr = new XMLHttpRequest()
      this.webXhr = xhr

      xhr.upload.onprogress = (e) => {
        if (e.lengthComputable) {
          this.webProgress = Math.round((e.loaded / e.total) * 90)
        }
      }

      xhr.onload = () => {
        if (xhr.status === 200) {
          this.webProgress = 100
          this.webState = 'done'
        } else {
          try {
            const err = JSON.parse(xhr.responseText)
            alert('升级失败: ' + (err.error || '未知错误'))
          } catch {
            alert('升级失败: HTTP ' + xhr.status)
          }
          this.clearFile('web')
        }
      }

      xhr.onerror = () => {
        alert('网络错误，上传中断')
        this.clearFile('web')
      }

      xhr.open('POST', '/api/web/update')
      xhr.setRequestHeader('Content-Type', 'application/octet-stream')
      xhr.send(this.webFile)
    }
  },
  beforeUnmount() {
    if (this.fwXhr) this.fwXhr.abort()
    if (this.webXhr) this.webXhr.abort()
  }
}
</script>

<style scoped>
.ota-page {
  position: relative;
  min-height: 100vh;
  background: linear-gradient(160deg, #e8f0fe 0%, #d4e4fc 30%, #ffffff 100%);
  overflow: hidden;
  padding-bottom: 40px;
}

/* ── 粒子 ── */
.bg-particles { position: absolute; inset: 0; pointer-events: none; overflow: hidden; }
.particle {
  position: absolute; border-radius: 50%;
  background: rgba(24, 119, 242, 0.12);
  animation: floatUp 6s ease-in-out infinite;
}
@keyframes floatUp {
  0%, 100% { transform: translateY(0) scale(1); opacity: 0.2; }
  50% { transform: translateY(-40px) scale(1.8); opacity: 0.7; }
}

/* ── 头部 ── */
.ota-header {
  text-align: center;
  padding: 28px 16px 16px;
}
.header-icon { margin-bottom: 8px; }
.ota-svg {
  width: 40px; height: 40px;
  fill: #1976d2;
  transition: transform 0.6s;
}
.ota-svg.active { animation: checkPulse 1s ease-in-out infinite; }
@keyframes checkPulse {
  0%, 100% { transform: scale(1); }
  50% { transform: scale(1.15); fill: #2e7d32; }
}
.ota-header h2 { font-size: 20px; color: #212121; margin: 0; }
.ota-header p { font-size: 13px; color: #757575; margin: 4px 0 0; }

/* ── Tab ── */
.tab-bar {
  display: flex;
  margin: 8px 16px;
  background: rgba(255,255,255,0.6);
  border-radius: 10px;
  padding: 4px;
  backdrop-filter: blur(8px);
}
.tab {
  flex: 1;
  padding: 10px 0;
  border: none;
  border-radius: 8px;
  background: transparent;
  font-size: 14px;
  font-weight: 500;
  color: #757575;
  cursor: pointer;
  transition: all 0.2s;
}
.tab.active {
  background: #1976d2;
  color: #fff;
  box-shadow: 0 2px 8px rgba(25,118,210,0.3);
}

/* ── 卡片 ── */
.card {
  margin: 12px 16px;
  padding: 20px;
  background: rgba(255,255,255,0.88);
  backdrop-filter: blur(8px);
  border-radius: 16px;
  box-shadow: 0 2px 12px rgba(0,0,0,0.05);
}
.card-title { font-size: 16px; font-weight: 600; color: #212121; margin-bottom: 4px; }
.card-desc { font-size: 13px; color: #9e9e9e; margin: 0 0 16px; line-height: 1.6; }
.card-desc code {
  display: block;
  margin-top: 6px;
  padding: 6px 10px;
  background: #f5f5f5;
  border-radius: 6px;
  font-size: 12px;
  color: #616161;
}

/* ── 拖拽区 ── */
.drop-zone {
  border: 2px dashed #bbdefb;
  border-radius: 12px;
  padding: 32px 16px;
  text-align: center;
  cursor: pointer;
  transition: all 0.2s;
  position: relative;
}
.drop-zone.dragover {
  border-color: #1976d2;
  background: rgba(25,118,210,0.06);
}
.drop-zone.hasfile {
  border-style: solid;
  border-color: #1976d2;
  background: rgba(25,118,210,0.04);
  display: flex;
  align-items: center;
  justify-content: center;
  gap: 12px;
  padding: 20px 16px;
}
.drop-icon { font-size: 36px; margin-bottom: 8px; }
.drop-zone p { font-size: 14px; color: #616161; margin: 0; }
.drop-hint { font-size: 12px; color: #bdbdbd; }
.file-input-hidden { display: none; }

.file-info { text-align: left; }
.file-name { display: block; font-size: 15px; font-weight: 500; color: #212121; word-break: break-all; }
.file-size { font-size: 12px; color: #9e9e9e; }

.btn-clear {
  flex-shrink: 0;
  width: 28px; height: 28px;
  border: none; border-radius: 50%;
  background: #ef5350; color: #fff;
  font-size: 14px; cursor: pointer;
  display: flex; align-items: center; justify-content: center;
}

/* ── 进度条 ── */
.progress-section { margin-top: 16px; }
.progress-bar {
  height: 6px;
  background: #e0e0e0;
  border-radius: 3px;
  overflow: hidden;
}
.progress-fill {
  height: 100%;
  background: linear-gradient(90deg, #1976d2, #42a5f5);
  border-radius: 3px;
  transition: width 0.4s ease;
}
.progress-fill.web-fill {
  background: linear-gradient(90deg, #00897b, #26a69a);
}
.progress-fill.done {
  background: linear-gradient(90deg, #2e7d32, #66bb6a);
}
.progress-label {
  font-size: 12px; color: #616161;
  margin: 8px 0 0; text-align: center;
}

/* ── 按钮 ── */
.btn-primary {
  width: 100%;
  margin-top: 16px;
  padding: 14px;
  border: none;
  border-radius: 12px;
  background: linear-gradient(135deg, #1565c0, #1976d2);
  color: #fff;
  font-size: 15px;
  font-weight: 600;
  cursor: pointer;
  box-shadow: 0 4px 16px rgba(25,118,210,0.3);
  transition: transform 0.15s, box-shadow 0.15s;
}
.btn-primary:hover { transform: translateY(-1px); box-shadow: 0 6px 20px rgba(25,118,210,0.4); }
.btn-primary.btn-web {
  background: linear-gradient(135deg, #00695c, #00897b);
  box-shadow: 0 4px 16px rgba(0,137,123,0.3);
}
.btn-primary.btn-web:hover { box-shadow: 0 6px 20px rgba(0,137,123,0.4); }

/* ── 提示 ── */
.tips {
  margin: 24px 16px;
  padding: 14px 16px;
  background: rgba(255,152,0,0.08);
  border-radius: 10px;
  border-left: 3px solid #ff9800;
}
.tips p { font-size: 13px; color: #e65100; margin: 0; }

/* ── 选中文本禁用（拖拽区） ── */
.drop-zone { user-select: none; -webkit-user-select: none; }
</style>
