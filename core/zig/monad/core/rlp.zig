const std = @import("std");

const assert = std.debug.assert;

const asBytes = std.mem.asBytes;
const eql = std.mem.eql;
const nativeToBig = std.mem.nativeToBig;

const expect = std.testing.expect;

const USIZE_BITS = @typeInfo(usize).Int.bits;
const USIZE_SIZE = USIZE_BITS / 8;
comptime {
    assert(USIZE_BITS % 8 == 0);
}

fn len_len(n: usize) usize {
    const lead_zero = @clz(n) / 8;
    return USIZE_SIZE - lead_zero;
}

fn enc_len(n: usize, d: []u8) usize {
    // TODO generates bad assembly
    const lead_zero = @clz(n) / 8;
    const n_be = nativeToBig(usize, n);
    const s = asBytes(&n_be);
    for (s[lead_zero..], 0..) |b, i| d[i] = b;
    return USIZE_SIZE - lead_zero;
}

test "len_len" {
    try expect(len_len(0) == 0);
    try expect(len_len(1) == 1);
    try expect(len_len(1 << 7) == 1);
    try expect(len_len(1 << 8) == 2);
    try expect(len_len(1 << 63) == 8);
}

test "enc_len" {
    var d = [8]u8{ 0, 0, 0, 0, 0, 0, 0, 0 };
    try expect(enc_len(0, &d) == 0);
    try expect(eql(u8, &d, &[_]u8{ 0, 0, 0, 0, 0, 0, 0, 0 }));
    try expect(enc_len(1, &d) == 1);
    try expect(eql(u8, &d, &[_]u8{ 1, 0, 0, 0, 0, 0, 0, 0 }));
    try expect(enc_len(128, &d) == 1);
    try expect(eql(u8, &d, &[_]u8{ 128, 0, 0, 0, 0, 0, 0, 0 }));
    try expect(enc_len(257, &d) == 2);
    try expect(eql(u8, &d, &[_]u8{ 1, 1, 0, 0, 0, 0, 0, 0 }));
    try expect(enc_len(511, &d) == 2);
    try expect(eql(u8, &d, &[_]u8{ 1, 255, 0, 0, 0, 0, 0, 0 }));
    const zero: usize = 0;
    try expect(enc_len(~zero, &d) == 8);
    try expect(eql(u8, &d, &[_]u8{ 255, 255, 255, 255, 255, 255, 255, 255 }));
}

export fn rlp_enc_len(n: usize, d: [*]u8, d_n: usize) usize {
    const dl = d[0..d_n];
    return enc_len(n, dl);
}

pub fn str_len(s: []const u8) usize {
    if (s.len == 1 and s[0] <= 0x7F) {
        return 1;
    } else if (s.len <= 55) {
        return 1 + s.len;
    } else {
        comptime var usize_bits = @typeInfo(usize).Int.bits;
        const len_bits = usize_bits - @clz(s.len);
        const len_bytes = (len_bits + 7) / 8;
        return 1 + len_bytes + s.len;
    }
}

export fn rlp_string_length_c(s: [*]const u8, n: usize) usize {
    const sl = s[0..n];
    return str_len(sl);
}

pub fn rlp_list_length(s: []const u8) usize {
    if (s.len <= 55) {
        return 1 + s.len;
    } else {
        comptime var usize_bits = @typeInfo(usize).Int.bits;
        const len_bits = usize_bits - @clz(s.len);
        const len_bytes = (len_bits + 7) / 8;
        return 1 + len_bytes + s.len;
    }
}

export fn rlp_list_length_c(s: [*]const u8, n: usize) usize {
    const sl = s[0..n];
    return rlp_list_length(sl);
}

pub fn rlp_encode_string(noalias s: []const u8, noalias d: []u8) usize {
    if (s.len == 1 and s[0] <= 0x7F) {
        d[0] = s[0];
        return 1;
    } else if (s.len <= 55) {
        d[0] = 0x80 + @intCast(u8, s.len);
        for (s, 0..) |b, i| d[i + 1] = b;
        return 1 + s.len;
    } else {}
    return 0;
}

export fn rlp_encode_string_c(noalias s: [*]const u8, s_n: usize, noalias d: [*]u8, d_n: usize) usize {
    const sl = s[0..s_n];
    const dl = d[0..d_n];
    return rlp_encode_string(sl, dl);
}
