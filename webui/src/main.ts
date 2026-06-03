import { createApp } from 'vue'
import App from './App.vue'
import { store } from './store'
import './styles.css'
import 'highlight.js/styles/github-dark.css'

store.init()
createApp(App).mount('#app')
