# Groundwork

> Sets up a fresh openSUSE install in one window, for anyone who would
> rather not paste commands from forum posts.

Groundwork lists what a new install usually lacks: media codecs,
Flathub, graphics drivers, better fonts and more. Each item has a
toggle, a plain explanation and a status. Every item checks itself
first, so running it again is safe. It works on Tumbleweed, Slowroll
and Leap 16 or later.

## Status

Not released yet. The wizard and its items are built and tested. The
first release, 0.1.0, waits on a test run on fresh Tumbleweed and
Leap 16 machines (GRND-0013). Translations are drafts until a fluent
speaker checks them.

## Install

(Once there is something to install.)

## Usage

```bash
groundwork                    # the setup window, in the system's language
groundwork --lang de          # the setup window, in German
groundwork --check            # list what is and is not set up; changes nothing
groundwork --version
```

Start it as yourself, not as root. It asks for your password once,
when it applies your choices.

## Documentation

| | |
|---|---|
| [ROADMAP.md](ROADMAP.md) | What is planned, in progress and shipped |
| [CHANGELOG.md](CHANGELOG.md) | What shipped, when |
| [docs/discovery.md](docs/discovery.md) | What this is for, and how we would know it works |
| [docs/design.md](docs/design.md) | The shape — the parts, and what may touch what |
| [docs/decisions/](docs/decisions/) | Why a close call went the way it did |
| [docs/specs/](docs/specs/) | The contract for one feature, where one was needed |

## License

[MIT](LICENSE).
