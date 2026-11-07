# Shared validation for the Ethernet service and dhcpcd DNS hook.
# Configuration is parsed as data; values are never sourced or evaluated.
network_error() { printf 'network: %s\n' "$*" >&2; return 1; }

valid_ipv4() {
    local value=$1 part
    local -a octets
    [[ $value =~ ^[0-9]+\.[0-9]+\.[0-9]+\.[0-9]+$ ]] || return 1
    IFS=. read -r -a octets <<< "$value"
    for part in "${octets[@]}"; do
        [[ ${#part} -le 3 ]] && (( 10#$part <= 255 )) || return 1
    done
}

valid_cidr() {
    [[ $1 == */* ]] || return 1
    local address=${1%/*} prefix=${1##*/}
    valid_ipv4 "$address" && [[ $prefix =~ ^[0-9]{1,2}$ ]] && (( 10#$prefix <= 32 ))
}

valid_device() { [[ $1 =~ ^[a-zA-Z0-9][a-zA-Z0-9_.:-]{0,14}$ ]]; }

valid_domain() {
    local domain=${1%.} label
    local -a labels
    [[ -n $domain && ${#domain} -le 253 && $domain != *..* ]] || return 1
    IFS=. read -r -a labels <<< "$domain"
    for label in "${labels[@]}"; do
        [[ ${#label} -le 63 && $label =~ ^[a-zA-Z0-9]([a-zA-Z0-9-]*[a-zA-Z0-9])?$ ]] || return 1
    done
}

netmask_prefix() {
    local mask=$1 octet value prefix=0 zero_seen=0 bit
    valid_ipv4 "$mask" || return 1
    local -a octets
    IFS=. read -r -a octets <<< "$mask"
    for octet in "${octets[@]}"; do
        value=$((10#$octet))
        for ((bit=7; bit>=0; bit--)); do
            if (( value & (1 << bit) )); then
                (( zero_seen == 0 )) || return 1
                prefix=$((prefix + 1))
            else
                zero_seen=1
            fi
        done
    done
    printf '%s\n' "$prefix"
}

read_network_config() {
    local file=$1 line key value
    ENABLED=yes DEVICE=eth0 MODE=dhcp ADDRESS= GATEWAY= DNS=
    [[ -r $file ]] || { network_error "cannot read $file"; return 1; }
    while IFS= read -r line || [[ -n $line ]]; do
        line=${line%$'\r'}
        [[ -z $line || $line == \#* ]] && continue
        [[ $line == *=* ]] || { network_error "invalid configuration line"; return 1; }
        key=${line%%=*}; value=${line#*=}
        case $key in
            ENABLED|DEVICE|MODE|ADDRESS|GATEWAY|DNS) printf -v "$key" '%s' "$value" ;;
            *) network_error "unknown key $key"; return 1 ;;
        esac
    done < "$file"
    [[ $ENABLED == yes || $ENABLED == no ]] || { network_error "ENABLED must be yes or no"; return 1; }
    valid_device "$DEVICE" || { network_error "invalid Ethernet device"; return 1; }
    [[ $MODE == dhcp || $MODE == static ]] || { network_error "MODE must be dhcp or static"; return 1; }
    if [[ $MODE == static ]]; then
        valid_cidr "$ADDRESS" || { network_error "static ADDRESS must be IPv4/prefix"; return 1; }
        [[ -z $GATEWAY ]] || valid_ipv4 "$GATEWAY" || { network_error "invalid gateway"; return 1; }
        for value in $DNS; do
            valid_ipv4 "$value" || { network_error "invalid DNS server"; return 1; }
        done
    fi
}

write_resolver() {
    local destination=$1 server temporary
    shift
    # Atomic replacement avoids a partially written resolver during renewal.
    temporary=$(/usr/bin/mktemp "${destination}.XXXXXX") || return 1
    {
        printf '# Managed by eeLinux Ethernet configuration\n'
        [[ -z ${RESOLVER_SEARCH:-} ]] || printf 'search %s\n' "$RESOLVER_SEARCH"
        for server in "$@"; do printf 'nameserver %s\n' "$server"; done
    } > "$temporary"
    /usr/bin/chmod 0644 "$temporary"
    /usr/bin/mv -f -- "$temporary" "$destination"
}
