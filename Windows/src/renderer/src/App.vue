<script setup lang="ts">
import { computed, onMounted, watchEffect } from 'vue'
import { useRoute, useRouter } from 'vue-router'
import { NConfigProvider, darkTheme, type GlobalThemeOverrides } from 'naive-ui'
import { useSettingsStore } from './stores/settings'
import { useChatStore } from './stores/chat'
import { useEsp32Store } from './stores/esp32'
import { usePerfStore } from './stores/perf'

const route = useRoute()
const router = useRouter()
const settings = useSettingsStore()
const chat = useChatStore()
const esp32 = useEsp32Store()
const perf = usePerfStore()

const theme = computed(() => (settings.theme === 'dark' ? darkTheme : null))
const isDark = computed(() => settings.theme === 'dark')

const themeOverrides = computed<GlobalThemeOverrides>(() => {
  const pink = isDark.value ? '#ff8ec3' : '#ff7eb9'
  const pinkHover = isDark.value ? '#ffa5d0' : '#f062a5'
  const textBase = isDark.value ? '#f0edfa' : '#2d2a3e'
  const text2 = isDark.value ? '#c4bedd' : '#6f6a87'
  const text3 = isDark.value ? '#918baf' : '#a8a2bd'
  const borderColor = isDark.value ? 'rgba(255,255,255,0.14)' : 'rgba(255,255,255,0.62)'
  const inputBg = isDark.value ? 'rgba(255,255,255,0.06)' : 'rgba(255,255,255,0.6)'
  const cardColor = isDark.value ? 'rgba(255,255,255,0.05)' : 'rgba(255,255,255,0.5)'
  const popColor = isDark.value ? '#231d3a' : '#ffffff'
  const errorColor = '#ff6b81'
  return {
    common: {
      primaryColor: pink,
      primaryColorHover: pinkHover,
      primaryColorPressed: pinkHover,
      primaryColorSuppl: pink,
      infoColor: pink,
      warningColor: '#f5a524',
      errorColor,
      successColor: '#3fb68b',
      textColorBase: textBase,
      textColor1: textBase,
      textColor2: text2,
      textColor3: text3,
      placeholderColor: isDark.value ? 'rgba(228,222,248,0.4)' : 'rgba(160,154,184,0.6)',
      borderColor,
      borderRadius: '12px',
      bodyColor: 'transparent',
      cardColor,
      modalColor: popColor,
      popoverColor: popColor,
      inputColor: inputBg,
      inputColorDisabled: isDark.value ? 'rgba(255,255,255,0.04)' : '#f2f2f7',
      actionColor: isDark.value ? 'rgba(255,255,255,0.04)' : 'rgba(255,255,255,0.4)',
      hoverColor: isDark.value ? 'rgba(255,255,255,0.08)' : 'rgba(255,255,255,0.5)',
      tableHeaderColor: isDark.value ? 'rgba(255,255,255,0.05)' : 'rgba(255,255,255,0.4)'
    },
    Select: {
      peers: {
        InternalSelection: {
          color: inputBg,
          colorActive: inputBg,
          colorHover: inputBg,
          colorDisabled: isDark.value ? 'rgba(255,255,255,0.04)' : '#f2f2f7',
          textColor: textBase,
          placeholderColor: isDark.value ? 'rgba(228,222,248,0.4)' : 'rgba(160,154,184,0.6)',
          border: `1px solid ${borderColor}`,
          borderHover: `1px solid ${pink}`
        }
      }
    },
    Input: {
      color: inputBg,
      colorFocus: inputBg,
      colorDisabled: isDark.value ? 'rgba(255,255,255,0.04)' : '#f2f2f7',
      border: `1px solid ${borderColor}`,
      borderHover: `1px solid ${pink}`,
      borderFocus: `1px solid ${pink}`,
      textColor: textBase,
      placeholderColor: isDark.value ? 'rgba(228,222,248,0.4)' : 'rgba(160,154,184,0.6)'
    },
    InputNumber: {
      color: inputBg,
      colorFocus: inputBg,
      colorDisabled: isDark.value ? 'rgba(255,255,255,0.04)' : '#f2f2f7',
      border: `1px solid ${borderColor}`,
      borderHover: `1px solid ${pink}`,
      borderFocus: `1px solid ${pink}`,
      textColor: textBase,
      placeholderColor: isDark.value ? 'rgba(228,222,248,0.4)' : 'rgba(160,154,184,0.6)'
    },
    Card: {
      color: cardColor,
      borderColor,
      titleTextColor: textBase,
      textColor: text2,
      actionColor: 'transparent'
    },
    Form: {
      labelTextColor: text2,
      feedbackTextColor: text3
    },
    Switch: {
      railColor: isDark.value ? 'rgba(255,255,255,0.2)' : 'rgba(0,0,0,0.18)',
      railColorActive: pink,
      railColorActiveHover: pinkHover
    },
    Button: {
      textColorPrimary: '#fff',
      textColorHoverPrimary: '#fff',
      textColorPressedPrimary: '#fff',
      textColorFocusPrimary: '#fff'
    },
    Alert: {
      color: isDark.value ? 'rgba(255,255,255,0.06)' : 'rgba(255,255,255,0.5)',
      borderColor,
      textColor: text2,
      titleTextColor: textBase,
      infoTextColor: textBase
    },
    Popover: {
      color: popColor,
      textColor: textBase,
      borderColor
    }
  }
})

onMounted(async () => {
  await settings.load()
  chat.setupListeners()
  esp32.setupListeners()
  perf.setupListeners()
  void esp32.refresh()
})

watchEffect(() => {
  document.documentElement.classList.toggle('dark', settings.theme === 'dark')
})

function go(name: 'chat' | 'settings'): void {
  void router.push({ name })
}
</script>

<template>
  <n-config-provider :theme="theme" :theme-overrides="themeOverrides">
    <div class="app-shell">
      <header class="app-header">
        <div class="brand">
          <span class="brand-avatar">{{ settings.persona.avatar }}</span>
          <div class="brand-text">
            <span class="brand-name">{{ settings.persona.name }}</span>
            <span class="brand-sub">AI 聊天机器人</span>
          </div>
        </div>
        <nav class="app-nav">
          <button class="nav-btn" :class="{ active: route.name === 'chat' }" @click="go('chat')">
            聊天
          </button>
          <button
            class="nav-btn"
            :class="{ active: route.name === 'settings' }"
            @click="go('settings')"
          >
            设置
          </button>
        </nav>
      </header>
      <main class="app-main">
        <router-view />
      </main>
    </div>
  </n-config-provider>
</template>
