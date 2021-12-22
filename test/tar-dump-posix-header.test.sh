#!/bin/sh

tar -c --to-stdout src/tar/test/tar-contents | test/tar-dump-posix-header
