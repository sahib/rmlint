#!/bin/sh

FILES=$(find src lib -iname '*.[ch]')

# Format the files according to the style defined in .clang-format
clang-format -i -style=file "${FILES}" || (echo 'clang-format failed'; exit 1);

# clang-format sometimes adds windows like line-endins (\r\n).
# Fix this by issueing dos2unix on every file.
dos2unix --quiet "${FILES}" || (echo 'dos2unix failed'; exit 2);

# Some bad editors might save files with a bad encoding.
# Since a few source code files contain unicode, we fix this with iconv.
for path in ${FILES}; do
    # iconv core dumps when using the same file as input and output.
    # (adding this to my wtf-of-the-day-list)
    iconv -t "utf-8" -o "${path}" < "${path}" || (echo "iconv failed on ${path}"; exit 3)
done;
