#!/usr/bin/env python3
"""Offline CSS geometry, native hit-testing and screenshot pixel regressions.

Set DILLO_RENDER_OUTPUT to retain screenshots, browser logs and results.json.
No public website or reference browser is needed. Pixel assertions use solid
interiors and generous color tolerances, independent of font rasterization.
"""
import functools
import http.server
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import threading
import time


def main():
    for program in ['Xvfb', 'import', 'convert', 'identify', 'xdotool']:
        if not shutil.which(program):
            print('SKIP: ' + program + ' is required for renderer screenshots')
            return 77
    bindir = Path(os.environ['DILLO_TEST_BINDIR']).resolve()
    work = Path(tempfile.mkdtemp(prefix='dillo-render-'))
    output = Path(os.environ.get('DILLO_RENDER_OUTPUT', str(work / 'artifacts')))
    output.mkdir(parents=True, exist_ok=True)
    results = []
    success = False

    class Handler(http.server.SimpleHTTPRequestHandler):
        def log_message(self, *_):
            pass

    handler = functools.partial(Handler, directory=str(Path(__file__).resolve().parent))
    server = http.server.ThreadingHTTPServer(('127.0.0.1', 0), handler)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    r, w = os.pipe()
    with (output / 'xvfb.log').open('w') as xlog, (output / 'browser.log').open('w') as log:
        xserver = subprocess.Popen(['Xvfb', '-ac', '-noreset', '-nolisten', 'tcp', '-displayfd', str(w),
                              '-screen', '0', '1024x768x24'], pass_fds=(w,), stdout=xlog, stderr=xlog)
        os.close(w)
        with os.fdopen(r) as ready:
            display = ready.readline().strip()
        env = os.environ.copy()
        env['DISPLAY'] = ':' + display
        env['LSAN_OPTIONS'] = 'suppressions=' + str(Path(__file__).with_name('lsan.supp').resolve())
        # Use an isolated user configuration; do not alter the developer's
        # preferences, cookies, storage, or running browser's IPC endpoint.
        env['HOME'] = str(work / 'home')
        config = Path(env['HOME']) / '.dillo'
        config.mkdir(parents=True)
        (config / 'dillorc').write_text('javascript_enabled=YES\nload_images=YES\nload_background_images=YES\nbg_color=#ffffff\n')
        url = 'http://127.0.0.1:' + str(server.server_port) + '/renderer.html'
        browser = subprocess.Popen([str(bindir / 'dillo'), '-g', '950x700+0+0', url], env=env,
                                   stdout=log, stderr=log)
        env['DILLO_PID'] = str(browser.pid)

        def command(*args):
            result = subprocess.run([str(bindir / 'dilloc'), *args], env=env, capture_output=True,
                                    text=True, timeout=8)
            assert result.returncode == 0, (args, result.stdout, result.stderr)
            return result.stdout.strip()

        def evaluate(expression):
            return json.loads(command('eval', 'JSON.stringify(' + expression + ')'))

        def rect(id):
            return evaluate('document.getElementById(' + json.dumps(id) + ').getBoundingClientRect()')

        def check(ok, name, detail=None):
            results.append({'test': name, 'status': 'pass' if ok else 'fail', 'detail': detail})
            assert ok, name + (': ' + str(detail) if detail is not None else '')

        def screenshot(name):
            command('wait', '5')
            file = output / (name + '.png')
            subprocess.run(['import', '-window', win, str(file)], env=env, check=True, timeout=8)
            dimensions = subprocess.check_output(['identify', '-format', '%w %h', str(file)], text=True).split()
            width, height = map(int, dimensions)
            data = subprocess.check_output(['convert', str(file), '-depth', '8', 'rgb:-'])
            return width, height, data

        def pixel(picture, x, y):
            width, height, data = picture
            x, y = int(x), int(y)
            assert 0 <= x < width and 0 <= y < height, (x, y, width, height)
            offset = 3 * (y * width + x)
            return tuple(data[offset:offset + 3])

        def color(picture, id, x, y, expected, name, tolerance=3):
            box = rect(id)
            actual = pixel(picture, origin[0] + box['left'] + x, origin[1] + box['top'] + y)
            check(max(abs(a-b) for a, b in zip(actual, expected)) <= tolerance, name, actual)

        try:
            deadline = time.monotonic() + 15
            while time.monotonic() < deadline:
                try:
                    if command('title') == 'Renderer ready':
                        break
                except AssertionError:
                    pass
                if browser.poll() is not None:
                    raise AssertionError('browser exited during startup')
                time.sleep(.05)
            else:
                raise AssertionError('renderer fixture did not load')
            win = subprocess.check_output(['xdotool', 'search', '--pid', str(browser.pid),
                                           '--name', 'Renderer ready'], env=env, text=True).splitlines()[0]
            picture = screenshot('initial')
            (output / 'initial-geometry.json').write_text(command('eval', 'JSON.stringify(Array.from(document.querySelectorAll("[id]")).map(e=>[e.id,e.getBoundingClientRect(),getComputedStyle(e).cssText]))'))
            width, _, data = picture
            marker = bytes((0x12, 0xa0, 0xb0))
            marker_pixel = next((n for n in range(0, len(data), 3) if data[n:n+3] == marker), None)
            check(marker_pixel is not None, 'viewport calibration marker is visible')
            origin = ((marker_pixel // 3) % width, (marker_pixel // 3) // width)

            box = rect('translated')
            check((box['left'], box['top'], box['width'], box['height']) == (80, 170, 80, 40),
                  'percentage translate uses its border box', box)
            box = rect('chain')
            check((box['left'], box['top']) == (250, 175), 'translation lists and calc compose', box)
            color(picture, 'visible-leaf', 40, 10, (224,32,48), 'visible overflow survives small ancestors')
            color(picture, 'hidden-leaf', 5, 10, (224,32,48), 'hidden overflow keeps its interior')
            color(picture, 'hidden-leaf', 40, 10, (255,255,255), 'hidden overflow clips at padding box')
            color(picture, 'escape-leaf', 40, 10, (224,32,48), 'absolute box escapes intermediate overflow')
            color(picture, 'horizontal', 1, 25, (252,0,3), 'horizontal gradient begins red', 5)
            color(picture, 'horizontal', 118, 25, (3,0,252), 'horizontal gradient ends blue', 5)
            color(picture, 'vertical', 60, 1, (247,0,8), 'default gradient points down', 6)
            color(picture, 'repeat', 5, 20, (255,0,0), 'repeating gradient first solid stop')
            color(picture, 'repeat', 15, 20, (0,0,255), 'repeating gradient second solid stop')
            color(picture, 'repeat', 25, 20, (255,0,0), 'repeating gradient repeats')
            color(picture, 'alpha', 118, 25, (35,161,66), 'alpha gradient composites without black fringe', 5)
            for id in ['round','border','ellipse','oversized','gradient-round','cascade','image-round']:
                color(picture, id, 0, 0, (255,255,255), id + ' has a transparent rounded corner')
            color(picture, 'round', 40, 30, (224,32,48), 'rounded background interior')
            color(picture, 'border', 40, 1, (16,32,48), 'rounded solid border')
            color(picture, 'border', 40, 30, (224,32,48), 'border leaves background visible')
            color(picture, 'asymmetric', 0, 0, (224,32,48), 'asymmetric square corner')
            color(picture, 'asymmetric', 79, 0, (255,255,255), 'asymmetric elliptical corner')
            color(picture, 'image-round', 40, 30, (224,32,48), 'rounded background image interior')
            box = rect('textclip')
            below = [pixel(picture, origin[0]+box['left']+x, origin[1]+box['top']+y)
                     for y in range(21, 39) for x in range(1, 89)]
            check(all(p == (255,255,255) for p in below), 'text painting obeys overflow clipping')
            color(picture, 'query', 150, 0, (78,110,242), 'native input does not cover CSS border')
            color(picture, 'search-button', 60, 39, (63,102,247), 'native button keeps gradient background', 5)
            color(picture, 'search-button', 119, 0, (255,255,255), 'native button keeps rounded corner')
            box = rect('search-button')
            white_text = [(x,y) for y in range(5,35) for x in range(5,115)
                          if min(pixel(picture, origin[0]+box['left']+x, origin[1]+box['top']+y)) > 235]
            check(len(white_text) > 15 and min(y for x,y in white_text) >= 10 and
                  max(y for x,y in white_text) <= 34 and min(x for x,y in white_text) >= 20,
                  'native button label is visible and centered', len(white_text))
            color(picture, 'flow-first', 10, 10, (224,32,48), 'translated flow box paints outside its original line')
            first, second = rect('flow-first'), rect('flow-second')
            check(second['top'] == 500 and first['top'] == 500, 'translation preserves the following normal flow', (first, second))

            for id in ['visible-leaf', 'escape-leaf']:
                box = rect(id)
                subprocess.run(['xdotool', 'mousemove', '--window', win,
                                str(int(origin[0]+box['left']+40)), str(int(origin[1]+box['top']+10)),
                                'click', '1'], env=env, check=True)
                check(command('title') == 'Clicked ' + id, 'visible overflow is clickable: ' + id)

            box = rect('hidden-leaf')
            old_title = command('title')
            subprocess.run(['xdotool', 'mousemove', '--window', win,
                            str(int(origin[0]+box['left']+40)), str(int(origin[1]+box['top']+10)),
                            'click', '1'], env=env, check=True)
            check(command('title') == old_title, 'clipped overflow cannot receive clicks')
            command('eval', 'query.focus()')
            subprocess.run(['xdotool', 'type', '--clearmodifiers', 'renderer'], env=env, check=True)
            box = rect('search-button')
            subprocess.run(['xdotool', 'mousemove', '--window', win,
                            str(int(origin[0]+box['left']+60)), str(int(origin[1]+box['top']+22)),
                            'click', '1'], env=env, check=True)
            check(command('title') == 'Search renderer', 'styled native form submits with a mouse click')

            parent, child = rect('contained'), rect('contained-fixed')
            check(child['left'] == parent['left']+14 and child['top'] == parent['top']+14,
                  'positioned offsets include containing-block borders once', (parent,child))

            fixed_ids = ['origin', 'fixed', 'fixed-bottom', 'escaped-fixed']
            before = {id: rect(id) for id in fixed_ids}
            contained = rect('contained-fixed')
            command('eval', 'scrollTo(120,400)')
            for _ in range(60):
                if evaluate('scrollY') == 400:
                    break
                time.sleep(.02)
            check(evaluate('[scrollX,scrollY]') == [120,400], 'programmatic horizontal and vertical scroll')
            after = {id: rect(id) for id in fixed_ids}
            check(before == after, 'viewport fixed boxes stay anchored while scrolling', {'before': before, 'after': after})
            moved = rect('contained-fixed')
            check(moved['left'] == contained['left']-120 and moved['top'] == contained['top']-400,
                  'transformed ancestors contain fixed descendants', (contained, moved))
            picture = screenshot('scrolled')
            color(picture, 'fixed', 20, 15, (32,64,208), 'fixed box repaints after scrolling')
            color(picture, 'escaped-fixed', 20, 15, (32,128,64), 'viewport fixed box escapes overflow ancestor')
            command('eval', 'scrollBy({left:-120,top:-400})')
            for _ in range(60):
                if evaluate('scrollY') == 0:
                    break
                time.sleep(.02)
            check(evaluate('[scrollX,scrollY]') == [0,0], 'relative scroll with options')
            picture = screenshot('returned')
            color(picture, 'horizontal', 118, 25, (3,0,252), 'gradient stays stable across partial repaint', 5)

            # Resize and native wheel scrolling exercise a different path
            # from window.scrollTo, including the viewport's scroll blitting.
            original_gap = evaluate('innerWidth') - rect('fixed')['right']
            subprocess.run(['xdotool', 'windowsize', win, '850', '650'], env=env, check=True)
            for _ in range(60):
                if evaluate('innerWidth') < 900:
                    break
                time.sleep(.02)
            check(evaluate('innerWidth')-rect('fixed')['right'] == original_gap,
                  'right-fixed box follows viewport resizing')
            before = {id: rect(id) for id in fixed_ids}
            subprocess.run(['xdotool', 'mousemove', '--window', win, '600', '570', 'click', '5'], env=env, check=True)
            for _ in range(60):
                if evaluate('scrollY') > 0:
                    break
                time.sleep(.02)
            check(evaluate('scrollY') > 0, 'native mouse wheel scrolls the viewport')
            check(before == {id: rect(id) for id in fixed_ids}, 'fixed boxes stay anchored during native wheel scrolling')
            command('eval', 'scrollTo(0,0)')
            command('wait', '5')

            # Removing the only flow content leaves the root allocation
            # shorter than the viewport. Its fixed descendants still paint.
            command('eval', 'document.getElementById("spacer").remove()')
            command('wait', '5')
            picture = screenshot('short-document')
            color(picture, 'fixed-bottom', 20, 15, (32,64,208), 'fixed box paints beyond a short root allocation')
            color(picture, 'fixed', 20, 15, (32,64,208), 'fixed box uses the resized viewport without scrollbars')

            # Real pages regularly exceed FLTK's fixed native clip-stack
            # depth. Check a deep branch and a sibling painted afterwards.
            markup = '<div id=origin></div><div style="position:absolute;left:20px;top:20px;width:130px;height:90px">'
            markup += '<div style="position:relative;width:130px;height:90px">' * 64
            markup += '<div style="position:relative;width:70px;height:40px;overflow:hidden"><div id=deep-leaf style="position:absolute;left:10px;top:10px;width:100px;height:60px;background:#e02030"></div></div>'
            markup += '</div>' * 65
            markup += '<div id=deep-sibling style="position:absolute;left:240px;top:20px;width:100px;height:60px;background:#008050"></div>'
            command('eval', 'document.body.innerHTML=' + json.dumps(markup))
            picture = screenshot('deep-clipping')
            color(picture, 'deep-leaf', 20, 15, (224,32,48), 'deeply nested content paints inside its clip')
            color(picture, 'deep-leaf', 80, 15, (255,255,255), 'deeply nested content keeps overflow clipping')
            color(picture, 'deep-sibling', 50, 30, (0,128,80), 'clip restoration preserves the following sibling')

            # Exercise syntax, color-stop fixup and cascade independently of
            # the main page's layout. Each case uses an exact solid interior
            # or a color with a small interpolation tolerance.
            cases = [
                ('modern-rgb', 'background:linear-gradient(.25turn,rgb(100% 0% 0%),rgb(0 0 255))',
                 [(1,25,(251,0,4)), (98,25,(4,0,251))]),
                ('hex-alpha', 'background:white;background-image:linear-gradient(90deg,#f008,#00ff)',
                 [(1,25,(248,117,124)), (98,25,(4,2,253))]),
                ('distributed-stops', 'background:linear-gradient(90deg,red,lime,blue)',
                 [(50,25,(0,252,3))]),
                ('stop-fixup', 'background:linear-gradient(90deg,red 70%,blue 30%)',
                 [(20,25,(255,0,0)), (80,25,(0,0,255))]),
                ('negative-stops', 'background:linear-gradient(90deg,red -20px,blue 20px)',
                 [(10,25,(61,0,194))]),
                ('current-color', 'background:linear-gradient(currentColor,currentColor);color:#123456',
                 [(50,25,(18,52,86))]),
                ('important-gradient', 'background-image:linear-gradient(red,red)!important;background-image:none',
                 [(50,25,(255,0,0))]),
                ('background-reset', 'background:linear-gradient(red,red);background:blue',
                 [(50,25,(0,0,255))]),
                ('background-none', 'background:linear-gradient(red,red);background:none',
                 [(50,25,(255,255,255))]),
                ('invalid-background', 'background:blue;background:linear-gradient(red,red) garbage',
                 [(50,25,(0,0,255))]),
                ('unsupported-layers', 'background:blue;background-image:linear-gradient(red,red),linear-gradient(lime,lime)',
                 [(50,25,(0,0,255))]),
                ('invalid-radius', 'background:red;border-radius:20px;border-top-left-radius:10px garbage',
                 [(0,0,(255,255,255)), (50,25,(255,0,0))]),
                ('important-radius', 'background:red;border-radius:20px!important;border-radius:0',
                 [(0,0,(255,255,255))]),
                ('corner-longhand', 'background:red;border-top-left-radius:20px 30px',
                 [(0,0,(255,255,255)), (99,0,(255,0,0))]),
                ('calc-radius', 'background:red;border-radius:calc(10px + 10px)',
                 [(0,0,(255,255,255))]),
                ('negative-calc-radius', 'background:red;border-radius:calc(-10px)',
                 [(0,0,(255,0,0))]),
                ('zero-repeat', 'background:repeating-linear-gradient(red 20px,blue 20px)',
                 [(50,25,(0,0,255))]),
                ('modern-alpha', 'background:#20a040;background-image:linear-gradient(rgb(255 255 255 / 0%),rgb(255 255 255 / 0%))',
                 [(50,25,(32,160,64))]),
                ('grad-angle', 'background:linear-gradient(100grad,red,blue)',
                 [(1,25,(251,0,4))]),
                ('negative-angle', 'background:linear-gradient(-90deg,red,blue)',
                 [(1,25,(4,0,251)), (98,25,(251,0,4))]),
                ('percent-repeat', 'background:repeating-linear-gradient(90deg,red 0% 10%,blue 10% 20%)',
                 [(5,25,(255,0,0)), (15,25,(0,0,255)), (25,25,(255,0,0))]),
                ('variable-decorations', '--r:20px;--g:linear-gradient(90deg,red,red);background:var(--g);border-radius:var(--r)',
                 [(0,0,(255,255,255)), (50,25,(255,0,0))]),
                ('unequal-borders', 'background:white;border-style:solid;border-width:2px 4px 6px 8px;border-color:red lime blue yellow;border-radius:20px',
                 [(50,0,(255,0,0)), (98,25,(0,255,0)), (50,48,(0,0,255)), (3,25,(255,255,0))]),
                ('transparent-background', 'background:red;background-color:transparent',
                 [(50,25,(255,255,255))]),
                ('diagonal-corners', 'background:linear-gradient(to top right,red,blue)',
                 [(1,48,(251,0,4)), (98,1,(4,0,251))]),
            ]
            markup = '<div id=origin></div>'
            for n, (id, css, samples) in enumerate(cases):
                markup += '<div id="value-' + id + '" style="position:absolute;box-sizing:border-box;left:' + str(20+155*(n%5)) + 'px;top:' + str(20+96*(n//5)) + 'px;width:100px;height:50px;' + css + '"></div>'
            command('eval', 'document.body.innerHTML=' + json.dumps(markup))
            picture = screenshot('css-values')
            for id, css, samples in cases:
                for x,y,expected in samples:
                    color(picture, 'value-'+id, x, y, expected, 'CSS value '+id+' at '+str((x,y)), 6)

            # Native controls are leaf widgets, so test their clipping and
            # hit-testing separately from the container/anchor cases above.
            cases = [
                ('rgba-background', 'background:rgba(255,0,0,.5)', [(50,25,(255,127,127))]),
                ('hex-four', 'background:#0f08', [(50,25,(119,255,119))]),
                ('hex-eight', 'background:#0000ff80', [(50,25,(127,127,255))]),
                ('hsl-background', 'background:hsl(120deg 100% 50%)', [(50,25,(0,255,0))]),
                ('hsla-background', 'background:hsl(.5turn 100% 50% / 50%)', [(50,25,(127,255,255))]),
                ('negative-hue', 'background:hsl(-120,100%,50%)', [(50,25,(0,0,255))]),
                ('modern-background', 'background:rgb(100% 0% 0% / 25%)', [(50,25,(255,191,191))]),
                ('background-current-color', 'background:currentColor;color:#123456', [(50,25,(18,52,86))]),
                ('transparent-rgba', 'background:red;background:rgba(0,255,0,0)', [(50,25,(255,255,255))]),
                ('invalid-hsl', 'background:blue;background:hsl(0,50,50)', [(50,25,(0,0,255))]),
                ('hsl-gradient', 'background:linear-gradient(hsl(120 100% 50%),hsl(120 100% 50%))', [(50,25,(0,255,0))]),
                ('shadow-offset', 'background:white;box-shadow:10px 8px red', [(105,25,(255,0,0)), (50,53,(255,0,0)), (50,25,(255,255,255))]),
                ('shadow-spread', 'background:white;box-shadow:0 0 0 6px #008050', [(-3,25,(0,128,80)), (50,-3,(0,128,80))]),
                ('shadow-inset', 'background:white;box-shadow:inset 0 0 0 6px blue', [(2,25,(0,0,255)), (50,25,(255,255,255))]),
                ('shadow-alpha', 'background:white;box-shadow:10px 0 rgba(255,0,0,.5)', [(105,25,(255,127,127))]),
                ('shadow-order', 'background:white;box-shadow:10px 0 red,20px 0 blue', [(105,25,(255,0,0)), (115,25,(0,0,255))]),
                ('shadow-current-color', 'background:white;box-shadow:10px 0;color:#008050', [(105,25,(0,128,80))]),
                ('shadow-invalid', 'background:white;box-shadow:10px 0 red;box-shadow:0 0 -2px blue', [(105,25,(255,0,0))]),
                ('shadow-none', 'background:white;box-shadow:10px 0 red;box-shadow:none', [(105,25,(255,255,255))]),
                ('shadow-blur', 'background:white;box-shadow:0 0 12px black', []),
                ('shadow-rounded', 'background:white;border-radius:20px;box-shadow:0 0 0 5px red', [(0,0,(255,255,255)), (50,-3,(255,0,0))]),
                ('shadow-calc', 'background:white;--shadow:calc(5px + 5px) 0 0 red;box-shadow:var(--shadow)', [(105,25,(255,0,0))]),
                ('outline-solid', 'background:white;outline:4px solid #008050;outline-offset:3px', [(-5,25,(0,128,80)), (-1,25,(255,255,255))]),
                ('outline-current-color', 'background:white;outline:4px solid;color:#008050', [(50,-2,(0,128,80))]),
                ('outline-negative-offset', 'background:white;outline:2px solid red;outline-offset:-4px', [(2,25,(255,0,0)), (0,25,(255,255,255))]),
                ('outline-important', 'background:white;outline:4px solid red!important;outline:none', [(50,-2,(255,0,0))]),
                ('outline-invalid', 'background:white;outline:4px solid red;outline:4px solid blue extra', [(50,-2,(255,0,0))]),
                ('outline-none', 'background:white;outline:4px solid red;outline:none', [(50,-2,(255,255,255))]),
                ('outline-variable', 'background:white;--o:4px solid red;outline:var(--o)', [(50,-2,(255,0,0))]),
                ('outline-rounded', 'background:white;border-radius:20px;outline:4px solid red', [(-1,-1,(255,255,255)), (50,-2,(255,0,0))]),
                ('outline-dashed', 'background:white;outline:4px dashed red', [(2,-2,(255,0,0)), (12,-2,(255,255,255))]),
                ('outline-double', 'background:white;outline:6px double red', [(50,-5,(255,0,0)), (50,-3,(255,255,255)), (50,-1,(255,0,0))]),
            ]
            markup = '<div id=origin></div>'
            for n, (id, css, samples) in enumerate(cases):
                markup += '<div id="decor-' + id + '" style="position:absolute;box-sizing:border-box;left:' + str(20+155*(n%5)) + 'px;top:' + str(20+80*(n//5)) + 'px;width:100px;height:50px;' + css + '"></div>'
            command('eval', 'document.body.innerHTML=' + json.dumps(markup))
            picture = screenshot('colors-and-shadows')
            for id, css, samples in cases:
                for x,y,expected in samples:
                    color(picture, 'decor-'+id, x, y, expected, 'decoration '+id+' at '+str((x,y)), 6)
            check(evaluate('getComputedStyle(document.getElementById("decor-rgba-background")).backgroundColor').startswith('rgba(255,0,0,'),
                  'computed background color retains alpha')
            check(evaluate('getComputedStyle(document.getElementById("decor-outline-solid")).outlineOffset') == '3px',
                  'computed outline offset reflects the rendered style')
            check(evaluate('getComputedStyle(document.getElementById("decor-shadow-offset")).boxShadow').startswith('10px 8px'),
                  'computed box shadow reflects rendered lengths')
            box = rect('decor-shadow-blur')
            near = pixel(picture, origin[0]+box['left']-2, origin[1]+box['top']+25)[0]
            far = pixel(picture, origin[0]+box['left']-10, origin[1]+box['top']+25)[0]
            check(100 < near < far < 255, 'blurred shadow fades outside the box', (near, far))

            # Reset keywords must override previous decorative declarations,
            # including deferred shorthand values and explicit inheritance.
            cases = [
                ('background-unset', 'background:red;background:unset', [(50,25,(255,255,255))]),
                ('background-initial', 'background:linear-gradient(red,red);background:initial', [(50,25,(255,255,255))]),
                ('background-color-unset', 'background:red;background-color:unset', [(50,25,(255,255,255))]),
                ('shadow-unset', 'box-shadow:10px 0 red;box-shadow:unset', [(105,25,(255,255,255))]),
                ('outline-unset', 'outline:4px solid red;outline:unset', [(50,-2,(255,255,255))]),
                ('radius-initial', 'background:red;border-radius:20px;border-radius:initial', [(0,0,(255,0,0))]),
                ('variable-reset', '--r:unset;background:red;background:var(--r,initial)', [(50,25,(255,255,255))]),
                ('invalid-outline-width', 'outline:4px solid red;outline-width:8px garbage', [(50,-3,(255,0,0)), (50,-6,(255,255,255))]),
                ('foreground-hsl', 'color:hsl(120 100% 50%);background:currentColor', [(50,25,(0,255,0))]),
                ('foreground-rgb', 'color:rgb(0 0 255);background:currentColor', [(50,25,(0,0,255))]),
                ('invalid-legacy-rgb', 'color:blue;color:rgb(255,0%,0);background:currentColor', [(50,25,(0,0,255))]),
                ('invalid-hue', 'background:blue;background:hsl(1e308turn 100% 50%)', [(50,25,(0,0,255))]),
            ]
            markup = '<div id=origin></div>'
            for n, (id, css, samples) in enumerate(cases):
                markup += '<div id="reset-' + id + '" style="position:absolute;left:' + str(20+155*(n%5)) + 'px;top:' + str(20+96*(n//5)) + 'px;width:100px;height:50px;' + css + '"></div>'
            markup += '<div style="position:absolute;left:20px;top:350px;width:100px;height:50px;background:#008050;border-radius:20px;box-shadow:10px 0 red;outline:4px solid blue"><div id=inherit-decoration style="width:100px;height:50px;background:inherit;border-radius:inherit;box-shadow:inherit;outline:inherit"></div></div>'
            markup += '<div style="position:absolute;left:200px;top:350px;width:100px;height:50px;background:blue"><div id=nested-alpha style="width:100px;height:50px;background:rgba(255,0,0,.5)"></div></div>'
            command('eval', 'document.body.innerHTML=' + json.dumps(markup))
            picture = screenshot('decoration-resets')
            for id, css, samples in cases:
                for x,y,expected in samples:
                    color(picture, 'reset-'+id, x, y, expected, 'decoration reset '+id, 6)
            color(picture, 'inherit-decoration', 50, 25, (0,128,80), 'background inheritance preserves color')
            color(picture, 'inherit-decoration', 0, 0, (255,255,255), 'radius inheritance preserves rounded corners')
            color(picture, 'inherit-decoration', 105, 25, (255,0,0), 'shadow inheritance preserves offsets')
            color(picture, 'inherit-decoration', 50, -2, (0,0,255), 'outline inheritance preserves color')
            color(picture, 'nested-alpha', 50, 25, (128,0,127), 'transparent background composites over its painted parent')
            command('eval', 'document.body.innerHTML="<div id=origin></div>";document.body.style.background="transparent";document.documentElement.style.background="rgba(255,0,0,.5)"')
            picture = screenshot('root-alpha')
            actual = pixel(picture, origin[0]+400, origin[1]+300)
            check(actual == (255,127,127), 'transparent root background composites once over the canvas', actual)
            command('eval', 'document.documentElement.style.background="transparent";document.body.style.background="rgba(255,0,0,.5)"')
            picture = screenshot('body-alpha')
            actual = pixel(picture, origin[0]+400, origin[1]+300)
            check(actual == (255,127,127), 'body alpha background propagates to a transparent root', actual)
            command('eval', 'document.documentElement.style.background="white";document.body.style.background="white"')

            markup = '''<div id=origin></div>
              <div style="position:absolute;left:20px;top:20px;width:200px;height:80px">
                <div style="width:10px;height:10px;overflow:hidden">
                  <input id=native-absolute style="position:absolute;left:50px;top:30px;width:100px;height:30px;box-sizing:border-box;padding:0;border:0;background:#e02030">
                </div>
              </div>
              <div style="position:absolute;left:300px;top:20px;width:10px;height:10px;overflow:hidden">
                <input id=native-fixed style="position:fixed;left:350px;top:50px;width:100px;height:30px;box-sizing:border-box;padding:0;border:0;background:#208040;outline:3px solid blue">
              </div>
              <div style="position:absolute;left:500px;top:20px;width:60px;height:60px;overflow:hidden">
                <input id=native-clipped style="position:absolute;left:45px;top:35px;width:100px;height:30px;box-sizing:border-box;padding:0;border:0;background:#e02030">
              </div>
              <div style="width:1200px;height:1200px"></div>'''
            command('eval', 'document.body.innerHTML=' + json.dumps(markup))
            picture = screenshot('native-clipping')
            color(picture, 'native-absolute', 80, 15, (224,32,48), 'native absolute input escapes intermediate clipping')
            color(picture, 'native-fixed', 80, 15, (32,128,64), 'native fixed input escapes ancestor clipping')
            color(picture, 'native-fixed', -2, 15, (0,0,255), 'native control outline paints outside its allocation')
            color(picture, 'native-clipped', 5, 15, (224,32,48), 'native clipped input paints inside padding box')
            color(picture, 'native-clipped', 40, 15, (255,255,255), 'native clipped input does not paint outside padding box')
            box = rect('native-absolute')
            subprocess.run(['xdotool', 'mousemove', '--window', win,
                            str(int(origin[0]+box['left']+80)), str(int(origin[1]+box['top']+15)),
                            'click', '1', 'type', '--clearmodifiers', 'escaped'], env=env, check=True)
            check(evaluate('document.getElementById("native-absolute").value') == 'escaped',
                  'escaped native absolute input accepts real mouse and keyboard input')
            box = rect('native-clipped')
            subprocess.run(['xdotool', 'mousemove', '--window', win,
                            str(int(origin[0]+box['left']+40)), str(int(origin[1]+box['top']+15)),
                            'click', '1'], env=env, check=True)
            check(evaluate('document.activeElement.id') != 'native-clipped',
                  'clipped native input cannot receive an outside mouse click')
            before = rect('native-fixed')
            command('eval', 'scrollTo(80,200)')
            command('wait', '5')
            check(rect('native-fixed') == before, 'native fixed input stays anchored during scrolling')
            picture = screenshot('native-clipping-scrolled')
            color(picture, 'native-fixed', 80, 15, (32,128,64), 'native fixed input repaints after scrolling')
            box = rect('native-fixed')
            subprocess.run(['xdotool', 'mousemove', '--window', win,
                            str(int(origin[0]+box['left']+80)), str(int(origin[1]+box['top']+15)),
                            'click', '1', 'type', '--clearmodifiers', 'fixed'], env=env, check=True)
            check(evaluate('document.getElementById("native-fixed").value') == 'fixed',
                  'scrolled native fixed input accepts real mouse and keyboard input')
            command('quit')
            check(browser.wait(timeout=8) == 0, 'clean browser shutdown')
            log.flush()
            browser_log = (output / 'browser.log').read_text()
            check('ERROR: AddressSanitizer' not in browser_log, 'no address sanitizer errors')
            check('clip stack overflow' not in browser_log and 'clip stack underflow' not in browser_log,
                  'native clipping stack stays balanced at arbitrary CSS depth')
            success = True
            print('PASS: ' + str(len(results)) + ' offline renderer checks')
            return 0
        except Exception:
            log.flush()
            if browser.poll() is None:
                try:
                    (output / 'dom.html').write_text(command('domdump'))
                    (output / 'geometry.json').write_text(command('eval', 'JSON.stringify(Array.from(document.querySelectorAll("[id]")).map(e=>[e.id,e.getBoundingClientRect()]))'))
                except Exception:
                    pass
            print('Renderer artifacts: ' + str(output))
            print((output / 'browser.log').read_text()[-8000:])
            raise
        finally:
            (output / 'results.json').write_text(json.dumps(results, ensure_ascii=False, indent=2))
            if browser.poll() is None:
                browser.terminate()
                browser.wait(timeout=8)
            xserver.terminate()
            xserver.wait(timeout=8)
            server.shutdown()
            if not os.environ.get('DILLO_RENDER_OUTPUT') and success:
                shutil.rmtree(work)


if __name__ == '__main__':
    raise SystemExit(main())
