#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Derive the renamed manual font from the pinned Source Han Sans input.

Run only when manual text needs additional glyphs. Ordinary PDF generation uses
committed fonts and does not require the original font or fontTools.
"""
import argparse
import hashlib
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SOURCE_HASH = '1ee89e1669362dee13851129c0a8a791a87521eb4148e5efbf5d26596738e25b'
TEXT_INPUTS = ('docs/user/device-manual.zh-CN.md', 'docs/user/device-manual.en.md',
               'scripts/generate-device-manual.py')
OUTPUT = 'docs/user/assets/fonts/MyBotManualSans.ttf'


def generate(source, output, root=ROOT):
    import fontTools
    from fontTools import subset
    from fontTools.fontBuilder import FontBuilder
    from fontTools.pens.cu2quPen import Cu2QuPen
    from fontTools.pens.ttGlyphPen import TTGlyphPen
    from fontTools.ttLib import TTFont

    if fontTools.version != '4.60.1':
        raise ValueError('Font regeneration requires fonttools==4.60.1')
    if hashlib.sha256(source.read_bytes()).hexdigest() != SOURCE_HASH:
        raise ValueError('Source Han Sans input differs from the pinned UI font source')
    text = ''.join((root / name).read_text(encoding='utf-8') for name in TEXT_INPUTS)
    codepoints = {ord(char) for char in text if ord(char) >= 32} | set(range(32, 127))
    font = TTFont(source, recalcTimestamp=False)
    missing = codepoints - set(font.getBestCmap())
    if missing:
        raise ValueError('Source font lacks glyphs: ' + ', '.join(f'U+{c:04X}' for c in sorted(missing)))
    copyright_notice = font['name'].getDebugName(0)
    options = subset.Options()
    options.layout_features = []
    subsetter = subset.Subsetter(options=options)
    subsetter.populate(unicodes=codepoints)
    subsetter.subset(font)
    glyphset = font.getGlyphSet()
    glyphs = {}
    for name in font.getGlyphOrder():
        pen = TTGlyphPen(None)
        glyphset[name].draw(Cu2QuPen(pen, 1.0, reverse_direction=True))
        glyphs[name] = pen.glyph()
    builder = FontBuilder(font['head'].unitsPerEm, isTTF=True)
    builder.setupGlyphOrder(font.getGlyphOrder())
    builder.setupCharacterMap(font.getBestCmap())
    builder.setupGlyf(glyphs)
    builder.setupHorizontalMetrics(font['hmtx'].metrics)
    builder.setupHorizontalHeader(ascent=font['hhea'].ascent, descent=font['hhea'].descent,
                                  lineGap=font['hhea'].lineGap)
    builder.setupNameTable({
        'familyName': 'MyBot Manual Sans', 'styleName': 'Regular',
        'uniqueFontIdentifier': 'MyBot Manual Sans 1', 'fullName': 'MyBot Manual Sans',
        'psName': 'MyBotManualSans', 'version': 'Version 1.000',
        'copyright': copyright_notice, 'licenseDescription': 'SIL Open Font License 1.1',
        'licenseInfoURL': 'https://openfontlicense.org/',
    })
    os2 = font['OS/2']
    builder.setupOS2(sTypoAscender=os2.sTypoAscender, sTypoDescender=os2.sTypoDescender,
                     usWinAscent=os2.usWinAscent, usWinDescent=os2.usWinDescent,
                     usWeightClass=os2.usWeightClass, fsType=os2.fsType,
                     fsSelection=os2.fsSelection)
    builder.setupPost(keepGlyphNames=False)
    builder.setupMaxp()
    builder.setupHead(created=2082844800, modified=2082844800)
    output.parent.mkdir(parents=True, exist_ok=True)
    builder.save(output)
    font.close()
    provenance = root / 'docs/user/assets/fonts/sources.json'
    if output.resolve() == (root / OUTPUT).resolve() and provenance.is_file():
        record = json.loads(provenance.read_text())
        record['fonts'][output.name]['sha256'] = hashlib.sha256(output.read_bytes()).hexdigest()
        provenance.write_text(json.dumps(record, indent=2) + '\n')
    print(f'Generated {output.name}: {len(glyphs)} glyphs, '
          f'SHA256 {hashlib.sha256(output.read_bytes()).hexdigest()}')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, default=ROOT / 'local/fonts/SourceHanSansSC-Normal.otf')
    parser.add_argument('--output', type=Path, default=ROOT / OUTPUT)
    args = parser.parse_args()
    try:
        generate(args.source, args.output)
    except (OSError, ValueError, ImportError) as error:
        parser.error(str(error))


if __name__ == '__main__':
    main()
