#!/bin/bash
# freeze-recorder — samples memory and I/O pressure, and writes a line only
# when it matters. Installed by Groundwork (GRND-0058); remove
# /usr/local/bin/freeze-recorder and freeze-recorder.service to undo.
#
# It writes to standard output, which the journal keeps, and to a plain ring
# log under /var/log that can be read after a freeze without journalctl.

set -u

INTERVAL=${INTERVAL:-10}        # seconds between samples
MEM_WARN=${MEM_WARN:-10}        # memory pressure "some" avg10 (%) worth a line
IO_WARN=${IO_WARN:-25}          # I/O pressure "some" avg10 (%)
SWAP_WARN=${SWAP_WARN:-10}      # % of swap in use
HEARTBEAT=${HEARTBEAT:-1800}    # seconds between "calm" lines
ONESHOT=${ONESHOT:-0}           # 1: sample once and exit (for testing)
RING=${RING:-/var/log/freeze-recorder.log}
RING_MAX=${RING_MAX:-20000}     # past this many lines, trim to RING_KEEP
RING_KEEP=${RING_KEEP:-10000}   # lines kept by a trim; the oldest go first
RING_TRIM_EVERY=${RING_TRIM_EVERY:-60}  # samples between trim checks
PROC=${PROC:-/proc}             # where pressure and meminfo are read (for testing)

# avg10 of the "some" or "full" line of a pressure file
psi() {
    awk -v key="$1" '$1==key {for(i=2;i<=NF;i++) if($i ~ /^avg10=/) {sub("avg10=","",$i); print $i+0; exit}}' "$2" 2>/dev/null || echo 0
}

meminfo() { awk -v k="$1:" '$1==k{print $2+0; exit}' "$PROC/meminfo"; }

# A cgroup's memory.current in MiB, or "?" if unreadable
cgmem() { local f="$1/memory.current"; [ -r "$f" ] && echo $(( $(cat "$f") / 1048576 )) || echo "?"; }

# The desktop's and the apps' memory, for each logged-in user
users_mem() {
    local d id out=""
    for d in /sys/fs/cgroup/user.slice/user-*.slice/user@*.service; do
        [ -d "$d" ] || continue
        id=${d##*user@}
        out+="user${id%.service}: desktop=$(cgmem "$d/session.slice")MB apps=$(cgmem "$d/app.slice")MB "
    done
    echo "$out"
}

last_heartbeat=0
alerting=0
tick=0

say() {
    echo "$*"
    [ -n "$RING" ] && echo "$(date '+%Y-%m-%d %H:%M:%S') $*" >> "$RING" 2>/dev/null
}

# Keep the newest RING_KEEP lines. tail to a temporary file, then mv, so a
# power cut mid-trim cannot leave the log cut short.
ring_trim() {
    [ -n "$RING" ] && [ -f "$RING" ] || return 0
    local n
    n=$(wc -l < "$RING" 2>/dev/null || echo 0)
    if [ "$n" -gt "$RING_MAX" ]; then
        tail -n "$RING_KEEP" "$RING" > "$RING.tmp" 2>/dev/null &&
            mv -f "$RING.tmp" "$RING" &&
            say "ring trimmed: dropped $(( n - RING_KEEP )) oldest lines"
    fi
}

while :; do
    now=$(date +%s)

    mem_some=$(psi some "$PROC/pressure/memory")
    mem_full=$(psi full "$PROC/pressure/memory")
    io_some=$(psi some "$PROC/pressure/io")
    cpu_some=$(psi some "$PROC/pressure/cpu")

    mem_avail=$(meminfo MemAvailable)
    swap_total=$(meminfo SwapTotal)
    swap_free=$(meminfo SwapFree)

    avail_mb=$(( mem_avail / 1024 ))
    if [ "$swap_total" -gt 0 ]; then
        swap_pct=$(( (swap_total - swap_free) * 100 / swap_total ))
        swap_used_mb=$(( (swap_total - swap_free) / 1024 ))
    else
        swap_pct=0; swap_used_mb=0
    fi

    hot=$(awk -v m="$mem_some" -v mf="$mem_full" -v io="$io_some" \
              -v mw="$MEM_WARN" -v iw="$IO_WARN" -v sp="$swap_pct" -v sw="$SWAP_WARN" \
              'BEGIN{print (m>=mw || mf>=1 || io>=iw || sp>=sw) ? 1 : 0}')

    summary="mem_psi=${mem_some}/${mem_full} io_psi=${io_some} cpu_psi=${cpu_some} avail=${avail_mb}MB swap=${swap_used_mb}MB(${swap_pct}%)"

    if [ "$hot" = "1" ]; then
        [ "$alerting" = "0" ] && say "PRESSURE RISING — $summary"
        alerting=1
        say "SAMPLE $summary $(users_mem)"
        say "TOP-RSS $(ps -eo rss=,comm= --sort=-rss 2>/dev/null | head -6 | awk '{printf "%s=%dMB ", $2, $1/1024}')"
        last_heartbeat=$now
    else
        if [ "$alerting" = "1" ]; then
            say "PRESSURE CLEARED — $summary"
            alerting=0
            last_heartbeat=$now
        elif [ $(( now - last_heartbeat )) -ge "$HEARTBEAT" ]; then
            say "calm $summary"
            last_heartbeat=$now
        fi
    fi

    tick=$(( tick + 1 ))
    [ $(( tick % RING_TRIM_EVERY )) -eq 0 ] && ring_trim

    [ "$ONESHOT" = "1" ] && break
    sleep "$INTERVAL"
done
