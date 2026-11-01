"""Generate a variadic return with an interior entry using production rules."""
from pathlib import Path
from types import SimpleNamespace
import sys

import capstone

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools/recomp"))
import translate as T


def generate(path):
    address = 0x0D02B000
    raw = bytes.fromhex("83 ec 04 52 90 5a 58 58 8d 24 94 ff e0")
    image = T.Image.__new__(T.Image)
    image.base, image.size, image.end = address, len(raw), address + len(raw)
    image.data, image.reloc_dir = raw, (0, 0)
    image.exec_ranges, image.data_ranges = [(address, image.end, ".text")], []
    image.iat_names, image.noreturn_callees = {}, set()
    image.md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    image.md.detail = True
    insns = [image.to_insn(ci) for ci in image.md.disasm(raw, address)]
    fn = T.Function(address, "variadic_return", len(raw), insns)
    fn.measure(image)
    tr = T.Translator(image, {address, address + 4}, SimpleNamespace(eager_flags=False))
    tr.prepare(fn)
    source = ['#include "x86.h"'] + tr.translate(fn, (address + 4,))
    Path(path).parent.mkdir(parents=True, exist_ok=True)
    Path(path).write_bytes(("\n".join(source) + "\n").encode())


if __name__ == "__main__":
    generate(sys.argv[1])
