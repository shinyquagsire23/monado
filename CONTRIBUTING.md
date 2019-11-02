# Contribution Guidelines

There are plenty of valid reasons why someone might not be able
to follow all of the guidelines in this section, and that's OK,
especially for new contributors or those new to open source entirely.
Just let us know and we'll figure out a way to help you get involved successfully.

> Important note: Unlike the guidelines here, the Code of Conduct,
> available at <https://www.freedesktop.org/wiki/CodeOfConduct/>,
> is **not** optional,
> and applies in its entirety to anyone involved in the project,
> for the safety and comfort of all.
> See the README for associated contacts.

## Pull/Merge Requests

- If you're considering starting work on a large change that you'd like to contribute,
  it is recommended to first open an issue before you start,
  to begin a discussion and help smooth the acceptance of your contribution.

- If you are able, please make sure to run clang-format
  (ideally version 7 or newer) before each commit,
  so that you only commit things that are cleanly styled.
  Consistent, machine-performed formatting improves readability and makes it easier for others to contribute.
  It also makes it easier to review changes.
  If you can't run clang-format, just mention this fact in your request and we'd be happy to help,
  either in a single "Clean up formatting." commit on top of your work,
  or by "re-writing history" (with your permission and leaving your commit authorship intact),
  revising each commit to apply formatting.

- Avoid including whitespace or other formatting changes to unrelated code when committing.
  The `git add -p` command or the "stage selected lines/hunks" feature of various Git GUIs are
  great ways of making sure you only stage and commit the changes that you mean to.
  Relatedly, `git commit -v` (if you commit from the command line) can be a great help
  in making sure you aren't committing things you don't mean to,
  by showing the diff you're committing in your commit message editor.
  (This can even be set system-wide in `git config --global commit.verbose true`
  if you find it as life-changing as many others have - thanks
  [emilyst](https://twitter.com/emilyst/status/1039205453010362368).)

- If you can, before submitting a pull/merge request, try building with clang-tidy enabled,
  and if you touched any code that has or should have documentation,
  build and check the documentation and see if it looks OK.

- We work to keep the code free of warnings -
  please help by making sure your changes build cleanly (and pass all tests).
  When on compilers that take warning flags like gcc and clang do,
  the build system automatically turns on quite a few of them.
  If a warning stumps you, just mention it in the request so we can figure it out together.

### Issues

Constructive issues are a valued form of contribution.
Please try to include any relevant information
(whether it is a request for improvement or a bug report).
We'll try to respond promptly,
but there is no guarantee or warranty (as noted in the license),
absent any externally-arranged consulting or support contract.

Since this is a runtime/implementation of an API used by other applications,
bug reports should include:

- details about your build environment
  - architecture
  - compiler
  - compiler version
  - build flags (defines, configuration/feature flags)
- associated application code
  - for logic/execution errors, a new (failing) test case is ideal,
    otherwise a description of expected and actual behavior
  - if you cannot disclose your code, or even if you can,
    an "artificial", minimally-sized example can be very valuable.

---

## Copyright and License for this CONTRIBUTING.md file

For this file only:

> Copyright 2018-2019 Collabora, Ltd.
>
> SPDX-License-Identifier: CC-BY-4.0
