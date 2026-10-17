/**
 * Panel bundles of the World packages bundled with the app. They are assembled as definitions,
 * so the extension loader never reads their `cortico.webUiClient`; these entries join the
 * extension asset table, which is where the WebUI server serves page bundles from.
 */
import { createHash } from 'node:crypto';
import { readFileSync } from 'node:fs';
import { join } from 'node:path';
import { fileURLToPath } from 'node:url';
import { extensionPackageFile } from 'cortico/extensions.ts';
import { parseExtensionManifest, type ExtensionWebUiAsset, type ExtensionPackageJson } from 'cortico/extensions/manifest.ts';
import { pageIdFor } from 'cortico/web/shared/webui-protocol.ts';

const NODE_MODULES = fileURLToPath(new URL('../node_modules/', import.meta.url));

export function bundledWebUiAssets(worlds: ReadonlyArray<{ id: string; packageName: string }>): ExtensionWebUiAsset[] {
  const out: ExtensionWebUiAsset[] = [];
  for (const { id, packageName } of worlds) {
    const dir = join(NODE_MODULES, packageName);
    const pkg = JSON.parse(readFileSync(join(dir, 'package.json'), 'utf8')) as ExtensionPackageJson & { version: string };
    const parsed = parseExtensionManifest(pkg);
    if (!parsed.ok || parsed.manifest.webUiClient === undefined) continue;
    // a missing file leaves the page without a bundle; the WebUI manifest names the page and the fix
    const jsFile = extensionPackageFile(dir, parsed.manifest.webUiClient);
    if (!jsFile) continue;
    const cssFile = parsed.manifest.webUiStyle === undefined ? null : extensionPackageFile(dir, parsed.manifest.webUiStyle);
    // the WebUI server lets browsers cache a bundle by its URL for a year; a rebuild changes the hash in it
    const hash = createHash('sha256').update(readFileSync(jsFile));
    if (cssFile) hash.update(readFileSync(cssFile));
    const version = `${pkg.version}-${hash.digest('hex').slice(0, 10)}`;
    out.push({ pageId: pageIdFor('world', id), packageName, version, jsFile, ...(cssFile ? { cssFile } : {}) });
  }
  return out;
}
