import os
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import check_release as c

FILES = {
    "CMakeLists.txt": "project(NyrexVJ VERSION 1.2.3 LANGUAGES C CXX)\n",
    "CHANGELOG.md": "# Changelog\n\n## 1.2.3 - 2026-01-02\n\nx\n\n## 1.2.2 - 2026-01-01\n\ny\n",
    "CHANGELOG.de.md": "# Changelog\n\n## 1.2.3 - 2026-01-02\n\nx\n\n## 1.2.2 - 2026-01-01\n\ny\n",
    "README.md": "# NyrexVJ\n\n> Status: v1.2.3. Text\n\n| `--soak N` | run |\n",
    "README.de.md": "# NyrexVJ\n\n> Stand: v1.2.3. Text\n\n| `--soak N` | Lauf |\n",
    "CONTRIBUTING.md": "x\n",
    "AGENTS.md": "x\n",
    "src/main.cpp": 'if (arg == "--soak") {}\n',
    "docs/DECISIONS.md": "| [0001](adr/0001-first.md) | First | ok |\n",
    "docs/adr/0001-first.md": "# first\n",
    "docs/RELEASE.md": "x\n",
    "docs/COMPATIBILITY.md": "x\n",
    "docs/KNOWN_ISSUES.md": "Checked at version: 1.2.3\n",
    "docs/templates/measurement-report.json": "{}\n",
    "docs/templates/test-log.md": "x\n",
    "tests/hardware/gates.json": "{}\n",
    "scripts/release_gate.py": "x\n",
    ".github/workflows/build.yml": "run: python3 scripts/release_gate.py\n  gates: tests/hardware/gates.json\n",
}


def build(folder, **changes):
    files = dict(FILES)
    files.update(changes)
    for path, text in files.items():
        if text is None:
            continue
        full = os.path.join(folder, path)
        os.makedirs(os.path.dirname(full), exist_ok=True)
        with open(full, "w", encoding="utf-8", newline="\n") as handle:
            handle.write(text)


def problems(**changes):
    with tempfile.TemporaryDirectory() as folder:
        build(folder, **changes)
        return c.run(folder)[0]


def release_problems(**changes):
    with tempfile.TemporaryDirectory() as folder:
        build(folder, **changes)
        return c.run(folder, release=True)[0]


class Consistency(unittest.TestCase):
    def test_a_consistent_tree_has_no_problems(self):
        self.assertEqual(problems(), [])

    def test_the_real_repository_is_consistent(self):
        found, version = c.run(c.ROOT)
        self.assertEqual(found, [])
        self.assertRegex(version, r"^\d+\.\d+\.\d+$")

    def test_a_build_file_that_lags_behind_the_changelog_is_caught(self):
        found = problems(**{"CMakeLists.txt": "project(NyrexVJ VERSION 1.2.2 LANGUAGES C CXX)\n"})
        self.assertTrue(any("CHANGELOG.md: newest entry is 1.2.3 but CMakeLists.txt says 1.2.2" in item for item in found))
        self.assertTrue(any("README.md says status 1.2.3" in item for item in found))

    def test_a_readme_status_that_lags_is_caught_in_both_languages(self):
        found = problems(**{"README.md": FILES["README.md"].replace("v1.2.3", "v1.2.2"), "README.de.md": FILES["README.de.md"].replace("v1.2.3", "v1.2.0")})
        self.assertEqual(len([item for item in found if "README" in item]), 2)

    def test_a_missing_german_changelog_entry_is_caught(self):
        found = problems(**{"CHANGELOG.de.md": "# Changelog\n\n## 1.2.3 - 2026-01-02\n\nx\n"})
        self.assertTrue(any("list different versions: 1.2.2" in item for item in found))

    def test_a_bad_date_and_a_wrong_order_are_caught(self):
        found = problems(**{"CHANGELOG.md": "## 1.2.3 - 2026-13-45\n\n## 1.2.2 - 2026-01-01\n\n## 1.2.4 - 2026-01-03\n"})
        self.assertTrue(any("not a date" in item for item in found))
        self.assertTrue(any("descending" in item for item in found))

    def test_an_adr_without_a_row_and_a_row_without_a_file_are_caught(self):
        found = problems(**{"docs/adr/0002-second.md": "# second\n"})
        self.assertTrue(any("no row for docs/adr/0002-second.md" in item for item in found))
        found = problems(**{"docs/DECISIONS.md": FILES["docs/DECISIONS.md"] + "| [0003](adr/0003-gone.md) | Gone | ok |\n"})
        self.assertTrue(any("missing file: docs/adr/0003-gone.md" in item for item in found))

    def test_a_row_that_links_the_wrong_number_is_caught(self):
        found = problems(**{"docs/DECISIONS.md": "| [0002](adr/0001-first.md) | First | ok |\n"})
        self.assertTrue(any("row 0002 links to 0001-first.md" in item for item in found))

    def test_an_undocumented_option_is_caught(self):
        found = problems(**{"src/main.cpp": 'if (arg == "--soak") {}\nif (arg == "--new-thing") {}\n'})
        self.assertTrue(any("--new-thing" in item for item in found))

    def test_an_option_in_only_one_readme_is_caught(self):
        found = problems(**{"README.de.md": "> Stand: v1.2.3.\n"})
        self.assertTrue(any("only one language" in item for item in found))

    def test_a_workflow_that_names_a_missing_script_is_caught(self):
        found = problems(**{".github/workflows/build.yml": "run: python3 scripts/not_there.py\n"})
        self.assertTrue(any("scripts/not_there.py" in item for item in found))

    def test_a_missing_release_document_is_caught(self):
        found = problems(**{"docs/RELEASE.md": None})
        self.assertTrue(any("docs/RELEASE.md" in item for item in found))

    def test_known_issues_must_name_a_version_and_not_a_future_one(self):
        self.assertTrue(any("no line" in item for item in problems(**{"docs/KNOWN_ISSUES.md": "x\n"})))
        self.assertTrue(any("newer version" in item for item in problems(**{"docs/KNOWN_ISSUES.md": "Checked at version: 9.0.0\n"})))


class ReleaseMode(unittest.TestCase):
    def test_release_mode_needs_the_known_issues_checked_at_this_version(self):
        self.assertEqual(release_problems(), [])
        found = release_problems(**{"docs/KNOWN_ISSUES.md": "Checked at version: 1.2.2\n"})
        self.assertTrue(any("release check" in item for item in found))

    def test_release_mode_refuses_a_future_date(self):
        text = "## 1.2.3 - 2999-01-01\n"
        found = release_problems(**{"CHANGELOG.md": text, "CHANGELOG.de.md": text})
        self.assertTrue(any("future" in item for item in found))


if __name__ == "__main__":
    unittest.main()
