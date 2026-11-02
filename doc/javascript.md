# JavaScript integration

QuickJS executes classic page scripts in a separate runtime for each document.
Lexbor owns the HTML5 DOM for that document. JavaScript is built and enabled by
default. Disable it in Tools → Settings → Enable JavaScript, or set
`javascript_enabled=NO` in `~/.dillo/dillorc`. Disabling it stops active page
scripts; reload a page after enabling it again.

## Build and test

The pinned engine sources are included under `vendor/`, so building needs no
network access. QuickJS is built as GNU C11; Dillo's C++ code remains C++11.

```sh
./autogen.sh
mkdir build
cd build
../configure --enable-html-tests
make -j4
make check # See below for the installed DPI plugins needed by file/HTML tests.
```

Use `../configure --disable-javascript` to omit both QuickJS and Lexbor from the
executable. On Linux, Pango/Cairo text shaping is selected automatically when
`pkg-config pangocairo` is available. Install `libpango1.0-dev` and a Chinese font
such as `fonts-noto-cjk` on Debian/Ubuntu. `--enable-modern-text` requires this
backend; `--disable-modern-text` retains FLTK text rendering.

The engine unit tests run without a display. Browser integration tests need
Python 3 and Xvfb; install xdotool to exercise real mouse and keyboard events,
and xsel for the Chinese clipboard regression.
The existing file/HTML tests additionally need installed `dpid` and DPI plugins;
configure an installation prefix, run `make install`, and put its `bin` directory
on PATH before running those tests. To run only the HTTP integration test:

```sh
DILLO_TEST_BINDIR="$PWD/src" python3 ../test/javascript/integration.py
```

The offline renderer suite also needs ImageMagick's `import`, `convert` and
`identify`, plus xdotool. It checks screenshots, element rectangles and real
native input without contacting a public website. Run it from the source root:

```sh
DILLO_TEST_BINDIR="$PWD/build/src" \
  DILLO_RENDER_OUTPUT=/tmp/dillo-rendering \
  python3 test/javascript/rendering.py
```

`DILLO_RENDER_OUTPUT` is optional; when set, screenshots, browser logs, geometry
and `results.json` are retained there. Otherwise successful runs clean up their
temporary files, and failed runs retain artifacts. The suite uses an isolated
HOME and enables background images in its test configuration. Missing screenshot
tools cause the suite to skip with exit status 77.

`dilloc domdump` prints the current DOM including script changes. `dilloc dump`
continues to show the downloaded source. `dilloc eval EXPRESSION` evaluates
JavaScript in the active document and prints its string value. For structured
output, use, for example, `dilloc eval 'JSON.stringify({url:location.href})'`.

## Implemented behavior

- Incremental HTML5 parsing, blocking classic scripts, external async/defer
  scripts, dynamically inserted scripts, document.write and lifecycle events.
- Persistent node identity, DOM queries and mutation, inline style reflection,
  collections, attributes, classList, dataset and MutationObserver.
- Event listeners, capture/bubbling, cancellation, inline handlers, native
  text input/textarea keyboard, input/focus events, buttons outside forms,
  form submission and link clicks. UTF-16 selection offsets map to native
  UTF-8 controls. Clipboard replies survive DOM changes made in key handlers.
- Native click events carry viewport, page and screen coordinates, released
  button state, click count and Ctrl/Shift/Alt/Meta modifiers. Text targets and
  native inputs, textareas and buttons share the viewport conversion, including
  after scrolling. MouseEvent exposes x/y/which aliases and getModifierState().
  Keyboard button activation has zero mouse coordinates and click count.
- Timers, Promise jobs, requestAnimationFrame, console and navigation.
- Same-document history.pushState/replaceState, history.state/length and
  back/forward/go share Dillo's navigation stack. Traversing these entries
  preserves the document and sends popstate/hashchange events. State is cloned,
  including cyclic plain objects, Date and typed arrays. Functions, Map, Set
  and other unsupported clone types raise DataCloneError.
- Asynchronous fetch and XMLHttpRequest, text/JSON responses, request headers,
  CORS/preflight checks, credential modes, redirects, cancellation and timeout.
  Script requests use Dillo's network stack and user agent. Mixed active content
  follows its existing restrictions. Each script/API transaction has a private
  cache key so methods, request headers and credential modes cannot share data.
- document.cookie uses the normal cookie policy and jar, hiding HttpOnly cookies
  and preventing scripts from overwriting them. Secure/path/domain checks apply.
- localStorage is stored per scheme/host/port under `~/.dillo/web-storage`;
  sessionStorage is scoped to the tab and origin and survives navigation.
- Element rectangles and common computed styles come from Dillo's rendered
  widgets. Linux text uses Pango font fallback/shaping and Cairo painting when
  available, so Chinese text no longer depends on one FLTK font having every glyph.
- Stylesheet load/error events, document.styleSheets, style/link.sheet and a
  CSSStyleSheet subset. Inline sheets support insertRule/deleteRule/replaceSync;
  external sheets expose readable rules only for the document's origin,
  including after redirects. Rule objects are snapshots; complete live CSSOM
  mutation and grouping rules are not implemented.
- getSelection() exposes selected text and clearing for native page/control
  selections. It does not implement DOM Range creation or editing.
- CSS custom properties and var() fallbacks/cycle detection, calc() arithmetic
  on supported units, box-sizing and automatic horizontal block margins.
  Flex containers support direction, wrap, grow/shrink, basis, order, gaps and
  common alignment. Grid supports automatic row placement, fixed/percentage/fr
  tracks, integer repeat() and gaps. These are subsets of Flexbox and Grid;
  intrinsic track sizing, minmax(), named lines, spans and automatic flex
  margins are not complete.
- Overflow-visible descendants paint and receive clicks outside small ancestor
  allocations. Overflow-hidden ancestors clip painting and native controls at
  the padding box; absolute/fixed descendants escape intermediate clipping
  ancestors according to their containing block.
  CSS clips use a separate stack rather than one native FLTK clip per element,
  so deeply nested pages do not exhaust FLTK's fixed clipping stack.
- Viewport-fixed boxes stay anchored during horizontal/vertical scrolling and
  resizing, including documents shorter than the viewport. A transformed
  ancestor establishes the containing block for absolute/fixed descendants.
- Translation transforms: translate(), translateX(), translateY() and
  translate3d(x,y,0), including lists, percentages, negative lengths and calc().
  Percentages use the border box. Transforms preserve normal flow, establish a
  stacking context, and affect painting, native controls, hit testing and element
  rectangles together.
- Up to eight linear-gradient(), repeating-linear-gradient() or none background
  layers, with the first listed layer above later layers and alpha compositing,
  including angle/directional syntax, up to 32 color stops, double-position
  stops, length/percentage/calc() positions and alpha interpolation. Colors
  include RGB/RGBA, HSL/HSLA, named colors, currentColor and 3/4/6/8-digit hex.
- background-clip and background-origin support border-box, padding-box and
  content-box lists. Short lists repeat across image layers. The background
  shorthand recognizes one or two box keywords per layer and a color in the
  final layer, resetting omitted longhands. Rounded clipping also applies to
  single URL backgrounds. Backgrounds paint below solid borders, including
  transparent borders used for layered gradient outlines.
- background-size supports auto, nonnegative lengths/percentages, calc(),
  cover/contain and lists that repeat across gradient layers. Single URL images
  preserve their intrinsic aspect ratio with an auto dimension or cover/contain;
  gradients fill auto dimensions using their background origin box. Single-layer
  background shorthand accepts position / size. Scaled images obey repetition,
  positioning and rounded clipping; root/body URL sizes propagate to the canvas.
  Decoded image scaling uses a bounded cache and follows streaming row updates.
  Removing root/body images clears their canvas paint, and requests canceled
  during restyling no longer leave the document's loading state pending.
- Alpha background colors support rgba(), modern space/slash RGB/HSL syntax,
  transparent, currentColor and 4/8-digit hex. Nested backgrounds composite over
  their painted parent; root backgrounds composite over the configured canvas
  color. Opaque modern RGB/HSL colors also work for text and borders.
- Elliptical border-radius shorthand and corner longhands, with percentages,
  calc(), overlap normalization, rounded backgrounds and solid borders. Native
  form controls leave their CSS border/background visible; styled buttons
  retain gradients and centered labels.
- box-shadow supports up to eight shadows, inset/outset painting, offsets,
  positive/negative spread, blur, rounded corners, alpha colors, currentColor,
  calc() and var(). The first shadow paints above later shadows. Blur uses
  three separable box filters and bounded tiles; blur radii are capped at 128px.
- outline shorthand and its color/style/width/offset longhands paint without
  changing layout or hit areas. Solid, dotted, dashed and double patterns are
  supported, including negative offsets and rounded corners; other border-style
  keywords use a solid outline fallback. Colors may have alpha.
- pointer-events:auto/none is inherited, permits descendants to restore auto,
  and applies to text, links, native controls and overlapping decorations.
  Native mouse dispatch follows CSS stacking and clipping rather than the
  toolkit's child order. Keyboard focus remains available for none controls.
- document.elementFromPoint(x,y) returns the rendered element under viewport
  coordinates, using the same hit test as native clicks. Inline text targets,
  translations, fixed positioning, scrolling, clipping and pointer-events are
  included. Invalid or offscreen coordinates return null. Pending visual changes
  flush before queries except while a native event callback is active.
- Backgrounds, radii, transforms, shadows, outlines and pointer-events recognize initial,
  inherit and unset, including their supported shorthands and var() expansion.
  Other properties do not yet have general CSS-wide keyword support.
  getComputedStyle() reports alpha background colors, shadow lengths, outline
  longhands, pointer-events and background origin/clip/size lists from the committed
  render tree.
- Immediate window.scroll(), scrollTo() and scrollBy(), using numeric arguments
  or options with left/top.

## Architecture and limits

`src/jsengine.cc` contains the GUI-independent QuickJS/Lexbor binding and
`src/js-dom.inc` supplies browser objects. `src/javascript.cc` owns document
lifetime, resource scheduling, native events and the connection to Dw.
`src/jsstorage.cc` owns Web Storage persistence. Navigation, Stop and disabling
JavaScript cancel document timers and requests before the document is destroyed.
The QuickJS libc/OS modules are not linked or exposed to pages.

The runtime limits each task to about one second of JavaScript execution, the
JS heap to 128 MiB and the JS stack to 1 MiB. Native layout work does not consume
the JavaScript deadline. Long microtask checkpoints yield back to the GUI and
resume, with a total limit of 10,000 jobs per checkpoint. Native events that
reenter JavaScript share the outer task's checkpoint. It also limits DOM
allocations, input, resource sizes and session request counts. History state is
limited to 512 KiB per serialized value and 16 MiB of encoded state per tab.
These limits are safeguards within
one process; this implementation does not add browser process isolation.

The persistent DOM is serialized to Dillo's existing parser when a visual update
is needed. The native widget tree is currently rebuilt, preserving text/control
state, input focus, selection and scroll. This is not the incremental unified
render tree envisioned for a later renderer. Measurement during an active native
event handler uses the last committed tree to avoid deleting the control that
is still delivering the event.

This is an initial browser API subset. It does not provide modules, workers,
WebSockets, IndexedDB, Canvas/WebGL, service workers, iframe browsing contexts,
streaming/binary fetch bodies, synchronous XHR or full HTML/CSSOM conformance.
Assigning a location fragment still uses full navigation; the same-document
history support above applies to pushState/replaceState and their traversal.
Media-query evaluation covers a subset and does not yet emit change
notifications. Storage events and tab cloning are not implemented.
Meta charset/refresh handling on the JavaScript path needs further integration.
Rendering remains a subset. Transforms do not implement scale, rotation,
perspective or general 3D; translate3d() requires a zero z component. Gradients
do not implement radial/conic shapes. Layered URL images and per-layer lists of
background-position/repeat/attachment remain unsupported; the existing
single-URL path is retained. A shorthand requiring these unsupported lists is
ignored atomically, preserving a previous fallback declaration. Scaled URL image
buffers are limited to 32767px per side and 16 million pixels; larger scaled
images are omitted. Canvas URL backgrounds retain the existing canvas reference
area, the larger of the document and viewport, for percentage sizes and position.
Rounded solid borders are supported; other border styles retain the legacy
square-border fallback. Background-image corner clipping uses row spans rather
than an antialiased image mask. Overflow clipping is rectangular: rounded corners
do not yet clip descendant content. Internal overflow scroll containers,
overflow-x/y remain unsupported. Element/group opacity,
transitions and animations are not implemented. Text and border colors do not
yet support alpha; native controls do not provide arbitrary translucent content
compositing over images or gradients. Scroll APIs are
immediate, without smooth scrolling or new scroll event notifications.

## Baidu acceptance status and remaining renderer work

Live checks with Dillo's unchanged default user agent have reached rendered
Baidu results using English Enter submission and English/Chinese button
submission. A live Chinese button search also passed clicking Next, opening
a result URL and using the browser Back button to return to the second result
page. Native typing, Chinese clipboard input and the site's script-driven
navigation are exercised. The homepage may redirect itself from HTTPS to HTTP.
Some live acceptance runs have been redirected to Baidu's CAPTCHA page; the
full four-case search matrix has not passed in a single run. Opening a result
checks navigation, not complete compatibility with the destination site.
The offline integration test also covers those native
navigation actions, scripts, storage, CORS, stylesheet redirects, history and
control state without depending on the public site's availability.

Run the optional public-site test separately from make check:

```sh
DILLO_TEST_BINDIR="$PWD/build/src" python3 test/javascript/baidu.py \
  --navigation --output /tmp/dillo-baidu-acceptance
```

It records the user agent, DOM snapshots, screenshots when ImageMagick is
available, the browser log and results.json. It requires Xvfb, xdotool and xsel.
It uses an isolated temporary profile with JavaScript and background images
enabled, keeping the developer's preferences and storage separate.
Use `--query 'QuickJS 引擎' --submit button` to run one case. A CAPTCHA redirect
is reported as an external acceptance blocker rather than a successful search.

Page appearance remains visibly different from Chrome. The offline renderer
suite covers the clipping, fixed positioning, translation, gradient, radius and
native-control behavior described above. It does not establish complete live
Baidu compatibility. Remaining layout differences, font icons and unsupported
decorative CSS still affect real sites. Further renderer work and incremental
DOM-to-render updates are needed before claiming the planned Chrome-like visual
compatibility goal.

The layered-background regression includes Baidu's white padding-box gradient
over its colored border-box gradient, avoiding the old solid blue fallback.
An isolated live check on 2026-11-02 also completed Chinese input and button
submission to rendered search results with the default user agent. This is one
successful acceptance run, not a guarantee that future requests bypass CAPTCHA
or that every page interaction is supported.
