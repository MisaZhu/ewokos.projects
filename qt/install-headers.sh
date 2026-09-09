#!/bin/sh
#
# Install syncqt include trees into the SDK, resolving forwarding headers.
#
# syncqt does not copy headers: almost every file under <tree>/include is a
# one-line forwarder - #include "../../src/corelib/text/qstring.h" - whose
# relative path only works from inside the Qt tree.  Copying such a file
# verbatim into the SDK would leave it pointing at nothing, so any file whose
# first line has the form  #include "../..."  is resolved here and the real
# header is copied in its place.  Everything else - class forwarders like
# QtCore/QString (#include "qstring.h", same directory, still valid after the
# copy), the *Depends files, .pri files - is copied as-is.
#
# Both trees are needed: the source tree holds the public/private/qpa headers,
# the build tree holds the generated configuration headers (qconfig.h,
# qt<module>-config.h and the private _p variants).  Trees are installed in
# the order given; later trees overwrite earlier ones.
#
# usage: install-headers.sh DEST TREE...

set -e

dst="$1"; shift
[ -n "$dst" ] || { echo "usage: install-headers.sh DEST TREE..." >&2; exit 1; }

for tree in "$@"; do
	[ -d "$tree" ] || continue
	# The file list goes through a temp file, not a pipe: in `find | while`
	# the pipeline's exit status is the while loop's, so a find that dies
	# midway would go unnoticed and a truncated tree would be installed with
	# a clean exit.  With a redirection, set -e catches the find failure.
	list="$dst/.filelist.$$"
	(cd "$tree" && find . -type f) > "$list"
	while IFS= read -r f; do
		rel="${f#./}"
		out="$dst/$rel"
		mkdir -p "$(dirname "$out")"
		first=$(head -n 1 "$tree/$rel" 2>/dev/null)
		case "$first" in
		'#include "../'*)
			inc=${first#\#include \"}
			inc=${inc%%\"*}
			real="$tree/$(dirname "$rel")/$inc"
			if [ -f "$real" ]; then
				cp -p "$real" "$out"
			else
				# a forwarder whose target is gone: keep the file so the
				# breakage is visible at compile time, not silently absent
				cp -p "$tree/$rel" "$out"
			fi
			;;
		*)
			cp -p "$tree/$rel" "$out"
			;;
		esac
	done < "$list"
	rm -f "$list"
done

# Completeness gate.  The caller stamps the tree as installed on our exit
# status, so a partial tree must never look like success.  qglobal.h is the
# root of every Qt include chain; its include guard only appears in the real
# header, never in the one-line forwarder, so this catches both a missing and
# an unresolved copy.
if ! grep -q '#ifndef QGLOBAL_H' "$dst/QtCore/qglobal.h" 2>/dev/null; then
	echo "install-headers.sh: $dst is incomplete (QtCore/qglobal.h missing or unresolved)" >&2
	exit 1
fi
