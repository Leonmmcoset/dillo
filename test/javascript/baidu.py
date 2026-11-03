#!/usr/bin/env python3
"""Optional live acceptance test. Makes searches on Baidu using native input.

Run separately from make check; the public site and network can change.
Artifacts and the exact default user agent are recorded in --output.
"""
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import time
import urllib.parse


class AcceptanceBlocked(Exception):
    pass


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--bindir', type=Path, default=os.environ.get('DILLO_TEST_BINDIR', 'build-js/src'))
    parser.add_argument('--output', type=Path)
    parser.add_argument('--query', help='Run a single query instead of the four-case matrix')
    parser.add_argument('--submit', choices=['enter', 'button'], default='button')
    parser.add_argument('--navigation', action='store_true', help='Also click Next, a result, and the browser Back button')
    args = parser.parse_args()
    for program in ['Xvfb', 'xdotool', 'xsel']:
        if not shutil.which(program):
            parser.error(program + ' is required')
    bindir = args.bindir.resolve()
    output = args.output or Path(tempfile.mkdtemp(prefix='dillo-baidu-'))
    output.mkdir(parents=True, exist_ok=True)
    output = output.resolve()
    print('Artifacts:', output, flush=True)
    env = os.environ.copy()
    profile = tempfile.TemporaryDirectory(prefix='dillo-baidu-home-')
    env['HOME'] = profile.name
    config = Path(profile.name) / '.dillo'
    config.mkdir()
    (config / 'dillorc').write_text('javascript_enabled=YES\nload_images=YES\nload_background_images=YES\n')
    readfd, writefd = os.pipe()
    xlog = (output / 'xvfb.log').open('w')
    x = subprocess.Popen(['Xvfb', '-ac', '-noreset', '-nolisten', 'tcp', '-displayfd', str(writefd),
                          '-screen', '0', '1366x900x24'], pass_fds=(writefd,), stdout=xlog, stderr=xlog)
    os.close(writefd)
    with os.fdopen(readfd) as ready:
        env['DISPLAY'] = ':' + ready.readline().strip()
    log = (output / 'browser.log').open('w')
    browser = subprocess.Popen([str(bindir / 'dillo'), '-g', '1366x900', 'https://www.baidu.com'],
                               env=env, stdout=log, stderr=log)
    env['DILLO_PID'] = str(browser.pid)
    results = []

    def command(*words):
        if browser.poll() is not None:
            raise AssertionError('browser exited: ' + str(browser.returncode))
        return subprocess.run([str(bindir / 'dilloc'), *words], env=env, capture_output=True,
                              text=True, timeout=12)

    def evaluate(expression):
        reply = command('eval', expression)
        if reply.returncode:
            return None
        try:
            return json.loads(reply.stdout)
        except ValueError:
            return None

    def wait_for(expression, message, seconds=60):
        deadline = time.monotonic() + seconds
        while time.monotonic() < deadline:
            try:
                value = evaluate('JSON.stringify(' + expression + ')')
                if value:
                    return value
                current = urllib.parse.urlsplit(command('url').stdout.strip())
                if current.hostname == 'wappass.baidu.com' and '/captcha/' in current.path:
                    raise AcceptanceBlocked('Baidu requested CAPTCHA verification during: ' + message)
            except subprocess.TimeoutExpired:
                pass
            time.sleep(.15)
        raise AssertionError('timed out: ' + message)

    def snapshot(name):
        # Commit pending DOM/style changes before capturing the native window.
        evaluate('JSON.stringify(document.documentElement.getBoundingClientRect().toJSON())')
        # Control-socket replies can arrive before FLTK paints the new tree.
        time.sleep(.35)
        (output / (name + '.html')).write_text(command('domdump').stdout)
        if shutil.which('import'):
            subprocess.run(['import', '-display', env['DISPLAY'], '-window', 'root', str(output / (name + '.png'))],
                           capture_output=True, timeout=8)

    def click_element(expression):
        evaluate('JSON.stringify((' + expression + ').scrollIntoView())')
        box = wait_for('(' + expression + ').getBoundingClientRect().toJSON()', 'element geometry')
        assert box['width'] > 0 and box['height'] > 0, 'element has no rendered box'
        subprocess.run(['xdotool', 'mousemove', '--window', win,
                        str(int(box['x'] + min(box['width'] / 2, 30))),
                        str(int(box['y'] + box['height'] / 2 + 74)), 'click', '1'], env=env, check=True)

    try:
        wait_for("document.querySelector('#chat-textarea')&&document.readyState==='complete'", 'Baidu home')
        windows = subprocess.check_output(['xdotool', 'search', '--pid', str(browser.pid), '--name', 'Dillo'], env=env, text=True)
        win = windows.splitlines()[0]
        agent = evaluate('JSON.stringify(navigator.userAgent)')
        (output / 'user-agent.txt').write_text(str(agent) + '\n')
        cases = [(args.query, args.submit)] if args.query else [
            ('quickjs', 'enter'), ('QuickJS 引擎', 'button'),
            ('dillo browser', 'button'), ('开源浏览器', 'enter')]
        for index, (query, submit) in enumerate(cases):
            if index:
                command('open', 'https://www.baidu.com')
            wait_for("document.querySelector('#chat-textarea')&&document.readyState==='complete'", 'search input')
            click_element("document.querySelector('#chat-textarea')")
            wait_for("document.activeElement?.id==='chat-textarea'", 'mouse focus')
            # Focus handlers can schedule a native control rebuild. Let the
            # browser flush it before sending keyboard events to that control.
            command('wait', '10')
            if query.isascii():
                subprocess.run(['xdotool', 'type', '--clearmodifiers', query], env=env, check=True)
            else:
                subprocess.run(['xsel', '--clipboard', '--input'], input=query, text=True, env=env, check=True)
                subprocess.run(['xdotool', 'key', '--clearmodifiers', 'ctrl+v'], env=env, check=True)
            wait_for("document.querySelector('#chat-textarea').value===" + json.dumps(query), 'native text input')
            snapshot(str(index) + '-home')
            if submit == 'enter':
                subprocess.run(['xdotool', 'key', '--clearmodifiers', 'Return'], env=env, check=True)
            else:
                command('wait', '10')
                click_element("document.querySelector('#chat-submit-button')")
            wait_for("location.pathname==='/s'&&new URL(location.href).searchParams.get('wd')===" + json.dumps(query) +
                     "&&document.readyState==='complete'&&Array.from(document.querySelectorAll('#content_left h3 a')).some(a=>{const r=a.getBoundingClientRect();return r.width>0&&r.height>0&&r.bottom>0&&r.top<innerHeight})", 'visible search results', 90)
            evaluate('document.activeElement?.blur()')
            command('wait', '10')
            snapshot(str(index) + '-results')
            diagnostic = command('eval', 'JSON.stringify((()=>{const heading=document.querySelector("#content_left h3");'
                'if(!heading)return {error:"result heading missing"};'
                'const describe=e=>({tag:e.tagName,id:e.id,className:e.className,'
                'rect:e.getBoundingClientRect().toJSON(),style:getComputedStyle(e).cssText});'
                'const chain=[];for(let e=heading;e&&chain.length<12;e=e.parentElement)chain.push(describe(e));'
                'return {url:location.href,viewport:{width:innerWidth,height:innerHeight},'
                'columns:["content_left","content_right"].map(id=>document.getElementById(id)).filter(Boolean).map(describe),'
                'chain,children:Array.from(heading.children).map(describe),'
                'descendants:Array.from(heading.querySelectorAll("*")).slice(0,20).map(describe),'
                'siblings:heading.parentElement&&Array.from(heading.parentElement.children).slice(0,5).map(describe)}})())')
            try:
                layout = json.loads(diagnostic.stdout)
            except ValueError:
                layout = {'returncode': diagnostic.returncode, 'stdout': diagnostic.stdout,
                          'stderr': diagnostic.stderr}
            (output / (str(index) + '-layout.json')).write_text(json.dumps(layout, ensure_ascii=False, indent=2) + '\n')
            results.append({'query': query, 'submit': submit, 'url': command('url').stdout.strip(), 'status': 'pass'})
            print('PASS:', query, submit, flush=True)
        if args.navigation:
            before = command('url').stdout.strip()
            next_link = "Array.from(document.querySelectorAll('#page a')).find(a=>a.textContent.includes('下一页'))"
            wait_for('!!(' + next_link + ')', 'Next link')
            click_element(next_link)
            wait_for("location.pathname==='/s'&&Number(new URL(location.href).searchParams.get('pn'))>0&&!!document.querySelector('#content_left h3 a')", 'next results page', 90)
            snapshot('next-page')
            page = command('url').stdout.strip()
            assert page != before, 'pagination did not update the URL'
            click_element("Array.from(document.querySelectorAll('#content_left h3 a')).find(a=>a.getBoundingClientRect().width>0)")
            wait_for("location.pathname!=='/s'&&location.hostname!=='www.baidu.com'", 'open search result', 90)
            snapshot('opened-result')
            subprocess.run(['xdotool', 'mousemove', '--window', win, '22', '45', 'click', '1'], env=env, check=True)
            wait_for("location.href===" + json.dumps(page), 'browser Back', 90)
            snapshot('back')
            results.append({'navigation': 'pagination, result link, back', 'status': 'pass'})
            print('PASS: pagination, result link, back', flush=True)
        command('quit')
        assert browser.wait(timeout=8) == 0, 'browser did not exit cleanly'
    except Exception as error:
        blocked = isinstance(error, AcceptanceBlocked)
        results.append({'status': 'blocked' if blocked else 'fail', 'error': str(error)})
        try:
            snapshot('failure')
        except (subprocess.TimeoutExpired, AssertionError):
            pass
        if blocked:
            print('BLOCKED:', error, flush=True)
            return 77
        raise
    finally:
        (output / 'results.json').write_text(json.dumps(results, ensure_ascii=False, indent=2) + '\n')
        if browser.poll() is None:
            browser.terminate()
            browser.wait(timeout=8)
        x.terminate()
        x.wait(timeout=4)
        log.close()
        xlog.close()
        profile.cleanup()


if __name__ == '__main__':
    raise SystemExit(main())
