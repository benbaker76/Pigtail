#!/usr/bin/env python3
"""
Generate a compact MAC prefix lookup header from an input text file.

Input lines:
  AABBCC   Vendor Name With Spaces
  00:1C:B3 Apple, Inc.
  00-12-47 Samsung Electronics Co.,Ltd

Output (C++ header):
  - enum class Vendor : uint8_t
  - static const char* VendorNames[] (display strings aligned to enum order)
  - packed struct MacEntry { uint8_t prefix[3]; Vendor vendor; }
  - mac_entries_0 .. mac_entries_N arrays (sorted by prefix)
  - GetVendor(const uint8_t mac[6]) -> Vendor (binary search across chunks)
  - IsMacRandomized(const uint8_t mac[6]) -> bool (locally administered bit)
  - VendorToString(Vendor) -> const char* (returns VendorNames[...])

Key point:
  Vendor detection uses *strict* phrase/token rules (word-boundary regex) on a
  normalized manufacturer string to avoid false positives like:
    "Information Technology Limited" -> TI   (wrong)
    "Texas Digital Systems"         -> TI   (wrong)
"""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path
from typing import List, Tuple, Pattern
import re

# ----------------------------
# Config
# ----------------------------

INPUT_FILE = "mac-prefixes"
OUTPUT_FILE = "src/macprefixes.h"
CHUNK_SIZE = 250

# Enum order (also used for VendorNames indexing)
VENDOR_ENUM_ORDER = [
    "Unknown",
    "Apple",
    "Asus",
    "Broadcom",
    "Chipolo",
    "Cisco",
    "Csr",
    "DLink",
    "Espressif",
    "Google",
    "Huawei",
    "Innway",
    "Intel",
    "Intelbras",
    "Mercury",
    "Mercusys",
    "Microsoft",
    "Mikrotik",
    "Motorola",
    "Netgear",
    "RaspberryPi",
    "Qualcomm",
    "Samsung",
    "Sony",
    "Ti",
    "Tile",
    "TpLink",
    "Tracki",
    "Ubiquiti",
]

# Display strings aligned 1:1 with VENDOR_ENUM_ORDER above.
# If you add/remove enum entries, update this mapping accordingly.
VENDOR_DISPLAY_NAMES = {
    "Unknown": "Unknown",
    "Apple": "Apple",
    "Asus": "Asus",
    "Broadcom": "Broadcom",
    "Chipolo": "Chipolo",
    "Cisco": "Cisco",
    "Csr": "Cambridge Silicon Radio",
    "DLink": "D-Link",
    "Espressif": "Espressif",
    "Google": "Google",
    "Huawei": "Huawei",
    "Innway": "Innway",
    "Intel": "Intel",
    "Intelbras": "Intelbras",
    "Mercury": "Mercury",
    "Mercusys": "Mercusys",
    "Microsoft": "Microsoft",
    "Mikrotik": "Mikrotik",
    "Motorola": "Motorola",
    "Netgear": "Netgear",
    "RaspberryPi": "Raspberry Pi",
    "Qualcomm": "Qualcomm",
    "Samsung": "Samsung",
    "Sony": "Sony",
    "Ti": "Texas Instruments",
    "Tile": "Tile",
    "TpLink": "TP-Link",
    "Tracki": "Tracki",
    "Ubiquiti": "Ubiquiti",
}

# Normalize manufacturer names to: UPPERCASE words/digits separated by single spaces
_NON_ALNUM = re.compile(r"[^0-9A-Z]+")
_PREFIX_HEX = re.compile(r"^[0-9A-F]{6}$")

def normalize_manufacturer(s: str) -> str:
    s = s.upper()
    s = _NON_ALNUM.sub(" ", s).strip()
    s = " ".join(s.split())
    return s

def rx(pattern: str) -> Pattern[str]:
    # Patterns are applied to the normalized string, so hyphens/punctuation already removed.
    return re.compile(pattern)

# Strict matching rules:
# - Use \bWORD\b for token matches
# - Use explicit phrases for tricky vendors (TI, CSR)
#
# IMPORTANT: Avoid short ambiguous tokens like "\bTI\b" entirely.
VENDOR_PATTERNS: List[Tuple[str, List[Pattern[str]]]] = [
    # Most specific / phrase-based first
    ("Csr",     [rx(r"\bCAMBRIDGE SILICON RADIO\b")]),
    ("Ti",      [rx(r"\bTEXAS INSTRUMENTS\b")]),  # do NOT match plain "TEXAS" or "TI"
    ("Mikrotik",[rx(r"\bMIKROTIK\b"), rx(r"\bROUTERBOARD\b")]),
    ("TpLink",  [rx(r"\bTP LINK\b")]),
    ("DLink",   [rx(r"\bD LINK\b")]),
    ("Ubiquiti",[rx(r"\bUBIQUITI\b")]),
    ("Mercusys",[rx(r"\bMERCUSYS\b")]),
    ("Mercury", [rx(r"\bMERCURY\b")]),
    ("Intelbras",[rx(r"\bINTELBRAS\b")]),
    ("Asus",    [rx(r"\bASUSTEK\b"), rx(r"\bASUS\b")]),
    ("Huawei",  [rx(r"\bHUAWEI\b")]),
    ("Google",  [rx(r"\bGOOGLE\b")]),
    ("Apple",   [rx(r"\bAPPLE\b")]),
    ("Samsung", [rx(r"\bSAMSUNG\b")]),
    ("Microsoft",[rx(r"\bMICROSOFT\b")]),
    ("Motorola",[rx(r"\bMOTOROLA\b")]),
    ("Intel",   [rx(r"\bINTEL\b")]),
    ("Cisco",   [rx(r"\bCISCO\b")]),
    ("Broadcom",[rx(r"\bBROADCOM\b")]),
    ("Espressif",[rx(r"\bESPRESSIF\b")]),
    ("Netgear", [rx(r"\bNETGEAR\b")]),
    ("RaspberryPi", [rx(r"\bRASPBERRY PI\b")]),
    ("Sony",    [rx(r"\bSONY\b")]),
    ("Tile",    [rx(r"\bTILE\b")]),
    ("Chipolo", [rx(r"\bCHIPOLO\b")]),
    ("Tracki",  [rx(r"\bTRACKI\b")]),
    ("Innway",  [rx(r"\bINNWAY\b")]),
]

def vendor_from_manufacturer(manufacturer: str) -> str:
    m = normalize_manufacturer(manufacturer)
    for vendor, patterns in VENDOR_PATTERNS:
        for p in patterns:
            if p.search(m):
                return vendor
    return "Unknown"

def parse_prefix_to_bytes(prefix: str) -> Tuple[int, int, int]:
    p = prefix.strip().upper()
    p = p.replace(":", "").replace("-", "").replace(" ", "")
    if not _PREFIX_HEX.match(p):
        raise ValueError(f"Invalid prefix '{prefix}' -> '{p}' (need 6 hex chars)")
    return int(p[0:2], 16), int(p[2:4], 16), int(p[4:6], 16)

@dataclass(frozen=True)
class Entry:
    b0: int
    b1: int
    b2: int
    vendor: str

def read_mac_file(file_path: str) -> List[Entry]:
    entries: List[Entry] = []
    with open(file_path, "r", encoding="utf-8") as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith("#"):
                continue

            parts = line.split(maxsplit=1)
            if len(parts) < 2:
                continue

            prefix_str, manufacturer = parts[0], parts[1]

            try:
                b0, b1, b2 = parse_prefix_to_bytes(prefix_str)
            except ValueError:
                continue

            vendor = vendor_from_manufacturer(manufacturer)
            if vendor == "Unknown":
                continue

            entries.append(Entry(b0, b1, b2, vendor))

    entries.sort(key=lambda e: (e.b0, e.b1, e.b2, e.vendor))
    return entries

def chunk_entries(entries: List[Entry], chunk_size: int) -> List[List[Entry]]:
    return [entries[i : i + chunk_size] for i in range(0, len(entries), chunk_size)]

# ----------------------------
# C++ generation
# ----------------------------

def cpp_enum_vendor() -> str:
    lines = []
    lines.append("enum class Vendor : std::uint8_t {")
    for i, name in enumerate(VENDOR_ENUM_ORDER):
        comma = "," if i + 1 < len(VENDOR_ENUM_ORDER) else ""
        lines.append(f"    {name}{comma}")
    lines.append("};")
    return "\n".join(lines)

def cpp_vendor_names_array() -> str:
    # Ensure every enum has a display name
    missing = [v for v in VENDOR_ENUM_ORDER if v not in VENDOR_DISPLAY_NAMES]
    if missing:
        raise KeyError(f"Missing display names for: {missing}")

    lines = []
    lines.append("static const char *VendorNames[] {")
    for i, enum_name in enumerate(VENDOR_ENUM_ORDER):
        disp = VENDOR_DISPLAY_NAMES[enum_name].replace('"', '\\"')
        comma = "," if i + 1 < len(VENDOR_ENUM_ORDER) else ""
        lines.append(f'    "{disp}"{comma}')
    lines.append("};")
    return "\n".join(lines)

def cpp_vendor_to_string() -> str:
    return "\n".join([
        "static inline const char* VendorToString(Vendor v)",
        "{",
        "    return VendorNames[static_cast<std::size_t>(v)];",
        "}",
    ])

def cpp_header(entries: List[Entry]) -> str:
    chunks = chunk_entries(entries, CHUNK_SIZE)

    out: List[str] = []
    out.append("#pragma once")
    out.append("")
    out.append("#include <cstdint>")
    out.append("#include <cstddef>")
    out.append("")
    out.append("// Auto-generated. Do not edit by hand.")
    out.append(f"// Source: {INPUT_FILE}")
    out.append("")

    out.append(cpp_enum_vendor())
    out.append("")
    out.append(cpp_vendor_names_array())
    out.append("")

    out.append("struct MacEntry {")
    out.append("    std::uint8_t prefix[3];")
    out.append("    Vendor vendor;")
    out.append("} __attribute__((packed));")
    out.append("")

    for ci, chunk in enumerate(chunks):
        out.append(f"static const MacEntry mac_entries_{ci}[] = {{")
        for e in chunk:
            out.append(
                f"    {{ {{0x{e.b0:02X}, 0x{e.b1:02X}, 0x{e.b2:02X}}}, Vendor::{e.vendor} }},"
            )
        out.append("};")
        out.append("")

    out.append("static const MacEntry* const mac_arrays[] = {")
    for ci in range(len(chunks)):
        out.append(f"    mac_entries_{ci},")
    out.append("};")
    out.append("")
    out.append("static const std::size_t mac_array_sizes[] = {")
    for chunk in chunks:
        out.append(f"    {len(chunk)},")
    out.append("};")
    out.append("")

    out.append("static inline int ComparePrefix3(const std::uint8_t a[3], const std::uint8_t b[3])")
    out.append("{")
    out.append("    if (a[0] != b[0]) return (int)a[0] - (int)b[0];")
    out.append("    if (a[1] != b[1]) return (int)a[1] - (int)b[1];")
    out.append("    if (a[2] != b[2]) return (int)a[2] - (int)b[2];")
    out.append("    return 0;")
    out.append("}")
    out.append("")

    out.append("static inline Vendor GetVendor(const std::uint8_t macAddress[6])")
    out.append("{")
    out.append("    const std::uint8_t key[3] = { macAddress[0], macAddress[1], macAddress[2] };")
    out.append("    const std::size_t numArrays = sizeof(mac_arrays) / sizeof(mac_arrays[0]);")
    out.append("    for (std::size_t ai = 0; ai < numArrays; ++ai) {")
    out.append("        const MacEntry* entries = mac_arrays[ai];")
    out.append("        const int size = (int)mac_array_sizes[ai];")
    out.append("        int low = 0;")
    out.append("        int high = size - 1;")
    out.append("        while (low <= high) {")
    out.append("            int mid = (low + high) >> 1;")
    out.append("            int cmp = ComparePrefix3(key, entries[mid].prefix);")
    out.append("            if (cmp == 0) return entries[mid].vendor;")
    out.append("            if (cmp > 0) low = mid + 1; else high = mid - 1;")
    out.append("        }")
    out.append("    }")
    out.append("    return Vendor::Unknown;")
    out.append("}")
    out.append("")

    out.append("static inline bool IsMacRandomized(const std::uint8_t macAddress[6])")
    out.append("{")
    out.append("    // Locally administered (U/L) bit set => very likely randomized/spoofed.")
    out.append("    return (macAddress[0] & 0x02u) != 0;")
    out.append("}")
    out.append("")

    out.append(cpp_vendor_to_string())
    out.append("")
    out.append(f"// Entries: {len(entries)} in {len(chunks)} chunk(s)")
    return "\n".join(out)

def main() -> int:
    entries = read_mac_file(INPUT_FILE)

    out_path = Path(OUTPUT_FILE)
    out_path.parent.mkdir(parents=True, exist_ok=True)

    out_path.write_text(cpp_header(entries) + "\n", encoding="utf-8", newline="\n")

    print(f"Generated: {OUTPUT_FILE}")
    print(f"Entries:   {len(entries)}")
    print(f"Chunks:    {(len(entries) + CHUNK_SIZE - 1) // CHUNK_SIZE} (chunk size {CHUNK_SIZE})")
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
