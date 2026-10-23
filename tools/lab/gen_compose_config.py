#!/usr/bin/env python3
"""Generate the compose lab's config set and network override (REVIEW-5ae0ef1 DEP-02, ADR-0503).

config/<nf>.json is written for one host: every peer, datastore and the NRF is 127.0.0.1. Inside
compose, 127.0.0.1 is the container itself, so most NFs could never reach the NRF or a peer. This
writes, from config/ (the single source of truth), never by hand:

  deploy/docker/lab/config/<nf>.json  config/<nf>.json with each loopback address replaced by the
                                      compose service that listens there, and `advertised_ipv4`
                                      (the address an NF registers in the NRF) set to the
                                      container's fixed IP;
  deploy/docker/docker-compose.lab.yml  a fixed-IP network for every service, <SERVICE>_CONFIG_FILE
                                      pointing each NF at its file (nf_config.hpp), the config-set
                                      mount, and the Valkey dependency the AAnF/AUSF lacked;
                                      each Valkey client's ACL user and password file (ADR-0514).

`--check` fails if the generated files are not what config/ and docker-compose.yml produce now.
"""
import argparse
import ipaddress
import json
import pathlib
import re
import sys

import yaml

ROOT = pathlib.Path(__file__).resolve().parents[2]
COMPOSE = ROOT / 'deploy/docker/docker-compose.yml'
OUT_DIR = ROOT / 'deploy/docker/lab/config'
OVERRIDE = ROOT / 'deploy/docker/docker-compose.lab.yml'
SUBNET = ipaddress.ip_network('172.30.0.0/24')
MOUNT = '/build/deploy/docker/lab/config'

# Datastores: the host port config/ uses -> the compose service and its in-network port.
INFRA = {
    6379: ('valkey', 6379),
    5434: ('postgres-chf', 5432),
    5437: ('postgres-udr', 5432),
    5438: ('postgres-adrf', 5432),
    5439: ('postgres-li', 5432),
    9092: ('kafka-1', 9092),
    9030: ('doris', 9030),
}
# Host-only keys whose value is a bare address, not a URL.
HOST_KEYS = {
    'doris_host': 'doris', 'feature_store_host': 'doris', 'host': 'doris',
    'mdf2_host': 'li-mdf', 'mdf3_host': 'li-mdf3',
}
LOOPBACK = r'(?:127\.0\.0\.1|localhost)'
# A compose service running another NF's binary: it reads that NF's config (and env var name).
CONFIG_OF = {'nwdaf-mtlf': 'nwdaf'}


def published_ports(services):
    """Port -> service, for every service that publishes a port under the same number."""
    ports = {}
    for name, svc in services.items():
        for p in svc.get('ports', []) or []:
            m = re.match(r'^"?(\d+):(\d+)', str(p))
            if m and m.group(1) == m.group(2):
                ports.setdefault(int(m.group(1)), name)
    return ports


def port_map(services):
    ports = published_ports(services)
    for name in services:
        cfg = ROOT / f'config/{name}.json'
        if not cfg.exists():
            continue
        for m in re.finditer(r'"x1_port":\s*(\d+)', cfg.read_text()):
            ports.setdefault(int(m.group(1)), name)
    return ports


def rewrite(node, key, ports, ips, svc, unresolved):
    if isinstance(node, dict):
        return {k: rewrite(v, k, ports, ips, svc, unresolved) for k, v in node.items()}
    if isinstance(node, list):
        return [rewrite(v, key, ports, ips, svc, unresolved) for v in node]
    if not isinstance(node, str) or not re.search(LOOPBACK, node):
        return node
    if key == 'advertised_ipv4':
        return ips[svc]
    if key in ('bind_address', 'sbi_bind_address'):
        return '0.0.0.0'
    if key.endswith('_sni') or key == 'sni':
        return node  # every lab certificate carries DNS:localhost (scripts/gen-lab-pki.sh)
    if key in HOST_KEYS and re.fullmatch(LOOPBACK, node):
        return HOST_KEYS[key]

    def sub(m):
        port = int(m.group(1))
        if port in INFRA:
            host, inner = INFRA[port]
            return f'{host}:{inner}'
        if port == 7797 and 'mtlf' in key:
            return 'nwdaf-mtlf:7797'  # the MTLF and the NSACF both listen on 7797
        if port in ports:
            return f'{ports[port]}:{port}'
        unresolved.append(f'{svc}.{key} = {node}')
        return m.group(0)

    return re.sub(LOOPBACK + r':(\d+)', sub, node)


def valkey_auth(data, nf, env):
    """ADR-0514: an NF on the single-node Valkey authenticates as its own ACL user; the AAnF's
    contexts are in logical database 1, which no other user may SELECT (scripts/gen-lab-pki.sh).
    A service whose environment points it elsewhere (the UDSF: the Valkey Cluster) is left as is."""
    url = env.get(f'{nf.upper().replace("-", "_")}_REDIS_URL', data.get('redis_url'))
    if url != 'tcp://valkey:6379':
        return
    data['redis_user'] = nf
    data['redis_password_file'] = f'/build/certs/{nf}/valkey.password'
    if nf == 'aanf':
        data['redis_url'] = 'tcp://valkey:6379/1'


def generate():
    compose = yaml.safe_load(COMPOSE.read_text())
    services = compose['services']
    ports = port_map(services)
    hosts = list(SUBNET.hosts())
    ips = {name: str(hosts[i + 9]) for i, name in enumerate(sorted(services))}
    files, unresolved = {}, []
    override = {'networks': {'lab': {'ipam': {'config': [{'subnet': str(SUBNET)}]}}}, 'services': {}}
    for name in sorted(services):
        entry = {'networks': {'lab': {'ipv4_address': ips[name]}}}
        binary = CONFIG_OF.get(name, name)
        cfg = ROOT / f'config/{binary}.json'
        svc = services[name]
        mounts_config = any(str(v).startswith('../../config:') for v in svc.get('volumes', []) or [])
        # A service that already names its own compose config (LI-ADMF, OAM GUI BFF) keeps it.
        own = any(k.endswith('_CONFIG_FILE') for k in (svc.get('environment') or {}))
        if cfg.exists() and mounts_config and not own:
            data = rewrite(json.loads(cfg.read_text()), '', ports, ips, name, unresolved)
            valkey_auth(data, binary, svc.get('environment') or {})
            files[f'{name}.json'] = json.dumps(data, indent=2) + '\n'
            entry['environment'] = {f'{binary.upper()}_CONFIG_FILE': f'{MOUNT}/{name}.json'}
            entry['volumes'] = [f'./lab/config:{MOUNT}:ro']
        if name in ('aanf', 'ausf'):
            entry['depends_on'] = ['valkey']
        override['services'][name] = entry
    header = ('# Generated by tools/lab/gen_compose_config.py from config/ and docker-compose.yml --\n'
              '# do not edit; run `make lab-config`. Used by `make lab-up` (ADR-0503).\n')
    files_out = {OUT_DIR / k: v for k, v in files.items()}
    files_out[OVERRIDE] = header + yaml.safe_dump(override, sort_keys=True, width=100)
    return files_out, unresolved


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--check', action='store_true')
    args = ap.parse_args()
    files, unresolved = generate()
    for u in unresolved:
        print('unresolved loopback (left as is):', u, file=sys.stderr)
    stale = [p for p, text in files.items() if not p.exists() or p.read_text() != text]
    if args.check:
        for p in stale:
            print('stale:', p.relative_to(ROOT), file=sys.stderr)
        return 1 if stale else 0
    OUT_DIR.mkdir(parents=True, exist_ok=True)
    for p, text in files.items():
        p.write_text(text)
    print(f'wrote {len(files) - 1} configs and {OVERRIDE.relative_to(ROOT)}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
