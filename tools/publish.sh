#!/bin/sh
# Copies this tree (as committed) to the public repo (default ../zappo-x),
# leaving out the private planning notes, and commits there.  Push by hand.
#   usage: tools/publish.sh "message"
set -e
R=$(cd "$(dirname "$0")/.." && pwd)
P=${PUBLIC:-$R/../zappo-x}
cd "$P"
git rm -rq --ignore-unmatch . >/dev/null
git -C "$R" archive HEAD | tar -x -C "$P"
rm -f ZAPPO-X-PLAN.md docs/progress.json docs/m3-twin.jpg   # private: the plan, the progress page and its pictures
[ -d docs ] && [ -z "$(ls -A docs)" ] && rmdir docs
git add -A
git -c user.name=Claude -c user.email=noreply@anthropic.com commit -q -m "$1" && git log --oneline -1
