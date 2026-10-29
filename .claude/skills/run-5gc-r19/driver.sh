#!/usr/bin/env bash
# Drive the 5GC NFs as native processes from build/, on ports shifted by PORT_OFFSET (default
# 10000: NRF 17777) so they coexist with CI and the compose lab, which use the real ports.
# Every request goes over HTTP/2 + mTLS with a real NRF-issued OAuth2 token (ADR-0499).
#
#   driver.sh up [nf...]           start the NRF + NFs (default: nrf eir nssf bsf udm pcf smf),
#                                  wait until each NF logged its NRF registration
#   driver.sh status               pid alive? registered? per NF
#   driver.sh consumer [TYPE]      register a stand-in consumer NF (default AMF) with the NRF
#   driver.sh token SCOPE TARGET   OAuth2 token for the consumer (e.g. nnrf-disc NRF)
#   driver.sh disc TARGET_TYPE     Nnrf_NFDiscovery as the consumer (e.g. 5G_EIR)
#   driver.sh call METHOD NF PATH [JSON]
#                                  request to NF's SBI as the consumer; the token scope is PATH's
#                                  first segment, the target NF type is NF's type
#   driver.sh restart NF           kill and start one NF again (same config)
#   driver.sh logs NF [N]          last N (default 40) lines of NF's log
#   driver.sh down                 stop everything this driver started
#   driver.sh gui-build            build gui/web/dist with Node 22 in Docker (no host Node needed)
#   driver.sh gui-serve            serve gui/web/dist on 127.0.0.1:$GUI_PORT (default 18080),
#                                  static only: no BFF, so the console shows its signed-out page
#   driver.sh shot URL [NAME]      headless-Chrome screenshot -> $RUN_DIR/shots/NAME.png
#
# State: $RUN_DIR (default /tmp/5gc-run): cfg/<nf>.json, <nf>.log, <nf>.pid, consumer.env.
set -euo pipefail

ROOT=$(git -C "$(dirname "$0")" rev-parse --show-toplevel)
RUN=${RUN_DIR:-/tmp/5gc-run}
OFF=${PORT_OFFSET:-10000}
CA=$ROOT/certs/ca/ca.crt
mkdir -p "$RUN/cfg"

die() { echo "driver: $*" >&2; exit 1; }
nf_type() { case $1 in eir) echo 5G_EIR ;; *) echo "${1^^}" ;; esac; }
# The NF's SBI port after the shift, from its generated config.
nf_port() { python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["port"])' "$RUN/cfg/$1.json"; }

gen_config() { # config/<nf>.json -> $RUN/cfg/<nf>.json, SBI/protocol listener ports shifted
    python3 - "$ROOT/config/$1.json" "$RUN/cfg/$1.json" "$OFF" <<'EOF'
import json, re, sys
src, dst, off = sys.argv[1], sys.argv[2], int(sys.argv[3])
# Datastore endpoints are NEVER shifted: shifted by 10000, 5434/5437 land on CI's own Postgres
# service containers (15434..15439). postgresql:// and tcp:// (Valkey) URLs, and anything under a
# datastore-named key (data_store, redis_*, doris_*, feature_store_*, cdr_*), keep their ports.
datastore = re.compile(r'store|redis|doris|database|_db|db_|cdr|postgres|feature')
hostport = re.compile(r'^((?:https?://)?)(127\.0\.0\.1|0\.0\.0\.0|localhost):(\d+)')
def shift_hp(m):
    p = int(m.group(3))
    host = '127.0.0.1' if m.group(2) == '0.0.0.0' else m.group(2)
    return f'{m.group(1)}{host}:{p + off if 1024 < p < 10000 else p}'
def walk(v, key='', in_store=False):
    in_store = in_store or bool(datastore.search(key))
    if isinstance(v, dict):
        return {k: walk(x, k, in_store) for k, x in v.items()}
    if isinstance(v, list):
        return [walk(x, key, in_store) for x in v]
    if in_store:
        return v
    if isinstance(v, int) and not isinstance(v, bool) and (key == 'port' or key.endswith('_port')):
        return v + off if 1024 < v < 10000 else v
    if isinstance(v, str):
        return '127.0.0.1' if v == '0.0.0.0' else hostport.sub(shift_hp, v)
    return v
json.dump(walk(json.load(open(src))), open(dst, 'w'), indent=1)
EOF
}

start_nf() {
    local nf=$1 bin=$ROOT/build/nfs/$1/$1
    [ -x "$bin" ] || die "$bin not built (cmake --build build --target $nf)"
    gen_config "$nf"
    local env_name
    env_name="$(echo "$nf" | tr 'a-z-' 'A-Z_')_CONFIG_FILE"
    # setsid: the NF survives the calling shell; the wrapper writes its own pid, then execs.
    rm -f "$RUN/$nf.pid"
    setsid bash -c "echo \$\$ > '$RUN/$nf.pid'; cd '$ROOT'; exec env $env_name='$RUN/cfg/$nf.json' '$bin'" \
        > "$RUN/$nf.log" 2>&1 < /dev/null &
    # The wrapper writes the pid asynchronously; alive() before that would read "exited".
    for _ in $(seq 1 50); do [ -s "$RUN/$nf.pid" ] && return 0; sleep 0.1; done
    die "$nf: no pid file after 5 s"
}

alive() { [ -f "$RUN/$1.pid" ] && kill -0 "$(cat "$RUN/$1.pid")" 2>/dev/null; }
registered() { grep -qE 'registered with NRF \(HTTP 20[01]\)' "$RUN/$1.log" 2>/dev/null; }
started() { for f in "$RUN"/*.pid; do [ -e "$f" ] && basename "$f" .pid; done; }

wait_port() { # until something listens on 127.0.0.1:$1
    for _ in $(seq 1 50); do
        (exec 3<>"/dev/tcp/127.0.0.1/$1") 2>/dev/null && return 0
        sleep 0.2
    done
    return 1
}

cmd_up() {
    local nfs=("$@")
    [ ${#nfs[@]} -gt 0 ] || nfs=(nrf eir nssf bsf udm pcf smf)
    [[ " ${nfs[*]} " == *" nrf "* ]] || alive nrf || nfs=(nrf "${nfs[@]}")
    for nf in "${nfs[@]}"; do
        alive "$nf" && { echo "$nf already running"; continue; }
        start_nf "$nf"
        if [ "$nf" = nrf ]; then
            wait_port "$(nf_port nrf)" || die "NRF did not listen on $(nf_port nrf); see $RUN/nrf.log"
        fi
    done
    local deadline=$((SECONDS + 40))
    for nf in "${nfs[@]}"; do
        [ "$nf" = nrf ] && continue
        until registered "$nf"; do
            alive "$nf" || die "$nf exited; tail $RUN/$nf.log"
            [ $SECONDS -lt $deadline ] || die "$nf not registered after 40 s; see $RUN/$nf.log"
            sleep 0.5
        done
    done
    cmd_status
}

cmd_status() {
    for nf in $(started); do
        local state=dead reg=-
        alive "$nf" && state=up
        [ "$nf" = nrf ] || [ "$nf" = gui ] || { registered "$nf" && reg=registered || reg=NOT-registered; }
        if [ "$nf" = gui ]; then
            printf '%-8s %-5s %-15s port %s\n' gui "$state" static "${GUI_PORT:-18080}"
            continue
        fi
        printf '%-8s %-5s %-15s port %s\n' "$nf" "$state" "$reg" "$(nf_port "$nf")"
    done
}

cmd_down() {
    for nf in $(started); do
        alive "$nf" && kill "$(cat "$RUN/$nf.pid")"
        rm -f "$RUN/$nf.pid"
    done
    echo stopped
}

nrf_url() { echo "https://127.0.0.1:$(nf_port nrf)"; }

load_consumer() {
    [ -f "$RUN/consumer.env" ] || die "no consumer; run: driver.sh consumer [TYPE]"
    # shellcheck disable=SC1091
    . "$RUN/consumer.env"
}

curl_as() { # curl over HTTP/2 + mTLS with the consumer's certificate
    curl -sS --http2 --cacert "$CA" --cert "$CERT" --key "$KEY" "$@"
}

cmd_consumer() {
    local type=${1:-AMF} id cert_dir
    id=$(python3 -c 'import uuid; print(uuid.uuid4())')
    cert_dir=$ROOT/certs/$(echo "$type" | tr 'A-Z' 'a-z' | sed 's/^5g_//')
    [ -f "$cert_dir/cert.pem" ] || die "no certificate $cert_dir/cert.pem for $type"
    printf 'ID=%s\nTYPE=%s\nCERT=%s\nKEY=%s\n' "$id" "$type" "$cert_dir/cert.pem" "$cert_dir/key.pem" \
        > "$RUN/consumer.env"
    load_consumer
    local code
    code=$(curl_as -o "$RUN/consumer.put.json" -w '%{http_code}' -X PUT \
        -H 'content-type: application/json' \
        -d "{\"nfInstanceId\":\"$ID\",\"nfType\":\"$TYPE\",\"nfStatus\":\"REGISTERED\",\"ipv4Addresses\":[\"127.0.0.1\"]}" \
        "$(nrf_url)/nnrf-nfm/v1/nf-instances/$ID")
    [ "$code" = 201 ] || die "NFRegister of the consumer answered $code: $(cat "$RUN/consumer.put.json")"
    echo "consumer $TYPE registered: $ID"
}

cmd_token() { # SCOPE TARGET_NF_TYPE -> prints the access token
    load_consumer
    local resp
    resp=$(curl_as -X POST -H 'content-type: application/x-www-form-urlencoded' \
        --data-urlencode grant_type=client_credentials --data-urlencode "nfInstanceId=$ID" \
        --data-urlencode "nfType=$TYPE" --data-urlencode "targetNfType=$2" \
        --data-urlencode "scope=$1" "$(nrf_url)/oauth2/token")
    python3 -c 'import json,sys
r = json.loads(sys.argv[1])
if "access_token" not in r: sys.exit("token refused: " + sys.argv[1])
print(r["access_token"])' "$resp"
}

cmd_disc() {
    load_consumer
    local tok
    tok=$(cmd_token nnrf-disc NRF)
    curl_as -H "authorization: Bearer $tok" \
        "$(nrf_url)/nnrf-disc/v1/nf-instances?target-nf-type=$1&requester-nf-type=$TYPE"
    echo
}

cmd_call() {
    [ $# -ge 3 ] || die "usage: call METHOD NF PATH [JSON]"
    load_consumer
    local method=$1 nf=$2 path=$3 scope tok
    scope=$(echo "$path" | cut -d/ -f2)
    tok=$(cmd_token "$scope" "$(nf_type "$nf")")
    local args=(-X "$method" -H "authorization: Bearer $tok" -w '\nHTTP %{http_code}\n')
    [ $# -ge 4 ] && args+=(-H 'content-type: application/json' -d "$4")
    curl_as "${args[@]}" "https://127.0.0.1:$(nf_port "$nf")$path"
}

cmd_restart() {
    alive "$1" && kill "$(cat "$RUN/$1.pid")"
    while alive "$1"; do sleep 0.2; done
    mv -f "$RUN/$1.log" "$RUN/$1.log.prev" 2>/dev/null || true
    start_nf "$1"
    [ "$1" = nrf ] && wait_port "$(nf_port nrf)"
    echo "$1 restarted (previous log: $RUN/$1.log.prev)"
}

cmd_gui_build() {
    # The host has no Node; node:22-alpine is the image deploy/docker/oam-gui-bff.Dockerfile builds
    # with. --user keeps dist/ owned by the caller, not root.
    docker run --rm --user "$(id -u):$(id -g)" -v "$ROOT/gui/web:/w" -w /w node:22-alpine \
        sh -c 'npm ci --no-audit --no-fund >/dev/null 2>&1 && npm run build 2>&1' | tail -2
}

cmd_gui_serve() {
    local port=${GUI_PORT:-18080}
    alive gui && { echo "gui already served on $port"; return; }
    [ -f "$ROOT/gui/web/dist/index.html" ] || die "no gui/web/dist; run: driver.sh gui-build"
    setsid bash -c "echo \$\$ > '$RUN/gui.pid'; exec python3 -m http.server $port --bind 127.0.0.1 --directory '$ROOT/gui/web/dist'" \
        > "$RUN/gui.log" 2>&1 < /dev/null &
    wait_port "$port" || die "static server did not listen on $port"
    echo "gui: http://127.0.0.1:$port/"
}

cmd_shot() {
    local url=$1 name=${2:-shot}
    mkdir -p "$RUN/shots"
    # --virtual-time-budget lets the SPA's scripts and fetches settle before the capture.
    google-chrome --headless=new --no-sandbox --disable-gpu --hide-scrollbars --window-size=1280,800 \
        --virtual-time-budget=5000 --screenshot="$RUN/shots/$name.png" "$url" 2>/dev/null | tail -1
    echo "$RUN/shots/$name.png"
}

case ${1:-} in
up) shift; cmd_up "$@" ;;
status) cmd_status ;;
down) cmd_down ;;
consumer) shift; cmd_consumer "$@" ;;
token) shift; cmd_token "$@" ;;
disc) shift; cmd_disc "$@" ;;
call) shift; cmd_call "$@" ;;
restart) cmd_restart "$2" ;;
gui-build) cmd_gui_build ;;
gui-serve) cmd_gui_serve ;;
shot) shift; cmd_shot "$@" ;;
logs) tail -n "${3:-40}" "$RUN/$2.log" ;;
*) awk 'NR > 1 && /^#/ { print; next } NR > 1 { exit }' "$0"; exit 1 ;;
esac
