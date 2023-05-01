//export fn get_nibble(d: []const u8, n: usize) u8 {
export fn get_nibble(d: [*]const u8, n: u32) u8 {
    var r: u8 = d[n / 2];
    if (n % 2 == 0) {
        r >>= 4;
    } else {
        r &= 0xF;
    }
    return r;
}

export fn set_nibble(d: [*]u8, n: u32, v: u8) void {
    var r: u8 = d[n / 2];
    if (n % 2 == 0) {
        r &= 0xF;
        r |= (v << 4);
    } else {
        r &= 0xF0;
        r |= (v & 0xF);
    }
    d[n / 2] = r;
}
