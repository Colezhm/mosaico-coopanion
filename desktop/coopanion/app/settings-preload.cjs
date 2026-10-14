/**
 * Preload of the settings window: the page reads its language from localStorage
 * (`cortico.console.language`) when it loads (console/base/language.ts); here that is the app
 * language, written before each page load.
 */
const { ipcRenderer } = require('electron');

try {
  localStorage.setItem('cortico.console.language', ipcRenderer.sendSync('settings:language'));
} catch { /* a page without storage (the starting page) */ }
