/** lwIP uses legacy, one-shot mDNS: its reply must go to the query's source
 * port with the same ID and question (RFC 6762 section 6.7). bonjour-service
 * 1.3.0 only multicasts replies; supplement its existing socket for our host. */
import { createSocket, type RemoteInfo, type Socket } from 'node:dgram';
import { isIPv4 } from 'node:net';
import { networkInterfaces, type NetworkInterfaceInfo } from 'node:os';
import { performance } from 'node:perf_hooks';

const MDNS_PORT = 5353;
const LEGACY_TTL_SEC = 10;
const MAX_REPLIES_PER_SEC = 10;
type Interfaces = Record<string, NetworkInterfaceInfo[] | undefined>;
const ipv4Number = (address: string) => address.split('.').reduce((n, octet) => (n << 8) | Number(octet), 0) >>> 0;

/** A narrow parser for the single, uncompressed IN/A question lwIP sends.
 * Other records, malformed input and normal multicast queries stay with Bonjour. */
export function legacyHostReply(query: Buffer, peer: Pick<RemoteInfo, 'address' | 'port'>,
  hostname: string, interfaces: Interfaces): Buffer | null {
  if (peer.port === MDNS_PORT || peer.port < 1024 || !isIPv4(peer.address) || query.length < 17 || query.length > 512) return null;
  // Only standard queries; allow RD, which the lwIP resolver sets.
  if ((query.readUInt16BE(2) & ~0x0100) !== 0 || query.readUInt16BE(4) !== 1 || !query.subarray(6, 12).equals(Buffer.alloc(6))) return null;
  const labels: string[] = [];
  let cursor = 12;
  while (cursor < query.length && query[cursor] !== 0) {
    const length = query[cursor++];
    if (length > 63 || cursor + length >= query.length) return null;
    labels.push(query.subarray(cursor, cursor + length).toString('utf8'));
    cursor += length;
  }
  if (++cursor + 4 !== query.length || labels.join('.').toLowerCase() !== hostname.replace(/\.$/, '').toLowerCase()) return null;
  if (query.readUInt16BE(cursor) !== 1 || query.readUInt16BE(cursor + 2) !== 1) return null;
  const client = ipv4Number(peer.address);
  // Answer with the interface on the querier's link, not a VPN/other NIC's IP.
  const local = Object.values(interfaces).flat().filter((i): i is NetworkInterfaceInfo => !!i &&
    i.family === 'IPv4' && !i.internal && i.mac !== '00:00:00:00:00:00' && isIPv4(i.address) && isIPv4(i.netmask))
    .filter(i => (client & ipv4Number(i.netmask)) === (ipv4Number(i.address) & ipv4Number(i.netmask)))
    .sort((a, b) => ipv4Number(b.netmask) - ipv4Number(a.netmask))[0];
  if (!local) return null;
  const mask = ipv4Number(local.netmask), network = (ipv4Number(local.address) & mask) >>> 0;
  if (client === network || client === ((network | ~mask) >>> 0)) return null;
  const header = Buffer.alloc(12);
  query.copy(header, 0, 0, 2);
  header.writeUInt16BE(0x8400, 2); // QR + AA, no cache-flush bit in the class.
  header.writeUInt16BE(1, 4);
  header.writeUInt16BE(1, 6);
  const answer = Buffer.alloc(16);
  answer.writeUInt16BE(0xc00c, 0); // Name is the original question at offset 12.
  answer.writeUInt16BE(1, 2);
  answer.writeUInt16BE(1, 4);
  answer.writeUInt32BE(LEGACY_TTL_SEC, 6);
  answer.writeUInt16BE(4, 10);
  Buffer.from(local.address.split('.').map(Number)).copy(answer, 12);
  return Buffer.concat([header, query.subarray(12), answer]);
}

/** Bonjour binds/joins/closes this socket. Replies begin only after its probe
 * succeeds; no second listener or independent discovery lifetime is created. */
export function createDiscoverySocket(hostname: string, published: () => boolean,
  reportError: (message: string) => void): Socket {
  const socket = createSocket({ type: 'udp4', reuseAddr: true });
  let windowAt = 0, replies = 0;
  socket.on('message', (query, peer) => {
    if (!published()) return;
    const now = performance.now();
    if (now - windowAt >= 1000) { windowAt = now; replies = 0; }
    if (replies >= MAX_REPLIES_PER_SEC) return;
    const response = legacyHostReply(query, peer, hostname, networkInterfaces());
    if (!response) return;
    replies++;
    socket.send(response, peer.port, peer.address, error => { if (error) reportError(error.message); });
  });
  return socket;
}
