# Contributing
Contributions to the Mercury project are always welcome and appreciated.

## Issues & Pull Requests
There are currently four available Issue templates on the Mercury GitHub repository: one for bug reports, one for feature requests, one for documentation improvements, and one for general questions.
If you'd like to go rogue and make your own Issue without a template that is allowed, but please stick to the available templates unless you know what you're doing.
Please make use of available labels for Issues and Pull Requests, they are there for a reason.

## Branch Cleanup
When closing a Pull Request, it is encouraged that you delete your branch from the remote (GitHub). While this isn't a hard *requirement*, it is strongly encouraged as it improves the clarity of what branches are open or not.

Committing directly to main is disabled, so you must create a Pull Request for every change you'd like to make to main.

## Reviewing Pull Requests
In general, Pull Requests must pass all tests that are applicable. For example, a PR that changes documentation will likely have no GitHub Actions available for it, but one that changes something in the source code will be subject to build tests and cross-platform test cases. Each PR should ideally be reviewed by a third-party, unless approved by [@travis-heavener](https://github.com/travis-heavener). Like Issues, the proper labels should be applied.
