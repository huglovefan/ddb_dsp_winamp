#!/bin/sh
#
# downloads the winamp sdk and extracts it to "./Winamp SDK"
# http://wiki.winamp.com/wiki/Plug-in_Developer
#

# check that these exist
command -v curl >/dev/null || curl || err=1
command -v 7z   >/dev/null || 7z   || err=1
[ -z "$err" ] || exit

url=http://download.nullsoft.com/winamp/plugin-dev/WA5.55_SDK.exe
url=https://web.archive.org/web/20190310094225/${url}
sha256=d88abbb9882add625e6286dc68fb7598645862a191d63577be84b72ef0870370
file=${url##*/}

[ -s "$file" ] || curl -L -o "$file" "$url" || exit

s=$(sha256sum "$file" | cut -d ' ' -f 1)
if [ "$s" != "$sha256" ]; then
	cat <<- EOF >&2
	error: sha256 mismatch!
	- try deleting ${file} and re-running the script to download it again,
	  or try downloading it manually from:
	  ${url}
	EOF
	exit 1
fi

7z x -o"Winamp SDK" "$file" || exit

cat << EOF
extracting complete, you can now delete this file:
  ${file}
EOF
