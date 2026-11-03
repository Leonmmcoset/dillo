R"DILLOJS(
/* Browser objects backed by the persistent native HTML5 document. */
(() => {
'use strict';
const Promise = globalThis.Promise;
// A Promise.then-based shim observes the page's Promise @@species. A page
// polyfill can use queueMicrotask itself, making that shim recurse forever.
const queueJob = fn => { native('microtask',0,fn); };
// Annex B RegExp statics are used by older browser module loaders. QuickJS
// deliberately omits them; update them through the standard RegExpExec hook.
const regexpExec = RegExp.prototype.exec;
let regexpMatch = null, regexpInput = '';
Object.defineProperty(RegExp.prototype, 'exec', {configurable:true,writable:true,value:function(input) {
    const text = String(input), match = regexpExec.call(this, text);
    if (match) { regexpMatch = match; regexpInput = text; }
    return match;
}});
for (let i = 1; i <= 9; ++i) Object.defineProperty(RegExp, '$' + i, {
    configurable:true,get(){return regexpMatch?.[i] || '';}
});
for (const [name, getter] of Object.entries({
    input:()=>regexpInput, '$_':()=>regexpInput,
    lastMatch:()=>regexpMatch?.[0] || '', '$&':()=>regexpMatch?.[0] || '',
    lastParen:()=>regexpMatch?.[regexpMatch.length-1] || '', '$+':()=>regexpMatch?.[regexpMatch.length-1] || '',
    leftContext:()=>regexpMatch ? regexpInput.slice(0,regexpMatch.index) : '',
    '$`':()=>regexpMatch ? regexpInput.slice(0,regexpMatch.index) : '',
    rightContext:()=>regexpMatch ? regexpInput.slice(regexpMatch.index+regexpMatch[0].length) : '',
    "$'":()=>regexpMatch ? regexpInput.slice(regexpMatch.index+regexpMatch[0].length) : ''
})) Object.defineProperty(RegExp,name,{configurable:true,get:getter,
    ...(['input','$_'].includes(name)?{set(value){regexpInput=String(value);}}:{})});
const native = globalThis.__native;
delete globalThis.__native;
const cache = new Map(), listeners = new WeakMap(), timers = new Map(), requests = new Map();
const documentRoots = new Map();
const imageState = new WeakMap(), imageDecodes = new WeakMap();
let nextTimer = 1, nextRequest = 1, state = 'loading', activeElement = null;
let pendingWindowScroll = null, pendingScrollOrigin = null, pendingScrollTimer = 0;
const mutationObservers = new Set();
const cssName = name => String(name).replace(/[A-Z]/g, c => '-' + c.toLowerCase());
const list = values => Object.assign(values, {item(i) { return this[i] || null; }});
function collection(get) {
    let version = -1, values;
    const current = () => {
        const revision = native('revision');
        if (revision !== version) { values = get(); version = revision; }
        return values;
    };
    return new Proxy({}, {
        get(_, key) {
            const values = current();
            if (key === 'length') return values.length;
            if (key === Symbol.iterator) return values[Symbol.iterator].bind(values);
            if (key === 'item') return i => values[i] || null;
            if (key === 'namedItem') return name => values.find(n => n.id === name || n.name === name) || null;
            if (key === 'forEach') return values.forEach.bind(values);
            if (typeof key === 'string' && /^\d+$/.test(key)) return values[Number(key)];
            return values.find(n => n.id === key || n.name === key);
        },
        ownKeys() { return current().map((_, i) => String(i)); },
        getOwnPropertyDescriptor(_, key) { return {configurable: true, enumerable: true, value: this.get(_, key)}; }
    });
}
function notify(target, type, extra = {}) {
    for (const observer of mutationObservers) {
        const registration = observer.targets.find(t =>
            (t.target === target || t.options.subtree && t.target.contains(target)) &&
            t.options[type] && (type !== 'attributes' || !t.options.attributeFilter ||
                t.options.attributeFilter.includes(extra.attributeName)));
        if (registration) {
            const record = Object.assign({target, type, addedNodes: [], removedNodes: [],
                attributeName: null, attributeNamespace: null, oldValue: null,
                previousSibling: null, nextSibling: null}, extra);
            if (type === 'attributes' && !registration.options.attributeOldValue ||
                type === 'characterData' && !registration.options.characterDataOldValue)
                record.oldValue = null;
            observer.records.push(record);
            if (!observer.pending) {
                observer.pending = true;
                queueJob(() => {
                    observer.pending = false;
                    const records = observer.takeRecords();
                    if (records.length) observer.callback(records, observer);
                });
            }
        }
    }
}
class Event {
    constructor(type, options = {}) {
        this.type = String(type); this.bubbles = !!options.bubbles;
        this.cancelable = !!options.cancelable; this.composed = !!options.composed;
        this.defaultPrevented = false; this.target = null; this.currentTarget = null;
        this.eventPhase = 0; this.timeStamp = performance.now(); this.isTrusted = false;
        this._stop = false; this._immediate = false;
    }
    preventDefault() { if (this.cancelable && !this._passive) this.defaultPrevented = true; }
    stopPropagation() { this._stop = true; this.cancelBubble = true; }
    stopImmediatePropagation() { this._immediate = true; this.stopPropagation(); }
    initEvent(type, bubbles, cancelable) { this.type = type; this.bubbles = !!bubbles; this.cancelable = !!cancelable; }
    composedPath() { return this._path ? this._path.slice() : []; }
    get returnValue() { return !this.defaultPrevented; }
    set returnValue(value) { if (!value) this.preventDefault(); }
}
Object.assign(Event, {NONE: 0, CAPTURING_PHASE: 1, AT_TARGET: 2, BUBBLING_PHASE: 3});
class CustomEvent extends Event {
    constructor(type, options = {}) { super(type, options); this.detail = options.detail ?? null; }
    initCustomEvent(type, bubbles, cancelable, detail) { this.initEvent(type, bubbles, cancelable); this.detail = detail; }
}
class MouseEvent extends Event {
    constructor(type, options = {}) {
        super(type, options);
        for (const key of ['clientX','clientY','screenX','screenY','pageX','pageY','button','buttons','detail']) this[key] = options[key] || 0;
        for (const key of ['ctrlKey','shiftKey','altKey','metaKey']) this[key] = !!options[key];
        this.relatedTarget = options.relatedTarget || null;
    }
    get x() { return this.clientX; }
    get y() { return this.clientY; }
    get which() { return this.button + 1; }
    getModifierState(key) { return !!this[{Control:'ctrlKey',Shift:'shiftKey',Alt:'altKey',Meta:'metaKey'}[key]]; }
}
class KeyboardEvent extends Event {
    constructor(type, options = {}) { super(type, options); Object.assign(this, {key: '', code: '', keyCode: 0, which: 0}, options); }
}
class EventTarget {
    addEventListener(type, callback, options = {}) {
        if (!callback) return;
        const settings = typeof options === 'object' && options !== null ? options : {};
        const capture = typeof options === 'boolean' ? options : !!settings.capture;
        const signal = settings.signal;
        if (signal?.aborted) return;
        let entries = listeners.get(this); if (!entries) listeners.set(this, entries = []);
        if (!entries.some(e => e.type === String(type) && e.callback === callback && e.capture === capture)) {
            const entry = {type: String(type), callback, capture, once: !!settings.once, passive: !!settings.passive, signal};
            if (signal) {
                entry.abort = () => this.removeEventListener(entry.type, callback, capture);
                signal.addEventListener('abort', entry.abort, {once:true});
            }
            entries.push(entry);
        }
    }
    removeEventListener(type, callback, options = {}) {
        const settings = typeof options === 'object' && options !== null ? options : {};
        const capture = typeof options === 'boolean' ? options : !!settings.capture;
        const entries = listeners.get(this) || [];
        const kept = [];
        for (const entry of entries) {
            if (entry.type === String(type) && entry.callback === callback && entry.capture === capture) {
                if (entry.signal && entry.abort) entry.signal.removeEventListener('abort', entry.abort);
            } else kept.push(entry);
        }
        listeners.set(this, kept);
    }
    dispatchEvent(event) {
        if (!(event instanceof Event) || !event.type) throw new TypeError('Expected an Event');
        event.target = this;
        const path = [this];
        for (let p = this.parentNode; p; p = p.parentNode) path.push(p);
        if (path[path.length - 1] === document) path.push(globalThis);
        event._path = path;
        const invoke = (target, capture, phase) => {
            event.currentTarget = target; event.eventPhase = phase;
            for (const e of (listeners.get(target) || []).slice()) {
                if (!(listeners.get(target) || []).includes(e)) continue;
                if (e.type !== event.type || e.capture !== capture || event._immediate) continue;
                if (e.once) target.removeEventListener(e.type, e.callback, e.capture);
                const passive = event._passive;
                event._passive = e.passive;
                try {
                    if (typeof e.callback === 'function') e.callback.call(target, event);
                    else e.callback.handleEvent(event);
                } catch (error) { console.error(String(error)+'\n'+(error.stack || '')); }
                finally { event._passive = passive; }
            }
            if (!capture && !event._immediate) {
                let handler = target['on' + event.type];
                if (!handler && target.getAttribute) {
                    const text = target.getAttribute('on' + event.type);
                    if (text) {
                        try { handler = new Function('event', text); }
                        catch (error) { console.error(error); }
                    }
                }
                if (typeof handler === 'function') {
                    try { if (handler.call(target, event) === false) event.preventDefault(); }
                    catch (error) { console.error(String(error)+'\n'+(error.stack || '')); }
                }
            }
        };
        for (let i = path.length - 1; i > 0 && !event._stop; --i) invoke(path[i], true, 1);
        if (!event._stop) { invoke(this, true, 2); invoke(this, false, 2); }
        if (event.bubbles) for (let i = 1; i < path.length && !event._stop; ++i) invoke(path[i], false, 3);
        event.currentTarget = null; event.eventPhase = 0;
        return !event.defaultPrevented;
    }
}
class Node extends EventTarget {
    constructor(id) { super(); Object.defineProperty(this, '_id', {value: id}); }
    get nodeType() { return native('get', this._id, 'type'); }
    get nodeName() { return native('get', this._id, 'name'); }
    get parentNode() { return wrap(native('get', this._id, 'parent')); }
    get parentElement() { const p = this.parentNode; return p && p.nodeType === 1 ? p : null; }
    get firstChild() { return wrap(native('get', this._id, 'first')); }
    get lastChild() { return wrap(native('get', this._id, 'last')); }
    get nextSibling() { return wrap(native('get', this._id, 'next')); }
    get previousSibling() { return wrap(native('get', this._id, 'prev')); }
    get ownerDocument() { return this.nodeType === 9 ? null : document; }
    get isConnected() { return native('get', this._id, 'connected'); }
    get childNodes() { return collection(() => native('children', this._id).map(wrap)); }
    get textContent() { return this.nodeType === 9 ? null : native('get', this._id, 'text'); }
    set textContent(value) {
        if (this.nodeType === 9) return;
        const type = this.nodeType === 3 || this.nodeType === 8 ? 'characterData' : 'childList';
        const oldValue = type === 'characterData' ? this.textContent : null;
        native('text', this._id, value == null ? '' : String(value));
        notify(this, type, {oldValue});
    }
    get nodeValue() { return this.nodeType === 3 || this.nodeType === 8 ? this.textContent : null; }
    set nodeValue(value) { if (this.nodeType === 3 || this.nodeType === 8) this.textContent = value; }
    appendChild(child) { return this.insertBefore(child, null); }
    insertBefore(child, before) {
        if (!(child instanceof Node) || before != null && !(before instanceof Node)) throw new TypeError('Expected a Node');
        native('insert', this._id, child._id, before ? before._id : 0);
        notify(this, 'childList', {addedNodes: [child]}); return child;
    }
    removeChild(child) {
        if (!child || child.parentNode !== this) throw new Error('NotFoundError');
        native('remove', child._id); notify(this, 'childList', {removedNodes: [child]}); return child;
    }
    replaceChild(child, old) { if (child === old) return old; this.insertBefore(child, old); this.removeChild(old); return old; }
    remove() { if (this.parentNode) this.parentNode.removeChild(this); }
    replaceWith(...nodes) { if (this.parentNode) { this.before(...nodes); this.remove(); } }
    append(...nodes) { for (const n of nodes) this.appendChild(n instanceof Node ? n : document.createTextNode(String(n))); }
    prepend(...nodes) { const before = this.firstChild; for (const n of nodes) this.insertBefore(n instanceof Node ? n : document.createTextNode(String(n)), before); }
    before(...nodes) { if (this.parentNode) for (const n of nodes) this.parentNode.insertBefore(n instanceof Node ? n : document.createTextNode(String(n)), this); }
    after(...nodes) { if (this.parentNode) { const before = this.nextSibling; for (const n of nodes) this.parentNode.insertBefore(n instanceof Node ? n : document.createTextNode(String(n)), before); } }
    replaceChildren(...nodes) { while (this.firstChild) this.removeChild(this.firstChild); this.append(...nodes); }
    cloneNode(deep = false) { return wrap(native('clone', this._id, String(!!deep))); }
    hasChildNodes() { return !!this.firstChild; }
    contains(node) { for (let n = node; n; n = n.parentNode) if (n === this) return true; return false; }
    isSameNode(node) { return this === node; }
    getRootNode() { let n = this; while (n.parentNode) n = n.parentNode; return n; }
    compareDocumentPosition(other) {
        if (this === other) return 0;
        if (!other || this.getRootNode() !== other.getRootNode()) return 1 | 32;
        if (this.contains(other)) return 4 | 16;
        if (other.contains(this)) return 2 | 8;
        const all = []; const visit = n => { all.push(n); for (const c of n.childNodes) visit(c); }; visit(this.getRootNode());
        return all.indexOf(this) < all.indexOf(other) ? 4 : 2;
    }
    normalize() {
        for (let n = this.firstChild; n;) {
            const next = n.nextSibling;
            if (n.nodeType === 3) {
                if (!n.data) { n.remove(); n = next; continue; }
                while (n.nextSibling && n.nextSibling.nodeType === 3) { const q = n.nextSibling; n.data += q.data; q.remove(); }
            } else n.normalize();
            n = n.nextSibling;
        }
    }
}
for (const [name, number] of Object.entries({ELEMENT_NODE:1,TEXT_NODE:3,COMMENT_NODE:8,DOCUMENT_NODE:9,DOCUMENT_FRAGMENT_NODE:11,DOCUMENT_POSITION_DISCONNECTED:1,DOCUMENT_POSITION_PRECEDING:2,DOCUMENT_POSITION_FOLLOWING:4,DOCUMENT_POSITION_CONTAINS:8,DOCUMENT_POSITION_CONTAINED_BY:16})) {
    Object.defineProperty(Node, name, {value: number}); Object.defineProperty(Node.prototype, name, {value: number});
}
class CharacterData extends Node {
    get data() { return this.textContent; } set data(v) { this.textContent = v; }
    get length() { return this.data.length; }
    appendData(v) { this.data += v; }
    deleteData(start, count) { this.data = this.data.slice(0, start) + this.data.slice(start + count); }
    insertData(start, value) { this.data = this.data.slice(0, start) + value + this.data.slice(start); }
    substringData(start, count) { return this.data.slice(start, start + count); }
}
class Text extends CharacterData {
    splitText(offset) { const tail = document.createTextNode(this.data.slice(offset)); this.data = this.data.slice(0, offset); if (this.parentNode) this.parentNode.insertBefore(tail, this.nextSibling); return tail; }
}
class Comment extends CharacterData {}
class DOMException extends Error {
    constructor(message='',name='Error') { super(message);this.name=String(name); }
}
function validateToken(token) {
    token=String(token);
    if(!token)throw new DOMException('The token provided must not be empty.','SyntaxError');
    if(/[\t\n\f\r ]/.test(token))throw new DOMException('The token provided contains ASCII whitespace.','InvalidCharacterError');
    return token;
}
class DOMTokenList {
    constructor(element,attribute='class') {
        Object.defineProperties(this,{_element:{value:element},_attribute:{value:attribute}});
        const index=key=>typeof key==='string'&&/^(0|[1-9]\d*)$/.test(key);
        return new Proxy(this,{
            get(target,key,receiver){return index(key)?target._tokens()[Number(key)]:Reflect.get(target,key,receiver);},
            set(target,key,value,receiver){return index(key)?false:Reflect.set(target,key,value,receiver);},
            has(target,key){return index(key)?Number(key)<target.length:Reflect.has(target,key);},
            ownKeys(target){return [...Reflect.ownKeys(target),...target._tokens().map((_,i)=>String(i))];},
            getOwnPropertyDescriptor(target,key){return index(key)&&Number(key)<target.length?
                {configurable:true,enumerable:true,writable:false,value:target._tokens()[Number(key)]}:
                Reflect.getOwnPropertyDescriptor(target,key);}
        });
    }
    _tokens(){return Array.from(new Set((this._element.getAttribute(this._attribute)||'').split(/[\t\n\f\r ]+/).filter(Boolean)));}
    _write(tokens){this._element.setAttribute(this._attribute,tokens.join(' '));}
    get value(){return this._element.getAttribute(this._attribute)||'';}
    set value(value){this._element.setAttribute(this._attribute,String(value));}
    get length(){return this._tokens().length;}
    item(index){return this._tokens()[Number(index)]??null;}
    contains(token){return this._tokens().includes(validateToken(token));}
    add(...tokens){tokens=tokens.map(validateToken);const values=this._tokens();for(const token of tokens)if(!values.includes(token))values.push(token);this._write(values);}
    remove(...tokens){tokens=tokens.map(validateToken);this._write(this._tokens().filter(token=>!tokens.includes(token)));}
    toggle(token,force){token=validateToken(token);const values=this._tokens(),present=values.includes(token),add=force===undefined?!present:!!force;if(add&&!present)values.push(token);else if(!add&&present)values.splice(values.indexOf(token),1);this._write(values);return add;}
    replace(oldToken,newToken){oldToken=validateToken(oldToken);newToken=validateToken(newToken);const values=this._tokens(),index=values.indexOf(oldToken);if(index<0)return false;if(oldToken===newToken)return true;values[index]=newToken;this._write(Array.from(new Set(values)));return true;}
    forEach(callback,thisArg){this._tokens().forEach((token,index)=>callback.call(thisArg,token,index,this));}
    keys(){return this._tokens().map((_,index)=>index)[Symbol.iterator]();}
    values(){return this._tokens()[Symbol.iterator]();}
    entries(){return this._tokens().map((token,index)=>[index,token])[Symbol.iterator]();}
    [Symbol.iterator](){return this.values();}
    toString(){return this.value;}
}
class CSSStyleSheet {
    constructor(ownerNode=null,href=null,text='',originClean=true) {
        this.ownerNode=ownerNode;this.href=href;this.disabled=false;this._text=text;this._originClean=originClean;
    }
    get cssRules() {
        if(!this._originClean || (this.href && new URL(this.href).origin!==location.origin))
            throw new DOMException('Cannot access cross-origin stylesheet rules','SecurityError');
        const text=this.ownerNode?.localName==='style'?this.ownerNode.textContent:this._text;
        const rules=[];let start=0,depth=0,quote='';
        for(let i=0;i<text.length;++i) {
            const c=text[i];
            if(c==='\\'){++i;continue;}
            if(quote){if(c===quote)quote='';continue;}
            if(c==='"'||c==="'"){quote=c;continue;}
            if(c==='{')++depth;
            if(c==='}' && --depth===0) {
                const cssText=text.slice(start,i+1).trim(),brace=cssText.indexOf('{');
                if(cssText)rules.push({cssText,type:cssText[0]==='@'?0:1,
                    selectorText:cssText.slice(0,brace).trim(),
                    style:styleFor({getAttribute:()=>cssText.slice(brace+1,-1),setAttribute(){}})});
                start=i+1;
            }
        }
        return list(rules);
    }
    get rules() { return this.cssRules; }
    insertRule(text,index=0) {
        const rules=Array.from(this.cssRules,r=>r.cssText);
        if(index<0||index>rules.length)throw new DOMException('Invalid rule index','IndexSizeError');
        rules.splice(index,0,String(text));this.replaceSync(rules.join('\n'));return index;
    }
    deleteRule(index) {
        const rules=Array.from(this.cssRules,r=>r.cssText);
        if(index<0||index>=rules.length)throw new DOMException('Invalid rule index','IndexSizeError');
        rules.splice(index,1);this.replaceSync(rules.join('\n'));
    }
    replaceSync(text) {
        if(this.href)throw new DOMException('Cannot replace an external stylesheet','NotAllowedError');
        this._text=String(text);if(this.ownerNode)this.ownerNode.textContent=this._text;
    }
    addRule(selector,text,index=this.cssRules.length) { this.insertRule(selector+'{'+text+'}',index);return -1; }
    removeRule(index=0) { this.deleteRule(index); }
}
class CSSStyleDeclaration {
    constructor(element) { Object.defineProperty(this, '_element', {value: element}); }
    _name(name) { name = String(name); return name.startsWith('--') ? name : name.toLowerCase(); }
    _entries() {
        const text = this._element.getAttribute('style') || '';
        if(text!==this._cachedText) {
            this._cachedText=text;
            this._cachedEntries=text.split(/;(?![^()]*\))/).map(v => { const i = v.indexOf(':'); return i < 0 ? null : [this._name(v.slice(0,i).trim()), v.slice(i+1).trim()]; }).filter(v => v && v[0]);
        }
        return this._cachedEntries;
    }
    get cssText() { return this._element.getAttribute('style') || ''; }
    set cssText(text) { this._element.setAttribute('style', String(text)); }
    get length() { return this._entries().length; }
    item(i) { return this._entries()[i]?.[0] || ''; }
    _property(name) { const entries=this._entries().filter(v=>v[0]===this._name(name)).reverse();return entries.find(v=>/!important\s*$/i.test(v[1]))||entries[0]; }
    getPropertyValue(name) { return this._property(name)?.[1].replace(/\s*!important\s*$/i, '') || ''; }
    getPropertyPriority(name) { return /!important\s*$/i.test(this._property(name)?.[1] || '') ? 'important' : ''; }
    setProperty(name, value, priority = '') {
        name = this._name(name);
        const entries = this._entries().filter(v => v[0] !== name);
        if (String(value)) entries.push([name, String(value) + (priority ? ' !important' : '')]);
        this.cssText = entries.map(v => v.join(': ')).join('; ');
    }
    removeProperty(name) { const old = this.getPropertyValue(name); this.setProperty(name, ''); return old; }
}
function styleFor(element) {
    return new Proxy(new CSSStyleDeclaration(element), {
        get(target, key) {
            if (key in target || typeof key === 'symbol') { const v = Reflect.get(target, key); return typeof v === 'function' ? v.bind(target) : v; }
            if (/^\d+$/.test(key)) return target.item(Number(key));
            return target.getPropertyValue(cssName(key));
        },
        set(target, key, value) { if (key === 'cssText') target.cssText = value; else target.setProperty(cssName(key), value); return true; }
    });
}
class Element extends Node {
    get tagName() { return this.nodeName; }
    get localName() { return this.tagName.toLowerCase(); }
    get namespaceURI() { return 'http://www.w3.org/1999/xhtml'; }
    get prefix() { return null; }
    get children() { return collection(() => Array.from(this.childNodes).filter(n => n.nodeType === 1)); }
    get childElementCount() { return this.children.length; }
    get firstElementChild() { return this.children[0] || null; }
    get lastElementChild() { return this.children[this.children.length - 1] || null; }
    get nextElementSibling() { let n = this.nextSibling; while (n && n.nodeType !== 1) n = n.nextSibling; return n; }
    get previousElementSibling() { let n = this.previousSibling; while (n && n.nodeType !== 1) n = n.previousSibling; return n; }
    get attributes() {
        const attributes = list(native('attrs', this._id).map(([name,value]) => ({name, nodeName: name, value, nodeValue: value, specified: true, expando: false, ownerElement: this})));
        for (const attribute of attributes) if (!(attribute.name in attributes)) attributes[attribute.name] = attribute;
        attributes.getNamedItem = name => attributes.find(a => a.name === name) || null;
        return attributes;
    }
    get sheet() {
        if(this.localName==='style')return this._sheet||(this._sheet=new CSSStyleSheet(this));
        if(this.localName!=='link'||!this.rel.toLowerCase().split(/[\t\n\f\r ]+/).includes('stylesheet'))return null;
        const href=this.href;
        if(this._sheet?.href===href)return this._sheet;
        const data=native('control',this._id,'stylesheet',href);
        this._sheetStatus=data[0];
        return data[0]==='\x02'?(this._sheet=new CSSStyleSheet(this,href,data.slice(3),data[1]==='s')):null;
    }
    getAttribute(name) { return native('attr', this._id, String(name).toLowerCase()); }
    getAttributeNames() { return native('attrs',this._id).map(([name])=>name); }
    getAttributeNode(name) { return this.attributes.find(a => a.name === name) || null; }
    hasAttribute(name) { return this.getAttribute(name) !== null; }
    hasAttributes() { return this.attributes.length > 0; }
    setAttribute(name, value) {
        name = String(name).toLowerCase(); const oldValue = this.getAttribute(name);
        native('setattr', this._id, name, String(value)); notify(this, 'attributes', {attributeName: name, oldValue});
        if(name==='src'&&this.localName==='img')this._imageSourceChanged(oldValue);
    }
    removeAttribute(name) { name=String(name).toLowerCase();const oldValue = this.getAttribute(name); native('removeattr', this._id, name); notify(this, 'attributes', {attributeName:name,oldValue});if(name==='src'&&this.localName==='img')this._imageSourceChanged(oldValue); }
    getAttributeNS(_, name) { return this.getAttribute(name); }
    setAttributeNS(_, name, value) { this.setAttribute(name, value); }
    removeAttributeNS(_, name) { this.removeAttribute(name); }
    toggleAttribute(name, force) { const yes = force === undefined ? !this.hasAttribute(name) : !!force; if (yes) this.setAttribute(name, ''); else this.removeAttribute(name); return yes; }
    get innerHTML() { return native('get', this._id, 'inner'); }
    set innerHTML(value) { native('inner', this._id, String(value)); notify(this, 'childList'); }
    get outerHTML() { return native('get', this._id, 'outer'); }
    set outerHTML(value) { if (this.parentNode) { const fragment = document.createElement('div'); fragment.innerHTML = value; this.before(...Array.from(fragment.childNodes)); this.remove(); } }
    get innerText() { return this.textContent; } set innerText(value) { this.textContent = value; }
    querySelectorAll(selector) { return list(native('query', this._id, String(selector)).map(wrap)); }
    querySelector(selector) { return this.querySelectorAll(selector)[0] || null; }
    matches(selector) { return native('matches', this._id, String(selector)); }
    webkitMatchesSelector(selector) { return this.matches(selector); }
    closest(selector) { for (let n = this; n; n = n.parentElement) if (n.matches(selector)) return n; return null; }
    getElementsByTagName(tag) { return collection(() => this.querySelectorAll(String(tag))); }
    getElementsByTagNameNS(_, tag) { return this.getElementsByTagName(tag); }
    getElementsByClassName(names) {
        const classes = String(names).trim().split(/\s+/).filter(Boolean);
        return collection(() => classes.length ? this.querySelectorAll(classes.map(name => '.' + CSS.escape(name)).join('')) : []);
    }
    get style() { if (!this._style) this._style = styleFor(this); return this._style; }
    set style(value) { this.style.cssText = value; }
    get classList() { return this._classList||(this._classList=new DOMTokenList(this)); }
    get dataset() {
        if(this._dataset)return this._dataset;
        const nameFor = key => 'data-' + String(key).replace(/[A-Z]/g, c => '-' + c.toLowerCase());
        const keyFor = name => name.slice(5).replace(/-([a-z])/g, (_, c) => c.toUpperCase());
        const keys = () => this.getAttributeNames().filter(name => name.startsWith('data-')).map(keyFor);
        return this._dataset = new Proxy({}, {
            get: (target, key) => typeof key === 'string' ? this.getAttribute(nameFor(key)) ?? Reflect.get(target,key) :
                key===Symbol.toStringTag?'DOMStringMap':Reflect.get(target,key),
            set: (target, key, value) => {
                if(typeof key!=='string')return Reflect.set(target,key,value);
                if(/-[a-z]/.test(key))throw new DOMException('Invalid dataset property name.','SyntaxError');
                this.setAttribute(nameFor(key), value);return true;
            },
            deleteProperty: (target, key) => { if (typeof key !== 'string')return Reflect.deleteProperty(target,key);this.removeAttribute(nameFor(key));return true; },
            has: (target, key) => typeof key === 'string' && this.hasAttribute(nameFor(key)) || Reflect.has(target,key),
            ownKeys: target => Array.from(new Set([...Reflect.ownKeys(target),...keys()])),
            getOwnPropertyDescriptor: (target, key) => typeof key === 'string' && keys().includes(key) ?
                {configurable:true,enumerable:true,writable:true,value:this.getAttribute(nameFor(key))} : Reflect.getOwnPropertyDescriptor(target,key)
        });
    }
    insertAdjacentHTML(where, text) {
        const container = document.createElement('div'); container.innerHTML = text;
        const nodes = Array.from(container.childNodes);
        if (where === 'beforebegin') this.before(...nodes); else if (where === 'afterend') this.after(...nodes);
        else if (where === 'afterbegin') this.prepend(...nodes); else if (where === 'beforeend') this.append(...nodes);
        else throw new SyntaxError('Invalid insertion position');
    }
    insertAdjacentElement(where, element) {
        if(!(element instanceof Element))throw new TypeError('Expected an Element');
        if(where==='beforebegin')this.before(element);
        else if(where==='afterend')this.after(element);
        else if(where==='afterbegin')this.prepend(element);
        else if(where==='beforeend')this.append(element);
        else throw new DOMException('Invalid insertion position.','SyntaxError');
        return element;
    }
    getBoundingClientRect() {
        const measured=native('control',this===document.documentElement&&document.body?document.body._id:this._id,'geometry');
        if(measured && measured!=='\x01') {
            const [x,y,width,height]=measured.split(',').map(Number);
            return {x,y,top:y,left:x,right:x+width,bottom:y+height,width,height,toJSON(){return {x,y,width,height,top:y,left:x,right:x+width,bottom:y+height};}};
        }
        const width = 0, height = 0;
        return {x:0,y:0,top:0,left:0,right:width,bottom:height,width,height,toJSON(){return {x:this.x,y:this.y,width:this.width,height:this.height};}};
    }
    getClientRects() { const r = this.getBoundingClientRect(); return list(r.width || r.height ? [r] : []); }
    get offsetWidth() { return this.getBoundingClientRect().width; } get offsetHeight() { return this.getBoundingClientRect().height; }
    get clientWidth() {
        if(this===document.documentElement||this===document.body)return viewport()[0];
        const style=getComputedStyle(this);
        return Math.max(0,this.offsetWidth-parseFloat(style.borderLeftWidth||0)-parseFloat(style.borderRightWidth||0));
    }
    get clientHeight() {
        if(this===document.documentElement||this===document.body)return viewport()[1];
        const style=getComputedStyle(this);
        return Math.max(0,this.offsetHeight-parseFloat(style.borderTopWidth||0)-parseFloat(style.borderBottomWidth||0));
    }
    get offsetTop() { return this.getBoundingClientRect().top; } get offsetLeft() { return this.getBoundingClientRect().left; }
    get offsetParent() { return this.parentElement; }
    get scrollHeight() { return this===document.documentElement||this===document.body?scrollMetrics()[1]:this.clientHeight; }
    get scrollWidth() { return this===document.documentElement||this===document.body?scrollMetrics()[0]:this.clientWidth; }
    get scrollTop() { return this===document.documentElement||this===document.body?scrollY:0; }
    set scrollTop(value) { if(this===document.documentElement||this===document.body)scrollTo(scrollX,value); }
    get scrollLeft() { return this===document.documentElement||this===document.body?scrollX:0; }
    set scrollLeft(value) { if(this===document.documentElement||this===document.body)scrollTo(value,scrollY); }
    focus() {
        if(this.disabled || activeElement===this)return;
        const old=activeElement;
        if(old)old.blur();
        native('control',this._id,'focus');
        if(activeElement!==this) {
            activeElement=this;this.dispatchEvent(new Event('focus'));
            this.dispatchEvent(new Event('focusin',{bubbles:true}));
        }
    }
    blur() {
        native('control',this._id,'blur');
        if(activeElement===this){activeElement=null;this.dispatchEvent(new Event('blur'));this.dispatchEvent(new Event('focusout',{bubbles:true}));}
    }
    _selection() {
        const current=native('control',this._id,'selection');
        return current && current!=='\x01'?current.split(',').map(Number):this._selectionRange||[this.value.length,this.value.length];
    }
    get selectionStart(){return this._selection()[0];}
    set selectionStart(v){this.setSelectionRange(v,Math.max(Number(v),this.selectionEnd));}
    get selectionEnd(){return this._selection()[1];}
    set selectionEnd(v){this.setSelectionRange(Math.min(this.selectionStart,Number(v)),v);}
    setSelectionRange(start,end,direction='none') {
        end=Math.max(0,Math.min(this.value.length,Number(end)||0));start=Math.max(0,Math.min(end,Number(start)||0));
        this._selectionRange=[start,end];this.selectionDirection=direction;
        native('control',this._id,'selection',start+','+end);
    }
    select(){this.setSelectionRange(0,this.value.length);this.dispatchEvent(new Event('select',{bubbles:true}));}
    click() {
        if(this.disabled||this._clicking)return;
        this._clicking=true;
        try {
            const type=this.type.toLowerCase(),toggle=this.localName==='input'&&['checkbox','radio'].includes(type);
            const before=this.checked;
            if(toggle)this.checked=type==='radio'||!before;
            const event=new MouseEvent('click',{bubbles:true,cancelable:true});
            if(!this.dispatchEvent(event)){if(toggle)this.checked=before;return;}
            if(toggle){this.dispatchEvent(new Event('input',{bubbles:true}));this.dispatchEvent(new Event('change',{bubbles:true}));}
            else if(this.localName==='a'&&this.href)location.assign(this.href);
            else if(this.form&&(this.localName==='button'||this.localName==='input')){
                if(type==='reset')this.form.reset();
                else if(type==='submit'||this.localName==='button'&&!type)this.form.requestSubmit(this);
            }
        } finally {this._clicking=false;}
    }
    scrollIntoView() { native('control',this._id,'scrollIntoView'); }
    get value() {
        if(this.localName==='select')return Array.from(this.options).find(o=>o.selected)?.value??'';
        const current=native('control',this._id,'value'),stored=native('state',this._id,'value');
        return current!=='\x01'?current:stored??this.getAttribute('value')??(this.localName==='textarea'||this.localName==='option'?this.textContent:'');
    }
    set value(value) {
        value=String(value);
        if(this.localName==='select'){for(const option of this.options)option.selected=option.value===value;return;}
        native('control',this._id,'value',value);native('state',this._id,'value',value);
    }
    get defaultValue() { return this.localName==='textarea'?this.textContent:this.getAttribute('value')||''; }
    set defaultValue(v) { if(this.localName==='textarea')this.textContent=v;else this.setAttribute('value',v); }
    get checked() { const current=native('control',this._id,'checked');return current!=='\x01'?current==='true':(native('state',this._id,'checked')??String(this.hasAttribute('checked')))==='true'; }
    set checked(v) { native('control',this._id,'checked',String(!!v));native('state',this._id,'checked',String(!!v)); }
    get defaultChecked() { return this.hasAttribute('checked'); } set defaultChecked(v) { this.toggleAttribute('checked',!!v); }
    get selected() {
        const current=native('control',this._id,'selected'),stored=native('state',this._id,'selected');
        if(current!=='\x01')return current==='true';
        if(stored!==null)return stored==='true';
        if(this.hasAttribute('selected'))return true;
        const select=this.closest('select');return !!select&&!select.multiple&&select.options[0]===this&&!Array.from(select.options).some(o=>o.hasAttribute('selected')||native('state',o._id,'selected')==='true');
    }
    set selected(v) {
        const select=this.closest('select');
        if(v&&select&&!select.multiple)for(const option of select.options)if(option!==this){native('control',option._id,'selected','false');native('state',option._id,'selected','false');}
        native('control',this._id,'selected',String(!!v));native('state',this._id,'selected',String(!!v));
    }
    get defaultSelected() { return this.hasAttribute('selected'); } set defaultSelected(v) { this.toggleAttribute('selected',!!v); }
    get options() { return this.getElementsByTagName('option'); }
    get selectedIndex() { return Array.from(this.options).findIndex(o => o.selected); } set selectedIndex(i) { Array.from(this.options).forEach((o,n) => o.selected = n === Number(i)); }
    get form() { return this.closest('form'); }
    get elements() { return collection(() => this.querySelectorAll('input,button,select,textarea,fieldset')); }
    submit(submitter) {
        if(this.localName!=='form')return;
        const values=new URLSearchParams();
        for(const element of this.elements) {
            const type=element.type.toLowerCase();
            if(!element.name||element.disabled||['submit','button','image','reset','file'].includes(type)||['checkbox','radio'].includes(type)&&!element.checked)continue;
            if(element.localName==='select') {
                for(const option of element.options)if(option.selected&&!option.disabled)values.append(element.name,option.value);
            } else values.append(element.name,element.value);
        }
        const url=new URL(this.action),method=(this.method||'get').toLowerCase();
        if(submitter&&submitter.name&&!submitter.disabled)values.append(submitter.name,submitter.value);
        if(method==='get')url.search=values.toString()?'?'+values:'';
        native('control',this._id,'form-submit',method+'\n'+url.href+'\n'+(method==='post'?values.toString():''));
    }
    requestSubmit(submitter=null) { const event=new Event('submit',{bubbles:true,cancelable:true});event.submitter=submitter;if(this.dispatchEvent(event))this.submit(submitter); }
    reset() { if(this.dispatchEvent(new Event('reset',{bubbles:true,cancelable:true})))for(const e of this.elements){e.value=e.defaultValue;e.checked=e.defaultChecked;if(e.localName==='select')for(const o of e.options)o.selected=o.defaultSelected;} }
    get href() { const value = this.getAttribute('href'); return value == null ? '' : new URL(value, document.baseURI).href; } set href(v) { this.setAttribute('href', v); }
    _imageSourceChanged(oldValue) {
        const oldSrc=oldValue==null?'':new URL(oldValue,document.baseURI).href,src=this.src;
        if(oldSrc===src)return;
        imageState.delete(this);
        const pending=imageDecodes.get(this)||[];imageDecodes.delete(this);
        for(const item of pending)item.reject(new TypeError('Image source changed before decoding'));
        if(!src||this.isConnected)return;
        const started=native('control',this._id,'imagePreload',src);
        if(started!=='1')__dillo.image(this._id,'error',0,0);
    }
    get src() { const value = this.getAttribute('src'); return value == null ? '' : new URL(value, document.baseURI).href; } set src(v) { this.setAttribute('src', v); }
    get naturalWidth() { return imageState.get(this)?.width || 0; }
    get naturalHeight() { return imageState.get(this)?.height || 0; }
    get complete() { return imageState.has(this) ? true : !this.hasAttribute('src') && !this.hasAttribute('srcset'); }
    decode() {
        if (this.complete && this.naturalWidth) return Promise.resolve();
        if (imageState.get(this)?.error) return Promise.reject(new TypeError('Image could not be decoded'));
        return new Promise((resolve,reject)=>{let list=imageDecodes.get(this);if(!list)imageDecodes.set(this,list=[]);list.push({resolve,reject});});
    }
    get action() { return new URL(this.getAttribute('action') || document.URL, document.baseURI).href; } set action(v) { this.setAttribute('action', v); }
    get text() { return this.textContent; } set text(v) { this.textContent = v; }
}
for (const [property, attribute] of Object.entries({id:'id',className:'class',name:'name',type:'type',title:'title',rel:'rel',target:'target',method:'method',htmlFor:'for',lang:'lang',dir:'dir',alt:'alt',content:'content',httpEquiv:'http-equiv',accept:'accept',placeholder:'placeholder',crossOrigin:'crossorigin'})) {
    Object.defineProperty(Element.prototype, property, {configurable:true,get(){ return this.getAttribute(attribute) || ''; },set(v){ this.setAttribute(attribute, v); }});
}
for (const property of ['disabled','hidden','multiple','readOnly','required','autofocus','async','defer']) {
    Object.defineProperty(Element.prototype, property, {configurable:true,get(){ return property === 'async' && this.localName === 'script' ? native('scriptasync',this._id) : this.hasAttribute(property.toLowerCase()); },set(v){ if(property === 'async' && this.localName === 'script') native('scriptasync',this._id,String(!!v)); this.toggleAttribute(property.toLowerCase(), v); }});
}
class DocumentFragment extends Node {}
class HTMLAnchorElement extends Element {
    toString() { return this.href; }
}
for (const property of ['origin','protocol','username','password','host','hostname','port','pathname','search','hash']) {
    Object.defineProperty(HTMLAnchorElement.prototype,property,{configurable:true,
        get(){try{return this.hasAttribute('href')?new URL(this.href)[property]:'';}catch(_){return '';}},
        ...(property==='origin'?{}:{set(value){try{const url=new URL(this.getAttribute('href')||'',document.baseURI);url[property]=value;this.href=url.href;}catch(_){}}})
    });
}
for (const name of ['querySelectorAll','querySelector','getElementsByTagName','getElementsByClassName']) DocumentFragment.prototype[name] = Element.prototype[name];
class Document extends Node {
    _root(tag) {
        const revision=native('revision'),saved=documentRoots.get(tag);
        if(saved?.revision===revision)return saved.node;
        const node=wrap(native('query',this._id,tag)[0]||0);
        documentRoots.set(tag,{revision,node});return node;
    }
    get documentElement() { return this._root('html'); }
    get scrollingElement() { return this.documentElement; }
    get head() { return this._root('head'); }
    get body() { return this._root('body'); }
    get URL() { return location.href; } get documentURI() { return this.URL; }
    get location() { return location; } set location(v) { location.assign(v); }
    get baseURI() { const base = this.querySelector('base[href]'); return base ? new URL(base.getAttribute('href'), location.href).href : location.href; }
    get title() { return this.querySelector('title')?.textContent || ''; }
    set title(value) { let title = this.querySelector('title'); if (!title && this.head) { title = this.createElement('title'); this.head.appendChild(title); } if (title) title.textContent = String(value); native('title', 0, String(value)); }
    get readyState() { return state; }
    get activeElement() { return activeElement || this.body; }
    get defaultView() { return globalThis; } get parentWindow() { return globalThis; }
    get compatMode() { return 'CSS1Compat'; } get characterSet() { return 'UTF-8'; } get charset() { return 'UTF-8'; }
    get contentType() { return 'text/html'; }
    get referrer() { return ''; }
    get forms() { return this.getElementsByTagName('form'); }
    get images() { return this.getElementsByTagName('img'); }
    get scripts() { return this.getElementsByTagName('script'); }
    get links() { return collection(() => this.querySelectorAll('a[href],area[href]')); }
    get styleSheets() { return list(Array.from(this.querySelectorAll('style,link'), n => n.sheet).filter(Boolean)); }
    get currentScript() { return wrap(globalThis.__currentScript || 0); }
    get cookie() { return native('control', this._id, 'cookie') === '\x01' ? '' : native('control', this._id, 'cookie'); }
    set cookie(value) { native('control', this._id, 'cookie', String(value)); }
    createElement(name) { return wrap(native('create', 1, String(name).toLowerCase())); }
    createElementNS(_, name) { return this.createElement(name); }
    createTextNode(text) { return wrap(native('create', 3, String(text))); }
    createComment(text) { return wrap(native('create', 8, String(text))); }
    createDocumentFragment() { return wrap(native('create', 11, '')); }
    createEvent(type) { return type === 'CustomEvent' ? new CustomEvent('') : type === 'MouseEvents' ? new MouseEvent('') : new Event(''); }
    getElementById(id) { return this.querySelector('#' + CSS.escape(String(id))); }
    elementFromPoint(x, y) {
        x=Number(x);y=Number(y);
        if(!Number.isFinite(x)||!Number.isFinite(y))return null;
        let element=wrap(Number(native('control',this._id,'elementFromPoint',x+','+y)));
        while(element&&element.nodeType!==1)element=element.parentElement;
        return element;
    }
    getElementsByName(name) { return collection(() => this.querySelectorAll('[name="' + String(name).replace(/["\\]/g, '\\$&') + '"]')); }
    hasFocus() { return true; }
    write(...values) { native('write', 0, values.join('')); }
    writeln(...values) { this.write(...values, '\n'); }
    open() { return this; } close() {}
    importNode(node, deep) { return node.cloneNode(deep); }
    adoptNode(node) { node.remove(); return node; }
}
for (const name of ['querySelectorAll','querySelector','getElementsByTagName','getElementsByTagNameNS','getElementsByClassName']) Document.prototype[name] = Element.prototype[name];
function wrap(id) {
    if (!id) return null;
    if (cache.has(id)) return cache.get(id);
    const type = native('get', id, 'type');
    const C = type === 9 ? Document : type === 1 ? ['A','AREA'].includes(native('get',id,'name'))?HTMLAnchorElement:Element : type === 3 ? Text : type === 8 ? Comment : type === 11 ? DocumentFragment : Node;
    const node = new C(id); cache.set(id, node); return node;
}
class URLSearchParams {
    constructor(value = '') {
        if (typeof value === 'string') this._pairs = value.replace(/^\?/, '').split('&').filter(Boolean).map(pair => { const i = pair.indexOf('='); return [i < 0 ? pair : pair.slice(0,i), i < 0 ? '' : pair.slice(i+1)].map(v => { try { return decodeURIComponent(v.replace(/\+/g,' ')); } catch (_) { return v; } }); });
        else this._pairs = Array.isArray(value) || value[Symbol.iterator] ? Array.from(value, p => [String(p[0]),String(p[1])]) : Object.entries(value).map(([k,v]) => [k,String(v)]);
    }
    append(k,v) { this._pairs.push([String(k),String(v)]); this._changed?.(); }
    set(k,v) {
        k=String(k);v=String(v);const index=this._pairs.findIndex(p=>p[0]===k);
        if(index<0)this._pairs.push([k,v]);
        else { this._pairs[index][1]=v;this._pairs=this._pairs.filter((p,i)=>p[0]!==k||i===index); }
        this._changed?.();
    }
    get(k) { return this._pairs.find(p => p[0] === String(k))?.[1] ?? null; }
    getAll(k) { return this._pairs.filter(p => p[0] === String(k)).map(p => p[1]); }
    has(k,value) { return this._pairs.some(p => p[0] === String(k) && (arguments.length<2||p[1]===String(value))); }
    delete(k,value) { this._pairs = this._pairs.filter(p => p[0] !== String(k) || arguments.length>1&&p[1]!==String(value)); this._changed?.(); }
    get size() { return this._pairs.length; }
    sort() { this._pairs.sort((a,b)=>a[0]<b[0]?-1:a[0]>b[0]?1:0); this._changed?.(); }
    toString() { return this._pairs.map(p => p.map(v => encodeURIComponent(v).replace(/[!'()~]/g,c=>'%'+c.charCodeAt(0).toString(16).toUpperCase()).replace(/%20/g,'+')).join('=')).join('&'); }
    entries() { return this._pairs[Symbol.iterator](); } keys() { return this._pairs.map(p=>p[0])[Symbol.iterator](); } values() { return this._pairs.map(p=>p[1])[Symbol.iterator](); }
    forEach(fn, self) { this._pairs.forEach(([k,v]) => fn.call(self,v,k,this)); }
    [Symbol.iterator]() { return this.entries(); }
}
class URL {
    constructor(value,base) { this._parts=native('urlparse',0,String(value),base===undefined?'':String(base));this._params=null; }
    get href() { return this._parts.href; }
    set href(value) { this._parts=native('urlparse',0,String(value),'');this._sync(); }
    get origin() { return this._parts.origin; }
    _sync() { if(this._params)this._params._pairs=new URLSearchParams(this.search)._pairs; }
    get searchParams() {
        if(!this._params){this._params=new URLSearchParams(this.search);this._params._changed=()=>{this.search=this._params.toString();};}
        return this._params;
    }
    toString() { return this.href; } toJSON() { return this.href; }
    static canParse(value,base) { try{new URL(value,base);return true;}catch(_){return false;} }
    static parse(value,base) { try{return new URL(value,base);}catch(_){return null;} }
}
for(const key of ['protocol','host','hostname','port','pathname','search','hash','username','password'])
    Object.defineProperty(URL.prototype,key,{get(){return this._parts[key];},set(value){this._parts=native('urlparse',0,this.href,'',key,String(value));this._sync();}});
const location = {
    get href() { return native('url'); }, set href(value) { this.assign(value); },
    assign(value) { native('navigate', 0, new URL(value, this.href).href); },
    replace(value) { native('navigate', 1, new URL(value, this.href).href); },
    reload() { native('control', 1, 'historyGo', '0'); },
    toString() { return this.href; }
};
for (const key of ['protocol','host','hostname','port','pathname','search','hash','origin']) Object.defineProperty(location, key, {get(){return new URL(this.href)[key];},set(v){const u = new URL(this.href); u[key] = v; this.assign(u.href);}});
const document = wrap(1);
class Selection {
    toString() {
        if(activeElement && /^(input|textarea)$/.test(activeElement.localName))
            return activeElement.value.slice(activeElement.selectionStart,activeElement.selectionEnd);
        const text=native('control',1,'selectionText');return text==='\x01'?'':text;
    }
    get isCollapsed() { return !this.toString(); }
    get rangeCount() { return this.isCollapsed?0:1; }
    get type() { return this.isCollapsed?'None':'Range'; }
    removeAllRanges() {
        native('control',1,'selectionText','');
        if(activeElement && /^(input|textarea)$/.test(activeElement.localName))
            activeElement.setSelectionRange(activeElement.selectionEnd,activeElement.selectionEnd);
    }
    empty() { this.removeAllRanges(); }
}
const selection = new Selection();
globalThis.Selection=Selection;globalThis.getSelection=()=>selection;
Document.prototype.getSelection=()=>selection;
Object.assign(globalThis, {Event,CustomEvent,MouseEvent,KeyboardEvent,EventTarget,Node,Element,HTMLElement:Element,HTMLAnchorElement,HTMLAreaElement:HTMLAnchorElement,HTMLInputElement:Element,HTMLFormElement:Element,HTMLImageElement:Element,HTMLScriptElement:Element,Document,HTMLDocument:Document,DocumentFragment,Text,Comment,DOMException,DOMTokenList,CSSStyleSheet,CSSStyleDeclaration,URL,URLSearchParams,document});
Object.defineProperty(globalThis, 'location', {configurable:false,get:()=>location,set:v=>location.assign(v)});
globalThis.window = globalThis; globalThis.self = globalThis; globalThis.top = globalThis; globalThis.parent = globalThis;
globalThis.frames = globalThis; globalThis.length = 0;
for (const name of ['addEventListener','removeEventListener','dispatchEvent']) globalThis[name] = EventTarget.prototype[name].bind(globalThis);
for (const name of ['click','dblclick','mousedown','mouseup','mousemove','mouseover','mouseout','mouseenter','mouseleave','focus','blur','focusin','focusout','keydown','keyup','keypress','input','change','submit','reset','load','error','resize','scroll','readystatechange']) {
    Element.prototype['on' + name] = null;
    globalThis['on' + name] = null;
}
const startTime = Date.now();
globalThis.performance = {now:()=>Date.now()-startTime,timeOrigin:startTime,timing:{navigationStart:startTime},getEntriesByType:()=>[],mark(){},measure(){}};
globalThis.navigator = {userAgent:native('ua'),appName:'Netscape',appVersion:native('ua'),platform:'Linux',language:'zh-CN',languages:['zh-CN','en'],cookieEnabled:true,mimeTypes:list([]),plugins:list([]),javaEnabled:()=>false,onLine:true,hardwareConcurrency:1,maxTouchPoints:0};
function viewport() {
    const v=native('control',1,'viewport');
    const values=v&&v!=='\x01'?v.split(',').map(Number):[800,600,0,0];
    if(pendingWindowScroll){values[2]=pendingWindowScroll[0];values[3]=pendingWindowScroll[1];}
    return values;
}
function scrollMetrics() {
    const value=native('control',1,'scrollMetrics');
    return value&&value!=='\x01'?value.split(',').map(Number):viewport().slice(0,2);
}
globalThis.screen={get width(){return viewport()[0];},get height(){return viewport()[1];},get availWidth(){return this.width;},get availHeight(){return this.height;},colorDepth:24,pixelDepth:24};
for(const [key,index] of Object.entries({innerWidth:0,innerHeight:1,outerWidth:0,outerHeight:1,pageXOffset:2,pageYOffset:3,scrollX:2,scrollY:3}))
    Object.defineProperty(globalThis,key,{get:()=>viewport()[index],configurable:true});
globalThis.devicePixelRatio=1;
function scrollCoordinates(x,y,relative) {
    const currentX=scrollX,currentY=scrollY;
    if(!pendingScrollTimer)pendingScrollOrigin=[currentX,currentY];
    if (x!==null&&typeof x==='object') {y=x.top===undefined?(relative?0:currentY):x.top;x=x.left===undefined?(relative?0:currentX):x.left;}
    const coordinate=v=>Number.isFinite(Number(v))?Math.max(-2147483647,Math.min(2147483647,Math.trunc(Number(v)))):0;
    x=coordinate(x)+(relative?currentX:0);y=coordinate(y)+(relative?currentY:0);
    native('control',1,'scroll',coordinate(x)+','+coordinate(y));
    pendingWindowScroll=[coordinate(x),coordinate(y)];
    clearTimeout(pendingScrollTimer);
    pendingScrollTimer=setTimeout(()=>{
        pendingScrollTimer=0;
        const target=pendingWindowScroll;
        if(pendingScrollOrigin&&target&&
           (pendingScrollOrigin[0]!==target[0]||pendingScrollOrigin[1]!==target[1])){
            globalThis.dispatchEvent(new Event('scroll'));
            document.dispatchEvent(new Event('scroll'));
        }
        pendingScrollOrigin=null;pendingWindowScroll=null;
    },0);
}
globalThis.scroll=globalThis.scrollTo=(x,y)=>scrollCoordinates(x,y,false);
globalThis.scrollBy=(x,y)=>scrollCoordinates(x,y,true);
globalThis.console = Object.fromEntries(['log','info','warn','error','debug','trace','assert'].map(name => [name,(...args)=>native('log',0,args.map(v=>typeof v==='string'?v:String(v)).join(' '))]));
globalThis.setTimeout = (fn, delay = 0, ...args) => { const id = nextTimer++; timers.set(id,{fn,args,delay:Math.max(0,Number(delay)||0),repeat:false}); native('timer',id,Math.max(0,Math.min(2147483647,Number(delay)||0))); return id; };
globalThis.setInterval = (fn, delay = 0, ...args) => { const id = setTimeout(fn,Math.max(4,Number(delay)||0),...args); timers.get(id).repeat = true; return id; };
globalThis.clearTimeout = globalThis.clearInterval = id => { timers.delete(Number(id)); native('timer',Number(id),-1); };
globalThis.queueMicrotask = queueJob;
globalThis.requestAnimationFrame = fn => setTimeout(()=>fn(performance.now()),16);
globalThis.cancelAnimationFrame = clearTimeout;
globalThis.getComputedStyle = element => {
    if(!(element instanceof Element))throw new TypeError('Expected Element');
    const css=native('control',element._id,'computedStyle');
    // During native events the last committed tree cannot be replaced. A
    // newly inserted element can still expose its default and inline style.
    const tag=element.localName;
    const display=/^(div|p|section|article|header|footer|main|nav|aside|form|h[1-6]|ul|ol|pre|blockquote|html|body)$/.test(tag)?'block':tag==='li'?'list-item':tag==='table'?'table':tag==='tr'?'table-row':/^(td|th)$/.test(tag)?'table-cell':/^(style|script|head|meta|link|title)$/.test(tag)?'none':'inline';
    const fallback='display:'+display+';position:static;'+(element.getAttribute('style')||'');
    const declaration=styleFor({getAttribute:()=>css==='\x01'?fallback:css||'',setAttribute(){throw new TypeError('Read-only style');}});
    return new Proxy(declaration,{get(target,key){if(key==='width'||key==='height')return element.getBoundingClientRect()[key]+'px';return target[key];},set(){throw new TypeError('Read-only style');}});
};
function mediaMatches(media) {
    return String(media).split(',').some(query=>{
        query=query.trim().toLowerCase();const negate=query.startsWith('not ');query=query.replace(/^(not|only)\s+/,'');
        let match=!/^(print|speech)/.test(query);
        for(const term of query.matchAll(/\((min-|max-)?(width|height):\s*([\d.]+)(px|em|rem)\)/g)) {
            const size=viewport()[term[2]==='width'?0:1],threshold=Number(term[3])*(term[4]==='px'?1:16);
            match=match&&(term[1]==='min-'?size>=threshold:term[1]==='max-'?size<=threshold:size===threshold);
        }
        if(/\(prefers-color-scheme:\s*dark\)/.test(query))match=false;
        if(/\(prefers-reduced-motion:\s*reduce\)/.test(query))match=false;
        return negate?!match:match;
    });
}
globalThis.matchMedia = media => {const result=Object.assign(new EventTarget(),{media:String(media),onchange:null,addListener(fn){this.addEventListener('change',fn);},removeListener(fn){this.removeEventListener('change',fn);}});Object.defineProperty(result,'matches',{get:()=>mediaMatches(result.media)});return result;};
function cssSupportsDeclaration(property,value) {
    property=String(property).toLowerCase();value=String(value).trim().toLowerCase();
    if(property.startsWith('--'))return !!value&&!/[;{}]/.test(value);
    if(!value)return false;
    const enums={
        display:['block','inline','inline-block','list-item','none','table','table-row-group','table-header-group','table-footer-group','table-row','table-cell','flex','inline-flex','grid','inline-grid','-webkit-box','-webkit-inline-box'],
        position:['static','relative','absolute','fixed'],overflow:['visible','hidden','scroll','auto'],
        visibility:['visible','hidden'],
        'box-sizing':['content-box','border-box'],'flex-direction':['row','row-reverse','column','column-reverse'],
        'flex-wrap':['nowrap','wrap','wrap-reverse'],
        'align-content':['normal','stretch','flex-start','flex-end','center','baseline','space-between','space-around','space-evenly','start','end'],
        'align-items':['normal','stretch','flex-start','flex-end','center','baseline','start','end'],
        'align-self':['auto','normal','stretch','flex-start','flex-end','center','baseline','start','end'],
        'justify-content':['normal','stretch','flex-start','flex-end','center','space-between','space-around','space-evenly','start','end'],
        'justify-items':['normal','stretch','flex-start','flex-end','center','baseline','start','end'],
        'justify-self':['auto','normal','stretch','flex-start','flex-end','center','baseline','start','end'],
        'white-space':['normal','pre','nowrap','pre-wrap','pre-line'],
        'word-break':['normal','break-all','keep-all','break-word'],
        'overflow-wrap':['normal','break-word','anywhere'],'word-wrap':['normal','break-word','anywhere'],
        '-webkit-box-orient':['horizontal','vertical'],
        'object-fit':['fill','contain','cover','none','scale-down'],
        'font-style':['normal','italic','oblique'],'font-variant':['normal','small-caps'],
        'font-weight':['normal','bold','bolder','lighter','light'],
        'font-size':['xx-small','x-small','small','medium','large','x-large','xx-large','smaller','larger'],
        'line-height':['normal'],'text-align':['left','right','center','justify'],
        'text-transform':['none','capitalize','uppercase','lowercase']
    };
    if(enums[property]) {
        if(enums[property].includes(value)||property==='font-weight'&&/^[1-9]00$/.test(value))return true;
        if(property!=='font-size'&&property!=='line-height')return false;
    }
    if(property==='-webkit-line-clamp')return value==='none'||/^[1-9]\d{0,2}$/.test(value)&&Number(value)<=100;
    const lengths=new Set(['width','height','min-width','max-width','min-height','max-height','top','right','bottom','left','margin','margin-top','margin-right','margin-bottom','margin-left','padding','padding-top','padding-right','padding-bottom','padding-left','gap','row-gap','column-gap','flex-basis','font-size','line-height','letter-spacing','word-spacing','border-radius','outline-width','outline-offset']);
    if(lengths.has(property))return value==='auto'&&/^(width|height|min-|max-|margin|flex-basis)/.test(property)||/^(?:0|[+-]?(?:\d+(?:\.\d*)?|\.\d+)(?:px|em|ex|ch|rem|vw|vh|vmin|vmax|%|mm|cm|in|pt|pc)?|calc\(.+\))$/i.test(value);
    if(property==='grid-template-columns'||property==='grid-template-rows')return /^(none|auto|(?:repeat\(\s*(?:\d+|auto-fill|auto-fit)\s*,\s*)?(?:minmax\([^;]+\)|auto|min-content|max-content|\d*\.?\d+(?:px|%|fr|em|rem))(?:\s+(?:minmax\([^;]+\)|auto|min-content|max-content|\d*\.?\d+(?:px|%|fr|em|rem)))*(?:\s*\))?)$/i.test(value);
    if(property==='transform')return /^(none|translate(?:x|y|3d)?\(.+\)(?:\s+translate(?:x|y|3d)?\(.+\))*)$/i.test(value);
    if(property==='color'||property==='background-color'||property==='background')return /^(transparent|currentcolor|#[0-9a-f]{3,8}|black|white|red|green|blue|yellow|gray|grey|navy|teal|maroon|purple|orange|silver|lime|fuchsia|aqua|rgba?\(.+\)|hsla?\(.+\)|linear-gradient\(.+\))$/i.test(value);
    return false;
}
function splitSupports(text,operator) {
    let depth=0,quote='',start=0;const parts=[];
    for(let i=0;i<text.length;i++) {
        const c=text[i];if(quote){if(c===quote&&text[i-1]!=='\\')quote='';continue;}
        if(c==='"'||c==="'"){quote=c;continue;}if(c==='(')depth++;else if(c===')')depth--;
        if(depth===0&&text.slice(i,i+operator.length).toLowerCase()===operator){parts.push(text.slice(start,i).trim());start=i+operator.length;i+=operator.length-1;}
    }
    if(!parts.length)return null;parts.push(text.slice(start).trim());return parts;
}
function cssSupports(condition,value) {
    if(arguments.length>1)return cssSupportsDeclaration(condition,value);
    let text=String(condition).trim();
    const ors=splitSupports(text,' or ');if(ors)return ors.some(part=>cssSupports(part));
    const ands=splitSupports(text,' and ');if(ands)return ands.every(part=>cssSupports(part));
    if(/^not\s+/i.test(text))return !cssSupports(text.replace(/^not\s+/i,''));
    while(text[0]==='('&&text[text.length-1]===')') {
        let depth=0,whole=true,quote='';
        for(let i=0;i<text.length-1;i++) {
            const c=text[i];if(quote){if(c===quote&&text[i-1]!=='\\')quote='';continue;}
            if(c==='"'||c==="'")quote=c;else if(c==='(')depth++;else if(c===')'&&--depth===0){whole=false;break;}
        }
        if(!whole)break;text=text.slice(1,-1).trim();
    }
    const colon=text.indexOf(':');return colon>0&&cssSupportsDeclaration(text.slice(0,colon),text.slice(colon+1));
}
globalThis.CSS = {escape(value) {
    const input=String(value);let result='';
    for(let i=0;i<input.length;i++) {
        const code=input.charCodeAt(i),char=input[i];
        if(code===0) { result+='\uFFFD'; continue; }
        if(code<32||code===127||i===0&&code>=48&&code<=57||
           i===1&&input.charCodeAt(0)===45&&code>=48&&code<=57) {
            result+='\\'+code.toString(16)+' ';continue;
        }
        if(i===0&&code===45&&input.length===1) { result+='\\-';continue; }
        if(code>=128||code===45||code===95||code>=48&&code<=57||
           code>=65&&code<=90||code>=97&&code<=122) result+=char;
        else result+='\\'+char;
    }
    return result;
}, supports:cssSupports};
globalThis.Image = function(width,height) { const n = document.createElement('img'); if(width)n.setAttribute('width',width); if(height)n.setAttribute('height',height); return n; };
globalThis.Option = function(text='',value=text,defaultSelected=false,selected=false) { const n=document.createElement('option');n.textContent=text;n.value=value;n.defaultSelected=defaultSelected;n.selected=selected;return n; };
globalThis.MutationObserver = class {
    constructor(callback) {
        if (typeof callback !== 'function') throw new TypeError('MutationObserver callback must be a function');
        this.callback=callback;this.targets=[];this.records=[];this.pending=false;
    }
    observe(target, options = {}) {
        if (!(target instanceof Node)) throw new TypeError('Expected a Node');
        options = options || {};
        const attributeFilter = options.attributeFilter === undefined ? null :
            Array.from(options.attributeFilter, name => String(name).toLowerCase());
        const attributeOldValue = !!options.attributeOldValue;
        const characterDataOldValue = !!options.characterDataOldValue;
        const attributes = options.attributes === undefined ?
            attributeOldValue || attributeFilter !== null : !!options.attributes;
        const characterData = options.characterData === undefined ?
            characterDataOldValue : !!options.characterData;
        if (attributeOldValue && !attributes || attributeFilter !== null && !attributes ||
            characterDataOldValue && !characterData)
            throw new TypeError('Inconsistent MutationObserver options');
        if (!options.childList && !attributes && !characterData)
            throw new TypeError('At least one mutation type must be observed');
        const registration = {target, options:{
            childList:!!options.childList, attributes, characterData,
            subtree:!!options.subtree, attributeOldValue, characterDataOldValue,
            attributeFilter
        }};
        const existing = this.targets.findIndex(item => item.target === target);
        if (existing < 0) this.targets.push(registration);
        else this.targets[existing] = registration;
        mutationObservers.add(this);
    }
    disconnect() { this.targets=[];this.records=[];mutationObservers.delete(this); }
    takeRecords() { const r=this.records;this.records=[];return r; }
};
globalThis.WebKitMutationObserver = MutationObserver;
class Storage {
    constructor(kind) { Object.defineProperty(this,'_kind',{value:kind});this._fallback=new Map(); }
    _load() { const data=native('control',1,this._kind);if(data==='\x01')return this._fallback;try{return new Map(JSON.parse(data));}catch(_){return new Map();} }
    _save(entries) { const data=JSON.stringify(Array.from(entries));if(data.length>5*1024*1024||native('control',1,this._kind,data)==='error')throw new Error('QuotaExceededError');this._fallback=entries; }
    get length() { return this._load().size; } key(i) { return Array.from(this._load().keys())[i] ?? null; }
    getItem(key) { return this._load().get(String(key)) ?? null; }
    setItem(key,value) { const entries=this._load();entries.set(String(key),String(value));this._save(entries); }
    removeItem(key) { const entries=this._load();entries.delete(String(key));this._save(entries); } clear() { this._save(new Map()); }
}
function storage(kind) {
    return new Proxy(new Storage(kind), {
        get(t,k) { if(k in t){const v=t[k];return typeof v==='function'?v.bind(t):v;}return t.getItem(k)??undefined; },
        set(t,k,v) { t.setItem(k,v);return true; },
        deleteProperty(t,k) { t.removeItem(k);return true; },
        ownKeys(t) { return Array.from(new Set([...Reflect.ownKeys(t),...t._load().keys()])); },
        getOwnPropertyDescriptor(t,k) { return Reflect.getOwnPropertyDescriptor(t,k)||{configurable:true,enumerable:true,value:t.getItem(k),writable:true}; }
    });
}
globalThis.Storage=Storage;globalThis.localStorage=storage('localStorage');globalThis.sessionStorage=storage('sessionStorage');
function headerName(value) {
    const name=String(value).toLowerCase();
    if(!/^[!#$%&'*+.^_`|~0-9a-z-]+$/.test(name))throw new TypeError('Invalid header name');
    return name;
}
function headerValue(value) {
    value=String(value).trim();
    if(/[\0\r\n]/.test(value))throw new TypeError('Invalid header value');
    return value;
}
class Headers {
    constructor(init={}) { this._values=new Map();for(const [k,v] of init?.[Symbol.iterator]?init:Object.entries(init||{}))this.append(k,v); }
    append(k,v) { k=headerName(k);v=headerValue(v);this._values.set(k,this.has(k)?this.get(k)+', '+v:v); }
    set(k,v) { this._values.set(headerName(k),headerValue(v)); } get(k) { return this._values.get(headerName(k))??null; }
    has(k) { return this._values.has(headerName(k)); } delete(k) { this._values.delete(headerName(k)); }
    entries() { return this._values.entries(); } [Symbol.iterator]() { return this.entries(); }
    keys() { return this._values.keys(); } values() { return this._values.values(); }
    forEach(fn,self) { this._values.forEach((v,k)=>fn.call(self,v,k,this)); }
    _serialize() { return Array.from(this,([k,v])=>k+': '+v+'\r\n').join(''); }
}
function requestOptions(method, body, init) {
    method=String(method||'GET').toUpperCase();
    if(!/^[!#$%&'*+.^_`|~0-9A-Z-]+$/.test(method)||['CONNECT','TRACE','TRACK'].includes(method))throw new TypeError('Invalid method');
    if((method==='GET'||method==='HEAD')&&body!=null)throw new TypeError('Request cannot have a body');
    const headers=new Headers(init);
    if(body!=null&&!headers.has('content-type'))headers.set('content-type',body instanceof URLSearchParams?'application/x-www-form-urlencoded;charset=UTF-8':'text/plain;charset=UTF-8');
    return {method,body:body==null?'':String(body),headers};
}
class Response {
    constructor(body='', options={}) { this._body=String(body);this.status=options.status??200;this.statusText=options.statusText||'';this.headers=new Headers(options.headers);this.url=options.url||'';this.bodyUsed=false; }
    get ok() { return this.status>=200&&this.status<300; }
    text() { if(this.bodyUsed)return Promise.reject(new TypeError('Body already consumed'));this.bodyUsed=true;return Promise.resolve(this._body); } json() { return this.text().then(JSON.parse); }
    clone() { if(this.bodyUsed)throw new TypeError('Body already consumed');return new Response(this._body,this); }
}
globalThis.Headers=Headers;globalThis.Response=Response;
globalThis.fetch = (url, options={}) => new Promise((resolve,reject)=>{
    if(options.mode&&options.mode!=='cors'&&options.mode!=='same-origin')throw new TypeError('Unsupported request mode');
    const target=new URL(url,document.baseURI);
    if(options.mode==='same-origin'&&target.origin!==location.origin)throw new TypeError('Cross-origin request');
    const request=requestOptions(options.method,options.body,options.headers);
    const credentials=options.credentials??'same-origin';
    if(!['omit','same-origin','include'].includes(credentials))throw new TypeError('Invalid credentials');
    if(options.signal?.aborted){reject(options.signal.reason);return;}
    const id=nextRequest++;
    const abort=()=>{if(!requests.has(id))return;requests.delete(id);native('abort',id);reject(options.signal.reason);};
    options.signal?.addEventListener('abort',abort,{once:true});
    requests.set(id,{resolve,reject,cleanup:()=>options.signal?.removeEventListener('abort',abort)});
    native('request',id,target.href,request.method,request.body,request.headers._serialize(),['omit','same-origin','include'].indexOf(credentials));
});
class XMLHttpRequest extends EventTarget {
    constructor(){super();this.readyState=0;this.status=0;this.responseText='';this.responseType='';this.responseURL='';this.timeout=0;this.withCredentials=false;this._headers=new Headers();this._request=0;}
    open(method,url,async=true){
        if(async===false)throw new Error('Synchronous XMLHttpRequest is unsupported');
        if(this._request)this.abort();
        this._method=requestOptions(method,null,{}).method;this._url=new URL(url,document.baseURI).href;
        this._headers=new Headers();this.status=0;this.responseText='';this.readyState=1;this.dispatchEvent(new Event('readystatechange'));
    }
    setRequestHeader(k,v){if(this.readyState!==1||this._request)throw new Error('InvalidStateError');this._headers.append(k,v);}
    getAllResponseHeaders(){return this.readyState>=2?this._responseHeaders||'':'';}
    getResponseHeader(name){return this.readyState>=2?this._responseHeaderValues?.get(name)??null:null;}
    overrideMimeType(type){this._mime=type;}
    send(body=null){
        if(this.readyState!==1||this._request)throw new Error('InvalidStateError');
        if(this._method==='GET'||this._method==='HEAD')body=null;
        const request=requestOptions(this._method,body,this._headers);
        const id=nextRequest++;this._request=id;
        requests.set(id,{xhr:this});
        if(this.timeout>0)this._timer=setTimeout(()=>this._cancel('timeout'),this.timeout);
        this.dispatchEvent(new Event('loadstart'));
        native('request',id,this._url,request.method,request.body,request.headers._serialize(),this.withCredentials?2:1);
    }
    _cancel(type){
        const pending=this._request;
        if(pending){requests.delete(pending);native('abort',pending);this._request=0;clearTimeout(this._timer);this.status=0;this.responseText='';this.readyState=4;this.dispatchEvent(new Event('readystatechange'));this.dispatchEvent(new Event(type));this.dispatchEvent(new Event('loadend'));}
        if(type==='abort')this.readyState=0;
    }
    abort(){this._cancel('abort');}
    get response(){if(this.responseType==='json'){try{return JSON.parse(this.responseText);}catch(_){return null;}}return this.responseText;}
}
Object.assign(XMLHttpRequest,{UNSENT:0,OPENED:1,HEADERS_RECEIVED:2,LOADING:3,DONE:4});
globalThis.XMLHttpRequest=XMLHttpRequest;
function abortSignal(signal, reason = new DOMException('The operation was aborted.','AbortError')) {
    if(signal.aborted)return;
    signal.aborted=true;signal.reason=reason;signal.dispatchEvent(new Event('abort'));
}
class AbortSignal extends EventTarget {
    constructor(){super();this.aborted=false;this.reason=undefined;}
    throwIfAborted(){if(this.aborted)throw this.reason;}
    static abort(reason){const signal=new AbortSignal();abortSignal(signal,reason);return signal;}
    static timeout(milliseconds){
        let remaining=Number(milliseconds);
        if(!Number.isFinite(remaining)||remaining<0||remaining>Number.MAX_SAFE_INTEGER)
            throw new RangeError('Invalid timeout.');
        remaining=Math.trunc(remaining);
        const signal=new AbortSignal();
        const schedule=()=>{
            const delay=Math.min(remaining,0x7fffffff);remaining-=delay;
            setTimeout(()=>remaining>0?schedule():abortSignal(signal,
                new DOMException('The operation timed out.','TimeoutError')),delay);
        };
        schedule();return signal;
    }
    static any(signals){
        if(signals==null||typeof signals[Symbol.iterator]!=='function')throw new TypeError('Expected an iterable of AbortSignals');
        const inputs=Array.from(signals);
        if(inputs.some(signal=>!(signal instanceof AbortSignal)))throw new TypeError('Expected an AbortSignal');
        const combined=new AbortSignal(),first=inputs.find(signal=>signal.aborted);
        if(first){abortSignal(combined,first.reason);return combined;}
        const subscriptions=[];
        for(const signal of new Set(inputs)){
            const listener=()=>{
                for(const [source,callback] of subscriptions)source.removeEventListener('abort',callback);
                abortSignal(combined,signal.reason);
            };
            subscriptions.push([signal,listener]);signal.addEventListener('abort',listener,{once:true});
        }
        return combined;
    }
}
globalThis.AbortSignal=AbortSignal;
globalThis.AbortController=class {
    constructor(){this.signal=new AbortSignal();}
    abort(reason){abortSignal(this.signal,reason);}
};
let historyToken='',historyState=null;
class PopStateEvent extends Event {
    constructor(type,options={}) { super(type,options);this.state=options.state??null; }
}
class HashChangeEvent extends Event {
    constructor(type,options={}) { super(type,options);this.oldURL=String(options.oldURL||'');this.newURL=String(options.newURL||''); }
}
function updateHistory(value,url,replace) {
    const target=new URL(url==null||url===''?location.href:url,document.baseURI);
    if(target.origin!==location.origin)throw new DOMException('History URL must have the document origin','SecurityError');
    let encoded;
    try { encoded=native('clone-serialize',0,value); }
    catch(e) { throw new DOMException(e.message,'DataCloneError'); }
    const result=native('control',1,replace?'historyReplace':'historyPush',target.href+'\n'+encoded);
    if(result==='security')throw new DOMException('History URL must have the document origin','SecurityError');
    if(result==='quota')throw new DOMException('History state storage is full','QuotaExceededError');
    historyToken='';
}
Object.assign(globalThis,{PopStateEvent,HashChangeEvent});
globalThis.history={
    get length() { return Number(native('control',1,'historyLength'))||1; },
    get state() {
        const token=native('control',1,'historyState');
        if(token!==historyToken) {
            historyToken=token;
            historyState=native('clone-deserialize',0,token.includes('\n')?token.slice(token.indexOf('\n')+1):'');
        }
        return historyState;
    },
    back() { this.go(-1); },forward() { this.go(1); },
    go(delta=0) { native('control',1,'historyGo',String(Number(delta)|0)); },
    pushState(value,_,url) { updateHistory(value,url,false); },
    replaceState(value,_,url) { updateHistory(value,url,true); }
};
globalThis.alert = message => console.log(message);
globalThis.atob = value => {
    const chars='ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/',s=String(value).replace(/\s|=/g,'');let result='',bits=0,b=0;
    for(const c of s){const n=chars.indexOf(c);if(n<0)throw new Error('InvalidCharacterError');b=b*64+n;bits+=6;if(bits>=8){bits-=8;result+=String.fromCharCode((b>>bits)&255);b&=(1<<bits)-1;}}return result;
};
globalThis.btoa = value => {const chars='ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/',s=String(value);let out='';for(let i=0;i<s.length;i+=3){const a=s.charCodeAt(i),b=s.charCodeAt(i+1),c=s.charCodeAt(i+2);if(a>255||b>255||c>255)throw new Error('InvalidCharacterError');const n=(a<<16)|((b||0)<<8)|(c||0);out+=chars[(n>>18)&63]+chars[(n>>12)&63]+(i+1<s.length?chars[(n>>6)&63]:'=')+(i+2<s.length?chars[n&63]:'=');}return out;};
globalThis.__dillo = {
    history(oldURL) {
        historyToken='';
        globalThis.dispatchEvent(new PopStateEvent('popstate',{state:history.state}));
        if(new URL(oldURL).hash!==location.hash)
            globalThis.dispatchEvent(new HashChangeEvent('hashchange',{oldURL,newURL:location.href}));
    },
    stylesheets() {
        const links=Array.from(document.querySelectorAll('link')).filter(link=>
            link.rel.toLowerCase().split(/[\t\n\f\r ]+/).includes('stylesheet'));
        for(const link of links) {
            if(link._loadedHref===link.href)continue;
            const sheet=link.sheet;
            if(!sheet&&link._sheetStatus!=='\x03')continue;
            link._loadedHref=link.href;link.dispatchEvent(new Event(sheet?'load':'error'));
        }
    },
    image(id,type,width,height) {
        const node=wrap(id);if(!node)return;
        const src=node.src||node.getAttribute('srcset')||'';
        if(imageState.get(node)?.src===src)return;
        if(type==='load')imageState.set(node,{width,height,src});else imageState.set(node,{width:0,height:0,error:true,src});
        const pending=imageDecodes.get(node)||[];imageDecodes.delete(node);
        for(const item of pending)type==='load'?item.resolve():item.reject(new TypeError('Image could not be decoded'));
        node.dispatchEvent(new Event(type));
    },
    event(id,type,key='',code=0,options={}) {
        const node=id?wrap(id):globalThis;
        if(!node)return true;
        if(type==='focus')activeElement=node;
        if(type==='blur'&&activeElement===node)activeElement=null;
        const event=type.startsWith('key')?new KeyboardEvent(type,{key,code:key,keyCode:code,which:code,bubbles:true,cancelable:true}):type==='click'?new MouseEvent(type,{...options,bubbles:true,cancelable:true}):new Event(type,{bubbles:!['focus','blur','load','error'].includes(type),cancelable:!['load','error','input','change'].includes(type)});
        event.isTrusted=true;
        const allowed=node.dispatchEvent(event);
        if(type==='focus'||type==='blur')node.dispatchEvent(new Event(type==='focus'?'focusin':'focusout',{bubbles:true}));
        return allowed;
    },
    timeout(id) { const t=timers.get(id);if(!t)return;if(!t.repeat)timers.delete(id);try{if(typeof t.fn==='function')t.fn(...t.args);else(0,eval)(String(t.fn));}finally{if(t.repeat&&timers.has(id))native('timer',id,t.delay);} },
    response(id,status,body,headers,url) {
        const entry=requests.get(id);if(!entry)return;requests.delete(id);entry.cleanup?.();
        const values=new Headers();for(const line of headers.split(/\r?\n/)){const i=line.indexOf(':');if(i>0)values.append(line.slice(0,i),line.slice(i+1).trim());}
        if(entry.xhr){const x=entry.xhr;x._request=0;clearTimeout(x._timer);x.status=status;x.responseURL=url;x._responseHeaders=headers;x._responseHeaderValues=values;
            for(const phase of [2,3,4]){x.readyState=phase;if(phase>=3)x.responseText=body;x.dispatchEvent(new Event('readystatechange'));}
            x.dispatchEvent(new Event(status?'load':'error'));x.dispatchEvent(new Event('loadend'));
        }else if(!status)entry.reject(new TypeError('Network request failed'));
        else entry.resolve(new Response(body,{status,headers:values,url}));
    },
    ready(complete) {
        if(state === 'loading') {
            state='interactive';document.dispatchEvent(new Event('readystatechange'));
            document.dispatchEvent(new Event('DOMContentLoaded',{bubbles:true}));
        }
        if(complete && state !== 'complete') {
            state='complete';document.dispatchEvent(new Event('readystatechange'));globalThis.dispatchEvent(new Event('load'));
        }
    },
    named() {
        for(const element of document.querySelectorAll('[id],[name]')) {
            const name=element.id||element.name;if(name&&!(name in globalThis))globalThis[name]=element;
            if(element.localName==='form')for(const input of element.elements){const key=input.name||input.id;if(key&&!(key in element))element[key]=input;}
        }
    }
};
})();
)DILLOJS"
