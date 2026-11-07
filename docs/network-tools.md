# Network administration

`ps2-tools` lists the installed command catalogue. Use a command's `--help`
or manual page for its full options. Network servers are started explicitly;
installing the tools does not start them or generate server keys at boot.

| Work | Commands |
| --- | --- |
| Addresses, routes and links | `ip`, `ss`, `tc`, `bridge`, `ethtool`, `iw`, `wg` |
| DNS and DNSSEC | `drill`, `ldns-*`, `adig`, `ahost`, `s6-dns*`, `unbound-host` |
| Connectivity and traffic | `ping`, `fping`, `traceroute`, `mtr`, `tcpdump`, `ngrep`, `nmap`, `iperf3` |
| Transfers and remote administration | `curl`, `wget`, `ssh`, `scp`, `sftp`, `rsync`, `lftp`, `ncftp`, `tftp` |
| Sockets, firewalls and connection tracking | `nc`, `ncat`, `socat`, `nft`, `iptables`, `conntrack` |

Kernel modules are loaded when a feature needs them. Physical adapters still
need a matching driver and any firmware required by that adapter.

## Start an SSH server

After configuring the intended account and authentication, generate host keys
once and check the configuration:

```sh
ssh-keygen -A
sshd -t
sshd -D -e
```

The last command runs in the foreground; Ctrl-C stops it. Existing host keys
are preserved by `ssh-keygen -A`. The server uses `/etc/ssh/sshd_config`.

## Start a local Unbound resolver

The image's filesystem is initially owned by root. Prepare the writable
runtime state after boot, then initialize the DNSSEC root trust anchor:

```sh
install -d -m 0755 /etc/unbound
install -d -o unbound -g unbound -m 0755 /var/lib/unbound
unbound-anchor -v -a /var/lib/unbound/root.key
test -s /var/lib/unbound/root.key
chown unbound:unbound /var/lib/unbound/root.key
```

Anchor initialization uses the network. Review its verbose output and the
created key file: its exit status alone does not distinguish every error
from a successful check. This follows the upstream `unbound-anchor(8)`
interface.

Create a separate configuration so the supplied example remains available:

```sh
cat > /etc/unbound/manual.conf <<'EOF'
server:
    directory: "/etc/unbound"
    chroot: ""
    username: "unbound"
    pidfile: "/var/lib/unbound/unbound.pid"
    interface: 127.0.0.1
    access-control: 127.0.0.0/8 allow
    auto-trust-anchor-file: "/var/lib/unbound/root.key"
EOF
unbound-checkconf /etc/unbound/manual.conf
unbound -d -c /etc/unbound/manual.conf
```

This foreground instance listens on loopback. From a second shell,
`drill @127.0.0.1 example.org` queries it. Ctrl-C stops the resolver.
The writable state directory lets the `unbound` account maintain its trust
anchor after dropping root privileges. No ownership changes to the image or
boot filesystem are required.

OpenSSL 4 is the default command and SDK. The maintained OpenSSL 3 compatibility
ABI has separate providers, configuration, headers and `openssl3.pc`; selected
DNS/NTP interfaces and existing applications can use it without replacing
the default SDK. Tcl similarly provides the current interpreter alongside
the maintained `tclsh8.6` compatibility interpreter used by stable Expect.
