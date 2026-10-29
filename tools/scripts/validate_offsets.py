#!/usr/bin/env python3
"""Validate data/offsets.json structure and pattern syntax (run in CI and before commits)."""
import json
import re
import sys

PATTERN_TOKEN = re.compile(r"^([0-9A-Fa-f]{2}|\?\??)$")


def main(path: str) -> int:
    with open(path, encoding="utf-8") as f:
        d = json.load(f)
    errors = []
    sigs = d.get("signatures", {})
    for name, s in sigs.items():
        pat = s.get("pattern", "")
        toks = pat.split()
        if pat and (not all(PATTERN_TOKEN.match(t) for t in toks) or all(t.startswith("?") for t in toks)):
            errors.append(f"signature {name}: bad pattern '{pat}'")
        rip = s.get("rip")
        if rip is not None and (not isinstance(rip, dict) or "disp" not in rip or "len" not in rip):
            errors.append(f"signature {name}: rip must be {{disp,len}}")
    for name, p in d.get("pointers", {}).items():
        base = p.get("base", "")
        if base != "module" and not base.startswith(("sig:", "ptr:")):
            errors.append(f"pointer {name}: base must be module|sig:<name>|ptr:<name>")
        if base.startswith("sig:") and base[4:] not in sigs:
            errors.append(f"pointer {name}: unknown signature '{base[4:]}'")
        if base.startswith("ptr:") and base[4:] not in d.get("pointers", {}):
            errors.append(f"pointer {name}: unknown pointer '{base[4:]}'")
        if p.get("type") not in {"vec3f", "vec3d", "f32", "quat_f32", "mat3_f32", "mat4_f32", "u32", "u64"}:
            errors.append(f"pointer {name}: unknown type '{p.get('type')}'")
        if not all(isinstance(x, int) for x in p.get("chain", [])):
            errors.append(f"pointer {name}: chain must be a list of integers")
    layout = d.get("rtti", {}).get("layout", {})
    for k in ("kindOffset", "nameOffset", "memberTableOffset", "inheritanceTableOffset", "headerSize"):
        if k not in layout:
            errors.append(f"rtti.layout: missing {k}")
    pending = sum(1 for s in sigs.values() if not s.get("pattern"))
    print(f"{path}: {len(sigs)} signatures ({pending} pending recon), {len(d.get('pointers', {}))} pointers, "
          f"rtti layout verified={layout.get('verified', False)}")
    for e in errors:
        print("ERROR:", e)
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1] if len(sys.argv) > 1 else "data/offsets.json"))
