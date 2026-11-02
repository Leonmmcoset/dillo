#!/usr/bin/env python3
"""Exercise the real browser/cache/FLTK event loop against an offline server."""
import http.server
import json
import os
import pathlib
import shutil
import socketserver
import subprocess
import tempfile
import threading
import time
import urllib.parse

if not shutil.which('Xvfb'):
    raise SystemExit(77)
bindir = pathlib.Path(os.environ['DILLO_TEST_BINDIR'])


class Server(socketserver.ThreadingMixIn, http.server.HTTPServer):
    daemon_threads = True


class Handler(http.server.BaseHTTPRequestHandler):
    def log_message(self, *_):
        pass

    def do_POST(self):
        body = self.rfile.read(int(self.headers.get('Content-Length', 0)))
        self.reply('<title>POST ' + body.decode() + '</title>')

    def do_HEAD(self):
        self.send_response(200)
        self.send_header('Content-Type', 'text/plain')
        self.send_header('Content-Length', '123')
        self.end_headers()

    def do_OPTIONS(self):
        self.reply('', headers={
            'Access-Control-Allow-Origin': self.headers.get('Origin', ''),
            'Access-Control-Allow-Methods': 'PUT',
            'Access-Control-Allow-Headers': 'x-test, content-type',
        })

    def do_PUT(self):
        body = self.rfile.read(int(self.headers.get('Content-Length', 0))).decode()
        self.reply(body + ':' + self.headers.get('X-Test', ''), headers={
            'Access-Control-Allow-Origin': self.headers.get('Origin', ''),
        })

    def reply(self, body, status=200, headers=None):
        body = body.encode()
        self.send_response(status)
        self.send_header('Content-Type', 'application/javascript' if self.path.endswith('.js') else 'text/css' if self.path.endswith('.css') else 'text/html; charset=utf-8')
        self.send_header('Content-Length', str(len(body)))
        for key, value in (headers or {}).items():
            self.send_header(key, value)
        self.end_headers()
        try:
            self.wfile.write(body)
        except (BrokenPipeError, ConnectionResetError):
            pass

    def do_GET(self):
        route = urllib.parse.urlsplit(self.path).path
        if route == '/cors-ok':
            self.reply('cors', headers={'Access-Control-Allow-Origin': '*', 'Access-Control-Expose-Headers': 'X-First, X-Second', 'X-Second': 'exposed'})
        elif route == '/cors-private':
            self.reply('private', headers={'Access-Control-Allow-Origin': self.headers.get('Origin', ''), 'Access-Control-Allow-Credentials': 'true'})
        elif route == '/network-tests':
            cross = 'http://127.0.0.1:' + str(self.server.other.server_port)
            self.reply("""<title>Network pending</title><script>
const cross='""" + cross + """';
let failures=[];
const check=(ok,name)=>{if(!ok)failures.push(name);};
const denied=p=>p.then(()=>false,()=>true);
Promise.all([
 fetch(cross+'/cors-ok').then(async r=>check(await r.text()==='cors'&&r.headers.get('X-Second')==='exposed','cors headers')),
 denied(fetch(cross+'/plain')).then(v=>check(v,'cors deny')),
 denied(fetch(cross+'/cors-ok',{credentials:'include'})).then(v=>check(v,'wildcard credentials')),
 fetch(cross+'/cors-private',{credentials:'include'}).then(r=>check(r.ok,'credentials')),
 fetch(cross+'/put',{method:'PUT',headers:{'X-Test':'header','Content-Type':'application/json'},body:'payload'}).then(r=>r.text()).then(t=>check(t==='payload:header','preflight')),
 fetch('/api',{headers:{'X-Test':'one'}}).then(r=>r.text()).then(t=>check(t==='network 中文','same origin headers')),
 fetch('/request-headers',{headers:{'Accept':'application/json','Accept-Language':'zh-CN'}}).then(r=>r.json()).then(h=>check(h.accept.length===1&&h.accept[0]==='application/json'&&h.language.length===1&&h.language[0]==='zh-CN','explicit headers replace defaults')),
 denied(fetch('/api',{headers:{'X-Bad':'bad\\r\\nInjected: header'}})).then(v=>check(v,'header injection')),
 new Promise(resolve=>{const c=new AbortController();fetch('/slow',{signal:c.signal}).then(()=>{check(false,'abort');resolve();},()=>resolve());c.abort();}),
 new Promise(resolve=>{const x=new XMLHttpRequest();x.open('GET','/slow');x.timeout=15;x.ontimeout=resolve;x.onerror=()=>{check(false,'xhr timeout');resolve();};x.onload=()=>{check(false,'xhr timeout');resolve();};x.send();}),
 fetch('/head',{method:'HEAD'}).then(r=>r.text()).then(t=>check(t==='','head')),
]).then(()=>document.title=failures.length?'FAIL '+failures.join(','):'Network PASS',e=>document.title='FAIL '+e);
</script>""")
        elif route == '/request-headers':
            import json
            self.reply(json.dumps({'accept': self.headers.get_all('Accept', []),
                                   'language': self.headers.get_all('Accept-Language', [])}))
        elif route == '/blocking.js':
            self.reply("order.push('blocking');document.write('<b id=written>中文</b>');")
        elif route == '/defer.js':
            self.reply("order.push(document.getElementById('end')?'defer':'BAD');")
        elif route == '/async.js':
            time.sleep(.1)
            self.reply("order.push('async');")
        elif route == '/dynamic.js':
            self.reply("window.dynamicOK=true;")
        elif route == '/redirect.js':
            self.reply('', 302, {'Location': '/dynamic.js'})
        elif route == '/api':
            self.reply('network 中文', headers={'Set-Cookie': 'secret=hidden; HttpOnly'})
        elif route == '/gone.js':
            self.reply('window.badScript=true;', 404)
        elif route == '/second':
            self.reply("<title>FAIL</title><script>document.title=localStorage.integration==='saved'&&sessionStorage.integration==='tab'?'Storage PASS':'Storage FAIL';</script>")
        elif route == '/script-form':
            self.reply("<form id=f method=post action=/posted><input name=wd value=hello></form><script>document.getElementById('f').submit()</script>")
        elif route == '/replace':
            self.reply("<script>location.replace('/second')</script>")
        elif route == '/leave':
            self.reply("<title>Leaving</title><script>setTimeout(()=>location.assign('/plain'),20);setInterval(()=>document.title='BAD old timer',50);fetch('/slow');</script>")
        elif route == '/slow':
            time.sleep(.3)
            self.reply('late')
        elif route == '/plain':
            self.reply('<title>Plain PASS</title><p>new document</p>')
        elif route == '/events':
            self.reply('''<title>Events ready</title><p><a id=a href=/plain>Cancel link</a></p>
<form id=f action=/search><input name=wd id=q><input type=submit value=Search></form>
<script>
let count=0;
a.addEventListener('click',e=>{e.preventDefault();document.title='Click PASS';});
document.getElementById('q').addEventListener('input',()=>count++);
f.addEventListener('submit',e=>{e.preventDefault();document.title=count&&q.value==='hello'?'Form PASS':'Form FAIL';});
</script>''')
        elif route == '/buttons':
            self.reply('''<title>Buttons ready</title>
<button id=outside type=button>Outside form</button>
<input id=label type=button value="Label outside">
<form><button id=inside type=button>Inside form</button></form>
<a id=empty href=/plain style="display:block;width:80px;height:32px;background-color:#eeeeee"><span style="display:none">Next</span></a>
<script>
let clicks=[];
for(const id of ['outside','label','inside'])document.getElementById(id).onclick=e=>{
e.preventDefault();clicks.push(id);document.title=clicks.join(',')==='outside,label,inside'?'Buttons PASS':'Buttons '+clicks.join(',');
};
</script>''')
        elif route == '/paging':
            page = urllib.parse.parse_qs(urllib.parse.urlsplit(self.path).query).get('pn', ['0'])[0]
            self.reply('<title>Page ' + page + '</title><p><a id=next href="/paging?pn=1">Next</a></p>'
                       '<p><a id=result href=/plain>Open result</a></p>')
        elif route == '/clipboard':
            self.reply('''<title>Clipboard ready</title><textarea id=paste></textarea><script>
paste.onkeydown=()=>paste.dataset.key=Date.now();
paste.oninput=()=>document.title=paste.value==='中文 paste'?'Clipboard PASS':'FAIL pasted text';
</script>''')
        elif route == '/history':
            self.reply('''<title>History pending</title><div id=identity>preserved</div>
<script>
const original=identity,initialLength=history.length,initialURL=location.href;
let phase=0,pops=0,hashes=0,failures=[];
const check=(ok,name)=>{if(!ok)failures.push(name);};
const data={step:1,date:new Date(1234),bytes:new Uint8Array([1,2])};data.self=data;
history.replaceState({step:0},'',initialURL);
history.pushState(data,'','/history/one#first');data.step=99;
check(history.state.step===1&&history.state.self===history.state&&history.state.date.getTime()===1234&&history.state.bytes[1]===2,'clone');
check(identity===original&&location.pathname==='/history/one'&&history.length===initialLength+1,'push preserves document');
history.replaceState({step:2},'','/history/two#second');
check(history.length===initialLength+1&&history.state.step===2,'replace');
try{history.pushState({},'','https://other.example/');check(false,'cross origin');}catch(e){check(e.name==='SecurityError','security exception');}
try{history.pushState(()=>{},'','');check(false,'function state');}catch(e){check(e.name==='DataCloneError','clone exception');}
addEventListener('hashchange',()=>hashes++);
addEventListener('popstate',e=>{
++pops;
check(identity===original&&e.state===history.state,'pop preserves identity');
if(phase===0){check(location.href===initialURL&&history.state.step===0,'back');phase=1;setTimeout(()=>history.forward(),10);}
else if(phase===1){check(location.pathname==='/history/two'&&history.state.step===2,'forward');phase=2;setTimeout(()=>{
check(pops===2&&hashes===2,'events');document.title=failures.length?'FAIL '+failures.join(','):'History PASS';
},20);}
});
setTimeout(()=>history.back(),20);
</script>''')
        elif route == '/styles-controls':
            self.reply('''<!doctype html><title>Styles pending</title><style>
:root{--size:380px;--accent:#123456}#box{width:calc(var(--size) - 2 * 10px);height:20px;background-color:var(--missing,var(--accent));border:var(--border,0px solid black)}
</style><div id=box></div><input id=edit value="中文😀abc"><input id=check type=checkbox>
<script>setTimeout(()=>{
let failures=[];const check=(ok,name)=>{if(!ok)failures.push(name);};
const edit=document.getElementById('edit'),box=document.getElementById('box');
edit.focus();edit.setSelectionRange(1,4);check(edit.selectionStart===1&&edit.selectionEnd===4,'selection');
check(document.activeElement===edit,'focus');
check(box.getBoundingClientRect().width===360,'calc width '+box.getBoundingClientRect().width);
check(getComputedStyle(box).backgroundColor==='#123456','variable color '+getComputedStyle(box).backgroundColor);
document.getElementById('check').click();check(document.getElementById('check').checked,'default click');
edit.addEventListener('input',()=>document.title=edit.value==='中Xabc'?'Selection input PASS':'FAIL selection value '+edit.value);
document.title=failures.length?'FAIL '+failures.join(','):'Styles PASS';
},100);</script>''')
        elif route == '/sheet.css':
            time.sleep(.15)
            self.reply('#styled { width: 230px; height: 15px }')
        elif route == '/sheet-redirect.css':
            self.reply('', 302, {'Location': '/sheet.css'})
        elif route == '/sheet-cross-redirect.css':
            cross = 'http://127.0.0.1:' + str(self.server.other.server_port)
            self.reply('', 302, {'Location': cross + '/sheet.css'})
        elif route == '/sheet-missing.css':
            self.reply('', 404)
        elif route == '/cssom':
            cross = 'http://127.0.0.1:' + str(self.server.other.server_port)
            self.reply('''<!doctype html><title>CSSOM pending</title>
<style id=inline></style><div id=styled></div><input id=selection value="中文abc">
<script>
let loaded=0,denied=false,redirected=false,redirectDenied=false,failed=0;
inline.sheet.insertRule('#styled { background-color: #123456 }');
const sheet=document.createElement('link');sheet.rel='stylesheet';sheet.href='/sheet.css';
sheet.onload=()=>{loaded++;};document.head.appendChild(sheet);
const cross=document.createElement('link');cross.rel='stylesheet';cross.href=''' + repr(cross + '/sheet.css') + ''';
cross.onload=()=>{try{cross.sheet.cssRules;}catch(e){denied=e.name==='SecurityError';}};
document.head.appendChild(cross);
const redirect=document.createElement('link');redirect.rel='stylesheet';redirect.href='/sheet-redirect.css';
redirect.onload=()=>{redirected=redirect.sheet.cssRules[0].selectorText==='#styled';};document.head.appendChild(redirect);
const crossRedirect=document.createElement('link');crossRedirect.rel='stylesheet';crossRedirect.href='/sheet-cross-redirect.css';
crossRedirect.onload=()=>{try{crossRedirect.sheet.cssRules;}catch(e){redirectDenied=e.name==='SecurityError'&&!crossRedirect.sheet._text;}};
document.head.appendChild(crossRedirect);
const missing=document.createElement('link');missing.rel='stylesheet';missing.href='/sheet-missing.css';
missing.onerror=()=>failed++;document.head.appendChild(missing);
for(let i=1;i<=5;i++)setTimeout(()=>styled.dataset.revision=i,10*i);
setTimeout(()=>{
const failures=[];
if(loaded!==1||!sheet.sheet||sheet.sheet.cssRules[0].selectorText!=='#styled')failures.push('stylesheet load');
if(!denied)failures.push('cross-origin stylesheet');
if(!redirected||!redirectDenied)failures.push('stylesheet redirects');
if(failed!==1||missing.sheet)failures.push('stylesheet error');
if(styled.getBoundingClientRect().width!==230||getComputedStyle(styled).backgroundColor!=='#123456')failures.push('stylesheet rendering');
const field=document.getElementById('selection');field.focus();field.setSelectionRange(1,3);
if(String(window.getSelection())!=='文a')failures.push('selection text');
document.title=failures.length?'FAIL '+failures.join(','):'CSSOM PASS';
},700);
</script>''')
        elif route == '/layout':
            self.reply('''<!doctype html><title>Layout pending</title><style>
#flex{display:flex;width:160px;gap:20px;align-items:center;height:40px}
#flex>div{flex:1;min-width:0;height:20px}
#grid{display:grid;width:330px;grid-template-columns:1fr 2fr;gap:10px 30px}
#grid>div{height:20px}
#wrap{display:flex;flex-wrap:wrap;width:120px;gap:10px}#wrap>span{width:50px;height:20px}
#column{display:flex;flex-direction:column;width:100px;height:100px;gap:10px}#column>div{flex:1;min-height:0}
</style><div id=flex><div id=a>A</div><div id=b>B</div></div>
<div id=grid><div id=c>C</div><div id=d>D</div><div id=e>E</div></div>
<div id=wrap><span id=f>F</span><span id=g>G</span><span id=h>H</span></div>
<div id=column><div id=i>I</div><div id=j>J</div></div>
<script>setTimeout(()=>{
const rect=id=>document.getElementById(id).getBoundingClientRect(),fail=[];
const check=(ok,name)=>{if(!ok)fail.push(name);};
check(rect('a').width===70&&rect('b').left-rect('a').right===20,'flex widths/gap '+JSON.stringify([rect('a'),rect('b')]));
check(rect('a').top-rect('flex').top===10,'align center');
check(rect('c').width===100&&rect('d').width===200&&rect('d').left-rect('c').right===30,'grid tracks');
check(rect('e').top-rect('c').bottom===10,'grid rows');
check(rect('h').top>rect('f').top&&rect('g').top===rect('f').top,'wrap');
check(rect('i').height===45&&rect('j').height===45&&rect('j').top-rect('i').bottom===10,'column grow');
document.title=fail.length?'FAIL '+fail.join(','):'Layout PASS';
},100);</script>''')
        elif route == '/position':
            self.reply('''<!doctype html><title>Position pending</title><style>
body{margin:0}#outer{position:relative;width:400px;height:200px}
#abs{position:absolute;left:25%;top:20px;width:100px;height:30px;z-index:2;background:green}
#rel{position:relative;left:10px;top:5px;width:80px;height:20px}
#fixed{position:fixed;left:30px;top:40px;width:40px;height:20px;z-index:3}
#flex{display:flex;position:relative;width:200px;height:100px}
#item{position:relative;left:10px;top:5px;width:60px;height:20px}
#nested{position:absolute;right:10px;top:10px;width:40px;height:30px}
#wide{position:absolute;left:0;top:70px;width:300px}#wide-child{height:10px}
#center{width:100px;margin:0 auto;height:10px}
</style><div id=outer><div id=abs></div><div id=rel></div><div id=normal style="height:10px"></div><div id=center></div><div id=wide><div id=wide-child></div></div></div>
<div id=fixed></div><div id=flex><div id=item></div><div id=nested></div></div>
<script>setTimeout(()=>{
const rect=id=>document.getElementById(id).getBoundingClientRect(),fail=[];
const check=(ok,name)=>{if(!ok)fail.push(name);};
check(rect('abs').left-rect('outer').left===100&&rect('abs').top-rect('outer').top===20,'absolute '+JSON.stringify(rect('abs')));
check(rect('rel').left-rect('outer').left===10&&rect('rel').top-rect('outer').top===5,'relative '+JSON.stringify([rect('outer'),rect('rel')]));
check(rect('normal').top-rect('outer').top===20,'relative preserves flow '+JSON.stringify(rect('normal')));
check(rect('fixed').left===30&&rect('fixed').top===40,'fixed '+JSON.stringify(rect('fixed')));
check(rect('item').left-rect('flex').left===10&&rect('item').top-rect('flex').top===5,'relative flex item');
check(rect('nested').left-rect('flex').left===150&&rect('nested').top-rect('flex').top===10,'absolute flex child '+JSON.stringify(rect('nested')));
check(rect('wide-child').width===300,'positioned containing block width '+JSON.stringify(rect('wide-child')));
check(rect('center').left-rect('outer').left===150,'automatic margins '+JSON.stringify(rect('center')));
document.title=fail.length?'FAIL '+fail.join(','):'Position PASS';
},100);</script>''')
        elif route == '/':
            self.reply('''<!doctype html><title>Loading</title>
<script>window.order=[];window.fail=[];window.domReady=false;window.asyncBeforeLoad=false;
document.addEventListener('DOMContentLoaded',()=>{domReady=true;if(order.indexOf('defer')<0)fail.push('defer ordering');});
window.addEventListener('load',()=>{asyncBeforeLoad=order.includes('async');});
</script><script src=/defer.js defer></script><script src=/async.js async></script>
<script src=/blocking.js></script><script>if(document.getElementById('written').textContent!=='中文')fail.push('write');order.push('inline');</script>
<p id=end>Parsed</p><script>
localStorage.integration='saved';sessionStorage.integration='tab';
let el=document.createElement('script');el.src='/redirect.js';el.onload=()=>{if(!dynamicOK)fail.push('redirect');};document.head.appendChild(el);
let missing=document.createElement('script');missing.src='/gone.js';missing.onerror=()=>{window.missingOK=true;};document.head.appendChild(missing);
fetch('/api').then(r=>{if(r.headers.has('set-cookie'))fail.push('header exposure');return r.text();}).then(t=>{window.apiOK=t==='network 中文';});
let xhr=new XMLHttpRequest();xhr.open('POST','/posted');xhr.onload=()=>{window.postOK=xhr.responseText.includes('wd=test');};xhr.send('wd=test');
setTimeout(()=>{if(!domReady||!asyncBeforeLoad||!window.dynamicOK||!window.missingOK||!window.apiOK||!window.postOK||window.badScript)fail.push('lifecycle/network');
if(order.indexOf('blocking')>order.indexOf('inline'))fail.push('blocking ordering');document.title=fail.length?'FAIL '+fail.join(',')+' '+[domReady,asyncBeforeLoad,window.dynamicOK,window.missingOK,window.apiOK,window.postOK].join('/')+' '+order.join('/'):'Integration PASS';},600);
</script>''')
        else:
            self.reply('<title>Unknown ' + route + '</title>', 404)


with tempfile.TemporaryDirectory(prefix='dillo-js-') as work:
    server = Server(('127.0.0.1', 0), Handler)
    other = Server(('127.0.0.1', 0), Handler)
    server.other = other
    other.other = server
    threading.Thread(target=other.serve_forever, daemon=True).start()
    threading.Thread(target=server.serve_forever, daemon=True).start()
    r, w = os.pipe()
    xlog = (pathlib.Path(work) / 'xvfb.log').open('w')
    x = subprocess.Popen(['Xvfb', '-ac', '-noreset', '-nolisten', 'tcp', '-displayfd', str(w), '-screen', '0', '1024x768x24'], pass_fds=(w,), stdout=xlog, stderr=xlog)
    os.close(w)
    with os.fdopen(r) as ready:
        display = ready.readline().strip()
    env = os.environ.copy()
    # Match the existing HTML suite: Fontconfig keeps process-global caches.
    env['LSAN_OPTIONS'] = 'suppressions=' + str(pathlib.Path(__file__).with_name('lsan.supp').resolve())
    env['DISPLAY'] = ':' + display
    base = 'http://127.0.0.1:' + str(server.server_port)
    logpath = pathlib.Path(work) / 'browser.log'
    log = logpath.open('w')
    browser = subprocess.Popen([str(bindir / 'dillo'), base + '/'], env=env, stdout=log, stderr=log)
    env['DILLO_PID'] = str(browser.pid)

    def command(*args):
        return subprocess.run([str(bindir / 'dilloc'), *args], env=env, capture_output=True, text=True, timeout=4)

    def title(expected):
        deadline = time.monotonic() + 12
        result = None
        while time.monotonic() < deadline and browser.poll() is None:
            result = command('title')
            if result.returncode == 0 and result.stdout.strip() == expected:
                return
            if result.stdout.startswith('FAIL') or result.stdout.startswith('Form FAIL'):
                break
            time.sleep(.05)
        raise AssertionError('expected title ' + expected + ', got ' + repr(result.stdout if result else 'browser exited'))

    try:
        title('Integration PASS')
        command('open', base + '/network-tests')
        title('Network PASS')
        command('open', base + '/styles-controls')
        title('Styles PASS')
        if shutil.which('xdotool'):
            subprocess.run(['xdotool', 'type', 'X'], env=env, check=True)
            title('Selection input PASS')
        command('open', base + '/layout')
        title('Layout PASS')
        assert command('eval', 'document.title').stdout.strip() == 'Layout PASS'
        command('open', base + '/position')
        title('Position PASS')
        command('open', base + '/cssom')
        title('CSSOM PASS')
        command('open', base + '/history')
        title('History PASS')
        command('open', base + '/second')
        title('Storage PASS')
        command('open', base + '/script-form')
        title('POST wd=hello')
        command('open', base + '/leave')
        title('Plain PASS')
        time.sleep(.4)
        title('Plain PASS')
        if shutil.which('xdotool'):
            command('open', base + '/buttons')
            title('Buttons ready')
            win = subprocess.check_output(['xdotool', 'search', '--pid', str(browser.pid), '--name', 'Buttons ready'], env=env, text=True).splitlines()[0]
            for selector in ['#outside', '#label', '#inside']:
                rect = json.loads(command('eval', 'JSON.stringify(document.querySelector(' + json.dumps(selector) + ').getBoundingClientRect())').stdout)
                subprocess.run(['xdotool', 'mousemove', '--window', win, str(int(rect['x'] + rect['width'] / 2)), str(int(rect['y'] + rect['height'] / 2 + 74)), 'click', '1'], env=env, check=True)
            title('Buttons PASS')
            rect = json.loads(command('eval', "JSON.stringify(document.querySelector('#empty').getBoundingClientRect())").stdout)
            subprocess.run(['xdotool', 'mousemove', '--window', win, str(int(rect['x'] + 20)), str(int(rect['y'] + 90)), 'click', '1'], env=env, check=True)
            title('Plain PASS')
            command('open', base + '/paging')
            title('Page 0')
            def click_link(selector):
                rect = json.loads(command('eval', 'JSON.stringify(document.querySelector(' + json.dumps(selector) + ').getBoundingClientRect())').stdout)
                subprocess.run(['xdotool', 'mousemove', '--window', win,
                                str(int(rect['x'] + rect['width'] / 2)),
                                str(int(rect['y'] + rect['height'] / 2 + 74)), 'click', '1'], env=env, check=True)
            click_link('#next')
            title('Page 1')
            click_link('#result')
            title('Plain PASS')
            subprocess.run(['xdotool', 'mousemove', '--window', win, '22', '45', 'click', '1'], env=env, check=True)
            title('Page 1')
            assert command('url').stdout.strip() == base + '/paging?pn=1', 'Back lost pagination URL'
            if shutil.which('xsel'):
                command('open', base + '/clipboard')
                title('Clipboard ready')
                command('eval', "document.querySelector('#paste').focus()")
                subprocess.run(['xsel', '--clipboard', '--input'], input='中文 paste', text=True, env=env, check=True)
                subprocess.run(['xdotool', 'key', '--clearmodifiers', 'ctrl+v'], env=env, check=True)
                title('Clipboard PASS')
            command('open', base + '/events')
            title('Events ready')
            win = subprocess.check_output(['xdotool', 'search', '--pid', str(browser.pid), '--name', 'Events ready'], env=env, text=True).splitlines()[0]
            subprocess.run(['xdotool', 'mousemove', '--window', win, '45', '90', 'click', '1'], env=env, check=True)
            title('Click PASS')
            subprocess.run(['xdotool', 'mousemove', '--window', win, '45', '118', 'click', '1', 'type', 'hello'], env=env, check=True)
            subprocess.run(['xdotool', 'key', 'Return'], env=env, check=True)
            title('Form PASS')
        command('quit')
        assert browser.wait(timeout=4) == 0, 'browser exit failed'
        log.flush()
        assert 'ERROR: AddressSanitizer' not in logpath.read_text()
        print('PASS: scripts, lifecycle, networking, storage, forms and navigation')
    except Exception:
        log.flush()
        xlog.flush()
        print(logpath.read_text())
        print((pathlib.Path(work) / 'xvfb.log').read_text())
        print('browser status', browser.poll(), flush=True)
        if browser.poll() is None:
            print(command('domdump').stdout, flush=True)
        if shutil.which('import'):
            subprocess.run(['import','-display',env['DISPLAY'],'-window','root','/tmp/dillo-js-integration-failed.png'], capture_output=True, timeout=3)
        raise
    finally:
        if browser.poll() is None:
            browser.terminate()
            browser.wait(timeout=4)
        x.terminate()
        x.wait(timeout=4)
        log.close()
        xlog.close()
        server.shutdown()
        other.shutdown()
