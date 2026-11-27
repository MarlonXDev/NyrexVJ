# Compatibility fixtures

These files are read by `nvj_compat_tests` (tests/test_compat.cpp). They check that files written by earlier shapes of the formats still load and that files from a newer version are refused where a version number exists.

Files named `*_v0.50.0.json` were written by the code of release 0.50.0 with `nvj_compat_tests --write-fixtures <folder>`. They are frozen: the test loads them and also checks that the current writer still produces the same JSON. If the writer output changes on purpose, write the files for the new release, keep the old ones, and add a load check for the old ones. Do not edit a frozen file by hand. The settings writer gained five keys in 0.64.0, so `settings_v0.64.0.json` is the frozen settings file that the current writer is compared with, and `settings_v0.50.0.json` stayed as a file that an earlier release really wrote: the test loads it and checks that the new keys take their defaults. The other kinds are still compared with their 0.50.0 files because their writers did not change.

Files named `*_older*.json` and `*_newer.json` and `venue_no_version.json` are written by hand. They show an older or a newer shape of a format. They were **not** written by an earlier release of NyrexVJ: they are an educated reconstruction from the loader code, so they prove what the loader accepts today, not what 0.30.0 really wrote.

The files hold no paths, names of devices or secrets.
