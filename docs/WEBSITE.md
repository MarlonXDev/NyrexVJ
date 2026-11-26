# Website

The folder `site/` holds a small static website for NyrexVJ: plain HTML and CSS, no scripts, no build step, no external requests. It is published with GitHub Pages and is separate from the program.

## Pages

| Where | Content |
| --- | --- |
| `site/index.html` | Start page: what NyrexVJ is, the main window, a tour with pictures, performance, the manual, a section on the newer features since 0.40.0 (each with a link to its manual section and a note on what is experimental or untested), more features, questions and answers, download |
| `site/download.html` | Links to the files of the Nightly release, and what to do after the download |
| `site/docs.html` | Links to the documents on GitHub, and a call-out for the manual |
| `site/privacy.html` | Privacy note |
| `site/de/*.html` | The same four pages in German |
| `site/manual/` | The user manual in English: `index.html` (contents), `all.html` (the whole manual on one page) and one page for each of the 36 chapters. **Generated.** |
| `site/de/handbuch/` | The same in German (`index.html`, `alle.html`, one page for each chapter). **Generated.** |
| `site/assets/manual/en/`, `site/assets/manual/de/` | The pictures of the manual and the start page, one set for each language (the interface language shows in them). **Generated** by `scripts/make_manual_shots.py`. |
| `site/assets/` | The stylesheet and the icon. The seven older pictures (`card-*.webp`, `shot-main.webp`) are no longer used by any page. |

The manual comes from `docs/manual/manual.en.md` and `docs/manual/manual.de.md`, the same files the program shows in its manual window. See [MANUAL.md](MANUAL.md). The pages of the manual are never edited by hand.

Links to documents, the changelog, the roadmap and the downloads point to `github.com`, so the site never has to be updated when a document changes. All colours are variables at the top of `site/assets/style.css`; the accent colour (the orange of the NyrexVJ interface) is `--accent`. The manual has a print style, so a chapter or the whole manual (`all.html`) can be printed or saved as PDF from the browser.

## Publishing

1. In the repository on GitHub open Settings, Pages and set Source to GitHub Actions. This is a one time step that only the owner can do.
2. The workflow `.github/workflows/pages.yml` then publishes the folder `site/` on every push to `main` that changes `site/**`, the manual sources `docs/manual/**`, `scripts/build_manual_site.py` or the workflow itself. It checks the site first (`python3 scripts/build_manual_site.py --check`) and publishes only if the check passes. It can also be started by hand from the Actions tab.
3. The address is `https://<owner>.github.io/NyrexVJ/`, for this repository `https://marlonxdev.github.io/NyrexVJ/`.

A change in `site/` does not start the program build: `build.yml` ignores pushes that only touch `site/**`, so no Nightly is published for a text change.

## Separation from the program

- The program files (`NyrexVJ.exe`, `NyrexVJ-linux-x64` and the archives) are built by CI and attached to the Nightly release. They never contain `site/`.
- `site/` is marked `export-ignore` in `.gitattributes`, so the source archives that GitHub makes ("Source code (zip)") leave it out. A normal `git clone` still contains it (about 2 MB, most of it the pictures of the manual).
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

The four start pages in each language are plain files and can be edited by hand. The manual pages are generated, and so are the header and the footer of every page.

1. Edit the hand-written pages, or the manual sources (`docs/manual/`).
2. Run `python3 scripts/build_manual_site.py`. It rewrites the manual pages and replaces the `<header class="top">` and `<footer class="footer">` of the eight hand-written pages, so the menu is the same everywhere.
3. Run `python3 scripts/build_manual_site.py --check`. It changes nothing and fails if a generated page is out of date or a rule below is broken. CI runs it in the Windows and the Linux job and before every publication.
4. After a change of the interface, make the pictures again with `scripts/make_manual_shots.py` (see [MANUAL.md](MANUAL.md)) and run step 2 once more.
5. Preview a page by opening the file in a browser.

The check keeps the privacy note true: no `<script>`, no inline event handler (`onclick` and so on), no `<iframe>`, `<form>`, `<input>`, `<object>` or `<embed>`, no address except `github.com` and `docs.github.com`, no files from other hosts, every link and every `#anchor` leads somewhere, every `id` is unique inside its page, every `<img>` has `alt`, `width` and `height`, every picture of the manual is used, and no picture stays behind that nothing uses. New pictures go inside `site/assets/` as WebP or PNG.
