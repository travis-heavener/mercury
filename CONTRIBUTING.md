# Contributing

Contributions to the Mercury project are always welcome and appreciated!

## Issues & Pull Requests

There are four available Issue templates on the Mercury GitHub repository: one for bug reports, one for feature requests, one for documentation improvements, and one for general questions.
If you'd like to go rogue and make your own Issue without a template that is allowed, but please stick to the available templates unless you know what you're doing.
Maintainers will apply the appropriate labels to your Issue or Pull Request after submission.

## Forking & Branch Workflow

Direct commits to `main` are disabled. To contribute changes:

1. **Fork** the repository to your own GitHub account.

2. Create a descriptive feature or bugfix branch on your fork (e.g., `fix/socket-timeout` or `feat/new-command`).

3. Open a **Pull Request** from your fork's branch into `Travis-Heavener/Mercury:main`.

When your Pull Request is merged or closed, it is encouraged that you delete your branch from your fork. While this isn't a hard *requirement*, it helps keep active work organized.

## Reviewing Pull Requests

In general, Pull Requests must pass all applicable CI checks before merging.
For example, a PR that only changes documentation may skip source-level checks, whereas a PR that modifies C++ source code will be subject to cross-platform builds and test suites.
Each PR should ideally be reviewed by a third-party, unless approved by [@travis-heavener](https://github.com/travis-heavener).

## Tests

For bug fixes and behavior changes, add a focused test that would catch the issue again.
Prefer adding a case to `tests/tests.json`; use `tests/regressions.py` when checking exact protocol bytes, binary payloads, or connection behavior requires direct socket control.
Both run through the same `tests/run.py` entry point in Build & Test on Linux and Windows.

See [tests/README.md](tests/README.md) for prerequisites, the JSON format, and examples.
Run the complete suite locally before opening a Pull Request, and check the platform workflow results once CI runs on your PR.

## Version Updates *(Maintainers Only)*

> **Note:** External contributors do not need to bump version numbers or edit the changelog in feature or bugfix Pull Requests.

When preparing a release, add a new `## [X.Y.Z] - YYYY-MM-DD` entry at the top of [CHANGELOG.md](CHANGELOG.md), following its existing bullet format.
Set `version.txt` to the matching `Mercury vX.Y.Z` without a trailing newline.
The Compare Changelog Version workflow checks that these versions match.
The release workflow generates the downloads website's version metadata when the release is built.
