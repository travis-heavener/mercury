# Contributing
Contributions to the Mercury project are always welcome and appreciated.

## Issues & Pull Requests
There are four available Issue templates on the Mercury GitHub repository: one for bug reports, one for feature requests, one for documentation improvements, and one for general questions.
If you'd like to go rogue and make your own Issue without a template that is allowed, but please stick to the available templates unless you know what you're doing.
Please make use of available labels for Issues and Pull Requests, they exist for a reason.

## Branch Cleanup
When closing a Pull Request, it is encouraged that you delete your branch from GitHub.
While this isn't a hard *requirement*, it is strongly encouraged as it improves the clarity as to what branches are actively being worked on.

Committing directly to main is disabled, you must open a Pull Request instead.

## Reviewing Pull Requests
In general, Pull Requests must pass all tests that are applicable.
For example, a PR that changes documentation will likely have no GitHub Actions available for it, but one that changes something in the source code will be subject to build tests and cross-platform test cases.
Each PR should ideally be reviewed by a third-party, unless approved by [@travis-heavener](https://github.com/travis-heavener).

Like Issues, the proper labels should be applied.

## Tests
For bug fixes and behavior changes, add a focused test that would catch the issue again.
Prefer adding a case to `tests/tests.json`; use `tests/regressions.py` when checking exact protocol bytes, binary payloads, or connection behavior requires direct socket control.
Both run through the same `tests/run.py` entry point in Build & Test on Linux and Windows.

See [tests/README.md](tests/README.md) for prerequisites, the JSON format, and examples.
Run the complete suite locally before opening a Pull Request, and check the platform workflow results before merging.

## Version Updates
When preparing a release, add a new `## vX.Y.Z` entry at the top of [CHANGELOG.md](CHANGELOG.md), following its existing bullet format.
Set `version.txt` to the matching `Mercury vX.Y.Z` without a trailing newline.
The Compare Changelog Version workflow checks that these versions match.
The release workflow generates the downloads website's version metadata when the release is built.

