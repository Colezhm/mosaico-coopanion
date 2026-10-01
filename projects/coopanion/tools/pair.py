#!/usr/bin/env python3
"""Create private WSS credentials, then provision through the workspace Iris CLI.

Run after installing the application. --prepare-only creates files without USB
writes. Wi-Fi passwords are prompted without echo and never passed on a process
command line. Raw RPC uses the official CLI's base64-stdin Gateway path.
"""
from pathlib import Path
import argparse, getpass, ipaddress, json, os, re, secrets, subprocess, sys

TOKEN_NAME = 'CORTICO_MOSAICO_TOKEN'

def save_token(path, token):
    """Merge only our key; an existing deployment's other secrets stay intact."""
    prior = path.read_text() if path.exists() else ''
    pattern = r'^' + TOKEN_NAME + r'=([^\r\n]*)$'
    found = re.search(pattern, prior, re.MULTILINE)
    if found and found.group(1) != token:
        raise SystemExit('Deployment already has another Mosaico token; use its pairing directory')
    if not found:
        with path.open('a') as output:
            if prior and not prior.endswith('\n'): output.write('\n')
            output.write(TOKEN_NAME + '=' + token + '\n')
    os.chmod(path, 0o600)

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--device-id',required=True);p.add_argument('--directory',type=Path,required=True)
    p.add_argument('--address',help='Optional fixed LAN IPv4/IPv6 address; default uses paired mDNS hostname')
    p.add_argument('--port',type=int,default=19773);p.add_argument('--prepare-only',action='store_true');p.add_argument('--ssid')
    p.add_argument('--deployment-dir',type=Path,help='Existing Cortico deployment; merge the token into its .env')
    a=p.parse_args()
    if not a.device_id.isascii() or not all(c.isalnum() or c in '_-' for c in a.device_id) or len(a.device_id)>64: p.error('Invalid device ID')
    if not 1024<=a.port<=65535:p.error('Port must be 1024..65535')
    address=str(ipaddress.ip_address(a.address)) if a.address else None
    directory=a.directory.expanduser().resolve();directory.mkdir(parents=True,exist_ok=True,mode=0o700);os.chmod(directory,0o700)
    pairing=directory/'pairing.json';host='coo-'+a.device_id+'.local'
    if pairing.exists():config=json.loads(pairing.read_text())
    else:
        cert=directory/'server.pem';key=directory/'server-key.pem';openssl=directory/'openssl.cnf'
        san='DNS:'+host+',DNS:localhost,IP:127.0.0.1'+(',IP:'+address if address else '')
        openssl.write_text('[req]\ndistinguished_name=dn\nx509_extensions=ext\nprompt=no\n[dn]\nCN='+host+'\n[ext]\nsubjectAltName='+san+'\nbasicConstraints=critical,CA:TRUE\nkeyUsage=critical,digitalSignature,keyEncipherment,keyCertSign\nextendedKeyUsage=serverAuth\n')
        subprocess.run(['openssl','req','-x509','-newkey','rsa:2048','-nodes','-days','3650','-config',str(openssl),'-keyout',str(key),'-out',str(cert)],check=True,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
        os.chmod(key,0o600);config={'deviceId':a.device_id,'certFile':str(cert),'keyFile':str(key),'port':a.port,'hostname':host,'address':address}
        pairing.write_text(json.dumps(config,indent=2)+'\n');os.chmod(pairing,0o600)
    if config['deviceId']!=a.device_id:raise SystemExit('Pairing directory belongs to another device')
    if config['port']!=a.port or config.get('address')!=address:raise SystemExit('Address or port changed; use a new pairing directory so the certificate matches')
    env_file=directory/'.env'
    cached=re.search(r'^'+TOKEN_NAME+r'=([a-f0-9]{64})$',env_file.read_text() if env_file.exists() else '',re.MULTILINE)
    token=cached.group(1) if cached else config.get('token') or secrets.token_hex(32)
    save_token(env_file,token)
    if 'token' in config:
        del config['token']
        pairing.write_text(json.dumps(config,indent=2)+'\n');os.chmod(pairing,0o600)
    if a.deployment_dir:
        deployment=a.deployment_dir.expanduser().resolve()
        if not (deployment/'config.json').is_file():raise SystemExit('deployment-dir must contain an existing config.json')
        save_token(deployment/'.env',token)
        print('Cortico deployment token installed in .env (value hidden).')
    else: print('Import CORTICO_MOSAICO_TOKEN from the private .env into your deployment .env before enabling Mosaico.')
    print('Desktop pairing file:',pairing)
    if a.prepare_only:return 0
    ssid=a.ssid or input('Wi-Fi SSID: ');password=getpass.getpass('Wi-Fi password (not displayed): ')
    target=address or config['hostname'];target='['+target+']' if ':' in target else target
    payload={'ssid':ssid,'password':password,'uri':f"wss://{target}:{config['port']}/mosaico/v1",'token':token,'certificate':Path(config['certFile']).read_text()}
    root=Path(__file__).resolve().parents[3];sys.path.insert(0,str(root))
    import mosaico
    result=mosaico.main(['iris','rpc','0x434f','1','--device-id',a.device_id,'--project',str(root/'projects/coopanion'),'--payload-hex',json.dumps(payload).encode().hex(),'--json'],tool_root=mosaico.TOOLS_ROOT)
    if result==0:print('Saved. Restart Mosaico normally, then enable the extension using the pairing file above.')
    return result
if __name__=='__main__':raise SystemExit(main())
