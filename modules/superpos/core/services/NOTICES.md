# Service dependency notices

SQLite 3.53.4 is in the public domain. Its amalgamation is compiled directly by the native xmake recipe from the checksum-locked upstream source archive. See https://www.sqlite.org/copyright.html.

The SQLite source remains a separate runtime dependency; Superpos service code is MIT licensed. The optional service profile does not add SQLite to the networking core or the EGP module.
