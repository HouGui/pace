#!/usr/bin/env python3
"""Check project consistency using Python's standard library; not firmware tests."""
import csv
import json
import re
import sys
import xml.etree.ElementTree as ET
from pathlib import Path
from urllib.parse import unquote, urlsplit

ROOT = Path(__file__).resolve().parents[1]
errors = []


def require(condition, message):
    if not condition:
        errors.append(message)


# Check authored public artifacts only, not private notes, dependencies or builds.
documents = [*ROOT.glob('*.md'), *(ROOT / 'docs').rglob('*.md'),
             *(ROOT / 'hardware').glob('*.md')]


def anchors(path):
    """GitHub-style anchors for this project's plain Markdown headings."""
    result, counts = set(), {}
    fenced = False
    for line in path.read_text().splitlines():
        if line.startswith('```'):
            fenced = not fenced
        if fenced or not re.match(r'^#{1,6} ', line):
            continue
        heading = re.sub(r'^#+\s+', '', line).strip().lower()
        slug = re.sub(r'[^\w\- ]', '', heading).replace(' ', '-')
        count = counts.get(slug, 0)
        counts[slug] = count + 1
        result.add(f'{slug}-{count}' if count else slug)
    return result


for path in documents:
    for target in re.findall(r'!?\[[^\]]*\]\(([^)]+)\)', path.read_text()):
        if urlsplit(target).scheme:
            continue
        local, _, fragment = target.partition('#')
        linked = path.parent / unquote(local) if local else path
        require(linked.exists(), f'{path.relative_to(ROOT)}: missing {target}')
        if fragment and linked.is_file() and linked.suffix == '.md':
            require(unquote(fragment) in anchors(linked),
                    f'{path.relative_to(ROOT)}: missing anchor {target}')

for path in (ROOT / 'docs').rglob('*.json'):
    try:
        json.loads(path.read_text())
    except (ValueError, UnicodeError) as exc:
        errors.append(f'{path.relative_to(ROOT)}: {exc}')

for path in [*(ROOT / 'docs').rglob('*.svg'), *(ROOT / 'hardware').glob('*.svg')]:
    try:
        ET.parse(path)
    except ET.ParseError as exc:
        errors.append(f'{path.relative_to(ROOT)}: {exc}')

partitions = {}
end = 0x9000  # Standard partition table occupies 0x8000–0x8fff.
rows = (line for line in (ROOT / 'firmware/partitions.csv').read_text().splitlines()
        if line.strip() and not line.lstrip().startswith('#'))
for row in csv.reader(rows, skipinitialspace=True):
    name, kind, subtype, offset_text, size_text, *_ = [x.strip() for x in row]
    offset, size = int(offset_text, 0), int(size_text, 0)
    require(name not in partitions, f'duplicate partition {name}')
    require(offset >= end, f'partition overlap/order: {name}')
    require(size > 0 and size % 0x1000 == 0, f'partition size/alignment: {name}')
    require(offset % (0x10000 if kind == 'app' else 0x1000) == 0,
            f'partition start alignment: {name}')
    end = offset + size
    require(end <= 8 * 1024 * 1024, f'partition exceeds allocated 8 MiB region: {name}')
    partitions[name] = (kind, subtype, size)
require(partitions.get('ota_0') == ('app', 'ota_0', 3 * 1024 * 1024), 'missing 3 MiB ota_0')
require(partitions.get('ota_1') == ('app', 'ota_1', 3 * 1024 * 1024), 'missing 3 MiB ota_1')
require(partitions.get('otadata') == ('data', 'ota', 0x2000), 'invalid OTA metadata')
require(partitions.get('calendar') == ('data', 'nvs', 0x40000), 'invalid cache NVS')

header = (ROOT / 'firmware/components/pace_board/include/pace_board.h').read_text()
wiring = (ROOT / 'hardware/wiring.md').read_text()
pins = dict((name, int(value)) for name, value in re.findall(
    r'(PACE_(?:I2C_\w+|TOUCH_IRQ))\s*=\s*(\d+)', header))
require(pins == {'PACE_I2C_SDA': 8, 'PACE_I2C_SCL': 9, 'PACE_TOUCH_IRQ': 4},
        'reference module GPIO mapping differs')
for name, pin in pins.items():
    require(re.search(rf'\bGPIO{pin}\b', wiring), f'{name} absent from wiring document')

defaults = (ROOT / 'firmware/sdkconfig.defaults').read_text().splitlines()
for setting in ('CONFIG_ESPTOOLPY_FLASHSIZE_8MB=y', 'CONFIG_ESPTOOLPY_FLASHMODE_DIO=y',
                'CONFIG_SPIRAM_MODE_OCT=y', 'CONFIG_ESP_CONSOLE_UART_DEFAULT=y'):
    require(setting in defaults, f'LCD-7 configuration missing: {setting}')

version = (ROOT / '.esp-idf-version').read_text().strip()
workflow_paths = [ROOT / '.github/workflows' / name
                  for name in ('ci.yml', 'ci.yml.disabled')]
workflows = [path for path in workflow_paths if path.is_file()]
require(len(workflows) == 1, 'keep exactly one CI definition: ci.yml or ci.yml.disabled')
for path in workflows:
    require(f'espressif/idf:{version}' in path.read_text(),
            'CI SDK differs from .esp-idf-version')
require(version in (ROOT / 'docs/development.md').read_text(), 'build guide SDK differs')
manifest = (ROOT / 'firmware/components/pace_ui/idf_component.yml').read_text()
require(f'=={version.removeprefix("v")}' in manifest, 'component SDK constraint differs')
require('==9.4.0' in manifest, 'LVGL pin differs')
require('9.4.0' in (ROOT / 'firmware/dependencies.lock').read_text(), 'LVGL lockfile missing pin')

fixture_path = ROOT / 'docs/interfaces/calendar-feed.example.json'
try:
    payload = fixture_path.read_bytes()
    fixture = json.loads(payload)
    require(len(payload) <= 65536, 'example exceeds wire-size bound')
    require(fixture['schema_version'] == 2 and fixture['ok'] is True, 'example envelope')
    require(len(fixture['days']) == 14, 'example must contain 14 local days')
    clock = fixture['clock']
    require(1 <= len(clock) <= 32, 'clock segment count')
    require(clock[0]['start_utc'] == fixture['days'][0]['start_utc'] and
            clock[-1]['end_utc'] == fixture['days'][-1]['end_utc'], 'clock coverage')
    require(len(fixture['events']) <= 128, 'example exceeds event limit')
    categories = fixture['categories']
    category_ids = {category['id'] for category in categories}
    require(len(categories) <= 8 and len(category_ids) == len(categories),
            'example category count/uniqueness')
    for event in fixture['events']:
        ids = event.get('category_ids', [])
        require(len(ids) <= 4 and len(set(ids)) == len(ids), 'example event categories')
        require(set(ids) <= category_ids, 'example references unknown category')
    for left, right in zip(fixture['days'], fixture['days'][1:]):
        require(left['start_utc'] < left['end_utc'] == right['start_utc'],
                'example day boundaries are not contiguous')
except (KeyError, TypeError, ValueError) as exc:
    errors.append(f'example structure: {exc}')

if errors:
    print('\n'.join(f'ERROR: {message}' for message in errors), file=sys.stderr)
    sys.exit(1)
print('PASS: local links/anchors, JSON/SVG syntax, flash layout, GPIO references and SDK pin')
print('This check does not validate firmware linking, feed-parser behavior or hardware.')
