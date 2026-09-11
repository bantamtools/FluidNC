"""Build the two release zips: the mirror tree (images + manifest) and the ELFs."""
import zipfile
from pathlib import Path


def write_images_zip(current_dir: Path, out_path: Path) -> list:
    current_dir = Path(current_dir)
    files = sorted(p for p in current_dir.rglob("*") if p.is_file())
    names = [p.relative_to(current_dir).as_posix() for p in files]
    with zipfile.ZipFile(out_path, "w", zipfile.ZIP_DEFLATED) as z:
        for p, n in zip(files, names):
            z.write(p, n)
    return names


def write_elf_zip(elf_paths: list, out_path: Path) -> list:
    paths = sorted((Path(p) for p in elf_paths), key=lambda p: p.name)
    for p in paths:
        if not p.is_file():
            raise FileNotFoundError(f"ELF not found: {p}")
    with zipfile.ZipFile(out_path, "w", zipfile.ZIP_DEFLATED) as z:
        for p in paths:
            z.write(p, p.name)
    return [p.name for p in paths]
