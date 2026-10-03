import { existsSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import type { Plugin } from 'vitest/config';

/**
 * Cortico loads providers with `import(pathToFileURL(entry).href)`. Vite keeps
 * the URL's percent-encoding, so a checkout whose path contains a space fails to
 * load them under Vitest (Node and Electron decode it correctly). Decode such ids
 * here instead of patching the pinned framework.
 */
export const decodeFileUrls: Plugin = {
  name: 'decode-file-urls',
  enforce: 'pre',
  resolveId(id) {
    const path = id.startsWith('file://') ? fileURLToPath(id) : id.includes('%') ? decodeURI(id) : null;
    return path && path !== id && existsSync(path) ? path : null;
  },
};
