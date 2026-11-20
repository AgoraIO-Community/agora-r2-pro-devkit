#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Render a maintained operations manual and its matching UI images into an A4 PDF.

Install scripts/requirements-manual.txt in an isolated environment. Canonical
generation uses the committed, redistributable fonts on every host.
Screenshots live with the Markdown source and are embedded in the output PDF.
This tool never builds, deploys, or connects to a device.
"""

import argparse
from html import escape
from pathlib import Path
import re
import json
import tempfile

from reportlab import rl_config
from reportlab.lib import colors
from reportlab.lib.enums import TA_LEFT
from reportlab.lib.pagesizes import A4
from reportlab.lib.styles import ParagraphStyle
from reportlab.lib.utils import ImageReader
from reportlab.pdfbase import pdfmetrics
from reportlab.pdfbase.ttfonts import TTFont
from reportlab.platypus import (Image, PageBreak, Paragraph,
                               Preformatted, SimpleDocTemplate, Spacer,
                               Table, TableStyle)

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / 'docs/user/device-manual.zh-CN.md'
OUTPUT = ROOT / 'docs/device-operations.zh-CN.pdf'
LANGUAGES = {
    'zh-CN': {
        'source': SOURCE,
        'output': OUTPUT,
        'images': 'device-manual',
        'cover': '设备操作手册',
        'subtitle': '从源码构建到日常使用',
        'platform': 'Ubuntu 24.04  ·  AArch64 Buildroot  ·  中文操作指南',
        'workflow': '构建  →  打包  →  部署  →  配网  →  使用',
        'hardware': '适用板卡  RV1126B TEST2 RK805B V10',
        'baseline': '应用基线  {version} / {baseline}    文档日期  {date}',
        'illustrations': '内含当前 UI 渲染示意图；热点、配对码为示例。',
        'header': 'MYBOT · RV1126B 设备操作手册',
        'footer': '操作步骤 / ',
        'contents': '操作路线与目录',
        'route': ('首次安装按 01–12 节执行；已部署设备的日常操作从 09 节开始。'
                  '升级和回退见 18 节，故障排查见 20–21 节。'),
        'navigation': '点击目录可跳转；代码块保留可选取文本。每步先核对执行位置和预期结果。',
        'title': 'agora-r2-pro-devkit RV1126B 设备操作手册',
        'subject': '构建、部署、配网、绑定、使用与维护',
    },
    'en-US': {
        'source': ROOT / 'docs/user/device-manual.en.md',
        'output': ROOT / 'docs/device-operations.en.pdf',
        'images': 'device-manual-en',
        'cover': 'Device Operations Manual',
        'subtitle': 'From source build to everyday use',
        'platform': 'Ubuntu 24.04  ·  AArch64 Buildroot  ·  English edition',
        'workflow': 'Build  →  Package  →  Deploy  →  Connect  →  Talk',
        'hardware': 'Hardware  RV1126B TEST2 RK805B V10',
        'baseline': 'Application  {version} / {baseline}    Document date  {date}',
        'illustrations': 'Includes current UI renderings with sample hotspot names and pairing codes.',
        'header': 'MYBOT · RV1126B Device Operations Manual',
        'footer': 'Operating steps / ',
        'contents': 'Reading route and contents',
        'route': ('For a first installation, follow sections 01–12. Daily use starts at 09. '
                  'See 18 for updates and rollback, and 20–21 for troubleshooting.'),
        'navigation': ('Click a heading to jump to its page. Commands are selectable text. '
                       'Check where each step runs and its expected result.'),
        'title': 'agora-r2-pro-devkit RV1126B Device Operations Manual',
        'subject': 'Build, deployment, Wi-Fi setup, pairing, use and maintenance',
    },
}
INK = colors.HexColor('#152B3C')
TEAL = colors.HexColor('#087F86')
MUTED = colors.HexColor('#506676')
PALE = colors.HexColor('#EFF5F7')
LINE = colors.HexColor('#D5E3E8')
PAGE_WIDTH, PAGE_HEIGHT = A4
CONTENT_WIDTH = PAGE_WIDTH - 88
PAGE_MARKER = '<!-- page -->'


def inline(text):
    text = escape(text)
    return re.sub(r'`([^`]+)`', r'<font name="Mono">\1</font>', text)


def find_fonts(args):
    fonts = ROOT / 'docs/user/assets/fonts'
    regular = (args.font or fonts / 'MyBotManualSans.ttf', args.font_index)
    bold = (args.bold_font, args.bold_index) if args.bold_font else regular
    for name, (path, index) in (('CJK', regular), ('CJK-Bold', bold)):
        if not path.is_file():
            raise ValueError('Missing committed manual font: ' + str(path))
        pdfmetrics.registerFont(TTFont(name, str(path), subfontIndex=index))
    pdfmetrics.registerFontFamily('CJK', normal='CJK', bold='CJK-Bold',
                                 italic='CJK', boldItalic='CJK-Bold')
    mono = fonts / 'DejaVuSansMono-Bold.ttf'
    if not mono.is_file():
        raise ValueError('Missing committed monospace manual font: ' + str(mono))
    pdfmetrics.registerFont(TTFont('Mono', str(mono)))


def check_glyphs(source, language):
    text = LANGUAGES[language]
    required = set(source.read_text(encoding='utf-8') + ''.join(value for value in text.values() if isinstance(value, str)))
    available = pdfmetrics.getFont('CJK').face.charToGlyph
    missing = sorted(ord(char) for char in required if ord(char) >= 32 and ord(char) not in available)
    if missing:
        raise ValueError('Manual font lacks glyphs; run scripts/generate-manual-font.py: ' +
                         ', '.join(f'U+{point:04X}' for point in missing))


def styles(language):
    wrap = 'CJK' if language == 'zh-CN' else None
    base = dict(fontName='CJK', textColor=INK, wordWrap=wrap, allowWidows=0,
                allowOrphans=0, splitLongWords=1)
    return {
        'body': ParagraphStyle('Body', fontSize=10.2, leading=15.8, spaceAfter=9, **base),
        'heading': ParagraphStyle('Heading', fontName='CJK-Bold', fontSize=20,
                                  leading=27, textColor=INK, spaceAfter=17, keepWithNext=1),
        'caption': ParagraphStyle('Caption', fontSize=8.2, leading=12,
                                  alignment=TA_LEFT, spaceAfter=0, **base),
        'cell': ParagraphStyle('Cell', fontSize=9, leading=13.5, spaceAfter=0, **base),
        'table-head': ParagraphStyle('TableHead', fontName='CJK-Bold', fontSize=9,
                                     leading=13.5, textColor=colors.white, wordWrap=wrap),
        'code': ParagraphStyle('Code', fontName='Mono', fontSize=8,
                               leading=11.8, textColor=INK, borderPadding=9,
                               backColor=PALE, borderColor=LINE, borderWidth=0.5,
                               spaceBefore=1, spaceAfter=13),
        'toc': ParagraphStyle('TOC', fontSize=11, leading=17, spaceAfter=0, **base),
        'small': ParagraphStyle('Small', fontSize=9, leading=14, spaceAfter=8, **base),
    }


def sections(source):
    result = []
    for raw in source.read_text(encoding='utf-8').split(PAGE_MARKER):
        lines = [line for line in raw.strip().splitlines()
                 if not line.startswith('<!--')]
        while lines and not lines[0].strip():
            lines.pop(0)
        if not lines or not lines[0].startswith('# '):
            raise ValueError('Each manual section must start with a level-one heading')
        result.append((lines[0][2:].strip(), lines[1:]))
    return result


def image_grid(entries, source, sheet):
    width = (CONTENT_WIDTH - 14) / 2
    cells = []
    for caption, relative in entries:
        path = (source.parent / relative).resolve()
        if not path.is_relative_to(source.parent.resolve()) or not path.is_file():
            raise ValueError('Missing or unsafe manual image: ' + relative)
        image = Image(str(path), width=width, height=width * 3 / 4)
        cells.append([image, Spacer(1, 5), Paragraph(inline(caption), sheet['caption'])])
    rows = [cells[index:index + 2] for index in range(0, len(cells), 2)]
    for row in rows:
        if len(row) == 1:
            row.append('')
    table = Table(rows, colWidths=[width + 7, width + 7], hAlign='LEFT')
    table.setStyle(TableStyle([
        ('VALIGN', (0, 0), (-1, -1), 'TOP'),
        ('LEFTPADDING', (0, 0), (-1, -1), 0),
        ('RIGHTPADDING', (0, 0), (-1, -1), 7),
        ('TOPPADDING', (0, 0), (-1, -1), 0),
        ('BOTTOMPADDING', (0, 0), (-1, -1), 12),
    ]))
    return table


def markdown_table(lines, sheet, title):
    rows = []
    for line in lines:
        cells = [value.strip() for value in line.strip('|').split('|')]
        if all(re.fullmatch(r':?-+:?', value) for value in cells):
            continue
        rows.append(cells)
    count = len(rows[0])
    if any(len(row) != count for row in rows):
        raise ValueError('Inconsistent table columns in ' + title)
    data = [[Paragraph(inline(cell), sheet['table-head'] if index == 0 else sheet['cell'])
             for cell in row] for index, row in enumerate(rows)]
    fraction = 0.58 if title.startswith('19 ') else 0.37
    widths = ([CONTENT_WIDTH * fraction, CONTENT_WIDTH * (1 - fraction)]
              if count == 2 else [CONTENT_WIDTH / count] * count)
    table = Table(data, colWidths=widths, hAlign='LEFT', repeatRows=1)
    table.setStyle(TableStyle([
        ('BACKGROUND', (0, 0), (-1, 0), INK),
        ('ROWBACKGROUNDS', (0, 1), (-1, -1), [colors.white, PALE]),
        ('LINEBELOW', (0, 0), (-1, -1), 0.4, LINE),
        ('VALIGN', (0, 0), (-1, -1), 'TOP'),
        ('LEFTPADDING', (0, 0), (-1, -1), 9),
        ('RIGHTPADDING', (0, 0), (-1, -1), 9),
        ('TOPPADDING', (0, 0), (-1, -1), 7),
        ('BOTTOMPADDING', (0, 0), (-1, -1), 7),
    ]))
    return [table, Spacer(1, 12)]


def parse_section(title, lines, source, sheet):
    result = []
    index = 0
    while index < len(lines):
        line = lines[index].strip()
        if not line:
            index += 1
        elif line.startswith('```'):
            index += 1
            block = []
            while index < len(lines) and not lines[index].startswith('```'):
                block.append(lines[index])
                index += 1
            if index == len(lines):
                raise ValueError('Unclosed code block in ' + title)
            widest = max((pdfmetrics.stringWidth(value, 'Mono', 8) for value in block), default=0)
            if widest > CONTENT_WIDTH - 20:
                raise ValueError('Code line too wide in ' + title)
            result.append(Preformatted('\n'.join(block), sheet['code']))
            index += 1
        elif line.startswith('|'):
            block = []
            while index < len(lines) and lines[index].strip().startswith('|'):
                block.append(lines[index].strip())
                index += 1
            result.extend(markdown_table(block, sheet, title))
        elif line.startswith('!['):
            entries = []
            while index < len(lines) and lines[index].strip().startswith('!['):
                match = re.fullmatch(r'!\[([^]]*)\]\(([^)]+)\)', lines[index].strip())
                if not match:
                    raise ValueError('Invalid manual image in ' + title)
                entries.append(match.groups())
                index += 1
            result.append(image_grid(entries, source, sheet))
        else:
            block = [line]
            index += 1
            while index < len(lines) and lines[index].strip() and not re.match(
                    r'(```|\||!\[|\d+\.)', lines[index].strip()):
                block.append(lines[index].strip())
                index += 1
            result.append(Paragraph(inline(' '.join(block)), sheet['body']))
    return result


class Manual(SimpleDocTemplate):
    def __init__(self, *args, titles, **kwargs):
        self.titles = titles
        self.last_page = 0
        super().__init__(*args, **kwargs)

    def afterFlowable(self, flowable):
        self.last_page = self.page
        if isinstance(flowable, Paragraph) and flowable.style.name == 'Heading':
            text = flowable.getPlainText()
            if text in self.titles:
                section = self.titles.index(text) + 1
                key = 'step-' + str(section)
                self.canv.bookmarkPage(key)
                self.canv.addOutlineEntry(text, key, level=0)


def draw_cover(canvas, version, baseline, date, hero, text):
    canvas.setFillColor(INK)
    canvas.rect(0, 0, PAGE_WIDTH, PAGE_HEIGHT, fill=1, stroke=0)
    canvas.setFillColor(colors.HexColor('#8EDAD3'))
    canvas.setFont('CJK', 11)
    canvas.drawString(47, PAGE_HEIGHT - 61, 'MYBOT / RV1126B')
    canvas.setFillColor(colors.white)
    canvas.setFont('CJK-Bold', 32)
    canvas.drawString(44, PAGE_HEIGHT - 126, text['cover'])
    canvas.setFont('CJK', 14)
    canvas.drawString(46, PAGE_HEIGHT - 158, text['subtitle'])
    canvas.setFillColor(colors.HexColor('#B8D1DB'))
    canvas.setFont('CJK', 10)
    canvas.drawString(46, PAGE_HEIGHT - 188, text['platform'])
    image_width = 408
    image_height = image_width * 3 / 4
    image_x = (PAGE_WIDTH - image_width) / 2
    image_y = 255
    canvas.setFillColor(colors.HexColor('#263F4E'))
    canvas.roundRect(image_x - 8, image_y - 8, image_width + 16, image_height + 16,
                     12, fill=1, stroke=0)
    canvas.drawImage(ImageReader(str(hero)), image_x, image_y, image_width, image_height)
    canvas.setFillColor(colors.HexColor('#8EDAD3'))
    canvas.setFont('CJK', 12)
    canvas.drawCentredString(PAGE_WIDTH / 2, 205, text['workflow'])
    canvas.setFillColor(colors.HexColor('#B8D1DB'))
    canvas.setFont('CJK', 9.5)
    canvas.drawString(46, 116, text['hardware'])
    canvas.drawString(46, 94, text['baseline'].format(version=version, baseline=baseline, date=date))
    canvas.setFont('CJK', 8.3)
    canvas.drawString(46, 62, text['illustrations'])


def render(source, output, date, language, baseline):
    text = LANGUAGES[language]
    chapters = sections(source)
    titles = [title for title, _ in chapters]
    version = re.search(r'project\(mybot_rv1126b\s+VERSION\s+([\d.]+)',
                        (ROOT / 'CMakeLists.txt').read_text()).group(1)
    check_glyphs(source, language)
    sheet = styles(language)
    total = len(chapters) + 2
    hero = source.parent / 'assets' / text['images'] / 'ready.png'

    def page_frame(canvas, doc):
        canvas.saveState()
        if doc.page == 1:
            draw_cover(canvas, version, baseline, date, hero, text)
        else:
            canvas.setStrokeColor(LINE)
            canvas.setLineWidth(0.6)
            canvas.line(44, PAGE_HEIGHT - 40, PAGE_WIDTH - 44, PAGE_HEIGHT - 40)
            canvas.setFillColor(MUTED)
            canvas.setFont('CJK', 8.4)
            canvas.drawString(44, PAGE_HEIGHT - 30, text['header'])
            canvas.drawRightString(PAGE_WIDTH - 44, PAGE_HEIGHT - 30, version)
            canvas.line(44, 39, PAGE_WIDTH - 44, 39)
            canvas.setFont('CJK', 8)
            canvas.drawString(44, 24, text['footer'] + date)
            canvas.drawRightString(PAGE_WIDTH - 44, 24, f'{doc.page:02d} / {total:02d}')
        canvas.restoreState()

    story = [Spacer(1, 20), PageBreak()]
    story.append(Paragraph(text['contents'], sheet['heading']))
    story.append(Paragraph(text['route'], sheet['body']))
    entries = []
    for index, title in enumerate(titles):
        label = f'<link href="#step-{index + 1}" color="#152B3C">{escape(title)}</link>'
        entries.append([Paragraph(label, sheet['toc']), str(index + 3).zfill(2)])
    toc = Table(entries, colWidths=[CONTENT_WIDTH - 34, 34], rowHeights=23.8)
    toc.setStyle(TableStyle([
        ('FONTNAME', (1, 0), (1, -1), 'CJK'),
        ('FONTSIZE', (1, 0), (1, -1), 10),
        ('TEXTCOLOR', (1, 0), (1, -1), TEAL),
        ('ALIGN', (1, 0), (1, -1), 'RIGHT'),
        ('VALIGN', (0, 0), (-1, -1), 'MIDDLE'),
        ('LINEBELOW', (0, 0), (-1, -1), 0.35, LINE),
        ('LEFTPADDING', (0, 0), (-1, -1), 0),
        ('RIGHTPADDING', (0, 0), (-1, -1), 0),
        ('BOTTOMPADDING', (0, 0), (-1, -1), 5),
    ]))
    story.extend([toc, Spacer(1, 15), Paragraph(text['navigation'], sheet['small'])])
    for title, lines in chapters:
        story.append(PageBreak())
        story.append(Paragraph(escape(title), sheet['heading']))
        story.extend(parse_section(title, lines, source, sheet))
    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='mybot-manual-') as temporary:
        draft = Path(temporary) / output.name
        doc = Manual(str(draft), titles=titles, pagesize=A4, leftMargin=44,
                     rightMargin=44, topMargin=61, bottomMargin=52,
                     title=text['title'], author='agora-r2-pro-devkit',
                     subject=text['subject'], pageCompression=1)
        doc.build(story, onFirstPage=page_frame, onLaterPages=page_frame)
        if doc.last_page != total:
            raise ValueError(f'Manual layout overflow: expected {total} pages, got {doc.last_page}')
        output.write_bytes(draft.read_bytes())
    print(f'Generated {output.relative_to(ROOT) if output.is_relative_to(ROOT) else output}: '
          f'{total} pages, version {version}, baseline {baseline}')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--language', choices=LANGUAGES, default='zh-CN')
    parser.add_argument('--source', type=Path, help='default: source matching --language')
    parser.add_argument('--output', type=Path, help='default: PDF matching --language')
    parser.add_argument('--font', type=Path, help='TrueType font or TTC collection for the selected language')
    parser.add_argument('--font-index', type=int, default=0, help='TTC subfont index')
    parser.add_argument('--bold-font', type=Path, help='Optional bold TrueType/TTC')
    parser.add_argument('--bold-index', type=int, default=0)
    parser.add_argument('--date', help='document date; default: committed manual manifest')
    parser.add_argument('--baseline', help='content baseline; default: committed screenshot digest for this language')
    args = parser.parse_args()
    rl_config.invariant = 1
    try:
        manifest_path = ROOT / 'docs/user/assets/manual-manifest.json'
        manifest = json.loads(manifest_path.read_text()) if manifest_path.is_file() else {}
        date = args.date or manifest.get('document_date')
        baseline = args.baseline or ('ui-' + manifest.get('scene_sha256', {}).get(args.language, '')[:12])
        if not date or not re.fullmatch(r'\d{4}-\d{2}-\d{2}', date) or baseline == 'ui-':
            raise ValueError('Provide --date and --baseline, or refresh the committed manual manifest')
        find_fonts(args)
        language = LANGUAGES[args.language]
        render((args.source or language['source']).resolve(),
               (args.output or language['output']).resolve(), date, args.language, baseline)
    except (OSError, ValueError) as error:
        parser.error(str(error))


if __name__ == '__main__':
    main()
