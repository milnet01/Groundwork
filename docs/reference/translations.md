# Translations — how to change them, and the traps

> **Purpose — so a change to text a user sees reaches every language,
> and lays out right in each.**

Moved from `CLAUDE.md` § Translations on 2026-10-15, word for word, so
that every session does not carry it. `CLAUDE.md` says when to read it.

Each language has `translations/groundwork_<code>.ts`, compiled into the
program at `:/i18n`. A language is offered only once its file has
translations. After changing any text a user sees, refresh the files:

```bash
cmake --build build --target update_translations
```

`tst_translations` fails until they match the source.

The trap: `lupdate` files a string under a context only from what it can
see at the call. A local `tr` helper in a free function gets the wrong
context, or none, so that text is never translated. Inside a free
function, name the context at the call:
`QCoreApplication::translate("gw::Name", "...")`. `tst_translations`
checks every context the code looks up. Two more of the same kind: a
count must be passed at that named call, or the string gets no plural
forms; and after a named call inside `?:`, `lupdate` filed the other
branch's `tr()` under `QCoreApplication`, so give each its own
statement (`updateitem.cpp`).

Right-to-left text has a trap of its own. Qt sets a label's direction
from its first letter. A translation that starts with a Latin name or a
`%1` lays out left to right, and its full stop lands at the wrong end.
Start such a translation with U+200F, the right-to-left mark. Put U+200E,
the left-to-right mark, after "C++" and before ".7z", or their symbols
land on the wrong side. A path breaks the same way: "/dev/vda3" showed
as "dev/vda3/". Wrap a path written into a translation in U+2066 and
U+2069; code wraps a path, name or filesystem it inserts with
`gw::isolated()`. Only a look at the window shows any of these faults.

A translation checked only by a machine (Claude, Gemini) is still a
draft (design.md, Text). A language leaves draft by adding its code to
`kChecked` in `src/core/translations.cpp`.
