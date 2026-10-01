import type { FeatureContext, FrameworkFeature } from '../feature.ts';

/** App navigation only; panels and config are declared by the existing World. */
async function mount(ctx: FeatureContext): Promise<void> {
  if (!ctx.consolePageHost) return;
  const host = ctx.consolePageHost({
    root: ctx.root,
    route: (_pageId, panelId) => ['mosaico', panelId],
  });
  ctx.lifecycle.own({ dispose: () => host.unmount() });
  await host.load();
  if (!ctx.signal.aborted) await host.show('world:desktop-pet', ctx.route.segments[1] ?? 'mosaico');
}

export const mosaicoFeature: FrameworkFeature = {
  route: 'mosaico', label: 'Mosaico', icon: 'cpu', navMode: 'primary', mount,
};
