# Contributing to SpeedBreaker: Carbon

Thanks for helping. Here's how this project works, so your time isn't wasted.

## Reporting a bug

1. In the game, open the settings (F1, or Back + Start on a controller), go to **Advanced** and
   choose **Save Bug Report**. You get one zip file with the logs, your settings and a
   screenshot. Nothing is uploaded.
2. Check the [open issues](https://github.com/SpeedBreakerProject/speedbreaker-carbon/issues)
   first.
3. Open a new issue (the bug report form asks for what helps) and attach the zip. Say which
   device and download you use, what you were doing, what you expected, and what happened.

**Never attach game files, disc images or anything from your game folder.** That includes the
generated `ppc-carbon/` folder, which is made from the game's code.

A bug that happens the same way in SpeedBreaker for *Most Wanted* can go to
[its issues](https://github.com/SpeedBreakerProject/speedbreaker/issues) instead: the two share
their engine, so a fix there reaches Carbon too.

## Pull requests

Pull requests are welcome, but they are never merged with the button. SpeedBreaker: Carbon is
developed in a separate tree, together with SpeedBreaker for Most Wanted, and each release
arrives here as a single commit.

1. If a change is accepted, it's applied by hand to the development tree, and you're credited in
   the commit and in the changelog.
2. It ships in the next release, and the pull request is closed with a note saying so.
3. If it isn't taken, you'll get the reason.

To make a pull request easy to accept:
- Keep it small and focused: one fix or feature per request.
- Say how you tested it: device, OS, and what you played or ran.
- Include no game code, game data or generated `ppc-carbon/` output.
- If you used AI tools, that's fine, as long as you've read, understood and tested the change
  yourself. Everything here is held to the same bar.

By contributing, you agree that your contribution is licensed under the project's license,
GPL-3.0-or-later.

## Scope

**Welcome:**
- bug fixes;
- device and OS support;
- performance;
- accuracy to the original Xbox 360 version.

**Out of scope:**
- anything that helps people get game files;
- online services;
- anything that changes how the game plays by default.
