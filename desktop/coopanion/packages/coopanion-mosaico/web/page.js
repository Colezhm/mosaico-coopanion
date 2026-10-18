/**
 * Mosaico's page module for the pet page (served at /relay/page.js, loaded by pet-app.js's `useRelay`).
 * It carries out the computer's half of a transfer: the body sinks into a portal at the floor and is
 * hidden, or rises out of it, and each step is acknowledged only after a frame actually showed it.
 * It also renders the board's character atlas on request (`asset_export`, assets.js).
 *
 * The presence journal on the host is the authority; the page keeps a copy in localStorage only so a
 * reload in the middle of a transfer does not flash a second body.
 */
const NS = 'http://www.w3.org/2000/svg';
const STORE = 'mosaico-presence';
/** Departure: the portal opens, then the body sinks; arrival: the portal opens, the body rises and settles. */
const T = { open: 300, sink: 650, arriveOpen: 250, rise: 600, close: 200 };

export function install(api) {
  const { send, layer, ui, body, bounds, hold, onOrder, onFrame } = api;
  const make = (tag, attrs) => { const el = document.createElementNS(NS, tag); for (const [k, v] of Object.entries(attrs)) el.setAttribute(k, String(v)); return el; };
  const svgLayer = () => make('svg', { 'aria-hidden': 'true', style: 'position:absolute;inset:0;width:100%;height:100%;pointer-events:none;overflow:visible' });
  const backSvg = svgLayer(), frontSvg = svgLayer();
  const defs = make('defs', {});
  const gradient = make('radialGradient', { id: 'mosaicoPortalLight' });
  gradient.append(make('stop', { offset: 0, 'stop-color': '#e7fff8', 'stop-opacity': '.9' }), make('stop', { offset: '.55', 'stop-color': '#57e8c0', 'stop-opacity': '.5' }), make('stop', { offset: 1, 'stop-color': '#2fd59b', 'stop-opacity': 0 }));
  defs.append(gradient);
  backSvg.append(defs);
  const glow = make('ellipse', { fill: 'url(#mosaicoPortalLight)', opacity: 0 });
  const rim = make('path', { fill: 'none', stroke: '#adffdf', 'stroke-width': 3, opacity: 0 });
  backSvg.append(glow);
  frontSvg.append(rim);
  layer.before(backSvg);
  layer.after(frontSvg);

  let owner = 'desktop', hidden = !!api.state?.hidden, frozen = !!api.state?.held, transition = null;
  let savedX = null, savedFacing = 1, lastId = '', lastEpoch = 0, emotion = null;
  try {
    const s = JSON.parse(localStorage.getItem(STORE) || 'null');
    if (s) { hidden = hidden || !!s.hidden; lastId = s.id || ''; lastEpoch = s.epoch || 0; savedX = s.x ?? null; }
  } catch { /* a page without storage starts from what the host said */ }
  const persist = () => { try { localStorage.setItem(STORE, JSON.stringify({ hidden, id: lastId, epoch: lastEpoch, x: savedX })); } catch { /* best effort */ } };
  const ack = (t) => send({ t, id: lastId, epoch: lastEpoch, at: performance.now() });

  function visibility() {
    const v = hidden ? 'hidden' : '';
    layer.style.visibility = v;
    for (const el of ui) el.style.visibility = hidden || frozen ? 'hidden' : '';
  }
  function reset() {
    layer.style.transform = '';
    layer.style.clipPath = '';
    glow.setAttribute('opacity', 0);
    rim.setAttribute('opacity', 0);
  }
  function apply() { hold(hidden || frozen); visibility(); persist(); }

  function order(m) {
    if (m.t === 'presence') {
      owner = m.owner;
      frozen = !!m.transfer;
      if (m.transfer && owner !== 'desktop') { hidden = true; lastId = m.transfer.id; lastEpoch = m.transfer.epoch; }
      if (!m.transfer) {
        transition = null;
        hidden = owner !== 'desktop';
        reset();
      }
      apply();
      return true;
    }
    if (typeof m.t !== 'string' || !m.t.startsWith('transfer_')) return false;
    if (m.t === 'transfer_status') {
      const tr = m.transfer;
      if (tr?.from === 'desktop') send({ t: 'transfer_report', id: tr.id, epoch: tr.epoch, hidden: hidden && lastId === tr.id });
      return true;
    }
    if (m.epoch < lastEpoch) return true;
    if (m.t === 'transfer_prepare') { lastId = m.id; lastEpoch = m.epoch; ack('transfer_ready'); return true; }
    const l = body()?.layout;
    if (m.t === 'transfer_depart') {
      if (lastId === m.id && hidden) { ack('transfer_hidden'); return true; }
      if (transition?.id === m.id) return true;
      savedX = l?.x ?? savedX ?? innerWidth / 2;
      savedFacing = l?.facing ?? 1;
      hidden = false;
      transition = { id: m.id, kind: 'depart', start: performance.now(), x: savedX };
    } else if (m.t === 'transfer_arrive') {
      if (transition?.id === m.id) return true;
      const x = Math.max(60, Math.min(innerWidth - 60, savedX ?? innerWidth / 2));
      body()?.place(x, savedFacing);
      emotion = typeof m.emotion === 'string' ? m.emotion : null;
      hidden = true;
      // the host's deadlines run on its own clock; it sends this page a relative delay
      transition = { id: m.id, kind: 'arrive', start: performance.now() + Math.max(0, m.delayMs ?? 500), x };
    } else return true;
    lastId = m.id; lastEpoch = m.epoch; frozen = true;
    apply();
    return true;
  }

  function frame(now) {
    const tr = transition;
    if (!tr) return;
    const b = bounds(), S = b.S, floor = b.floorY + 2, depth = 300 * S + 24, radius = 105 * S;
    const t = now - tr.start;
    let open = 0, dy = 0;
    if (tr.kind === 'depart') {
      open = Math.min(1, Math.max(0, t / T.open));
      const u = Math.min(1, Math.max(0, (t - T.open) / T.sink));
      dy = u * u * depth;
      if (t >= T.open + T.sink) {
        hidden = true; transition = null; reset(); apply();
        requestAnimationFrame(() => ack('transfer_hidden'));
        return;
      }
    } else {
      if (t < 0) return;
      open = Math.min(1, t / T.arriveOpen);
      dy = depth;
      if (t >= T.arriveOpen) {
        hidden = false;
        const u = Math.min(1, (t - T.arriveOpen) / T.rise);
        dy = (1 - u) * depth - Math.sin(Math.PI * u) * 60 * S;
      }
      if (t >= T.arriveOpen + T.rise) open = 1 - Math.min(1, (t - T.arriveOpen - T.rise) / T.close);
      if (t >= T.arriveOpen + T.rise + T.close) {
        hidden = false; frozen = false; transition = null; reset(); apply();
        if (emotion) body()?.do(emotion);
        requestAnimationFrame(() => ack('transfer_arrived'));
        return;
      }
    }
    // the cut stays at the portal's mouth while the body moves through it
    layer.style.transform = `translate(0px, ${dy}px)`;
    layer.style.clipPath = `inset(0 0 ${Math.max(0, b.H - (floor - dy))}px 0)`;
    glow.setAttribute('cx', tr.x); glow.setAttribute('cy', floor);
    glow.setAttribute('rx', radius * open * 1.6); glow.setAttribute('ry', radius * .3 * open); glow.setAttribute('opacity', open);
    rim.setAttribute('d', `M${tr.x - radius * open} ${floor} A${radius * open || .01} ${radius * .23 * open || .01} 0 0 0 ${tr.x + radius * open} ${floor}`);
    rim.setAttribute('opacity', open);
    visibility();
  }

  function exportOrder(m) {
    if (m.t !== 'asset_export') return false;
    import('./assets.js')
      .then((a) => a.exportAtlas(m.skin))
      .then((data) => send({ t: 'asset_exported', id: m.id, data }))
      .catch((error) => send({ t: 'asset_exported', id: m.id, error: String(error?.message ?? error) }));
    return true;
  }

  onOrder((m) => exportOrder(m) || order(m));
  onFrame(frame);
  apply();
}
