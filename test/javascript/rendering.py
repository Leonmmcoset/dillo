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

            structural = evaluate('["nth-last-child","nth-last-of-type","last-of-type","last-child","only-child-target","only-of-type-target"].map(id=>getComputedStyle(document.getElementById(id)).backgroundColor)')
            check(structural == ['#008000'] * 6,
                  'last and only child/type selectors match in reverse sibling order', structural)
            font = evaluate('(s=>({size:s.fontSize,weight:s.fontWeight,style:s.fontStyle,variant:s.fontVariant,lineHeight:s.lineHeight}))(getComputedStyle(document.getElementById("font-shorthand")))')
            check(font == {'size':'20px','weight':'700','style':'italic','variant':'small-caps','lineHeight':'30px'},
                  'font shorthand applies optional fields and unitless line-height', font)
            font_order = evaluate('(s=>({size:s.fontSize,weight:s.fontWeight,style:s.fontStyle,lineHeight:s.lineHeight}))(getComputedStyle(document.getElementById("font-order")))')
            check(font_order == {'size':'20px','weight':'700','style':'italic','lineHeight':'25px'},
                  'font shorthand accepts reordered optional fields and quoted families', font_order)
            font_reset = evaluate('(s=>({size:s.fontSize,weight:s.fontWeight,style:s.fontStyle,variant:s.fontVariant,lineHeight:s.lineHeight}))(getComputedStyle(document.getElementById("font-reset")))')
            check(font_reset == {'size':'16px','weight':'400','style':'normal','variant':'normal','lineHeight':'normal'},
                  'font shorthand resets omitted longhands', font_reset)
            invalid_weight = evaluate('getComputedStyle(document.getElementById("font-invalid")).fontWeight')
            check(invalid_weight == '700', 'font shorthand without a family is rejected atomically', invalid_weight)
            escaped = evaluate('[CSS.escape("1a"),CSS.escape("-2a"),CSS.escape("-"),CSS.escape("a:b")]')
            check(escaped == ['\\31 a','-\\32 a','\\-','a\\:b'],
                  'CSS.escape handles leading digits and punctuation', escaped)
            class_queries = evaluate('[document.getElementsByClassName("").length,document.getElementsByClassName("   ").length]')
            check(class_queries == [0,0], 'empty live class collections do not create invalid selectors', class_queries)
            signal_listeners = evaluate('(()=>{const t=new EventTarget(),c=new AbortController();let n=0;t.addEventListener("ping",()=>c.abort());t.addEventListener("ping",()=>n++,{signal:c.signal});t.dispatchEvent(new Event("ping"));return n})()')
            check(signal_listeners == 0, 'AbortSignal removes listeners before their turn in dispatch', signal_listeners)
            passive = evaluate('(()=>{const t=new EventTarget(),events=[];t.addEventListener("wheel",e=>{e.preventDefault();events.push(e.defaultPrevented)},{passive:true});t.addEventListener("wheel",e=>{events.push(e.defaultPrevented);e.preventDefault();events.push(e.defaultPrevented)});return [t.dispatchEvent(new Event("wheel",{cancelable:true})),events]})()')
            check(passive == [False,[False,False,True]], 'passive listeners cannot cancel an event and do not affect later active listeners', passive)
            signals = evaluate('(()=>{const a=new AbortController(),b=new AbortController(),s=AbortSignal.any([a.signal,b.signal,a.signal]),reason={why:"cancel"};let n=0;s.onabort=()=>n++;b.abort(reason);a.abort("late");let thrown=false;try{s.throwIfAborted()}catch(e){thrown=e===reason}const immediate=AbortSignal.abort(),ready=AbortSignal.any([immediate]);return [s.aborted,s.reason===reason,n,thrown,immediate.reason.name,ready.reason===immediate.reason,AbortSignal.any([]).aborted]})()')
            check(signals == [True,True,1,True,'AbortError',True,False],
                  'AbortSignal factories combine cancellation, preserve reasons and dispatch once', signals)
            signal_errors = evaluate('(()=>{let n=0;for(const f of [()=>AbortSignal.any([{}]),()=>AbortSignal.any({}),()=>AbortSignal.timeout(-1),()=>AbortSignal.timeout(Infinity)])try{f()}catch(e){n++}return n})()')
            check(signal_errors == 4, 'AbortSignal rejects invalid signal collections and timeout values', signal_errors)
            evaluate('(()=>{const s=AbortSignal.timeout(1);window.timeoutProbe=s;s.onabort=()=>document.title=s.reason.name;return s.aborted})()')
            deadline = time.monotonic() + 5
            while time.monotonic() < deadline:
                if command('title') == 'TimeoutError':
                    break
                time.sleep(.05)
            check(evaluate('timeoutProbe.aborted&&timeoutProbe.reason.name==="TimeoutError"'),
                  'AbortSignal.timeout asynchronously aborts with TimeoutError')
            command('eval', 'document.title="Renderer ready"')

            root_scroll = evaluate('[document.scrollingElement===document.documentElement,document.documentElement.scrollWidth,document.documentElement.scrollHeight,document.body.scrollHeight,document.documentElement.clientWidth,document.documentElement.clientHeight,innerWidth,innerHeight]')
            check(root_scroll[0] and root_scroll[1] == 1500 and root_scroll[2] > 2000 and
                  root_scroll[2] == root_scroll[3] and root_scroll[4:6] == root_scroll[6:8],
                  'document scrollingElement reports page extents separately from viewport dimensions', root_scroll)
            client_box = evaluate('(()=>{const e=document.getElementById("scroll-client");return [e.offsetWidth,e.offsetHeight,e.clientWidth,e.clientHeight,e.scrollWidth,e.scrollHeight]})()')
            check(client_box == [144,104,134,94,134,94],
                  'client dimensions exclude borders while non-root scroll dimensions stay within the box', client_box)
            command('eval', 'document.documentElement.scrollTop=400;document.body.scrollLeft=120')
            immediate_offsets=evaluate('[document.documentElement.scrollTop,document.body.scrollTop,document.documentElement.scrollLeft,scrollX,scrollY]')
            check(immediate_offsets == [400,400,120,120,400],
                  'consecutive root scroll setters expose the combined pending position immediately', immediate_offsets)
            command('wait', '5')
            root_offsets = evaluate('[document.documentElement.scrollTop,document.body.scrollTop,document.documentElement.scrollLeft,scrollX,scrollY]')
            check(root_offsets == [400,400,120,120,400], 'root scroll offsets reflect writes through body or documentElement', root_offsets)
            command('eval', 'document.body.scrollTop=0;document.documentElement.scrollLeft=0')
            command('wait', '5')
            command('eval', 'window.scrollEvents=[];addEventListener("scroll",()=>scrollEvents.push(["window",scrollX,scrollY]));document.addEventListener("scroll",()=>scrollEvents.push(["document",scrollX,scrollY]));scrollTo(20,30);scrollBy(10,15)')
            deadline=time.monotonic()+5
            while time.monotonic()<deadline and evaluate('scrollEvents.length')<2:
                time.sleep(.025)
            scroll_events=evaluate('scrollEvents')
            check(scroll_events == [['window',30,45],['document',30,45]],
                  'programmatic scrolling coalesces native offsets and dispatches window and document events', scroll_events)
            command('eval', 'scrollTo(0,0)')
            command('wait', '5')
            target_scroll=evaluate('(()=>{const e=document.getElementById("scroll-target"),r=e.getBoundingClientRect(),x=Math.trunc(r.left+r.width/2-innerWidth/2),y=Math.trunc(r.top+r.height/2-innerHeight/2);e.scrollIntoView({block:"center",inline:"center",behavior:"smooth"});return [scrollX,scrollY,x,y]})()')
            check(target_scroll[0:2] == target_scroll[2:4],
                  'scrollIntoView centers a target on both axes and accepts smooth behavior', target_scroll)
            command('wait', '5')
            end_scroll=evaluate('(()=>{const e=document.getElementById("scroll-target"),r=e.getBoundingClientRect(),y=Math.trunc(scrollY+r.bottom-innerHeight);e.scrollIntoView(false);return [scrollY,y]})()')
            check(end_scroll[0] == end_scroll[1],
                  'legacy false scrollIntoView argument aligns the target with the viewport bottom', end_scroll)
            command('wait', '5')
            nearest_scroll=evaluate('(()=>{const before=[scrollX,scrollY];document.getElementById("scroll-target").scrollIntoView({block:"nearest",inline:"nearest"});return [before,scrollX,scrollY]})()')
            check(nearest_scroll == [[nearest_scroll[1],nearest_scroll[2]],nearest_scroll[1],nearest_scroll[2]],
                  'nearest alignment leaves a fully visible target in place', nearest_scroll)
            edge_scroll=evaluate('(()=>{const e=document.getElementById("scroll-target"),r=e.getBoundingClientRect(),x=Math.max(0,Math.min(document.documentElement.scrollWidth-innerWidth,Math.trunc(scrollX+r.right-innerWidth))),y=Math.max(0,Math.min(document.documentElement.scrollHeight-innerHeight,Math.trunc(scrollY+r.top)));e.scrollIntoView({block:"start",inline:"end"});return [scrollX,scrollY,x,y]})()')
            check(edge_scroll[0:2] == edge_scroll[2:4],
                  'start/end options align the corresponding target edges', edge_scroll)
            default_scroll=evaluate('(()=>{const before=[scrollX,scrollY];document.getElementById("scroll-target").scrollIntoView();return [before,scrollX,scrollY]})()')
            check(default_scroll == [[default_scroll[1],default_scroll[2]],default_scroll[1],default_scroll[2]],
                  'scrollIntoView defaults to block start and inline nearest', default_scroll)
            invalid_scroll=evaluate('(()=>{let n=0;for(const options of [{block:"middle"},{inline:"middle"},{behavior:"later"}])try{document.getElementById("scroll-target").scrollIntoView(options)}catch(e){if(e instanceof TypeError)n++}return n})()')
            check(invalid_scroll == 3, 'scrollIntoView rejects unsupported alignment and behavior values', invalid_scroll)
            command('eval', 'scrollTo(0,0)')
            command('wait', '5')

            grid = evaluate('(()=>{const a=document.getElementById("grid-first").getBoundingClientRect(),b=document.getElementById("grid-second").getBoundingClientRect();return [a.x,a.width,b.x,b.width]})()')
            check(grid == [500,97,770,30], 'grid minmax/fr tracks and justify-self place items in their tracks', grid)
            auto_grid = evaluate('(()=>{const f=document.getElementById("fit-first").getBoundingClientRect(),s=document.getElementById("fit-second").getBoundingClientRect(),a=document.getElementById("fill-first").getBoundingClientRect(),b=document.getElementById("fill-second").getBoundingClientRect();return [f.x,f.width,s.x,s.width,a.x,a.width,b.x,b.width]})()')
            check(auto_grid == [20,315,345,315,20,207,237,206],
                  'grid auto-fit collapses unused tracks while auto-fill keeps them', auto_grid)
            flex_wrapping = evaluate('["flex-heading","flex-heading-link","flex-summary"].map(id=>document.getElementById(id).getBoundingClientRect())')
            heading, link, summary = flex_wrapping
            check(link['height'] > 24 and heading['height'] >= link['height'] and summary['top'] >= link['bottom'],
                  'nested flex rows remeasure wrapped descendants and keep following summaries below the title', flex_wrapping)
            float_columns = evaluate('["float-columns","float-sidebar","float-main","float-nowrap","float-summary"].map(id=>document.getElementById(id).getBoundingClientRect())')
            columns, sidebar, main, nowrap, summary = float_columns
            check(main['width'] == 200 and main['top'] == sidebar['top'] and main['right'] <= sidebar['left'],
                  'fixed float columns stay beside each other when a nested flex title has long nowrap text', float_columns)
            check(nowrap['width'] == 200 and nowrap['height'] == 24 and summary['top'] >= nowrap['bottom'],
                  'nested nowrap title clips within its flex column and leaves space for the summary', float_columns)
            flex_overflow = evaluate('["flex-overflow-hidden","flex-overflow-fixed"].map(id=>document.getElementById(id).getBoundingClientRect().width)')
            check(flex_overflow == [150,50], 'hidden overflow removes the automatic flex minimum for a long title', flex_overflow)
            block_overflow = evaluate('["fixed-overflow","fixed-overflow-block"].map(id=>document.getElementById(id).getBoundingClientRect().width)')
            check(block_overflow == [180,180], 'nowrap block fills its fixed containing box without enlarging it', block_overflow)
            command('eval', 'window.flexAutoProbe=(parent,first,second)=>{const p=document.getElementById("flex-auto"),a=document.getElementById("flex-auto-a"),b=document.getElementById("flex-auto-b");p.style.cssText="position:absolute;left:20px;top:1500px;display:flex;width:200px;height:100px;"+parent;a.style.cssText="width:40px;height:20px;flex-shrink:0;"+first;b.style.cssText="width:40px;height:20px;flex-shrink:0;"+second;const r=p.getBoundingClientRect();return [a,b].map(e=>{const b=e.getBoundingClientRect();return [b.x-r.x,b.y-r.y,b.width,b.height]})}')
            auto_end = evaluate('flexAutoProbe("justify-content:center","","margin-left:auto")')
            check(auto_end == [[0,0,40,20],[160,0,40,20]],
                  'flex automatic margins consume free space before justify-content', auto_end)
            auto_center = evaluate('flexAutoProbe("","","margin-left:auto;margin-right:auto")')
            check(auto_center == [[0,0,40,20],[100,0,40,20]],
                  'both automatic main margins share the free space', auto_center)
            auto_reverse = evaluate('flexAutoProbe("flex-direction:row-reverse","","margin-right:auto")')
            check(auto_reverse == [[160,0,40,20],[0,0,40,20]],
                  'reverse flex directions resolve physical automatic margins on the correct side', auto_reverse)
            auto_column = evaluate('flexAutoProbe("flex-direction:column","","margin-top:auto;margin-left:auto;margin-right:auto")')
            check(auto_column == [[0,0,40,20],[80,80,40,20]],
                  'column flex items support automatic margins on both axes', auto_column)
            auto_natural = evaluate('flexAutoProbe("flex-direction:column","","width:auto;padding:0 10px;margin-left:auto")')
            check(auto_natural[1] == [180,20,20,20],
                  'cross-axis automatic margins suppress stretch and retain natural item width', auto_natural)
            auto_cross = evaluate('flexAutoProbe("align-items:flex-end","","margin-top:auto;margin-bottom:auto")')
            check(auto_cross == [[0,80,40,20],[40,40,40,20]],
                  'cross-axis automatic margins center an item independently of align-items', auto_cross)
            auto_overflow = evaluate('flexAutoProbe("width:60px","","margin-left:auto")')
            check(auto_overflow == [[0,0,40,20],[40,0,40,20]],
                  'overflowing flex lines resolve automatic margins to zero', auto_overflow)
            auto_grow = evaluate('flexAutoProbe("","flex-grow:1","margin-left:auto")')
            check(auto_grow == [[0,0,160,20],[160,0,40,20]],
                  'flex growth consumes free space before automatic margins', auto_grow)
            centered_containers = evaluate('["margin-flex","margin-grid"].map(id=>document.getElementById(id).getBoundingClientRect().x)')
            check(centered_containers == [500,500],
                  'block-level flex and grid containers center with horizontal automatic margins', centered_containers)
            class_list = evaluate('(()=>{const e=document.createElement("div");e.className="x x y";const c=e.classList;c.add("z");const same=c.replace("x","x");c.replace("x","y");let errors=[];for(const token of ["","a b"])try{c.add(token)}catch(error){errors.push(error.name)}return [e.classList===c,same,e.className,c.length,[...c],errors]})()')
            check(class_list == [True,True,'y z',2,['y','z'],['SyntaxError','InvalidCharacterError']],
                  'classList is live, de-duplicates tokens and validates mutations', class_list)
            token_index = evaluate('(()=>{const e=document.createElement("div");e.className="a x b y";const t=e.classList;t.replace("a","b");return [t[0],t[1],t[3],"2" in t,"3" in t,Object.keys(t),e.className,t instanceof DOMTokenList]})()')
            check(token_index == ['b','x',None,True,False,['0','1','2'],'b x y',True],
                  'DOMTokenList exposes live indexed keys and replacement preserves the earliest token position', token_index)
            dataset = evaluate('(()=>{const e=document.createElement("div");e.dataset.pageId="42";e.setAttribute("data-ready","yes");const keys=Object.keys(e.dataset).sort();return [e.dataset.pageId,e.dataset.ready,"ready" in e.dataset,keys,Object.entries(e.dataset).sort()]})()')
            check(dataset == ['42','yes',True,['pageId','ready'],[['pageId','42'],['ready','yes']]],
                  'dataset reflects live data attributes and enumerates camel-cased keys', dataset)
            dataset_names = evaluate('(()=>{const e=document.createElement("div"),d=e.dataset;d[""]="empty";d.pageId="42";let error="";try{d["page-id"]="bad"}catch(e){error=e.name}delete d.pageId;return [d===e.dataset,d[""],d.pageId,error,Object.keys(d),Object.prototype.toString.call(d),d.hasOwnProperty("")]})()')
            check(dataset_names == [True,'empty',None,'SyntaxError',[''],'[object DOMStringMap]',True],
                  'dataset keeps its identity, supports empty names and rejects ambiguous dashed keys', dataset_names)
            query_params = evaluate('(()=>{const p=new URLSearchParams("b=2&a=1&b=3&space=a+b&special=!*~");p.set("b","4");const before=[p.toString(),p.size,p.getAll("b")];p.sort();return [before,p.toString(),p.has("b","4"),p.delete("b","4"),p.has("b")]})()')
            check(query_params == [['b=4&a=1&space=a+b&special=%21*%7E',4,['4']],
                  'a=1&b=4&space=a+b&special=%21*%7E',True,None,False],
                  'URLSearchParams preserves set order and form-encodes values', query_params)
            supports = evaluate('[CSS.supports("display","grid"),CSS.supports("(display: grid) and (gap: 8px)"),CSS.supports("grid-template-columns","repeat(auto-fit,minmax(200px,1fr))"),CSS.supports("display: masonry"),CSS.supports("unknown-property","1px")]')
            check(supports == [True,True,True,False,False], 'CSS.supports reports implemented layout declarations', supports)
            numeric_supports = evaluate('[CSS.supports("font-size","20px"),CSS.supports("line-height","1.5"),CSS.supports("display","20px")]')
            check(numeric_supports == [True,True,False], 'CSS.supports accepts numeric font sizes and line heights', numeric_supports)
            text_supports = evaluate('[CSS.supports("word-break","break-all"),CSS.supports("overflow-wrap","anywhere"),CSS.supports("word-wrap","break-word")]')
            check(text_supports == [True,True,True], 'CSS.supports recognizes implemented text wrapping properties', text_supports)
            conditional_styles = evaluate('[getComputedStyle(document.getElementById("supports-declaration")).backgroundColor,getComputedStyle(document.getElementById("supports-and")).backgroundColor]')
            check(conditional_styles == ['#008050','#008050'], '@supports evaluates declarations and compound conditions', conditional_styles)
            visibility = evaluate('[getComputedStyle(document.getElementById("visibility-hidden")).visibility,getComputedStyle(document.getElementById("visibility-restored")).visibility,document.getElementById("visibility-hidden").getBoundingClientRect().height,CSS.supports("visibility","hidden")]')
            check(visibility == ['hidden','visible',30,True],
                  'visibility is inherited and preserves layout while visible descendants override it', visibility)
            color(picture, 'visibility-hidden', 10, 15, (255,255,255), 'hidden boxes do not paint backgrounds or text')
            color(picture, 'visibility-restored', 10, 15, (0,128,80), 'visible descendants paint inside hidden parents')
            color(picture, 'visibility-image', 10, 15, (255,255,255), 'visibility hides image pixels')
            color(picture, 'visibility-control', 10, 15, (255,255,255), 'visibility hides native controls')
            inline = rect('visibility-inline')
            hidden_pixels = [pixel(picture, origin[0]+inline['left']+x, origin[1]+inline['top']+y)
                             for y in range(25) for x in range(120)]
            check(all(p == (255,255,255) for p in hidden_pixels), 'visibility hides inline text and backgrounds')
            visibility_hits = evaluate('[document.elementFromPoint(460,555)?.id,document.elementFromPoint(540,555)?.id,document.elementFromPoint(890,465)?.id]')
            check(visibility_hits[0] != 'visibility-hidden' and visibility_hits[1] == 'visibility-restored' and
                  visibility_hits[2] != 'visibility-control', 'hidden boxes and controls do not receive hits; restored descendants do', visibility_hits)
            visibility_keywords = evaluate('(()=>{const e=document.getElementById("visibility-restored"),values=[];for(const value of ["unset","initial","inherit","visible"]){e.style.visibility=value;values.push(getComputedStyle(e).visibility)}return values})()')
            check(visibility_keywords == ['hidden','visible','hidden','visible'],
                  'visibility handles initial, inherit and unset without removing descendant layout', visibility_keywords)
            wrapping = evaluate('[document.getElementById("word-wrap-anywhere").getBoundingClientRect().height,document.getElementById("word-break-all").getBoundingClientRect().height,getComputedStyle(document.getElementById("word-wrap-anywhere")).overflowWrap,getComputedStyle(document.getElementById("word-break-all")).wordBreak]')
            check(wrapping[0] > 20 and wrapping[1] > 20 and wrapping[2:] == ['anywhere','break-all'],
                  'overflow-wrap and word-break wrap long identifiers inside narrow columns', wrapping)
            cjk_wrapping = evaluate('[document.getElementById("word-keep-all").getBoundingClientRect().height,document.getElementById("word-cjk-normal").getBoundingClientRect().height]')
            check(cjk_wrapping[0] == 20 and cjk_wrapping[1] > 20,
                  'word-break keep-all suppresses automatic CJK breaks while normal text wraps', cjk_wrapping)
            webkit_layout = evaluate('(()=>{const c=document.getElementById("webkit-clamp").getBoundingClientRect(),n=document.getElementById("webkit-nowrap").getBoundingClientRect();return [c.height,n.height,CSS.supports("display","-webkit-box"),CSS.supports("-webkit-line-clamp","2"),CSS.supports("-webkit-box-orient","vertical")]})()')
            check(webkit_layout[0] == 40 and 0 < webkit_layout[1] <= 22 and webkit_layout[2:] == [True,True,True],
                  'WebKit box aliases clamp text and nowrap keeps result titles to one line', webkit_layout)
            short_clamp = evaluate('["webkit-short","webkit-inert","webkit-none"].map(id=>document.getElementById(id).getBoundingClientRect().height)')
            check(short_clamp[0] == 20 and short_clamp[1] > 40 and short_clamp[2] > 40,
                  'line clamp keeps short text natural, requires the prefixed box layout and accepts none', short_clamp)
            clamp_support = evaluate('[CSS.supports("-webkit-line-clamp","none"),CSS.supports("-webkit-line-clamp","0"),CSS.supports("-webkit-line-clamp","101")]')
            check(clamp_support == [True,False,False], 'line-clamp feature detection matches the implemented line limit', clamp_support)
            dom_helpers = evaluate('(()=>{const p=document.createElement("div"),a=document.createElement("i"),b=document.createElement("b");p.append(a);const replaced=p.replaceChild(a,a)===a,inserted=a.insertAdjacentElement("afterend",b)===b;return [replaced,inserted,p.firstChild===a,p.lastChild===b,a.getAttributeNames()]})()')
            check(dom_helpers == [True,True,True,True,[]], 'DOM replacement, insertion and attribute APIs preserve node identity', dom_helpers)
            mutation_title = '[["id",null],["class",null],["class","old"]]'
            evaluate('(()=>{const e=document.createElement("div"),o=new MutationObserver(records=>{document.title=JSON.stringify(records.map(r=>[r.attributeName,r.oldValue]))});e.id="first";o.observe(e,{attributes:true,attributeFilter:["id"]});e.id="second";o.observe(e,{attributes:true,attributeOldValue:true});e.className="old";e.className="new";return true})()')
            deadline = time.monotonic() + 5
            observed = None
            while time.monotonic() < deadline:
                observed = command('title')
                if observed == mutation_title:
                    break
                time.sleep(.05)
            check(observed == mutation_title,
                  'MutationObserver replaces registrations, filters attributes and retains requested old values', observed)
            observer_errors = evaluate('(()=>{const e=document.createElement("div"),o=new MutationObserver(()=>{});let n=0;for(const options of [{},{attributes:false,attributeOldValue:true}])try{o.observe(e,options)}catch(error){if(error instanceof TypeError)n++}return n})()')
            check(observer_errors == 2, 'MutationObserver rejects empty and inconsistent observe options', observer_errors)
            command('eval', 'document.title="Renderer ready"')

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
            clamp = rect('webkit-clamp')
            clipped = [pixel(picture, origin[0]+clamp['left']+x, origin[1]+clamp['top']+42+y)
                       for y in range(4) for x in range(130)]
            check(all(p == (255,255,255) for p in clipped),
                  'WebKit line clamp clips text after its second line')
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
                ('multiple-layers', 'background:blue;background-image:linear-gradient(red,red),linear-gradient(lime,lime)',
                 [(50,25,(255,0,0))]),
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

            cases = [
                ('baidu-border', 'border:6px solid transparent;border-radius:18px;background:#4e6ef2;background:linear-gradient(white,white) padding-box,linear-gradient(160deg,#3377fe,#4c6fff,#8370ff,#ba59ff) border-box',
                 [(50,25,(255,255,255)), (0,0,(255,255,255))]),
                ('layer-alpha', 'background:linear-gradient(#ff000080,#ff000080),linear-gradient(blue,blue)',
                 [(50,25,(128,0,127))]),
                ('none-layers', 'background:blue;background-image:none,linear-gradient(red,red),none',
                 [(50,25,(255,0,0))]),
                ('clip-content', 'border:4px solid transparent;padding:10px;background:red;background-clip:content-box',
                 [(50,25,(255,0,0)), (5,25,(255,255,255)), (1,25,(255,255,255))]),
                ('clip-padding', 'border:4px solid transparent;padding:10px;background:red;background-clip:padding-box',
                 [(50,25,(255,0,0)), (5,25,(255,0,0)), (1,25,(255,255,255))]),
                ('clip-border', 'border:4px solid transparent;padding:10px;background:red',
                 [(50,25,(255,0,0)), (1,25,(255,0,0))]),
                ('layer-clip-list', 'border:6px solid transparent;padding:6px;background-image:linear-gradient(red,red),linear-gradient(blue,blue);background-clip:content-box,border-box',
                 [(50,25,(255,0,0)), (1,25,(0,0,255)), (8,25,(0,0,255))]),
                ('layer-clip-repeat', 'border:6px solid transparent;padding:6px;background-image:linear-gradient(red,red),linear-gradient(lime,lime),linear-gradient(blue,blue);background-clip:content-box,padding-box',
                 [(50,25,(255,0,0)), (8,25,(0,255,0)), (1,25,(255,255,255))]),
                ('layer-last-color', 'border:6px solid transparent;padding:6px;background:linear-gradient(transparent,transparent) border-box,blue content-box',
                 [(50,25,(0,0,255)), (8,25,(255,255,255)), (1,25,(255,255,255))]),
                ('two-boxes', 'border:6px solid transparent;padding:6px;background:linear-gradient(red,red) border-box content-box',
                 [(50,25,(255,0,0)), (8,25,(255,255,255))]),
                ('origin-border', 'border:10px solid transparent;background:linear-gradient(90deg,red,blue);background-origin:border-box',
                 [(1,25,(251,0,4)), (98,25,(4,0,251))]),
                ('origin-content', 'border:5px solid transparent;padding:15px;background:linear-gradient(90deg,red,blue);background-origin:content-box;background-clip:content-box',
                 [(21,25,(249,0,6)), (78,25,(6,0,249)), (10,25,(255,255,255))]),
                ('single-url-clip', 'border:5px solid transparent;padding:10px;background:url(tile.svg);background-clip:content-box;background-origin:content-box',
                 [(50,25,(224,32,48)), (8,25,(255,255,255))]),
                ('layer-important', 'background:linear-gradient(red,red) padding-box,linear-gradient(blue,blue) border-box!important;background:lime',
                 [(50,25,(255,0,0))]),
                ('layer-invalid-box', 'background:blue;background:linear-gradient(red,red) padding-box content-box border-box',
                 [(50,25,(0,0,255))]),
                ('layer-color-first', 'background:blue;background:red,linear-gradient(lime,lime)',
                 [(50,25,(0,0,255))]),
                ('layer-trailing-comma', 'background:blue;background:linear-gradient(red,red),',
                 [(50,25,(0,0,255))]),
                ('layer-unknown', 'background:blue;background:linear-gradient(red,red),unknown',
                 [(50,25,(0,0,255))]),
                ('layer-url-supported', 'background:blue;background:url(tile.svg),linear-gradient(red,red)',
                 [(50,25,(224,32,48))]),
                ('clip-invalid-list', 'border:5px solid transparent;background:red;background-clip:content-box,invalid',
                 [(1,25,(255,0,0))]),
                ('layer-reset', 'border:5px solid transparent;background:linear-gradient(red,red) content-box,linear-gradient(blue,blue) content-box;background:lime',
                 [(1,25,(0,255,0))]),
                ('layer-clip-initial', 'border:5px solid transparent;background:red;background-clip:content-box;background-clip:initial',
                 [(1,25,(255,0,0))]),
                ('layer-variables', '--layers:linear-gradient(red,red) content-box,linear-gradient(blue,blue) border-box;border:5px solid transparent;padding:5px;background:var(--layers)',
                 [(50,25,(255,0,0)), (1,25,(0,0,255))]),
                ('layer-limit', 'background:blue;background-image:' + ','.join(['linear-gradient(red,red)']*9),
                 [(50,25,(0,0,255))]),
                ('layer-max', 'background-image:' + ','.join(['none']*7+['linear-gradient(red,red)']),
                 [(50,25,(255,0,0))]),
                ('solid-border-above', 'border:6px solid lime;background:linear-gradient(red,red) border-box',
                 [(50,25,(255,0,0)), (1,25,(0,255,0))]),
            ]
            markup = '<div id=origin></div>'
            for n, (id, css, samples) in enumerate(cases):
                markup += '<div id="layers-' + id + '" style="position:absolute;box-sizing:border-box;left:' + str(20+155*(n%5)) + 'px;top:' + str(20+80*(n//5)) + 'px;width:100px;height:50px;' + css + '"></div>'
            command('eval', 'document.body.innerHTML=' + json.dumps(markup))
            picture = screenshot('layered-backgrounds')
            for id, css, samples in cases:
                for x,y,expected in samples:
                    color(picture, 'layers-'+id, x, y, expected, 'layered background '+id+' at '+str((x,y)), 6)
            box = rect('layers-baidu-border')
            edge = pixel(picture, origin[0]+box['left']+50, origin[1]+box['top']+2)
            check(edge[2] > 200 and edge[0] < 210 and edge[1] < 180, 'Baidu border has gradient at the edge and white in the center', edge)
            check(evaluate('getComputedStyle(document.getElementById("layers-two-boxes")).backgroundOrigin') == 'border-box', 'computed background origin reflects shorthand')
            check(evaluate('getComputedStyle(document.getElementById("layers-layer-clip-repeat")).backgroundClip') == 'content-box,padding-box,content-box', 'computed background clip repeats its list for each image')
            command('eval', '''document.body.innerHTML='<div id=origin></div><div style="position:absolute;left:20px;top:20px;width:120px;height:70px;border:5px solid transparent;padding:10px;background:linear-gradient(red,red) content-box,linear-gradient(blue,blue) border-box"><div id=layer-inherited style="width:100px;height:50px;border:5px solid transparent;padding:5px;box-sizing:border-box;background:inherit"></div></div>' ''')
            picture = screenshot('layered-inheritance')
            color(picture, 'layer-inherited', 50, 25, (255,0,0), 'background inherit preserves all image layers')
            color(picture, 'layer-inherited', 1, 25, (0,0,255), 'background inherit preserves layer clips')
            check(evaluate('getComputedStyle(document.getElementById("layer-inherited")).backgroundOrigin') == 'content-box,border-box', 'background inherit preserves origin list')

            cases = [
                ('gradient-repeat', 'background:linear-gradient(90deg,red 50%,blue 50%);background-size:40px 20px',
                 [(5,5,(255,0,0)), (25,5,(0,0,255)), (45,25,(255,0,0))]),
                ('gradient-no-repeat', 'background:linear-gradient(red,red) no-repeat;background-size:40px 20px',
                 [(5,5,(255,0,0)), (45,5,(255,255,255)), (5,25,(255,255,255))]),
                ('gradient-percent', 'background:linear-gradient(red,red) no-repeat;background-size:50% 50%',
                 [(45,20,(255,0,0)), (55,20,(255,255,255)), (10,30,(255,255,255))]),
                ('gradient-centered', 'background:linear-gradient(red,red) center / 40px 20px no-repeat',
                 [(35,20,(255,0,0)), (25,20,(255,255,255)), (35,10,(255,255,255))]),
                ('gradient-calc', 'background:linear-gradient(red,red) no-repeat;background-size:calc(50% - 10px) calc(50% + 5px)',
                 [(35,25,(255,0,0)), (45,25,(255,255,255)), (5,35,(255,255,255))]),
                ('gradient-auto-height', 'background:linear-gradient(red,red) no-repeat;background-size:40px',
                 [(35,45,(255,0,0)), (45,45,(255,255,255))]),
                ('gradient-zero', 'background:linear-gradient(red,red);background-size:0 20px',
                 [(5,5,(255,255,255))]),
                ('gradient-negative-calc', 'background:linear-gradient(red,red);background-size:calc(-10px) 20px',
                 [(5,5,(255,255,255))]),
                ('gradient-cover', 'background:linear-gradient(red,red);background-size:cover',
                 [(5,5,(255,0,0)), (95,45,(255,0,0))]),
                ('gradient-contain', 'background:linear-gradient(red,red);background-size:contain',
                 [(5,5,(255,0,0)), (95,45,(255,0,0))]),
                ('size-layers', 'background-image:linear-gradient(red,red),linear-gradient(blue,blue);background-size:40px 20px,80px 40px;background-repeat:no-repeat',
                 [(5,5,(255,0,0)), (55,5,(0,0,255)), (5,30,(0,0,255)), (85,5,(255,255,255))]),
                ('size-list-repeat', 'background-image:none,linear-gradient(blue,blue),linear-gradient(red,red);background-size:40px 20px,20px 40px;background-repeat:no-repeat',
                 [(5,30,(0,0,255)), (30,5,(255,0,0)), (30,30,(255,255,255))]),
                ('size-origin', 'border:5px solid transparent;padding:10px;background:linear-gradient(red,red) content-box no-repeat;background-size:50% 100%',
                 [(20,25,(255,0,0)), (55,25,(255,255,255)), (5,25,(255,255,255))]),
                ('size-rounded', 'background:linear-gradient(red,red);background-size:40px 20px;border-radius:20px',
                 [(0,0,(255,255,255)), (50,25,(255,0,0))]),
            ]
            markup = '<div id=origin></div>'
            for n, (id, css, samples) in enumerate(cases):
                markup += '<div id="size-' + id + '" style="position:absolute;box-sizing:border-box;left:' + str(20+155*(n%5)) + 'px;top:' + str(20+85*(n//5)) + 'px;width:100px;height:50px;' + css + '"></div>'
            command('eval', 'document.body.innerHTML=' + json.dumps(markup))
            picture = screenshot('gradient-sizes')
            for id, css, samples in cases:
                for x,y,expected in samples:
                    color(picture, 'size-'+id, x, y, expected, 'background size '+id+' at '+str((x,y)), 6)
            check(evaluate('getComputedStyle(document.getElementById("size-size-list-repeat")).backgroundSize') == '40px 20px,20px 40px,40px 20px',
                  'computed background sizes repeat across image layers')
            check(evaluate('getComputedStyle(document.getElementById("size-gradient-cover")).backgroundSize') == 'cover',
                  'computed background size preserves cover')
            # Rejected declarations must preserve the previous size atomically.
            invalid_sizes = ['-10px', '20px -10%', 'cover auto', '20px 10px 5px',
                             '20px,', ',20px', 'contain,garbage', ','.join(['10px'] * 9),
                             '20px garbage', 'inherit 20px']
            for invalid in invalid_sizes:
                command('eval', 'document.getElementById("size-gradient-zero").style.cssText=' + json.dumps('background-size:30px 20px;background-size:'+invalid))
                check(evaluate('getComputedStyle(document.getElementById("size-gradient-zero")).backgroundSize') == '30px 20px',
                      'invalid background size is ignored: '+invalid)
            for css, expected in [
                ('background-size:30px 20px;background:red', 'auto auto'),
                ('background-size:30px 20px;background-size:initial', 'auto auto'),
                ('background-size:30px 20px;background-size:unset', 'auto auto'),
                ('background-size:30px 20px!important;background-size:cover', '30px 20px'),
                ('--s:50% 10px;background-size:var(--s)', '50% 10px'),
                ('--s:center / 40px 20px;background:linear-gradient(red,red) var(--s) no-repeat', '40px 20px'),
                ('background-size:30px 20px;background:linear-gradient(red,red) / cover', '30px 20px'),
                ('background-size:30px 20px;background:linear-gradient(red,red) center / cover / contain', '30px 20px'),
            ]:
                command('eval', 'document.getElementById("size-gradient-zero").style.cssText=' + json.dumps(css))
                actual = evaluate('getComputedStyle(document.getElementById("size-gradient-zero")).backgroundSize')
                check(actual == expected, 'background size cascade '+css, actual)
            command('eval', '''document.getElementById('size-size-layers').innerHTML='<div id=size-inherit style="width:100%;height:100%;background:inherit"></div>' ''')
            check(evaluate('getComputedStyle(document.getElementById("size-inherit")).backgroundSize') == '40px 20px,80px 40px',
                  'background inheritance preserves size lists')

            cases = [
                ('image-default', 'background:url(background-size.svg) no-repeat',
                 [(5,5,(255,0,0)), (25,5,(0,255,0)), (45,5,(0,0,255)), (65,5,(255,255,0)), (85,5,(255,255,255))]),
                ('image-explicit', 'background:url(background-size.svg) no-repeat;background-size:40px 20px',
                 [(5,5,(255,0,0)), (15,5,(0,255,0)), (25,5,(0,0,255)), (35,5,(255,255,0)), (45,5,(255,255,255)), (5,25,(255,255,255))]),
                ('image-auto-height', 'background:url(background-size.svg) no-repeat;background-size:40px',
                 [(15,15,(0,255,0)), (5,25,(255,255,255))]),
                ('image-auto-width', 'background:url(background-size.svg) no-repeat;background-size:auto 20px',
                 [(35,15,(255,255,0)), (45,15,(255,255,255))]),
                ('image-percent', 'background:url(background-size.svg) no-repeat;background-size:50% 50%',
                 [(30,25,(0,0,255)), (55,25,(255,255,255)), (5,35,(255,255,255))]),
                ('image-cover', 'background:url(background-size.svg) center / cover no-repeat',
                 [(5,5,(255,0,0)), (22,5,(0,255,0)), (55,5,(0,0,255)), (95,55,(255,255,0))]),
                ('image-contain', 'background:url(background-size.svg) center / contain no-repeat',
                 [(5,1,(255,255,255)), (22,10,(255,0,0)), (30,10,(0,255,0)), (95,55,(255,255,255))]),
                ('image-center', 'background:url(background-size.svg) center / 40px 20px no-repeat',
                 [(25,25,(255,255,255)), (35,25,(255,0,0)), (45,25,(0,255,0)), (65,25,(255,255,0)), (75,25,(255,255,255))]),
                ('image-right-bottom', 'background:url(background-size.svg) right bottom / 40px 20px no-repeat',
                 [(55,45,(255,255,255)), (65,45,(255,0,0)), (95,55,(255,255,0)), (65,35,(255,255,255))]),
                ('image-repeat', 'background:url(background-size.svg);background-size:40px 20px',
                 [(5,5,(255,0,0)), (45,25,(255,0,0)), (95,55,(0,255,0))]),
                ('image-repeat-x', 'background:url(background-size.svg) repeat-x;background-size:40px 20px',
                 [(45,5,(255,0,0)), (45,25,(255,255,255))]),
                ('image-repeat-y', 'background:url(background-size.svg) repeat-y;background-size:40px 20px',
                 [(5,25,(255,0,0)), (45,25,(255,255,255))]),
                ('image-tiny-repeat', 'background:url(background-size.svg);background-size:4px 2px',
                 [(44,24,(255,0,0)), (45,25,(0,255,0)), (46,26,(0,0,255))]),
                ('image-zero', 'background:url(background-size.svg);background-size:0 20px',
                 [(5,5,(255,255,255))]),
                ('image-rounded', 'background:url(background-size.svg);background-size:100% 100%;border-radius:20px',
                 [(0,0,(255,255,255)), (35,30,(0,255,0)), (95,30,(255,255,0))]),
            ]
            markup = '<div id=origin></div>'
            for n, (id, css, samples) in enumerate(cases):
                markup += '<div id="size-' + id + '" style="position:absolute;box-sizing:border-box;left:' + str(20+155*(n%5)) + 'px;top:' + str(20+90*(n//5)) + 'px;width:100px;height:60px;' + css + '"></div>'
            command('eval', 'document.body.innerHTML=' + json.dumps(markup))
            picture = screenshot('image-sizes')
            for id, css, samples in cases:
                for x,y,expected in samples:
                    color(picture, 'size-'+id, x, y, expected, 'background size '+id+' at '+str((x,y)), 6)

            command('eval', 'document.getElementById("size-image-explicit").style.backgroundSize="80px 40px"')
            picture = screenshot('image-size-updated')
            color(picture, 'size-image-explicit', 35, 25, (0,255,0), 'background size mutation updates decoded image scaling')
            command('eval', 'document.getElementById("size-image-explicit").style.backgroundSize="0 0"')
            picture = screenshot('image-size-removed')
            color(picture, 'size-image-explicit', 35, 25, (255,255,255), 'zero background size removes previously painted image')

            command('eval', 'document.body.innerHTML="<div id=origin></div>";document.body.style.background="transparent";document.documentElement.style.background="url(background-size.svg) 0 0 / 40px 20px"')
            picture = screenshot('root-image-size')
            actual = pixel(picture, origin[0]+45, origin[1]+25)
            check(actual == (255,0,0), 'root URL background propagates its size to the canvas', actual)
            command('eval', 'document.documentElement.style.background="transparent";document.body.style.background="url(background-size.svg) 0 0 / 40px 20px"')
            picture = screenshot('body-image-size')
            actual = pixel(picture, origin[0]+55, origin[1]+25)
            check(actual == (0,255,0), 'body URL background propagates its size to the canvas', actual)
            command('eval', 'document.body.innerHTML+="<div style=width:1200px;height:1200px></div>";scrollTo(80,200)')
            picture = screenshot('body-image-size-scrolled')
            actual = pixel(picture, origin[0]+55, origin[1]+25)
            check(actual == (0,255,0), 'scaled canvas background stays aligned after scrolling', actual)
            command('eval', 'scrollTo(0,0);document.documentElement.style.background="white";document.body.style.background="white"')
            picture = screenshot('canvas-image-removed')
            actual = pixel(picture, origin[0]+55, origin[1]+25)
            check(actual == (255,255,255), 'removing root and body backgrounds clears the previous canvas image', actual)

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

            # JS hit queries and real native clicks must share the renderer's
            # stacking, pointer-events inheritance and clipping decisions.
            markup = '''<div id=origin></div>
              <button id=hit-button style="position:absolute;left:20px;top:20px;width:140px;height:50px">Click</button>
              <div id=hit-decoration style="position:absolute;z-index:5;left:20px;top:20px;width:140px;height:50px;pointer-events:none"></div>
              <div id=hit-parent style="position:absolute;left:200px;top:20px;width:140px;height:80px;pointer-events:none">
                <div id=hit-inherited style="width:130px;height:25px">Ignored</div>
                <span id=hit-inline style="pointer-events:auto">Inline target</span>
              </div>
              <div id=hit-clipped style="position:absolute;left:400px;top:20px;width:50px;height:50px;overflow:hidden">
                <div id=hit-overflow style="width:120px;height:50px;background:red"></div>
              </div>
              <input id=hit-input-under style="position:absolute;left:20px;top:130px;width:140px;height:40px">
              <input id=hit-input-over style="position:absolute;z-index:2;left:20px;top:130px;width:140px;height:40px;pointer-events:none">
              <div id=hit-transformed style="position:absolute;left:200px;top:130px;width:100px;height:40px;transform:translate(30px,10px)"></div>
              <div id=hit-fixed style="position:fixed;left:400px;top:130px;width:100px;height:40px"></div>
              <div style="height:1200px;width:1200px"></div>'''
            command('eval', 'document.body.innerHTML=' + json.dumps(markup) + ';window.hitClicks=[];document.addEventListener("click",e=>hitClicks.push(e.target.id))')
            command('wait', '5')

            def hit(id, x=10, y=10):
                return evaluate('(()=>{const r=document.getElementById(' + json.dumps(id) + ').getBoundingClientRect();return document.elementFromPoint(r.left+' + str(x) + ',r.top+' + str(y) + ')?.id||""})()')

            def click(id, x=10, y=10):
                box = rect(id)
                subprocess.run(['xdotool', 'mousemove', '--window', win,
                                str(int(origin[0]+box['left']+x)), str(int(origin[1]+box['top']+y)),
                                'click', '1'], env=env, check=True)

            check(hit('hit-button') == 'hit-button', 'elementFromPoint passes through decorative layers')
            click('hit-button')
            check(evaluate('hitClicks[hitClicks.length-1]') == 'hit-button', 'real click passes through decorative layers to native button')
            check(hit('hit-inherited') not in ['hit-inherited','hit-parent'], 'pointer-events none inherits to children')
            check(hit('hit-inline', 20, 8) == 'hit-inline', 'inline descendant can restore pointer-events auto')
            click('hit-inline', 20, 8)
            check(evaluate('hitClicks[hitClicks.length-1]') == 'hit-inline', 'real click reaches inline auto descendant')
            check(hit('hit-overflow', 20, 20) == 'hit-overflow', 'elementFromPoint includes visible clipped content')
            check(hit('hit-overflow', 80, 20) != 'hit-overflow', 'elementFromPoint excludes content outside its clip')
            check(hit('hit-input-under') == 'hit-input-under', 'elementFromPoint ignores native pointer-events none input')
            click('hit-input-under')
            subprocess.run(['xdotool', 'type', '--clearmodifiers', 'under'], env=env, check=True)
            check(evaluate('document.getElementById("hit-input-under").value') == 'under' and
                  evaluate('document.getElementById("hit-input-over").value') == '', 'real input targets the lower native control')
            check(hit('hit-transformed') == 'hit-transformed', 'elementFromPoint follows translated geometry')
            check(evaluate('[-1,NaN,Infinity,1e100].every(x=>document.elementFromPoint(x,20)===null)&&document.elementFromPoint(20,innerHeight)===null'), 'elementFromPoint rejects invalid and offscreen coordinates')
            command('eval', 'document.getElementById("hit-decoration").style.pointerEvents="auto"')
            check(hit('hit-button') == 'hit-decoration', 'elementFromPoint flushes pending style changes')
            click('hit-button')
            check(evaluate('hitClicks[hitClicks.length-1]') == 'hit-decoration', 'pointer-events auto decoration intercepts native button activation')
            command('eval', 'document.getElementById("hit-input-over").focus()')
            subprocess.run(['xdotool', 'type', '--clearmodifiers', 'keyboard'], env=env, check=True)
            check(evaluate('document.getElementById("hit-input-over").value') == 'keyboard', 'pointer-events none preserves programmatic keyboard focus')
            command('eval', '''const p=document.getElementById("hit-parent"),i=document.getElementById("hit-inline");i.style.pointerEvents="unset"''')
            check(hit('hit-inline',20,8) not in ['hit-parent','hit-inline'], 'pointer-events unset inherits none')
            command('eval', 'document.getElementById("hit-inline").style.pointerEvents="initial"')
            check(hit('hit-inline',20,8) == 'hit-inline', 'pointer-events initial restores auto')
            command('eval', 'document.getElementById("hit-inline").style.cssText="--p:none;pointer-events:var(--p)!important;pointer-events:auto"')
            check(hit('hit-inline',20,8) != 'hit-inline', 'pointer-events honors variables and important')
            command('eval', 'document.getElementById("hit-decoration").style.pointerEvents="none";scrollTo(80,200)')
            command('wait', '5')
            check(hit('hit-fixed') == 'hit-fixed', 'elementFromPoint follows fixed elements after scrolling')
            command('eval', 'scrollTo(0,0)')
            command('wait', '5')

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

            command('eval', 'scrollTo(0,0)')
            markup = '''<div id=origin></div>
              <div id=mouse-box style="position:absolute;left:40px;top:40px;width:160px;height:70px;background:#ddd"><span id=mouse-inline>Inline target</span></div>
              <input id=mouse-input style="position:fixed;left:300px;top:50px;width:150px;height:30px">
              <button id=mouse-button type=button style="position:fixed;left:500px;top:50px;width:120px;height:35px">Mouse button</button>
              <textarea id=mouse-editor style="position:fixed;left:300px;top:120px;width:150px;height:60px"></textarea>
              <div id=mouse-scrolled style="position:absolute;left:150px;top:450px;width:160px;height:70px;background:#ddd"></div>
              <div style="width:1200px;height:1200px"></div>'''
            command('eval', 'document.body.innerHTML=' + json.dumps(markup))
            command('eval', '''globalThis.mouseEvents=[];
              document.addEventListener('click',e=>{
                mouseEvents.push({target:e.target.id,hit:document.elementFromPoint(e.clientX,e.clientY)?.id,
                  clientX:e.clientX,clientY:e.clientY,pageX:e.pageX,pageY:e.pageY,screenX:e.screenX,screenY:e.screenY,
                  button:e.button,buttons:e.buttons,detail:e.detail,
                  ctrlKey:e.ctrlKey,shiftKey:e.shiftKey,altKey:e.altKey,metaKey:e.metaKey,
                  trusted:e.isTrusted,mouse:e instanceof MouseEvent});
              });'''.replace('\n', ' '))
            command('wait', '5')
            # Move the browser so screen coordinates cannot accidentally pass
            # by copying client coordinates or window-relative positions.
            subprocess.run(['xdotool', 'windowmove', win, '35', '25'], env=env, check=True)

            def native_mouse(id, dx=10, dy=10, modifiers=(), repeat=1):
                command('eval', 'mouseEvents=[]')
                box = rect(id)
                cx, cy = int(box['left']+dx), int(box['top']+dy)
                subprocess.run(['xdotool', 'mousemove', '--window', win,
                                str(origin[0]+cx), str(origin[1]+cy)], env=env, check=True)
                try:
                    if modifiers:
                        subprocess.run(['xdotool', 'keydown', *modifiers], env=env, check=True)
                    subprocess.run(['xdotool', 'click', '--repeat', str(repeat), '--delay', '100', '1'], env=env, check=True)
                finally:
                    if modifiers:
                        subprocess.run(['xdotool', 'keyup', *reversed(modifiers)], env=env, check=True)
                events = evaluate('mouseEvents')
                check(len(events) == repeat, 'one native mouse event per click on '+id, events)
                event = events[-1]
                check(event['target'] == id and event['hit'] == id,
                      'mouse coordinates agree with renderer hit testing on '+id, event)
                sx, sy = evaluate('[scrollX,scrollY]')
                check((event['clientX'],event['clientY'],event['pageX'],event['pageY']) == (cx,cy,cx+sx,cy+sy),
                      'native client and page coordinates on '+id, event)
                position = subprocess.check_output(['xdotool', 'getmouselocation', '--shell'], env=env, text=True)
                actual = dict(line.split('=',1) for line in position.splitlines())
                check((event['screenX'],event['screenY']) == (int(actual['X']),int(actual['Y'])),
                      'native screen coordinates on '+id, event)
                check(event['mouse'] and event['trusted'] and event['button'] == 0 and event['buttons'] == 0,
                      'native click class, trust and released button state on '+id, event)
                return event

            native_mouse('mouse-box',100,40)
            native_mouse('mouse-inline',5,8)
            native_mouse('mouse-input',30,15)
            native_mouse('mouse-button',60,18)
            native_mouse('mouse-editor',30,20)
            event = native_mouse('mouse-box',100,40,('ctrl','shift','alt','Super_L'))
            check(all(event[key] for key in ['ctrlKey','shiftKey','altKey','metaKey']),
                  'native click preserves all four modifier keys', event)
            event = native_mouse('mouse-box',100,40,repeat=2)
            check(event['detail'] >= 2, 'native double click exposes the click count', event)
            command('eval', 'scrollTo(80,200)')
            command('wait', '5')
            native_mouse('mouse-scrolled',100,40)
            native_mouse('mouse-input',30,15)
            native_mouse('mouse-button',60,18)
            command('eval', 'mouseEvents=[];document.getElementById("mouse-button").focus()')
            subprocess.run(['xdotool', 'key', 'space'], env=env, check=True)
            events = evaluate('mouseEvents')
            check(len(events) == 1 and events[0]['target'] == 'mouse-button' and
                  all(events[0][key] == 0 for key in ['clientX','clientY','pageX','pageY','screenX','screenY','detail']),
                  'native keyboard activation does not inherit the last mouse coordinates', events)
            command('eval', '''globalThis.detachedImage=new Image();globalThis.detachedImageEvents=[];
              detachedImage.onload=()=>detachedImageEvents.push('load');
              detachedImage.onerror=()=>detachedImageEvents.push('error');
              detachedImage.src='tile.svg?detached-image';'''.replace('\n', ' '))
            for _ in range(60):
                if evaluate('detachedImage.complete'):
                    break
                time.sleep(.05)
            check(evaluate('[detachedImage.complete,detachedImage.naturalWidth,detachedImage.naturalHeight,detachedImageEvents]') ==
                  [True,80,60,['load']], 'detached Image src assignment loads and dispatches load')
            command('eval', '''globalThis.detachedAttributeImage=new Image();globalThis.detachedAttributeEvents=[];
              detachedAttributeImage.onload=()=>detachedAttributeEvents.push('load');
              detachedAttributeImage.setAttribute('src','tile.svg?detached-attribute');'''.replace('\n', ' '))
            for _ in range(60):
                if evaluate('detachedAttributeImage.complete'):
                    break
                time.sleep(.05)
            check(evaluate('[detachedAttributeImage.complete,detachedAttributeImage.naturalWidth,detachedAttributeEvents]') ==
                  [True,80,['load']], 'detached Image setAttribute starts loading')
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
