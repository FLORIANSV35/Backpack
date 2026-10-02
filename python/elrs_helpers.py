import os
import re
import subprocess


def git_cmd(*args):
    return subprocess.check_output(["git"] + list(args)).decode("utf-8").rstrip('\r\n')


def get_git_version():
    """
    Return a dict with keys
    version: fixed version string for this fork (graphn-hdz-bkpk), independent
        of whether the build environment has access to git/.git history
    sha: the 6 character short sha for the current HEAD revison, falling back to
        VERSION file if not in a git repo, or "000000" if neither is available
    """
    ver = "1.5.9-graphn"
    sha = "000000"

    try:
        sha = git_cmd("rev-parse", "HEAD")
    except:
        if os.path.exists("VERSION"):
            with open("VERSION") as _f:
                data = _f.readline()
                _f.close()
            sha = data.split()[1].strip()

    return dict(version=ver, sha=sha[:6])
