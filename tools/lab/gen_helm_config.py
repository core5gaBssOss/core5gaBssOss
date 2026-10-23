#!/usr/bin/env python3
"""Generate each NF Helm chart's config file (REVIEW-5ae0ef1 DEP-01, ADR-0513).

No chart could start its NF: the images hold only the binary, and the binaries read
/build/config/<nf>.json and /build/certs/... (ADR-0453's failure class, in Helm). Each chart now
renders a ConfigMap from deploy/helm/<nf>/files/<nf>.json, written here, never by hand, from the
compose lab config (deploy/docker/lab/config/<nf>.json, itself generated from config/ by
gen_compose_config.py). That file already names every peer and datastore by its compose service,
and each chart's Service carries the same name (`nrf`, `amf`, ...), which is also the DNS name in
the NF's lab certificate (scripts/gen-lab-pki.sh). Only `advertised_ipv4` differs: the compose lab
pins a container IP, while a pod's IP is known only at runtime.

  - upf: "auto" (resolved at startup to the address the pod reaches the NRF from, ADR-0466);
  - amf: removed; the chart sets AMF_ADVERTISED_IPV4 from the pod IP (downward API), which
    nf_config::require reads before the file, so a missing variable fails fast instead of
    registering a stale address.

`--check` fails if the generated files are not what the compose lab config produces now.
"""
import argparse
import json
import pathlib
import sys

import yaml

ROOT = pathlib.Path(__file__).resolve().parents[2]
HELM = ROOT / 'deploy/helm'
LAB = ROOT / 'deploy/docker/lab/config'
COMPOSE = ROOT / 'deploy/docker/docker-compose.yml'
# Charts that are not NFs reading a config/<nf>.json of their own.
NOT_NF = {'keycloak', 'oam-gui-bff'}
# NFs whose compose config is hand-written rather than generated (ADR-0503).
SOURCE = {'li-admf': ROOT / 'deploy/docker/li-admf.compose.json'}
# A chart running another NF's binary: it reads that NF's variables (ADR-0517; gen_compose_config's
# CONFIG_OF).
BINARY_OF = {'nwdaf-mtlf': 'nwdaf'}
ADVERTISED_AUTO = {'upf'}
# Each reads <NF>_ADVERTISED_IPV4 before the file; its chart sets it from the pod IP (ADR-0515 for
# all but the AMF; ADR-0516 for the product catalog, which builds its resource hrefs from it).
ADVERTISED_FROM_ENV = {nf: nf.upper().replace('-', '_') + '_ADVERTISED_IPV4'
                       for nf in ('amf', 'adrf', 'dccf', 'mfaf', 'nsacf', 'nwdaf', 'udsf',
                                  'product-catalog')}
# The file has the key but no source line reads it (ADR-0516): dropped, not given a pod IP.
ADVERTISED_UNREAD = {'provisioning'}


def charts():
    return sorted(p.parent.name for p in HELM.glob('*/Chart.yaml') if p.parent.name not in NOT_NF)


def compose_env(nf):
    """The compose service's <NF>_<KEY> settings (ADR-0515): compose sets some only in
    `environment:` (the UDSF's Valkey Cluster, the MFAF's Kafka brokers, the MTLF's role and
    training paths), and nf_config reads the variable before the file, so the lab config file
    alone misses them. Compose interpolations and the advertised address (a pod IP here) are left
    out."""
    env = (yaml.safe_load(COMPOSE.read_text())['services'].get(nf) or {}).get('environment') or {}
    prefix = BINARY_OF.get(nf, nf).upper().replace('-', '_') + '_'
    return {k[len(prefix):].lower(): str(v) for k, v in env.items()
            if k.startswith(prefix) and '${' not in str(v) and not k.endswith('_ADVERTISED_IPV4')}


def render(nf):
    src = SOURCE.get(nf, LAB / f'{nf}.json')
    cfg = json.loads(src.read_text())
    for key, value in compose_env(nf).items():
        # A top-level key, or <SECTION>_<KEY> for a key of a config section, as the NWDAF reads
        # training.python from NWDAF_TRAINING_PYTHON (ADR-0517).
        owner, name = (cfg, key) if key in cfg else next(
            ((cfg[s], key[len(s) + 1:]) for s in cfg if isinstance(cfg[s], dict)
             and key.startswith(s + '_') and key[len(s) + 1:] in cfg[s]), (None, None))
        if owner is not None and not isinstance(owner[name], (dict, list)):
            # Typed as the file types it: "9030" stays a number where the file has one.
            old = owner[name]
            owner[name] = type(old)(value) if not isinstance(old, bool) else value == 'true'
    if 'advertised_ipv4' in cfg:
        if nf in ADVERTISED_AUTO:
            cfg['advertised_ipv4'] = 'auto'
        elif BINARY_OF.get(nf, nf) in ADVERTISED_FROM_ENV or nf in ADVERTISED_UNREAD:
            del cfg['advertised_ipv4']
        else:
            sys.exit(f'{nf}: advertised_ipv4 needs a Kubernetes rule in gen_helm_config.py (a pod IP is not fixed)')
    text = json.dumps(cfg, indent=2) + '\n'
    if '172.30.' in text:
        sys.exit(f'{nf}: a compose lab container IP is left in the chart config')
    return text


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--check', action='store_true')
    a = ap.parse_args()
    stale = []
    for nf in charts():
        out = HELM / nf / 'files' / f'{nf}.json'
        text = render(nf)
        if a.check:
            if not out.exists() or out.read_text() != text:
                stale.append(str(out.relative_to(ROOT)))
            continue
        out.parent.mkdir(exist_ok=True)
        out.write_text(text)
        print(f'wrote {out.relative_to(ROOT)}')
    if stale:
        sys.exit('stale Helm chart configs (run `make lab-config`): ' + ', '.join(stale))
    if a.check:
        print(f'OK: {len(charts())} Helm chart configs current')


if __name__ == '__main__':
    main()
