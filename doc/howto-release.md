# How to release

These instructions assumes that the version you are making is `21.0.0`.

## Generate changelog

Run proclamation in the `doc/changes`.

```sh
proclamation build 21.0.0 --delete-fragments --overwrite
```

Commit changes, split in two commits to help unrolling or editing changes.

```sh
git commit -m"doc: Update CHANGELOG.md" doc/CHANGELOG.md
git commit -m"doc: Remove old changelog fragments" doc/changes
```


## Update versions

Edit the files

* `CMakelists.txt`
* `meson.build`
* `src/xrt/state_trackers/oxr/oxr_instance.c`

See previous commits for exact places.

```sh
git commit -a -m"monado: Update version"
```


## Tag the code

Do the tagging from git, do **not** do it from gitlab, also make sure to prefix
the version with `v` so that `21.0.0` becomes `v21.0.0`.

```sh
git tag v21.0.0 -m"v21.0.0"
```


## Do gitlab release

The Gitlab UI has a friendly interface, just follow the guide there.
