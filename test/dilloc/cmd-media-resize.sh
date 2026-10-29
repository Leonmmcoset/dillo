#!/bin/bash
# A media breakpoint must update a loaded page after a viewport resize.
set -eux
command -v xdotool >/dev/null || exit 77
command -v convert >/dev/null || exit 77

cat > "$WORKDIR/responsive.html" <<'HTML'
<!doctype html><html><head><title>Responsive CSS test</title>
<style>
body {margin:0; height:1500px; background:red}
@media (min-width:700px) {body {background:green}}
</style></head><body></body></html>
HTML

$DILLOC open "file://$(readlink -f "$WORKDIR/responsive.html")"
$DILLOC wait 5
window=$(xdotool search --onlyvisible --pid "$DILLO_PID" --name 'Dillo:' | head -n 1)

function expect_color() {
  for attempt in {1..50}; do
    color=$(xwd -silent -id "$window" | convert xwd:- -format '%[pixel:p{300,400}]' info:)
    if [ "$color" = "$1" ]; then return 0; fi
    sleep 0.05
  done
  echo "Expected $1, got $color" >&2
  return 1
}

xdotool windowsize --sync "$window" 950 700
expect_color 'srgb(0,128,0)'
xdotool windowsize --sync "$window" 600 700
expect_color 'srgb(255,0,0)'
xdotool windowsize --sync "$window" 950 700
expect_color 'srgb(0,128,0)'
