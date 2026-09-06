import { defineConfig } from 'vite'
import vue from '@vitejs/plugin-vue'

export default defineConfig({
  plugins: [vue()],
  
  // 开发时的代理配置（方便本地调试）
  server: {
    proxy: {
      '/api': {
        target: 'http://192.168.4.1', // ESP32 AP 模式的 IP
        changeOrigin: true
      }
    }
  },

  // 构建配置
  build: {
    // 输出到 dist 目录
    outDir: 'dist',
    
    // 最小化输出
    minify: 'terser',
    
    // 将 CSS 内联到 JS 中（减少请求数）
    cssCodeSplit: false,
    
    // 将小图片转成 base64
    assetsInlineLimit: 4096,
    
    // 关闭源码映射（减小体积）
    sourcemap: false,
    
    // 打包成一个 JS 文件（不分割代码）
    rollupOptions: {
      output: {
        manualChunks: undefined
      }
    }
  }
})
