#!/bin/sh

gen_tar() {
    tar -c --to-stdout --sort=name src/tar/test/tar-contents # unfortunately, this depends on gnu tar for sorting by name
}

gen_tar | tar -tf -
gen_tar | $DEBUG_PROGRAM test/list-tar
