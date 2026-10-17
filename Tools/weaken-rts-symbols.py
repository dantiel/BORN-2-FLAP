#!/usr/bin/env python3
"""Weaken strong RTS symbols inside libborn2flap_math-ios.a.

GHC's RTS ships `mprotectForLinker` (MMap.o) and `osReserveHeapMemory`
(OSMem.o) as *strong* definitions. On iOS the app must replace them at link
time (cbits/bridge.c provides the overrides, compiled with -DB2F_IOS_RTS=1):

  * mprotectForLinker   -> no-op             (iOS W^X: there is no runtime JIT)
  * osReserveHeapMemory -> capped reservation (GHC asks for 1 TiB; iOS grants ~1 GB)

Marking only the *RTS* definitions `N_WEAK_DEF` (bit 0x0080 of n_desc) lets
ld64 prefer the app's strong overrides in bridge.o instead of failing with
duplicate symbols. The overrides themselves must remain STRONG, so each symbol
is weakened only inside the RTS member that owns its default definition.

This mirrors the approach documented by the ghc-ios toolchain
(Novavero-AI/ghc-ios): "the build weakens the RTS's own symbols in
libHaskellApp.a with llvm-objcopy, and the platform shell provides
replacements that win at link time." llvm-objcopy is not part of Xcode, so
we edit the Mach-O symbol table directly.

Usage:
  Tools/weaken-rts-symbols.py libborn2flap_math-ios.a [SYMBOL=MEMBER ...]
"""
import argparse
import os
import shutil
import struct
import subprocess
import sys
import tempfile

# Mach-O constants (mach-o/nlist.h)
N_TYPE = 0x0E        # n_type mask
N_SECT = 0x0E        # defined symbol (value == N_TYPE for a defined sym)
N_WEAK_DEF = 0x0080  # n_desc bit: "coalesced symbol is a weak definition"
LC_SYMTAB = 0x2

MH_MAGIC_64 = 0xFEEDFACF
MH_CIGAM_64 = 0xCFFAEDFE  # byte-swapped

# symbol -> RTS archive member that owns its default definition. The app's
# overrides live in cbits/bridge.c (bridge.o) and must stay STRONG.
#
# The diagnostic routing overrides (barf/errorBelch/sysErrorBelch/debugBelch
# and their va_list variants) are the third link-time override the ghc-ios
# toolchain requires: GHC's RtsMessages.o defines them as strong symbols that
# fprintf(stderr) — invisible on a phone — so bridge.o replaces them with
# os_log + stderr routing. Marking only the RTS definitions weak lets the
# bridge.o overrides win.
DEFAULT_TARGETS = {
    "_mprotectForLinker": "MMap.o",
    "_osReserveHeapMemory": "OSMem.o",
    "_barf": "RtsMessages.o",
    "_vbarf": "RtsMessages.o",
    "_errorBelch": "RtsMessages.o",
    "_verrorBelch": "RtsMessages.o",
    "_sysErrorBelch": "RtsMessages.o",
    "_vsysErrorBelch": "RtsMessages.o",
    "_debugBelch": "RtsMessages.o",
    "_vdebugBelch": "RtsMessages.o",
}


def weaken_object(data, targets):
    """Return (new_bytes, weakened_count) for a single Mach-O object.

    `targets` is a set of bytes; only those symbol names are weakened here.
    """
    if len(data) < 32:
        return data, 0
    magic = struct.unpack_from("<I", data, 0)[0]
    if magic not in (MH_MAGIC_64, MH_CIGAM_64):
        return data, 0
    data = bytearray(data)

    ncmds = struct.unpack_from("<I", data, 16)[0]
    symoff = nsyms = stroff = strsize = None
    off = 32
    for _ in range(ncmds):
        cmd, cmdsize = struct.unpack_from("<II", data, off)
        if cmd == LC_SYMTAB:
            symoff, nsyms, stroff, strsize = struct.unpack_from(
                "<IIII", data, off + 8)
        off += cmdsize

    if symoff is None or nsyms == 0:
        return data, 0

    count = 0
    for i in range(nsyms):
        entry = symoff + i * 16
        n_strx, n_type, _n_sect, n_desc = struct.unpack_from("<IBBH", data, entry)
        if (n_type & N_TYPE) != N_SECT:
            continue  # only strong defined symbols can be weakened
        # Symbol name is a NUL-terminated C string in the string table.
        end = data.index(b"\x00", stroff + n_strx)
        name = bytes(data[stroff + n_strx:end])
        if name in targets:
            n_desc |= N_WEAK_DEF
            struct.pack_into("<H", data, entry + 6, n_desc)
            count += 1
    return bytes(data), count


def archive_members(path):
    out = subprocess.run(["ar", "-t", path], capture_output=True, text=True,
                         check=True).stdout
    return [m for m in out.splitlines() if m]


def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument("archive")
    ap.add_argument("symbols", nargs="*",
                    default=[f"{s}={m}" for s, m in DEFAULT_TARGETS.items()],
                    help="SYMBOL=MEMBER pairs to weaken (default: RTS members)")
    args = ap.parse_args(argv)

    # symbol -> set of members in which to weaken it (None = any member)
    target_members = {}
    for spec in args.symbols:
        if "=" in spec:
            sym, member = spec.split("=", 1)
            member = member.encode()
        else:
            sym, member = spec, None
        target_members.setdefault(sym.encode(), set()).add(member)

    members = archive_members(args.archive)

    tmp = tempfile.mkdtemp(prefix="weaken-rts-")
    total = 0
    weakened_members = []
    try:
        subprocess.run(["ar", "-x", args.archive], cwd=tmp, check=True)
        for m in members:
            member_targets = set()
            for sym, owners in target_members.items():
                if None in owners or m.encode() in owners:
                    member_targets.add(sym)
            if not member_targets:
                continue

            path = os.path.join(tmp, m)
            if not os.path.isfile(path):
                continue
            with open(path, "rb") as f:
                data = f.read()
            new, n = weaken_object(data, member_targets)
            if n:
                with open(path, "wb") as f:
                    f.write(new)
                total += n
                weakened_members.append(m)

        if not total:
            print(f"weaken-rts-symbols: no matching strong symbols found in {args.archive}",
                  file=sys.stderr)
            return 1

        # Replace the weakened members back into the archive in place. ar -r
        # substitutes same-named members and -s rewrites the ranlib symbol
        # table, preserving the original member order.
        subprocess.run(
            ["ar", "-r", "-s", args.archive] + weakened_members,
            cwd=tmp, check=True)

        print(f"  weakened members: {', '.join(weakened_members)}")
        print(f"weaken-rts-symbols: done ({total} definition(s) marked weak)")
        return 0
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))