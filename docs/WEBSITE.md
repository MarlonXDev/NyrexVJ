# Website

The folder `site/` holds a small static website for NyrexVJ: plain HTML and CSS, no scripts, no build step, no external requests. It is published with GitHub Pages and is separate from the program.

## Pages

| File | Content |
| --- | --- |
| `site/index.html` | Start page with a short tour |
| `site/download.html` | Links to the files of the Nightly release |
| `site/docs.html` | Links to the documents on GitHub |
| `site/privacy.html` | Privacy note |
| `site/de/*.html` | The same four pages in German |
| `site/assets/` | One stylesheet, the icon and the pictures (WebP) |

Links to documents, the changelog, the roadmap and the downloads point to `github.com`, so the site never has to be updated when a document changes. All colours are variables at the top of `site/assets/style.css`; the two accent colours (orange and cyan, taken from the NyrexVJ icon and interface) are `--accent` and `--accent-2`.

## Publishing

1. In the repository on GitHub open Settings, Pages and set Source to GitHub Actions. This is a one time step that only the owner can do.
2. The workflow `.github/workflows/pages.yml` then publishes the folder `site/` on every push to `main` that changes `site/**` (or the workflow itself). It can also be started by hand from the Actions tab.
3. The address is `https://<owner>.github.io/NyrexVJ/`, for this repository `https://marlonxdev.github.io/NyrexVJ/`.

A change in `site/` does not start the program build: `build.yml` ignores pushes that only touch `site/**`, so no Nightly is published for a text change.

## Separation from the program

- The program files (`NyrexVJ.exe`, `NyrexVJ-linux-x64` and the archives) are built by CI and attached to the Nightly release. They never contain `site/`.
- `site/` is marked `export-ignore` in `.gitattributes`, so the source archives that GitHub makes ("Source code (zip)") leave it out. A normal `git clone` still contains it (about 0.6 MB).
- CMake does not know the folder; nothing in `site/` is compiled or installed.

## Privacy and legal notice

The site is built so that it needs as little legal text as possible:

- Non-commercial: no shop, no ads, no donation buttons, no affiliate links.
- No cookies, no tracking, no analytics, no forms, no comments, no embedded content, no web fonts or scripts from other servers (the `referrer` is switched off as well).
- The only links that leave the site point to GitHub, which also serves the pages.
- No names, addresses or contact data are published. Questions go through GitHub issues.
- The privacy note says what GitHub Pages does as the host. The footer says that the project is not affiliated with Resolume, because the start page names Resolume Arena as the program NyrexVJ is modelled on.

This is a design decision, not legal advice. In Germany the duty to publish a legal notice (Impressum) depends on whether a site is a "geschäftsmäßiges" telemedia service; a purely private, non-commercial site without income is generally treated differently, but the owner is responsible for judging his own case. If the site ever gets a shop, ads, donations, a newsletter, comments or analytics, the notice and the privacy statement have to be revisited first.

## Changing the site

The pages are plain files and can be edited by hand. Keep these rules so the privacy note stays true: no `<script>`, no `http` address except `github.com` and `docs.github.com`, no files from other hosts, and every new picture inside `site/assets/` as WebP or PNG. Preview a page by opening the file in a browser.
