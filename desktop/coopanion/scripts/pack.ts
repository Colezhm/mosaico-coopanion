/**
 * Assembles `build/app/`, the directory electron-builder packages, then runs electron-builder.
 *
 * The workspace's pnpm layout (symlinked packages) is not what an installed app should carry, so
 * the app directory is built flat: the app code, the release notes, the staged Cortico copy, a `package.json` with
 * only the runtime dependencies installed by npm, and the three workspace packages copied into
 * `node_modules/` as real directories.
 *
 * Run: `pnpm run build:installer` (builds build/cortico first). Output: `dist/Coopanion-Setup-<version>.exe` on
 * Windows, `dist/Coopanion-<version>-mac-<arch>.dmg` and `.zip` on a Mac, `dist/Coopanion-<version>-linux-x64.AppImage`
 * and `.deb` on Linux. `PACK_ARCH` (x64 or arm64) builds for
 * another architecture than the machine's: npm installs that architecture's native packages (esbuild, sherpa-onnx) and
 * electron-builder packs that Electron, so one Apple silicon Mac builds both Mac downloads.
 *
 * `COOPANION_CHANNEL=steam` with `STEAM_APP_ID` builds for Steam instead: the app directory without an
 * installer (`dist/win-unpacked`, `dist/linux-unpacked`), which Steam installs and updates, carrying
 * steamworks.js for the Workshop (app/steam.cjs) and without the whale pack, which goes out through the Workshop.
 * The channel and app id go into the app's package.json as `coopanion` (app/main.cjs).
 */
import { cpSync, existsSync, mkdirSync, readFileSync, rmSync, writeFileSync } from 'node:fs';
import { join, relative, sep } from 'node:path';
import { spawnSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';

const ROOT = fileURLToPath(new URL('../', import.meta.url));
const OUT = join(ROOT, 'build', 'app');
const run = (cmd: string, args: string[], cwd: string) => {
  const r = spawnSync(cmd, args, { cwd, stdio: 'inherit', shell: process.platform === 'win32' });
  if (r.status !== 0) process.exit(r.status ?? 1);
};

const ARCH = process.env.PACK_ARCH || process.arch;
const STEAM = process.env.COOPANION_CHANNEL === 'steam';
const STEAM_APP_ID = Number(process.env.STEAM_APP_ID);
if (STEAM && !(Number.isInteger(STEAM_APP_ID) && STEAM_APP_ID > 0)) {
  console.error('COOPANION_CHANNEL=steam 需要 STEAM_APP_ID(Steamworks 后台的 App ID)');
  process.exit(1);
}
if (STEAM && process.platform !== 'win32' && process.platform !== 'linux') {
  console.error('Steam 版只在 Windows 和 Linux 上构建');
  process.exit(1);
}
const rootPkg = JSON.parse(readFileSync(join(ROOT, 'package.json'), 'utf8')) as Record<string, unknown> & { version: string };
const WORKSPACE = ['cortico-world-desktop-pet', 'cortico-provider-coo', 'coopanion-mosaico'];
/**
 * Runtime dependencies: Core (express, ws), the TypeScript loader, pnpm for extension installs, the updater
 * (app/updater.cjs) or for Steam steamworks.js (app/steam.cjs), and the workspace packages' own.
 */
const DEPS: Record<string, string> = {
  express: '^4.22.3', ws: '^8.21.3', tsx: '^4.23.15', pnpm: '11.5.0',
  ...STEAM ? { 'steamworks.js': '0.4.0' } : { 'electron-updater': '^6.8.9' },
};
for (const name of WORKSPACE) {
  const pkg = JSON.parse(readFileSync(join(ROOT, 'packages', name, 'package.json'), 'utf8')) as { dependencies?: Record<string, string> };
  // a workspace package another one depends on is copied in below, not fetched
  for (const [dep, range] of Object.entries(pkg.dependencies ?? {})) if (!range.startsWith('workspace:')) DEPS[dep] = range;
}

const skip = (base: string) => (src: string) => {
  const rel = relative(base, src).split(sep);
  // the top-level extensions/ of the staged Cortico is a junction to the developer's own data
  if (rel[0] === 'extensions') return false;
  // the Steam build's whale comes from the Workshop
  if (STEAM && rel[1] === 'whale' && (rel[0] === 'web' || rel[0] === 'examples')) return false;
  return !rel.some((p) => ['node_modules', '.git', 'tests', 'scratch'].includes(p));
};

rmSync(OUT, { recursive: true, force: true });
mkdirSync(OUT, { recursive: true });
cpSync(join(ROOT, 'app'), join(OUT, 'app'), { recursive: true });
cpSync(join(ROOT, 'core'), join(OUT, 'core'), { recursive: true });
// the coopanion World (core/notice.ts) tells Coo these after an update
cpSync(join(ROOT, 'docs', 'releases'), join(OUT, 'docs', 'releases'), { recursive: true });
cpSync(join(ROOT, 'build', 'cortico'), join(OUT, 'build', 'cortico'), { recursive: true, filter: skip(join(ROOT, 'build', 'cortico')) });
// the settings window (app/main.cjs serves it)
cpSync(join(ROOT, 'build', 'console'), join(OUT, 'build', 'console'), { recursive: true, filter: (src) => !src.endsWith('.map') });
writeFileSync(join(OUT, 'package.json'), JSON.stringify({
  name: rootPkg.name, productName: rootPkg.productName, version: rootPkg.version, description: rootPkg.description,
  license: rootPkg.license, author: rootPkg.author, homepage: rootPkg.homepage, type: 'module', main: 'app/main.cjs',
  dependencies: DEPS,
  coopanion: STEAM ? { channel: 'steam', steamAppId: STEAM_APP_ID } : { channel: 'github' },
}, null, 2));
// Mosaico: the runtime dependencies resolve once into installer/package-lock.json (committed); later
// packs install exactly that. Delete the file to resolve them again after a dependency change.
const runtimeLock = join(ROOT, 'installer', 'package-lock.json');
if (!existsSync(runtimeLock)) {
  run('npm', ['install', '--package-lock-only', '--ignore-scripts', '--no-audit', '--no-fund', '--loglevel=error'], OUT);
  cpSync(join(OUT, 'package-lock.json'), runtimeLock);
} else cpSync(runtimeLock, join(OUT, 'package-lock.json'));
run('npm', ['ci', '--omit=dev', '--no-audit', '--no-fund', '--loglevel=error', `--os=${process.platform}`, `--cpu=${ARCH}`], OUT);
if (STEAM) {
  // steamworks.js carries the Steam library of every platform; this build loads its own platform's
  const own = process.platform === 'win32' ? 'win64' : 'linux64';
  const dist = join(OUT, 'node_modules', 'steamworks.js', 'dist');
  for (const dir of ['win64', 'linux64', 'osx']) if (dir !== own) rmSync(join(dist, dir), { recursive: true, force: true });
}
const manifest = JSON.parse(readFileSync(join(OUT, 'package.json'), 'utf8')) as { dependencies: Record<string, string> };
for (const name of WORKSPACE) {
  const from = join(ROOT, 'packages', name);
  cpSync(from, join(OUT, 'node_modules', name), { recursive: true, filter: skip(from) });
  // listed only now: npm must not fetch them, but electron-builder ships only listed dependencies
  manifest.dependencies[name] = (JSON.parse(readFileSync(join(from, 'package.json'), 'utf8')) as { version: string }).version;
}
writeFileSync(join(OUT, 'package.json'), JSON.stringify(manifest, null, 2));
console.log(`app directory ready: ${relative(ROOT, OUT)}`);

const target = process.platform === 'darwin' ? '--mac' : process.platform === 'linux' ? '--linux' : '--win';
run(join(ROOT, 'node_modules', '.bin', process.platform === 'win32' ? 'electron-builder.cmd' : 'electron-builder'), [target, `--${ARCH}`, ...STEAM ? ['--dir'] : [], '--publish', 'never', ...process.argv.slice(2)], ROOT);

if (STEAM) {
  // electron-builder takes node_modules from the pnpm workspace's dependency graph, which has no steamworks.js and
  // has the workspace packages as they are in packages/ (the whale included): the Steam build ships build/app's
  const shipped = join(ROOT, 'dist', `${process.platform === 'win32' ? 'win' : 'linux'}-unpacked`, 'resources', 'app', 'node_modules');
  rmSync(shipped, { recursive: true, force: true });
  cpSync(join(OUT, 'node_modules'), shipped, { recursive: true });
  console.log(`node_modules of ${relative(ROOT, OUT)} copied into ${relative(ROOT, shipped)}`);
}
