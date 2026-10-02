import { describe, expect, it } from 'vitest';
import type { NetworkInterfaceInfo } from 'node:os';
import { legacyHostReply } from '../src/mosaico/discovery.ts';

const hostname='coo-00000000000000000000000000000001.local';
// Observed lwIP wire layout with a fictional identity and TEST-NET addresses.
const query=Buffer.concat([Buffer.from('b5f40100000100000000000024','hex'),
  Buffer.from('coo-00000000000000000000000000000001'),Buffer.from('056c6f63616c0000010001','hex')]);
const peer={address:'192.0.2.76',port:6507};
const nic=(address:string,netmask='255.255.255.0'):NetworkInterfaceInfo=>({address,netmask,family:'IPv4',internal:false,mac:'10:20:30:40:50:60',cidr:null});
const interfaces={en0:[nic('192.0.2.250')],other:[nic('10.0.0.1')]};

describe('Mosaico legacy mDNS compatibility',()=>{
  it('answers the lwIP query layout with its ID/question and a short-lived IN/A record',()=>{
    const reply=legacyHostReply(query,peer,hostname,interfaces)!;
    expect(reply).not.toBeNull();
    expect(reply.readUInt16BE(0)).toBe(0xb5f4);
    expect(reply.readUInt16BE(2)).toBe(0x8400);
    expect(reply.readUInt16BE(4)).toBe(1);expect(reply.readUInt16BE(6)).toBe(1);
    expect(reply.subarray(12,query.length)).toEqual(query.subarray(12));
    expect(reply.readUInt16BE(query.length+4)).toBe(1);
    expect(reply.readUInt32BE(query.length+6)).toBe(10);
    expect([...reply.subarray(-4)]).toEqual([192,0,2,250]);
  });
  it('leaves full multicast clients and unrelated names to Bonjour',()=>{
    expect(legacyHostReply(query,{...peer,port:5353},hostname,interfaces)).toBeNull();
    expect(legacyHostReply(query,peer,'unrelated.local',interfaces)).toBeNull();
    expect(legacyHostReply(query,peer,hostname.toUpperCase()+'.',interfaces)).not.toBeNull();
  });
  it('does not answer off-link, loopback, or directed broadcast destinations',()=>{
    for(const address of ['8.8.8.8','127.0.0.1','192.0.2.0','192.0.2.255'])
      expect(legacyHostReply(query,{...peer,address},hostname,interfaces)).toBeNull();
  });
  it('rejects truncated, compressed, extra-section and response packets without throwing',()=>{
    for(let size=0;size<query.length;size++)expect(legacyHostReply(query.subarray(0,size),peer,hostname,interfaces)).toBeNull();
    for(const [offset,value] of [[2,0x84],[12,0xc0],[7,1],[query.length-1,3]]){
      const bad=Buffer.from(query);bad[offset]=value;
      expect(legacyHostReply(bad,peer,hostname,interfaces)).toBeNull();
    }
    expect(legacyHostReply(Buffer.concat([query,Buffer.from([0])]),peer,hostname,interfaces)).toBeNull();
  });
  it('uses the current interface address after DHCP changes',()=>{
    expect([...legacyHostReply(query,peer,hostname,{en0:[nic('192.0.2.20')]})!.subarray(-4)]).toEqual([192,0,2,20]);
  });
});
