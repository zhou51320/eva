const state = {
  sessions: [],
  activeSessionId: null,
  backendState: null,
  models: [],
  streaming: false,
  currentChatAbort: null,
};

const els = {
  navItems: Array.from(document.querySelectorAll('.nav-item[data-panel]')),
  auxTabs: Array.from(document.querySelectorAll('.aux-tab[data-aux]')),
  auxPanes: {
    overview: document.getElementById('aux-overview'),
    runtime: document.getElementById('aux-runtime'),
    models: document.getElementById('aux-models'),
  },
  sessionSearch: document.getElementById('session-search'),
  sessionCount: document.getElementById('session-count'),
  chatPanel: document.getElementById('chat-panel'),
  runtimePanel: document.getElementById('runtime-panel'),
  messageList: document.getElementById('message-list'),
  messageTemplate: document.getElementById('message-template'),
  sessionList: document.getElementById('session-list'),
  healthChip: document.getElementById('health-chip'),
  brandSubtitle: document.getElementById('brand-subtitle'),
  sourceBadgeText: document.getElementById('source-badge-text'),
  runtimeSummary: document.getElementById('runtime-summary'),
  heroMode: document.getElementById('hero-mode'),
  heroModel: document.getElementById('hero-model'),
  heroEndpoint: document.getElementById('hero-endpoint'),
  modeSelect: document.getElementById('mode-select'),
  localFields: document.getElementById('local-fields'),
  linkFields: document.getElementById('link-fields'),
  localModelSelect: document.getElementById('local-model-select'),
  backendSelect: document.getElementById('backend-select'),
  endpointInput: document.getElementById('endpoint-input'),
  apikeyInput: document.getElementById('apikey-input'),
  remoteModelInput: document.getElementById('remote-model-input'),
  portInput: document.getElementById('port-input'),
  nthreadInput: document.getElementById('nthread-input'),
  nctxInput: document.getElementById('nctx-input'),
  loadFeedback: document.getElementById('load-feedback'),
  stateGrid: document.getElementById('state-grid'),
  modelsList: document.getElementById('models-list'),
  chatInput: document.getElementById('chat-input'),
  streamToggle: document.getElementById('stream-toggle'),
  stopChat: document.getElementById('stop-chat'),
  sendChat: document.getElementById('send-chat'),
  refreshAll: document.getElementById('refresh-all'),
  applyLoad: document.getElementById('apply-load'),
  newChat: document.getElementById('new-chat'),
};

const STORAGE_KEY = 'eva-acp-webui-sessions';

function loadSessions() {
  try {
    const raw = localStorage.getItem(STORAGE_KEY);
    if (!raw) return;
    const parsed = JSON.parse(raw);
    if (!Array.isArray(parsed)) return;
    state.sessions = parsed.slice(0, 1);
    if (state.sessions.length > 0) {
      state.activeSessionId = state.sessions[0].id;
    }
  } catch (_) {}
}

function saveSessions() {
  localStorage.setItem(STORAGE_KEY, JSON.stringify(state.sessions.slice(0, 12)));
}

async function resetRuntimeConversation() {
  const response = await fetch('/api/runtime/reset', { method: 'POST' });
  const text = await response.text();
  let json = {};
  try {
    json = text ? JSON.parse(text) : {};
  } catch (_) {
    json = { raw: text };
  }
  if (!response.ok) {
    throw new Error(json.error || json.details || text || `HTTP ${response.status}`);
  }
  return json;
}

async function stopRuntimeTurn() {
  const response = await fetch('/api/runtime/stop', { method: 'POST' });
  const text = await response.text();
  let json = {};
  try {
    json = text ? JSON.parse(text) : {};
  } catch (_) {
    json = { raw: text };
  }
  if (!response.ok) {
    throw new Error(json.error || json.details || text || `HTTP ${response.status}`);
  }
  return json;
}

function createSession() {
  const session = {
    id: String(Date.now()),
    title: '新对话',
    messages: [],
    createdAt: new Date().toISOString(),
  };
  state.sessions = [session];
  state.activeSessionId = session.id;
  saveSessions();
  renderSessions();
  renderMessages();
  return session;
}

function getActiveSession() {
  return state.sessions.find((item) => item.id === state.activeSessionId) || createSession();
}

function renderSessions() {
  els.sessionList.innerHTML = '';
  const keyword = (els.sessionSearch?.value || '').trim();
  const visibleSessions = keyword ? state.sessions.filter((session) => (session.title || '').includes(keyword) || (session.messages[0]?.content || '').includes(keyword)) : state.sessions;
  if (els.sessionCount) els.sessionCount.textContent = String(visibleSessions.length);
  if (visibleSessions.length === 0) {
    const empty = document.createElement('div');
    empty.className = 'empty-hint';
    empty.textContent = '会话会保存在浏览器本地。';
    els.sessionList.appendChild(empty);
    return;
  }
  visibleSessions.forEach((session) => {
    const item = document.createElement('button');
    item.type = 'button';
    item.className = 'session-chip';
    item.innerHTML = `<strong>${escapeHtml(session.title)}</strong><span>${escapeHtml(session.messages[0]?.content || '空会话')}</span>`;
    if (session.id === state.activeSessionId) {
      item.style.borderColor = 'rgba(79,140,255,0.44)';
      item.style.background = 'rgba(79,140,255,0.08)';
    }
    item.addEventListener('click', () => {
      state.activeSessionId = session.id;
      renderSessions();
      renderMessages();
    });
    els.sessionList.appendChild(item);
  });
}

function renderMessages() {
  els.messageList.innerHTML = '';
  const session = getActiveSession();
  if (session.messages.length === 0) {
    const empty = document.createElement('div');
    empty.className = 'empty-hint';
    empty.textContent = '输入一条消息开始。首版 WebUI 先覆盖聊天、状态、模型和装载。';
    els.messageList.appendChild(empty);
    return;
  }
  session.messages.forEach((message) => {
    const fragment = els.messageTemplate.content.cloneNode(true);
    const root = fragment.querySelector('.message');
    root.dataset.role = message.role;
    fragment.querySelector('.message-role').textContent = roleLabel(message.role);
    fragment.querySelector('.message-body').textContent = message.content || '';
    fragment.querySelector('.message-meta').textContent = message.meta || '';
    els.messageList.appendChild(fragment);
  });
  els.messageList.scrollTop = els.messageList.scrollHeight;
}

function renderBackendState() {
  const info = state.backendState || {};
  const caps = info.capabilities || {};
  const tts = caps.tts || {};
  const availableTools = Array.isArray(caps.enabled_tools) && caps.enabled_tools.length > 0 ? caps.enabled_tools.join(', ') : '无';
  const configuredTools = Array.isArray(caps.configured_tools_list) && caps.configured_tools_list.length > 0 ? caps.configured_tools_list.join(', ') : '无';
  const mode = info.mode || '-';
  const endpoint = info.endpoint || info.api_endpoint || '-';
  const model = info.current_model || info.api_model || '-';
  const source = info.state_source || (info.direct_runtime ? 'direct_runtime' : (info.bridge_available ? 'bridge' : 'legacy_acp'));
  const sourceLabel = source === 'direct_runtime' ? '直接运行层' : (source === 'bridge' ? '主程序桥接' : 'ACP 降级');

  els.healthChip.textContent = `${sourceLabel} · ${info.state || 'unknown'} · ${info.ready ? 'ready' : 'idle'}`;
  if (els.brandSubtitle) els.brandSubtitle.textContent = source === 'direct_runtime' ? 'ACP runtime console' : 'ACP bridge console';
  if (els.sourceBadgeText) els.sourceBadgeText.textContent = source === 'direct_runtime' ? '通过 EvaRuntime 无窗口运行' : (source === 'bridge' ? '通过 EVA 主程序桥接运行' : 'ACP 本地降级运行');
  if (els.runtimeSummary) els.runtimeSummary.textContent = source === 'direct_runtime' ? '统一查看当前会话、模型与运行时状态，并通过 EVA 无窗口运行层执行装载与对话。' : '统一查看当前会话、模型与运行时状态；当前由桥接或 ACP 降级路径执行。';
  els.heroMode.textContent = mode;
  els.heroModel.textContent = model;
  els.heroEndpoint.textContent = endpoint;

  els.modeSelect.value = mode === 'link' ? 'link' : 'local';
  toggleModeFields();
  if (info.api_endpoint) els.endpointInput.value = info.api_endpoint;
  if (info.api_model) els.remoteModelInput.value = info.api_model;
  if (info.port) els.portInput.value = info.port;
  if (info.nthread) els.nthreadInput.value = info.nthread;
  if (info.nctx) els.nctxInput.value = info.nctx;
  if (info.backend_choice && info.backend_choice !== 'link') els.backendSelect.value = info.backend_choice;

  const pairs = [
    ['运行状态', info.state || '-'],
    ['当前模式', mode],
    ['当前模型', model],
    ['端点', endpoint],
    ['状态源', sourceLabel],
    ['聊天路径', info.chat_route || '-'],
    ['EVA能力', caps.full_eva_stack ? '完整主程序桥接' : 'direct runtime 基础推理'],
    ['会话归属', caps.conversation_owner || '-'],
    ['输入模式', caps.message_input_mode || '-'],
    ['桥接', info.bridge_available ? '可用' : '未使用'],
    ['命令能力', `chat:${caps.chat ? 'on' : '-'} stream:${caps.stream ? 'on' : '-'} stop:${caps.stop ? 'on' : '-'}`],
    ['可执行工具', availableTools],
    ['已配置工具', configuredTools],
    ['知识库/MCP', `knowledge:${caps.knowledge ? 'on' : '-'} mcp:${caps.mcp ? 'on' : '-'}`],
    ['TTS', `model:${tts.model_configured ? 'ok' : '-'} program:${tts.program_available ? 'ok' : '-'}`],
    ['后端选择', info.backend_choice || '-'],
    ['后端解析', info.backend_resolved || '-'],
    ['线程', info.nthread || '-'],
    ['上下文', info.nctx || '-'],
    ['错误', info.last_error || '无'],
  ];
  els.stateGrid.innerHTML = pairs.map(([label, value]) => `<div class="state-item"><strong>${escapeHtml(label)}</strong><span>${escapeHtml(String(value || '-'))}</span></div>`).join('');
}

function renderModels() {
  els.modelsList.innerHTML = '';
  if (!Array.isArray(state.models) || state.models.length === 0) {
    els.modelsList.innerHTML = '<div class="empty-hint">当前没有可用模型。链接模式下请先应用端点和模型名。</div>';
    refreshLocalModelSelect();
    return;
  }
  state.models.forEach((model) => {
    const item = document.createElement('div');
    item.className = `model-item${model.current ? ' current' : ''}`;
    const detail = model.source === 'remote' ? (model.endpoint || '') : (model.path || '');
    item.innerHTML = `<strong>${escapeHtml(model.id || 'unnamed')}</strong><span>${escapeHtml(detail)}</span>`;
    els.modelsList.appendChild(item);
  });
  refreshLocalModelSelect();
}

function refreshLocalModelSelect() {
  const localModels = (state.models || []).filter((item) => item.source !== 'remote');
  els.localModelSelect.innerHTML = '';
  if (localModels.length === 0) {
    const opt = document.createElement('option');
    opt.value = '';
    opt.textContent = '未发现本地模型';
    els.localModelSelect.appendChild(opt);
    return;
  }
  localModels.forEach((model) => {
    const opt = document.createElement('option');
    opt.value = model.path || '';
    opt.textContent = model.id || model.path || 'unnamed';
    if (model.current) opt.selected = true;
    els.localModelSelect.appendChild(opt);
  });
}

function toggleModeFields() {
  const link = els.modeSelect.value === 'link';
  els.localFields.classList.toggle('hidden', link);
  els.linkFields.classList.toggle('hidden', !link);
}

function switchPanel(panel) {
  const runtime = panel === 'runtime';
  els.chatPanel.classList.toggle('hidden', runtime);
  els.runtimePanel.classList.toggle('hidden', !runtime);
  els.navItems.forEach((item) => item.classList.toggle('active', item.dataset.panel === panel));
  if (runtime) switchAuxTab('runtime');
}

function switchAuxTab(name) {
  Object.entries(els.auxPanes).forEach(([key, pane]) => {
    if (!pane) return;
    pane.classList.toggle('hidden', key !== name);
    pane.classList.toggle('active', key === name);
  });
  els.auxTabs.forEach((tab) => tab.classList.toggle('active', tab.dataset.aux === name));
}

async function fetchJson(url, options) {
  const response = await fetch(url, options);
  const text = await response.text();
  let json = {};
  try {
    json = text ? JSON.parse(text) : {};
  } catch (_) {
    json = { raw: text };
  }
  if (!response.ok) {
    throw new Error(json.error || json.details || text || `HTTP ${response.status}`);
  }
  return json;
}

async function refreshAll() {
  try {
    const [health, backend, models] = await Promise.all([
      fetchJson('/health'),
      fetchJson('/api/backend/state'),
      fetchJson('/v1/models'),
    ]);
    state.backendState = Object.assign({}, health, backend);
    state.models = models.data || [];
    renderBackendState();
    renderModels();
  } catch (error) {
    els.healthChip.textContent = `错误 · ${error.message}`;
  }
}

async function applyLoad() {
  const mode = els.modeSelect.value;
  const payload = { mode };
  if (els.portInput.value.trim()) payload.port = els.portInput.value.trim();
  if (els.nthreadInput.value.trim()) payload.nthread = Number(els.nthreadInput.value.trim());
  if (els.nctxInput.value.trim()) payload.nctx = Number(els.nctxInput.value.trim());

  if (mode === 'link') {
    payload.api_endpoint = els.endpointInput.value.trim();
    payload.api_key = els.apikeyInput.value.trim();
    payload.api_model = els.remoteModelInput.value.trim();
  } else {
    payload.backend = els.backendSelect.value;
    if (els.localModelSelect.value) payload.model_path = els.localModelSelect.value;
  }

  els.loadFeedback.textContent = '正在应用…';
  try {
    const data = await fetchJson('/api/backend/load', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(payload),
    });
    state.backendState = data;
    els.loadFeedback.textContent = '已应用。';
    await refreshAll();
  } catch (error) {
    els.loadFeedback.textContent = error.message;
  }
}

function addMessage(role, content, meta) {
  const session = getActiveSession();
  session.messages.push({ role, content, meta: meta || '' });
  if (role === 'user' && session.title === '新对话') {
    session.title = content.slice(0, 18) || '新对话';
  }
  saveSessions();
  renderSessions();
  renderMessages();
}

function updateLastAssistant(content, meta) {
  const session = getActiveSession();
  const message = session.messages[session.messages.length - 1];
  if (!message || message.role !== 'assistant') return;
  message.content = content;
  message.meta = meta || '';
  saveSessions();
  renderMessages();
}

function normalizeErrorText(raw, fallback) {
  const text = String(raw || '').trim();
  if (!text) return fallback;
  try {
    const parsed = JSON.parse(text);
    return parsed.error || parsed.details || parsed.message || text;
  } catch (_) {
    return text;
  }
}

async function sendChat() {
  if (state.streaming) return;
  const input = els.chatInput.value.trim();
  if (!input) return;

  const session = getActiveSession();
  const messages = session.messages
    .filter((item) => item.role === 'user' || item.role === 'assistant' || item.role === 'system')
    .map((item) => ({ role: item.role, content: item.content }));
  messages.push({ role: 'user', content: input });

  addMessage('user', input, new Date().toLocaleTimeString());
  addMessage('assistant', '...', '等待响应');
  els.chatInput.value = '';
  state.streaming = true;
  state.currentChatAbort = new AbortController();
  els.sendChat.disabled = true;
  if (els.stopChat) els.stopChat.disabled = false;

  const payload = {
    messages,
    stream: els.streamToggle.checked,
  };

  if (els.modeSelect.value === 'link' && els.remoteModelInput.value.trim()) {
    payload.model = els.remoteModelInput.value.trim();
  } else if (els.localModelSelect.value) {
    payload.model = els.localModelSelect.value;
  }

  try {
    const response = await fetch('/v1/chat/completions', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(payload),
      signal: state.currentChatAbort.signal,
    });

    if (!response.ok) {
      const errorText = await response.text();
      throw new Error(normalizeErrorText(errorText, `HTTP ${response.status}`));
    }

    if (!els.streamToggle.checked) {
      updateLastAssistant('...', '接收完整响应');
      const data = await response.json();
      const content = data.choices?.[0]?.message?.content || '';
      updateLastAssistant(content || '(空响应)', content ? '完成' : '完成 · 空响应');
      return;
    }

    const reader = response.body.getReader();
    const decoder = new TextDecoder();
    let buffer = '';
    let answer = '';
    let reasoning = '';
    let streamFailed = false;

    while (true) {
      const { value, done } = await reader.read();
      if (done) break;
      if (streamFailed) break;
      buffer += decoder.decode(value, { stream: true });
      const lines = buffer.split(/\r?\n/);
      buffer = lines.pop() || '';
      for (const line of lines) {
        if (!line.startsWith('data:')) continue;
        const payloadText = line.slice(5).trim();
        if (!payloadText || payloadText === '[DONE]') continue;
        try {
          const data = JSON.parse(payloadText);
          const delta = data.choices?.[0]?.delta || {};
          if (data.error) {
            throw new Error(String(data.error));
          }
          if (typeof delta.content === 'string') answer += delta.content;
          if (typeof delta.reasoning === 'string') reasoning += delta.reasoning;
          if (typeof delta.reasoning_content === 'string') reasoning += delta.reasoning_content;
          if (!delta.content && data.choices?.[0]?.message?.content) answer += data.choices[0].message.content;
          const meta = reasoning ? `流式输出 · 推理 ${reasoning.length} chars` : '流式输出';
          updateLastAssistant(answer || '...', meta);
        } catch (error) {
          streamFailed = true;
          updateLastAssistant(`请求失败：${error.message}`, '错误');
          break;
        }
      }
    }

    if (streamFailed) return;
    updateLastAssistant(answer || '(空响应)', reasoning ? '完成 · 含推理内容' : '完成');
  } catch (error) {
    if (error.name === 'AbortError') {
      updateLastAssistant('已停止。', '停止');
    } else {
      updateLastAssistant(`请求失败：${error.message}`, '错误');
    }
  } finally {
    state.streaming = false;
    state.currentChatAbort = null;
    els.sendChat.disabled = false;
    if (els.stopChat) els.stopChat.disabled = true;
    refreshAll();
  }
}

function roleLabel(role) {
  if (role === 'assistant') return 'assistant';
  if (role === 'system') return 'system';
  return 'user';
}

function escapeHtml(value) {
  return String(value)
    .replace(/&/g, '&amp;')
    .replace(/</g, '&lt;')
    .replace(/>/g, '&gt;')
    .replace(/"/g, '&quot;')
    .replace(/'/g, '&#39;');
}

function bindEvents() {
  els.navItems.forEach((item) => item.addEventListener('click', () => switchPanel(item.dataset.panel)));
  els.auxTabs.forEach((tab) => tab.addEventListener('click', () => switchAuxTab(tab.dataset.aux)));
  els.sessionSearch?.addEventListener('input', renderSessions);
  els.modeSelect.addEventListener('change', toggleModeFields);
  els.applyLoad.addEventListener('click', applyLoad);
  els.refreshAll.addEventListener('click', refreshAll);
  els.sendChat.addEventListener('click', sendChat);
  els.stopChat?.addEventListener('click', async () => {
    if (state.currentChatAbort) state.currentChatAbort.abort();
    try {
      await stopRuntimeTurn();
    } catch (error) {
      updateLastAssistant(`停止失败：${error.message}`, '错误');
    } finally {
      await refreshAll();
    }
  });
  els.newChat.addEventListener('click', async () => {
    try {
      await resetRuntimeConversation();
    } catch (_) {}
    createSession();
    await refreshAll();
  });
  els.chatInput.addEventListener('keydown', (event) => {
    if ((event.ctrlKey || event.metaKey) && event.key === 'Enter') {
      event.preventDefault();
      sendChat();
    }
  });
}

async function bootstrap() {
  loadSessions();
  if (state.sessions.length === 0) createSession();
  renderSessions();
  renderMessages();
  bindEvents();
  toggleModeFields();
  switchPanel('chat');
  switchAuxTab('overview');
  await refreshAll();
}

bootstrap();
