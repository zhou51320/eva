import MarkdownIt from 'markdown-it'
import hljs from 'highlight.js/lib/core'

// Curated language set — keeps the single-file bundle small while covering the
// languages an EVA user is most likely to see.
import bash from 'highlight.js/lib/languages/bash'
import c from 'highlight.js/lib/languages/c'
import cpp from 'highlight.js/lib/languages/cpp'
import cmake from 'highlight.js/lib/languages/cmake'
import css from 'highlight.js/lib/languages/css'
import diff from 'highlight.js/lib/languages/diff'
import go from 'highlight.js/lib/languages/go'
import ini from 'highlight.js/lib/languages/ini'
import java from 'highlight.js/lib/languages/java'
import javascript from 'highlight.js/lib/languages/javascript'
import json from 'highlight.js/lib/languages/json'
import markdown from 'highlight.js/lib/languages/markdown'
import python from 'highlight.js/lib/languages/python'
import rust from 'highlight.js/lib/languages/rust'
import shell from 'highlight.js/lib/languages/shell'
import sql from 'highlight.js/lib/languages/sql'
import typescript from 'highlight.js/lib/languages/typescript'
import xml from 'highlight.js/lib/languages/xml'
import yaml from 'highlight.js/lib/languages/yaml'

const LANGS: Record<string, any> = {
  bash,
  c,
  cpp,
  cmake,
  css,
  diff,
  go,
  ini,
  java,
  javascript,
  json,
  markdown,
  python,
  rust,
  shell,
  sql,
  typescript,
  xml,
  yaml,
}
for (const [name, def] of Object.entries(LANGS)) hljs.registerLanguage(name, def)

const ALIASES: Record<string, string> = {
  js: 'javascript',
  ts: 'typescript',
  py: 'python',
  sh: 'bash',
  'c++': 'cpp',
  h: 'cpp',
  hpp: 'cpp',
  html: 'xml',
  yml: 'yaml',
  text: 'plaintext',
  txt: 'plaintext',
}

function escapeHtml(value: string): string {
  return value
    .replace(/&/g, '&amp;')
    .replace(/</g, '&lt;')
    .replace(/>/g, '&gt;')
    .replace(/"/g, '&quot;')
}

const md = new MarkdownIt({
  html: false, // never trust model output as raw HTML
  linkify: true,
  breaks: false,
})

// Custom fenced code block: language label + copy button + highlighted body.
md.renderer.rules.fence = (tokens, idx) => {
  const token = tokens[idx]
  const raw = token.content
  const info = (token.info || '').trim().split(/\s+/)[0].toLowerCase()
  const lang = ALIASES[info] || info
  let body: string
  let label = lang || 'text'
  if (lang && hljs.getLanguage(lang)) {
    body = hljs.highlight(raw, { language: lang, ignoreIllegals: true }).value
  } else {
    body = escapeHtml(raw)
    label = lang || 'text'
  }
  return (
    `<div class="codeblock">` +
    `<div class="codeblock__bar"><span class="codeblock__lang">${escapeHtml(label)}</span>` +
    `<button class="codeblock__copy" type="button">复制</button></div>` +
    `<pre class="codeblock__pre"><code class="hljs">${body}</code></pre>` +
    `</div>`
  )
}

// Open links in a new tab.
const defaultLinkOpen =
  md.renderer.rules.link_open ||
  ((tokens, idx, options, _env, self) => self.renderToken(tokens, idx, options))
md.renderer.rules.link_open = (tokens, idx, options, env, self) => {
  tokens[idx].attrSet('target', '_blank')
  tokens[idx].attrSet('rel', 'noopener noreferrer')
  return defaultLinkOpen(tokens, idx, options, env, self)
}

export function renderMarkdown(source: string): string {
  return md.render(source || '')
}
