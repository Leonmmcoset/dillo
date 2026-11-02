#include "src/jsengine.hh"
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>
#include <vector>

static void check(bool ok, const char *message)
{ if (!ok) { fprintf(stderr, "FAIL: %s\n", message); exit(1); } }

class Host : public JsEngine::Host {
public:
   std::vector<std::string> logs;
   std::map<int, int> timers;
   std::string pageTitle, destination;
   int changes;
   JsEngine *focusEngine;
   Host(): changes(0), focusEngine(NULL) {}
   void log(const std::string &s) { logs.push_back(s); fprintf(stderr, "%s\n", s.c_str()); }
   void changed() { ++changes; }
   void title(const std::string &s) { pageTitle = s; }
   void navigate(const std::string &s, bool) { destination = s; }
   void timer(int id, int delay) { if (delay < 0) timers.erase(id); else timers[id] = delay; }
   void request(int, const std::string &, const std::string &, const std::string &, const std::string &, int) {}
   void abort(int) {}
   std::string userAgent() { return "Dillo/test"; }
   std::string control(int node, const std::string &property, const std::string *) {
      if (property == "focus" && focusEngine) { focusEngine->event(node, "focus"); return "0,0"; }
      return "\1";
   }
};

static void drain(JsEngine &engine)
{
   JsEngine::Script script;
   int count = 0;
   while (engine.nextScript(script)) {
      check(++count < 30, "script execution terminates");
      check(script.url.empty(), "fixture scripts are inline");
      check(engine.evaluate(script.source, "fixture.html"), "fixture script executes");
      engine.scriptStarted(script.node);
   }
}

int main()
{
   Host host;
   JsEngine engine(&host, "https://example.test/path/page.html");
   check(engine.valid(), "engine and browser bootstrap initialize");
   check(engine.evaluate("/(\\.[a-z0-9]+)$/i.test('module.js');globalThis.extension=RegExp.$1;", "legacy-regexp"), "legacy browser regular expressions");
   check(engine.stringValue("extension") == ".js", "RegExp legacy captures support module loader URL construction");
   std::string html = "<!doctype html><html><head><title>Initial</title></head><body>"
      "<div id='out' class='a'>old<span>child</span></div>"
      "<script type='application/json'>{\"data\":true}</script>"
      "<script>globalThis.order=['first'];globalThis.old=document.getElementById('out').firstChild;"
      "document.getElementById('out').textContent='new';"
      "document.write('<p id=written>inserted</p>');</script>"
      "<script>order.push(document.getElementById('written').textContent);</script></body></html>";
   for (size_t i = 0; i < html.size(); i += 7) {
      engine.append(html.data() + i, std::min(static_cast<size_t>(7), html.size() - i)); drain(engine);
   }
   engine.finish(); drain(engine); engine.loaded();
   check(engine.stringValue("order.join(',')") == "first,inserted", "parser script ordering and document.write insertion");
   check(engine.stringValue("old.textContent") == "old", "detached nodes survive content replacement");
   check(engine.stringValue("document.readyState") == "complete", "ready lifecycle completes");
   check(engine.stringValue("document.querySelectorAll('div.a').length") == "1", "native CSS selector matching");
   check(engine.stringValue("document.getElementById('out')===document.querySelector('#out')") == "true", "stable node wrapper identity");
   check(engine.evaluate("globalThis.live=document.getElementsByTagName('p');globalThis.before=live.length;let extra=document.createElement('p');document.body.appendChild(extra);globalThis.after=live.length;extra.remove();", "live-collection"), "live collection caching invalidates on DOM mutation");
   check(engine.stringValue("before+','+after+','+live.length") == "1,2,1", "cached collections remain live");
   check(engine.evaluate("document.title='Updated';let p=document.createElement('p');p.id='made';p.classList.add('one','two');document.body.appendChild(p);p.dataset.test='yes';p.style.width='50px';", "dom-test"), "DOM mutation");
   check(host.pageTitle == "Updated", "title reaches browser host");
   check(engine.stringValue("document.querySelector('#made.two').dataset.test") == "yes", "class and dataset reflection");
   check(engine.stringValue("document.querySelector('#made').style.width") == "50px", "inline style reflection");
   check(engine.evaluate("p.style.setProperty('--Color','red');p.style.setProperty('--color','blue');p.style.setProperty('WIDTH','60px');", "css-names"), "custom property mutation");
   check(engine.stringValue("p.style.getPropertyValue('--Color')+','+p.style.getPropertyValue('--color')+','+p.style.width") == "red,blue,60px", "custom property names preserve case while ordinary CSS names fold case");
   check(engine.evaluate("let sheetNode=document.createElement('style');document.head.appendChild(sheetNode);globalThis.sheet=sheetNode.sheet;sheet.insertRule('#made { color: red }');sheet.insertRule('p { width: 20px }',1);sheet.deleteRule(0);", "stylesheet-rules"), "inline stylesheet rules update the DOM");
   check(engine.stringValue("sheet===document.styleSheets[0]&&sheet.cssRules.length===1&&sheet.cssRules[0].selectorText==='p'&&sheet.cssRules[0].style.width==='20px'") == "true", "stylesheet identity and rule reflection");
   check(engine.evaluate("p.style.cssText='color:red !important;color:blue;width:1px;width:2px';", "style-cascade"), "inline declaration cascade");
   check(engine.stringValue("p.style.color+','+p.style.width") == "red,2px", "last declaration wins at the same priority");
   check(engine.evaluate("globalThis.events=[];document.addEventListener('click',()=>events.push('capture'),true);document.getElementById('made').addEventListener('click',e=>{events.push('target');e.preventDefault()});document.addEventListener('click',()=>events.push('bubble'));", "event-test"), "event handlers attach");
   check(!engine.event(engine.find("#made"), "click"), "preventDefault reaches browser host");
   check(engine.stringValue("events.join(',')") == "capture,target,bubble", "capture and bubbling order");
   check(engine.evaluate("p.addEventListener('click',e=>{globalThis.mouse=e;});", "mouse-handler"), "mouse event handler attaches");
   JsEngine::MouseEvent mouse;
   mouse.clientX = 31; mouse.clientY = 47; mouse.pageX = 131; mouse.pageY = 247;
   mouse.screenX = 431; mouse.screenY = 547; mouse.detail = 2;
   mouse.ctrlKey = mouse.shiftKey = true;
   check(!engine.event(engine.find("#made"), "click", "", 0, &mouse), "native mouse metadata preserves cancellation");
   check(engine.stringValue("[mouse instanceof MouseEvent,mouse.isTrusted,mouse.clientX,mouse.clientY,mouse.pageX,mouse.pageY,mouse.screenX,mouse.screenY,mouse.button,mouse.buttons,mouse.detail,mouse.ctrlKey,mouse.shiftKey,mouse.altKey,mouse.metaKey].join(',')") ==
      "true,true,31,47,131,247,431,547,0,0,2,true,true,false,false", "native mouse coordinates and state reach JavaScript");
   check(engine.stringValue("[mouse.x,mouse.y,mouse.which,mouse.getModifierState('Control'),mouse.getModifierState('Alt')].join(',')") ==
      "31,47,1,true,false", "mouse coordinate aliases and modifier queries");
   engine.event(engine.find("#made"), "click");
   check(engine.stringValue("[mouse.clientX,mouse.pageY,mouse.detail,mouse.ctrlKey].join(',')") == "0,0,0,false",
      "keyboard activation does not reuse previous mouse metadata");
   check(engine.evaluate("globalThis.jobs=[];setTimeout(()=>jobs.push('timer'),1);Promise.resolve().then(()=>jobs.push('microtask'));", "timer-test"), "timers and promises initialize");
   check(engine.stringValue("jobs.join(',')") == "microtask", "microtasks run before timers");
   check(!host.timers.empty(), "timer reaches browser scheduler");
   engine.timeout(host.timers.begin()->first);
   check(engine.stringValue("jobs.join(',')") == "microtask,timer", "timer callback executes");
   check(engine.evaluate("globalThis.originalPromise=globalThis.Promise;globalThis.Promise={resolve(){throw new Error('page replacement');}};queueMicrotask(()=>jobs.push('intrinsic'));globalThis.Promise=originalPromise;", "promise-intrinsic"), "browser microtasks use the intrinsic Promise even when a page replaces the global constructor");
   check(engine.stringValue("jobs[jobs.length-1]") == "intrinsic", "browser microtasks do not recurse through a page Promise polyfill");
   check(engine.evaluate("globalThis.promiseConstructor=Object.getOwnPropertyDescriptor(Promise.prototype,'constructor');Object.defineProperty(Promise.prototype,'constructor',{configurable:true,get(){throw new Error('page species');}});queueMicrotask(()=>jobs.push('native-job'));Object.defineProperty(Promise.prototype,'constructor',promiseConstructor);", "microtask-species"), "queueMicrotask bypasses page Promise species");
   check(engine.stringValue("jobs[jobs.length-1]") == "native-job", "native microtask runs with a modified Promise constructor");
   host.focusEngine = &engine;
   check(engine.evaluate("globalThis.focusOrder=[];p.addEventListener('focus',()=>{focusOrder.push('focus');queueMicrotask(()=>focusOrder.push('job'));});p.focus();focusOrder.push('return');", "native-focus"), "native focus can synchronously reenter the document");
   check(engine.stringValue("focusOrder.join(',')") == "focus,return,job", "microtasks wait for the outer native-event task to finish");
   host.focusEngine = NULL;
   check(engine.evaluate("location.replace(location.href.replace('https:', 'http:'));", "navigation-test"), "location navigation");
   check(host.destination == "http://example.test/path/page.html", "HTTPS script redirect resolves correctly");
   check(engine.stringValue("new URL('', 'https://EXAMPLE.test:443/path/page?q=1').href") == "https://example.test/path/page?q=1", "URL empty relative reference and default port");
   check(engine.stringValue("new URL('../中文', 'https://example.test/a/b').pathname") == "/%E4%B8%AD%E6%96%87", "URL resolves paths and encodes Unicode");
   check(engine.stringValue("new URL('http://[::1]:80/').origin") == "http://[::1]", "IPv6 URL origin");
   check(engine.evaluate("globalThis.anchor=document.createElement('a');anchor.href='/search?q=test';", "anchor-url"), "anchor URL reflection");
   check(engine.stringValue("anchor.protocol+'|'+anchor.hostname+'|'+anchor.pathname+'|'+anchor.search") == "https:|example.test|/search|?q=test", "anchor components support browser HTTP clients");
   check(engine.evaluate("globalThis.u=new URL('https://example.test/?a=1');globalThis.params=u.searchParams;params.set('a','two words');u.hash='中文';", "url-test"), "URL component mutation");
   check(engine.stringValue("u.search+'|'+params.get('a')+'|'+u.hash") == "?a=two+words|two words|#%E4%B8%AD%E6%96%87", "search parameters stay live after URL mutation");
   check(engine.evaluate("globalThis.field=document.createElement('input');field.setAttribute('value','default');field.value='edited';document.body.appendChild(field);globalThis.area=document.createElement('textarea');area.textContent='initial';area.value='typed';document.body.appendChild(area);", "form-state"), "form controls keep current values");
   check(engine.stringValue("field.value+'|'+field.defaultValue+'|'+area.value+'|'+area.defaultValue") == "edited|default|typed|initial", "current and default form values differ");
   check(engine.serialize(true).find(">typed</textarea>") != std::string::npos, "textarea current value reaches the view");
   check(engine.stringValue("area.textContent") == "initial", "view serialization preserves textarea DOM children");
   check(engine.evaluate("area.value='中文😀abc';area.setSelectionRange(1,4);globalThis.checkBox=document.createElement('input');checkBox.type='checkbox';document.body.appendChild(checkBox);checkBox.click();", "control-api"), "selection and default click behavior");
   check(engine.stringValue("area.selectionStart+','+area.selectionEnd+','+checkBox.checked") == "1,4,true", "UTF-16 selection offsets and checkbox activation");
   check(engine.evaluate("checkBox.onclick=e=>e.preventDefault();checkBox.click();", "cancel-control"), "checkbox canceled activation");
   check(engine.stringValue("checkBox.checked") == "true", "canceling a checkbox click restores checked state");
   check(engine.stringValue("document.createElement('div').getBoundingClientRect().width") == "0", "detached element has no layout box");
   std::string rendered = engine.serialize(true);
   check(rendered.find("data-dillo-node=") != std::string::npos, "view serialization maps DOM nodes");
   check(engine.serialize().find("data-dillo-node=") == std::string::npos, "view metadata is absent from persistent DOM");
   check(engine.stringValue("typeof std + ',' + typeof os") == "undefined,undefined", "host filesystem modules are unavailable");
   check(!engine.evaluate("while(true) {}", "timeout-test"), "infinite script is interrupted");
   check(engine.evaluate("globalThis.alive=true", "after-timeout"), "context remains usable after interrupted task");
   check(!engine.evaluate("let memory=[];for(;;)memory.push(new Array(100000).fill(0));", "memory-test"), "JavaScript heap allocation is bounded");
   Host jobHost;
   JsEngine jobEngine(&jobHost, "https://example.test/jobs");
   check(!jobEngine.evaluate("queueMicrotask(function again(){queueMicrotask(again);});", "job-budget"), "self-replenishing microtasks stop at the checkpoint limit");
   check(!jobEngine.valid(), "exhausting the microtask limit stops the document engine");
   return 0;
}
