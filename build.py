#!/usr/bin/python
# --------------------------------------------------------------------
# build.py - A Xeno build script.
#
# Author: Lain Musgrove (lain.proliant@gmail.com)
# Date: Thursday March 7, 2024
# --------------------------------------------------------------------

import os
from pathlib import Path
from xeno.build import build, provide, task
from xeno.shell import Environment
from xeno.recipes.cxx import compile, ENV


LINK_ENV = Environment(ENV).append(LDFLAGS="-lcrypto")


# --------------------------------------------------------------------
@provide
def cd_local():
    os.chdir(Path(__file__).absolute().parent)


# --------------------------------------------------------------------
@provide
def sources(cd_local):
    return Path.cwd().glob("*.cc")


# --------------------------------------------------------------------
@provide
def headers(cd_local):
    return Path.cwd().glob("*.h")


# --------------------------------------------------------------------
@task
def objects(sources, headers):
    return [compile(src, headers=headers, obj=True) for src in sources]


# --------------------------------------------------------------------
@task(default=True)
def executable(objects):
    return compile(objects.components(), target="hasher", env=LINK_ENV)


# --------------------------------------------------------------------
build()
