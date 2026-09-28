# Lifetime file

## Library

- Name, as `pkg-config` knows it:
- Version:
- Source of the version (for example the output of `pkg-config --modversion <name>`):

## SemVer

Write yes, no or not known. Write yes or no only when the library's own
documentation says it. Give the link to that documentation.

- The library follows SemVer:
- Source:

## File

- Path: `lifetimes/<name>/<version>.lifetime`

A major version file (`lifetimes/<name>/<major>.lifetime`) is used only
when the library's authors confirm that the library follows SemVer.

## Declarations

One row for each declaration in the file.

| Class | Word | Function | Where the library states it |
|-------|------|----------|-----------------------------|
|       |      |          |                             |

## Checks

- [ ] The build fails without the file.
- [ ] The build passes with the file.

## Confirmation by the library's authors

The maintainers of this repository fill in this section. The file is
merged only when the library's authors confirm the declarations.

- Confirmed by:
- Where they confirmed it (link):
- SemVer confirmed by (only for a major version file):
