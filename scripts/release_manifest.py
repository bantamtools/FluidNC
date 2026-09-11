"""Manifest fields derived from FluidNC/data/configs.json (written by pull_configs.py)."""
import json
from pathlib import Path

MANIFEST_NAME = "configs.json"


def configs_manifest_fields(data_dir: Path) -> dict:
    p = Path(data_dir) / MANIFEST_NAME
    if not p.is_file():
        raise FileNotFoundError(
            f"{p} is missing: FluidNC/data machine configs are generated from shared-data; "
            "run python3 scripts/pull_configs.py before building a release")
    m = json.loads(p.read_text())
    return {
        "shared_data_commit": m["shared_data_commit"],
        "default_config": m["default"],
        "configs": [{"key": c["key"], "file": c["file"], "sha256": c["sha256"], "meta": c.get("meta", "")} for c in m["configs"]],
    }
