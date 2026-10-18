import { fileURLToPath } from 'node:url';
import { defineConfig } from 'vitest/config';

/** `cortico/*` resolves to the pinned framework source, as for the desktop pet package; keep in step with tsconfig `paths`. */
const FRAMEWORK_SRC = fileURLToPath(new URL('../../vendor/cortico/src/', import.meta.url));

export default defineConfig({
  resolve: {
    alias: [{ find: /^cortico\//, replacement: FRAMEWORK_SRC }],
  },
  server: {
    fs: { allow: [fileURLToPath(new URL('../', import.meta.url)), FRAMEWORK_SRC] },
  },
  test: {
    include: ['tests/**/*.test.ts'],
    environmentOptions: { jsdom: { url: 'http://127.0.0.1/pet' } },
    env: { CORTICO_LANGUAGE: 'zh' },
    testTimeout: 20000,
    pool: 'forks',
    maxWorkers: 2,
    minWorkers: 1,
  },
});
