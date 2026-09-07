#!/usr/bin/env python3
"""Validate the static guide; optionally exercise playback in headless Chrome."""
import argparse
import html
import json
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
SITE = ROOT / 'docs/tutorial'


def load_assignment(filename, variable):
    return json.loads((SITE / filename).read_text().split(variable + ' = ', 1)[1].rstrip(';\n'))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--browser', help='Path to Chromium or Google Chrome')
    args = parser.parse_args()
    guide = load_assignment('content.js', 'window.SAMPLOTRON_GUIDE')
    screens = load_assignment('screens.js', 'window.OLED_SCREENS')
    ids = [scene['id'] for scene in guide['scenes']]
    assert len(ids) == len(set(ids)) == 21
    assert guide['language'] == 'en'
    assert not re.search('[ąćęłńóśźżĄĆĘŁŃÓŚŹŻ]', json.dumps(guide, ensure_ascii=False))
    assets = {asset['id'] for asset in guide['assets']}
    for asset in guide['assets']:
        assert (SITE / asset['image']).is_file(), asset['image']
    for scene in guide['scenes']:
        assert scene['duration'] > 0
        for field in ('left', 'right'):
            assert not scene.get(field) or scene[field] in assets
        direction = scene['direction']
        assert 0 <= direction['readSeconds'] < scene['duration']
        for cues in (scene.get('screenCues', []), direction.get('actions', []), direction.get('cards', []), direction.get('focusCues', [])):
            times = [cue['at'] for cue in cues]
            assert times == sorted(set(times))
            for cue in cues:
                assert 0 <= cue['at'] < scene['duration']
                for key in ('screen', 'settledScreen'):
                    assert key not in cue or cue[key] in screens, (scene['id'], cue)
        for wire in scene['wires']:
            assert wire['source'] and wire['target'] and re.fullmatch(r'#[0-9a-fA-F]{6}', wire['color'])
    page = (SITE / 'index.html').read_text()
    for ref in re.findall(r'(?:src|href)="([^"]+)"', page):
        if not ref.startswith(('https:', '#')):
            assert not ref.startswith('/') and (SITE / ref).is_file(), ref
    production = '\n'.join((SITE / name).read_text() for name in ('player.js', 'renderer.js'))
    assert not re.search(r'localStorage|FileReader|createObjectURL|validateDirection|scaleSceneTiming|validPhoto|photoScene', production)
    print(f"Static checks passed: {len(ids)} steps, {len(screens)} OLED frames, {sum(s['duration'] for s in guide['scenes'])} seconds.")
    if args.browser:
        with tempfile.TemporaryDirectory(prefix='samplotron-guide-check-') as tmp:
            folder = Path(tmp)
            timing = '<script>let testFrames=new Map(),testId=0;window.requestAnimationFrame=f=>{testFrames.set(++testId,f);return testId};window.cancelAnimationFrame=id=>testFrames.delete(id);window.testTick=t=>{const frames=[...testFrames.values()];testFrames.clear();frames.forEach(f=>f(t))};</script>'
            base = f'<base href="{SITE.as_uri()}/">'
            test_script = f'<script src="{(ROOT / "tools/tutorial/browser-check.js").as_uri()}"></script>'
            harness = page.replace('<head>', '<head>' + base + timing).replace('</body>', test_script + '</body>')
            (folder / 'check.html').write_text(harness)
            result = subprocess.run([args.browser, '--headless', '--no-sandbox', '--disable-gpu', '--disable-dev-shm-usage', '--allow-file-access-from-files', '--virtual-time-budget=2000', '--window-size=1366,640', f'--user-data-dir={folder / "profile"}', '--dump-dom', (folder / 'check.html').as_uri()], capture_output=True, text=True, timeout=60)
            match = re.search(r'<pre id="test-result">(.*?)</pre>', result.stdout, re.S)
            assert match, 'Browser did not report results: ' + result.stderr[-1500:]
            report = json.loads(html.unescape(match.group(1)))
            assert not report['failures'], '\n'.join(report['failures'])
            print(f"Browser checks passed: {report['checks']} assertions.")


if __name__ == '__main__':
    main()
