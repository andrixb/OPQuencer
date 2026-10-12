# Contributing to OPQuencer

Thanks for helping! To keep things easy to follow, please use the right place:

| What | Where |
|---|---|
| A bug you can reproduce | [Issues](https://github.com/andrixb/OPQuencer/issues/new/choose): one issue per bug |
| A feature idea or request | [Discussions › Ideas](https://github.com/andrixb/OPQuencer/discussions/categories/ideas) (no promise it will be added) |
| How to use it, or how something works | [Discussions › Q&A](https://github.com/andrixb/OPQuencer/discussions/categories/q-a) |
| Forks, mods, music, demos | [Discussions › Show and tell](https://github.com/andrixb/OPQuencer/discussions/categories/show-and-tell) |
| Anything else | [Discussions › General](https://github.com/andrixb/OPQuencer/discussions/categories/general) |

Feature requests posted as issues will be moved to Discussions or closed. Bugs already present in
upstream Felucca 1.5.1 are best reported to
[Felucca Issues](https://github.com/hugelton/Felucca/issues/new/choose) too.

## Reporting a bug

Please include the OPQuencer version (hold HOME > ABOUT), your computer's OS and browser if the editor or
installer is involved, the steps to reproduce it, what you expected and what happened. A photo or a short
video helps.

## Pull requests

Pull requests are welcome, including AI-assisted ones, with the same bar for all:

- keep it small and focused on one thing;
- make sure you understand the change and can explain it;
- test it on a real FM-1;
- run `tests/run_tests.sh` (see [BUILDING.md](BUILDING.md));
- new files carry the GPL-3.0-only SPDX header.

By contributing you agree that your contribution is licensed under GPL-3.0-only.

The web editor and installer in `web/` are this repo's copies of Felucca's; the upstream editor's
development lives in [Felucca-WebApp](https://github.com/hugelton/Felucca-WebApp).
