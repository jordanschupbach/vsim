#!/usr/bin/env python3
"""Embed compiler-produced SPIR-V; installed binaries do not depend on source paths."""
from pathlib import Path
import struct
import sys

output, *inputs = map(Path, sys.argv[1:])
lines = ["#pragma once", "#include <cstdint>", "namespace vsim::shaders {"]
for path in inputs:
    data = path.read_bytes()
    if len(data) < 20 or len(data) % 4 or struct.unpack_from("<I", data)[0] != 0x07230203:
        raise SystemExit(f"Invalid SPIR-V: {path}")
    words = struct.unpack(f"<{len(data)//4}I", data)
    name = path.name.replace(".", "_")
    lines.append(f"inline constexpr std::uint32_t {name}[] = {{")
    lines.extend(",".join(f"0x{word:08x}" for word in words[i:i+8]) + "," for i in range(0,len(words),8))
    lines.append("};")
lines.append("}")
output.parent.mkdir(parents=True, exist_ok=True)
output.write_text("\n".join(lines)+"\n", encoding="utf-8")
