# Groundwork

> Sets up a fresh openSUSE install in one window, for anyone who would
> rather not paste commands from forum posts.

Groundwork lists what a new install usually lacks: media codecs,
Flathub, graphics drivers, better fonts and more. Each item has a
toggle, a plain explanation and a status. Every item checks itself
first, so running it again is safe. It works on Tumbleweed, Slowroll
and Leap 16 or later.

It can also save this computer's setup to one file: the programs you
asked for, the software sources you added, and your settings app by
app. Settings that hold saved passwords or keys start switched off.
On a new install, it can start from that file and put it all back.

## Status

Released. What each version added is in [CHANGELOG.md](CHANGELOG.md).
Translations are drafts until a fluent speaker checks them.

## Install

There are two ways: the openSUSE package, or the AppImage.

### openSUSE package

Add the repository for your system, then install. On Tumbleweed:

```bash
sudo zypper addrepo https://download.opensuse.org/repositories/home:/milnet:/groundwork/openSUSE_Tumbleweed/home:milnet:groundwork.repo
sudo zypper install groundwork
```

On Slowroll, replace `openSUSE_Tumbleweed` with `openSUSE_Slowroll`.
On Leap 16.0, replace it with `openSUSE_Leap_16.0`.
`zypper` asks you once to trust the repository's signing key.

### AppImage

Download `Groundwork-x86_64.AppImage` from the
[latest release](https://github.com/milnet01/Groundwork/releases/latest),
then run it:

```bash
chmod +x Groundwork-x86_64.AppImage
./Groundwork-x86_64.AppImage
```

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

Groundwork is an independent project, not an official openSUSE or SUSE
product, and is not endorsed by the openSUSE Project or SUSE.
