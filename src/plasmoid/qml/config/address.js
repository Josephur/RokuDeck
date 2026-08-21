// SPDX-License-Identifier: GPL-3.0-or-later
.pragma library

// Mirrors the daemon's own check so the Add button only lights up for an
// address the daemon will actually accept. The daemon still validates every
// request: this exists to explain the problem before a round trip, not to
// replace the authoritative check.
function isValidIpv4(address) {
    const octets = String(address).split(".");
    if (octets.length !== 4) {
        return false;
    }
    let raw = 0;
    for (const octet of octets) {
        if (!/^(0|[1-9][0-9]{0,2})$/.test(octet)) {
            return false;
        }
        const value = Number(octet);
        if (value > 255) {
            return false;
        }
        raw = raw * 256 + value;
    }
    if (raw === 0 || raw === 4294967295) {
        return false;
    }
    const highOctet = Math.floor(raw / 16777216);
    return highOctet !== 127 && highOctet < 224;
}
