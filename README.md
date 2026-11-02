# Dillo web browser

Dillo is a multi-platform graphical web browser, known for its speed and
small footprint, that is developed with a focus on personal security and
privacy. It is built with the [FLTK 1.3 GUI toolkit](http://fltk.org).

Screenshot of the [Dillo Website][dillo] rendered in Dillo:

[dillo]: https://dillo-browser.org/

![Screenshot of Dillo website rendered in Dillo](doc/dillo.png)

To install Dillo follow the [installation guide](doc/install.md).

This tree includes an initial QuickJS JavaScript integration with a persistent
HTML5 DOM. JavaScript is enabled by default and can be disabled in Settings or
with `--disable-javascript` at build time. See the [JavaScript guide](doc/javascript.md)
for supported APIs, tests and the remaining modern layout/Baidu compatibility work.

The user interface supports English and Simplified Chinese. Dillo chooses the
language at startup from the first nonempty variable among `LC_ALL`,
`LC_MESSAGES`, and `LANG`. Simplified Chinese locales (`zh_CN`, `zh_SG`,
`zh_Hans` / `zh-Hans`, and `zh`) select Chinese; other locales use English.
For example, run `LC_ALL=zh_CN.UTF-8 dillo` for Chinese or `LC_ALL=C dillo` for
English. Chinese UI text requires a font with Chinese glyphs, such as Noto Sans
CJK. Translations are built into the executable; no extra language packages are
required. See [the translation guide](doc/i18n.md) for development details.

You can clone the latest version of Dillo directly from our git server:

- <https://git.dillo-browser.org/dillo>

We also keep the following mirrors updated in case our git server fails:

- <https://codeberg.org/dillo/dillo>
- <https://git.sr.ht/~dillo/dillo>

The commits are signed with the same OpenPGP we use for the releases.

See also other related forks: [dillo-plus][dillo-plus],
[dilloNG][dilloNG], [D+ browser][dplus-browser] and [Mobilized
Dillo][mobilized].

[dillo-plus]: https://github.com/crossbowerbt/dillo-plus
[dilloNG]: https://github.com/w00fpack/dilloNG
[dplus-browser]: https://sourceforge.net/projects/dplus-browser/
[mobilized]: https://www.toomanyatoms.com/software/mobilized_dillo.html

> **Warning**:
> As of December 2023, the host `dillo.org` is [no longer under control][gone]
> of Dillo developers. A copy of the old website is archived in
> [our website][old] and the [Wayback Machine (May 2022)][may].

[gone]: https://dillo-browser.org/dillo.org.html
[old]: https://dillo-browser.org/old/
[may]: http://web.archive.org/web/20220508022123/https://www.dillo.org/

