"""End-to-end binary archive I/O tests; also runnable under Wine."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile

parser = argparse.ArgumentParser()
parser.add_argument('art')
parser.add_argument('grp')
parser.add_argument('--runner')
args = parser.parse_args()
art, grp = str(Path(args.art).resolve()), str(Path(args.grp).resolve())
prefix = [args.runner] if args.runner else []

with tempfile.TemporaryDirectory(prefix='duke-tools-') as directory:
    root = Path(directory)
    def run(tool, *arguments, succeeds=True):
        result = subprocess.run(prefix + [tool, *arguments], cwd=root,
                                stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=60)
        assert (result.returncode == 0) == succeeds, (arguments, result.stdout, result.stderr)
        return result.stdout

    pixels = bytes([0, 10, 13, 26, 128, 255])
    (root/'pixels.raw').write_bytes(pixels)
    run(art, 'create', 'tiles.art', '42', '2', '3', 'pixels.raw')
    original = (root/'tiles.art').read_bytes()
    run(art, 'create', 'tiles.art', '42', '2', '3', 'pixels.raw', succeeds=False)
    assert (root/'tiles.art').read_bytes() == original
    run(art, 'replace', 'tiles.art', '42', '4', '4', 'pixels.raw', succeeds=False)
    assert (root/'tiles.art').read_bytes() == original
    run(art, 'replace', 'tiles.art', '42', '3', '2', 'pixels.raw')
    run(art, 'append', 'tiles.art', '2', '3', 'pixels.raw')
    run(art, 'validate', 'tiles.art')
    run(art, 'extract', 'tiles.art')
    assert (root/'tile42.raw').read_bytes() == pixels
    assert (root/'tile43.raw').read_bytes() == pixels
    assert b'42 3x2' in run(art, 'list', 'tiles.art')

    (root/'data').mkdir()
    (root/'data'/'nested').mkdir()
    (root/'data'/'B.BIN').write_bytes(pixels)
    (root/'data'/'A.TXT').write_bytes(b'original\r\n\x1a')
    run(grp, 'create', 'game.grp', 'data')
    original = (root/'game.grp').read_bytes()
    run(grp, 'create', 'game.grp', 'data', succeeds=False)
    assert (root/'game.grp').read_bytes() == original
    listing = run(grp, 'list', 'game.grp')
    assert listing.index(b'A.TXT') < listing.index(b'B.BIN')
    run(grp, 'append', 'game.grp', 'pixels.raw')
    run(grp, 'replace', 'game.grp', 'A.TXT', 'pixels.raw')
    run(grp, 'validate', 'game.grp')
    run(grp, 'extract', 'game.grp')
    for name in ['A.TXT', 'B.BIN', 'pixels.raw']:
        assert (root/name).read_bytes() == pixels
    run(grp, 'get', 'game.grp', 'B.BIN')
    modified = (root/'game.grp').read_bytes()
    run(grp, 'replace', 'game.grp', 'MISSING', 'pixels.raw', succeeds=False)
    assert (root/'game.grp').read_bytes() == modified
    (root/'empty').mkdir()
    run(grp, 'create', 'empty.grp', 'empty')
    run(grp, 'validate', 'empty.grp')
    assert not list(root.glob('*.tmp.*')), 'temporary files leaked'
print('ART and GRP create/replace/append/extract and failure preservation passed')
