# SPDX-FileCopyrightText: 2025-2026 Elias Bachaalany
# SPDX-License-Identifier: GPL-2.0-or-later
"""Compile the public-only key manifest; no issuer or private dependency is used."""
import argparse
import base64
import json
from pathlib import Path
import re


def unique(pairs):
    result = {}
    for name, value in pairs:
        if name in result:
            raise ValueError(f"duplicate field: {name}")
        result[name] = value
    return result


def load_keys(path):
    obj = json.loads(Path(path).read_text(encoding="utf-8"), object_pairs_hook=unique)
    if set(obj) != {"schema_version", "keys"} or type(obj["schema_version"]) is not int or obj["schema_version"] != 1 or not isinstance(obj["keys"], list):
        raise ValueError("invalid public key manifest")
    result, ids = [], set()
    for key in obj["keys"]:
        if not isinstance(key, dict) or set(key) != {"key_id", "algorithm", "purpose", "public_key_base64url"}:
            raise ValueError("public key fields must match the allowlist")
        kid = key["key_id"]
        if not isinstance(kid, str) or not re.fullmatch(r"[a-z0-9][a-z0-9-]{0,31}", kid) or kid in ids or key["algorithm"] != "ed25519" or key["purpose"] not in ("supporter", "content"):
            raise ValueError("invalid or conflicting public key")
        raw = key["public_key_base64url"]
        if not isinstance(raw, str) or not re.fullmatch(r"[A-Za-z0-9_-]+", raw):
            raise ValueError("invalid key encoding")
        value = base64.urlsafe_b64decode(raw + "=" * (-len(raw) % 4))
        if len(value) != 32 or base64.urlsafe_b64encode(value).rstrip(b"=").decode() != raw:
            raise ValueError("invalid public key length/encoding")
        ids.add(kid)
        result.append((kid, key["purpose"], value))
    return sorted(result)


def generate(manifest, output):
    rows = [f'        {{"{kid}", "{purpose}", {{{", ".join(str(b) for b in raw)}}}}},' for kid, purpose, raw in load_keys(manifest)]
    text = ('// Generated from the public manifest.\n#include "supporter/Supporter.h"\n'
            'namespace Sally::Supporter {\nconst std::vector<PublicKey>& BuiltinPublicKeys() {\n'
            '    static const std::vector<PublicKey> keys = {\n' + "\n".join(rows) + '\n    };\n    return keys;\n}\n}\n')
    path = Path(output)
    path.parent.mkdir(parents=True, exist_ok=True)
    if not path.exists() or path.read_text(encoding="utf-8") != text:
        path.write_text(text, encoding="utf-8", newline="\n")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--manifest", required=True)
    parser.add_argument("--output", required=True)
    args = parser.parse_args()
    generate(args.manifest, args.output)
