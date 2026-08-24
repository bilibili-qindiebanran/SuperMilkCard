import { app, shell, BrowserWindow, protocol, session } from 'electron'
import { join, extname } from 'path'
import { readFile } from 'fs/promises'
import { electronApp, optimizer, is } from '@electron-toolkit/utils'
import icon from '../../resources/icon.png?asset'
import { registerIpc } from './ipc'
import { modelsDir, builtinDir } from './services/live2dModels'

// 注册 live2d:// 自定义协议，用于打包后从本地读取 Live2D 模型资源（规避 file:// 的 XHR 限制）
protocol.registerSchemesAsPrivileged([
  {
    scheme: 'live2d',
    privileges: { standard: true, secure: true, supportFetchAPI: true, corsEnabled: true }
  }
])

function mimeFor(filePath: string): string {
  switch (extname(filePath).toLowerCase()) {
    case '.json':
      return 'application/json'
    case '.png':
      return 'image/png'
    case '.js':
      return 'text/javascript'
    default:
      return 'application/octet-stream'
  }
}

function registerLive2dProtocol(): void {
  // 候选目录：用户导入的模型目录优先，其次是内置目录
  const bases = [modelsDir(), builtinDir()]
  protocol.handle('live2d', async (request) => {
    const url = new URL(request.url)
    const rel = decodeURIComponent(url.host + url.pathname)
    for (const base of bases) {
      const filePath = join(base, rel)
      try {
        const data = await readFile(filePath)
        return new Response(data, {
          headers: {
            'Content-Type': mimeFor(filePath),
            'Access-Control-Allow-Origin': '*'
          }
        })
      } catch {
        // 尝试下一个目录
      }
    }
    return new Response('Not Found', {
      status: 404,
      headers: { 'Access-Control-Allow-Origin': '*' }
    })
  })
}

function createWindow(): void {
  // Create the browser window.
  const mainWindow = new BrowserWindow({
    width: 1200,
    height: 800,
    minWidth: 960,
    minHeight: 640,
    show: false,
    autoHideMenuBar: true,
    ...(process.platform === 'linux' ? { icon } : {}),
    webPreferences: {
      preload: join(__dirname, '../preload/index.js'),
      sandbox: false
    }
  })

  mainWindow.on('ready-to-show', () => {
    mainWindow.show()
  })

  mainWindow.webContents.setWindowOpenHandler((details) => {
    shell.openExternal(details.url)
    return { action: 'deny' }
  })

  // HMR for renderer base on electron-vite cli.
  // Load the remote URL for development or the local html file for production.
  if (is.dev && process.env['ELECTRON_RENDERER_URL']) {
    mainWindow.loadURL(process.env['ELECTRON_RENDERER_URL'])
  } else {
    mainWindow.loadFile(join(__dirname, '../renderer/index.html'))
  }
}

// This method will be called when Electron has finished
// initialization and is ready to create browser windows.
// Some APIs can only be used after this event occurs.
app.whenReady().then(() => {
  // Set app user model id for windows
  electronApp.setAppUserModelId('com.supermilkcard.app')

  // 麦克风权限：允许渲染层使用 getUserMedia（语音输入/网页 STT）。
  // 仅放行 media 权限，其它敏感权限（如通知/定位/摄像头）保持默认拒绝。
  session.defaultSession.setPermissionRequestHandler((_wc, permission, callback) => {
    if (permission === 'media') {
      callback(true)
    } else {
      callback(false)
    }
  })
  // 处理媒体权限检查（部分平台走 checkHandler）
  session.defaultSession.setPermissionCheckHandler((_wc, permission) => {
    return permission === 'media'
  })

  // Default open or close DevTools by F12 in development
  // and ignore CommandOrControl + R in production.
  // see https://github.com/alex8088/electron-toolkit/tree/master/packages/utils
  app.on('browser-window-created', (_, window) => {
    optimizer.watchWindowShortcuts(window)
  })

  registerLive2dProtocol()
  registerIpc()

  createWindow()

  app.on('activate', function () {
    // On macOS it's common to re-create a window in the app when the
    // dock icon is clicked and there are no other windows open.
    if (BrowserWindow.getAllWindows().length === 0) createWindow()
  })
})

// Quit when all windows are closed, except on macOS. There, it's common
// for applications and their menu bar to stay active until the user quits
// explicitly with Cmd + Q.
app.on('window-all-closed', () => {
  if (process.platform !== 'darwin') {
    app.quit()
  }
})

// In this file you can include the rest of your app's specific main process
// code. You can also put them in separate files and require them here.
