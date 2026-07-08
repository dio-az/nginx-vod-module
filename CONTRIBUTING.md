# Contributing

Thanks for contributing! **Please note the conventions below** before opening a pull request.

### Code style

Format C sources with [clang-format](https://clang.llvm.org/docs/ClangFormat.html) **22** or newer.
Older versions reject some options in the config.

To format the lines you changed, stage them and run `git clang-format`. To format a whole file, run
`clang-format -i <file>`.

Leave `// clang-format off` and `// clang-format on` guards in place. They protect hand-tuned
tables such as byte arrays and generated headers.

### Commit message

```
hls: remove muxed track spec leftover
^-^  ^------------------------------^
|    |
|    +-> Summary in imperative mood, lowercase, without a trailing period.
|
+------> Optional scope: a component (hls, dash, mp4, ...) or meta area (docs, ci, ...).
```

This format is inspired by [Conventional Commits](https://conventionalcommits.org) but drops the
type prefix. Omit the scope for broad changes that do not map to a single area.

For more info about message body, see:

- [Writing git commit messages](http://365git.tumblr.com/post/3308646748/writing-git-commit-messages)
- [A note about git commit messages](http://tbaggery.com/2008/04/19/a-note-about-git-commit-messages.html)

### Branch

The repository keeps one branch per active major:

- **`main`** hosts the latest major, carrying its in-development work and releases.
- **`vN.x`** maintains a superseded major (e.g. `v1.x`), and its releases are cut from it.

When the first breaking change for the next major lands on `main`, the outgoing major is branched to
`vN.x` for continued maintenance while `main` moves on.

A change that also applies to a maintained older major is made on `main` and ported to the relevant
`vN.x` branch.

Recommended branch naming: `<type>/<kebab-description>`, where `type` follows the
[Conventional Commit](https://conventionalcommits.org) types (such as `feat`, `fix`, or `docs`),
e.g. `docs/contributing`, `feat/cpix-support`.

### Pull request

Unlike a commit, the title carries **no scope**. Keep it sentence-case and imperative, with no
trailing period. It becomes the CHANGELOG entry, so phrase it for readers:

```
Improve upstream error status handling
Drop support for FFmpeg below v5.1
```

Apply the label that selects its CHANGELOG section (such as `enhancement`, `bug`, or `breaking`).

### Versioning

The project follows [Semantic Versioning](https://semver.org). The version is set manually in
[`ngx_http_vod_module.h`](./ngx_http_vod_module.h):

```c
#define NGINX_VOD_VERSION "1.9.0"
```

Bump it only in the release commit, never in a feature PR, since it is a per-branch, release-time
decision.

### Release

*Maintainers only*. Cut from the branch that owns the target major (`main` for the latest, `vN.x`
for an older one). Using `1.9.0` as an example:

1. Bump `NGINX_VOD_VERSION` in [`ngx_http_vod_module.h`](./ngx_http_vod_module.h).
2. Prepend a manually written [`CHANGELOG.md`](./CHANGELOG.md) entry, placing each item under the
   section matching its PR label:
   - `breaking` -> `### ⚠ BREAKING CHANGES`
   - `enhancement` -> `### Features`
   - `bug` -> `### Bug Fixes`
3. Commit with the version as the subject, then sign-tag and push:
   ```sh
   git commit -m '1.9.0'
   git tag -sm '1.9.0' v1.9.0
   git push --follow-tags
   ```
