7-Zip 26.03 (x86) standalone console executable, reduced version (7z format only).
Source: https://www.7-zip.org/a/7zr.exe (downloaded 2026-09-10), Igor Pavlov, GNU LGPL.
SHA-256: ad4c82fadcbdf93c03b4fc440f300509c7d60c5c2f4d183e35d9d70d6957037d
PE32 i386 console program, MSVC-built, relocatable (preferred base 0x400000).
Imports KERNEL32, USER32, ADVAPI32, OLEAUT32 and the dynamic MSVCRT.

7zr-sample.7z: deterministic test archive (generated with py7zr 1.0.0, LZMA2 preset 5,
256 KiB dictionary, one solid block). SHA-256:
09f283630cca24f2e3ab0ccd547589599fc96a2f4e8b9ded46daf2d9438c5a8e
  wine-nx-sample.txt  260000 bytes  CRC32 59965AA4  "Line NNNNN: The quick brown fox ..." x4000
  wine-nx-sample.bin   65536 bytes  CRC32 C2C3F618  LCG bytes (seed 12345, a=1103515245, c=12345, >>23)
"7zr t" decompresses both and checks the CRCs; success prints "Everything is Ok".

7zr-tree.7z: the folder tree from tools/make-7zr-tree.py, archived by this 7zr.exe under
Wine 11.0 with "7zr a wine-nx-tree.7z 7zr-tree -mx1" (LZMA2, solid). 90149 bytes. SHA-256:
e477719f40d14d1d34127a14b3e9b657c596032ca3d6bd99dd3f3fc051bc8524
  7zr-tree\, 7zr-tree\data\, 7zr-tree\data\text\  (3 folders)
  7zr-tree\readme.txt                        155 bytes
  7zr-tree\data\wine-nx-sample.bin         65536 bytes  CRC32 C2C3F618
  7zr-tree\data\text\wine-nx-sample.txt   260000 bytes  CRC32 59965AA4
"7zr x C:\7zr-tree.7z -oC:\7zr-out -y" creates the folders and files and checks the CRCs.
