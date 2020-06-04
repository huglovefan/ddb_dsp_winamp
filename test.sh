#!/bin/sh

# just make sure it still works

# this isn't actually always stable but it's close enough
# should make a test plugin to test with

set -u

error() {
	echo "error: $*"
	exit 1
}

hash_dl() {
	url=
	hash=
	output=
	while getopts 'u:h:o:' o; do
		case $o in
		u) url=$OPTARG;;
		h) hash=$OPTARG;;
		o) output=$OPTARG;;
		esac
	done
	shift $(( OPTIND-1 ))
	[ $# -eq 0 ] || return
	if ! [ -e "$output" ]; then
		tmp=$output.crdownload
		curl "$url" -o "$tmp" || return
		if [ "$(sha256sum "$tmp" | cut -d' ' -f1)" != "$hash" ]; then
			rm -f "$tmp"
			return 1
		fi
		mv -f "$tmp" "$output"
	else
		if [ "$(sha256sum "$output" | cut -d' ' -f1)" != "$hash" ]; then
			return 1
		fi
	fi
}

hash_dl \
	-u https://www.gnu.org/music/free-software-song-herzog.ogg \
	-h 0e11642e434ae5f7c1f4ea81b21578f16bdf9eeeae1db3a56596db32cb73bba1 \
	-o input.ogg || error "failed to download/verify test file"

testit() {
	size=$1
	hash=$2
	v env REASONABLE_BUFFER=$((size)) sh process.sh -B 16 -i input.ogg -o output.wav ~/.local/lib/dsp_freeverb.dll || error "process.sh failed"
	outhash=$(sha256sum output.wav | cut -d' ' -f1)
	case $outhash in
	"$hash")
		echo "OK ($outhash)"
		;;
	*)
		echo "error: hash of output file doesn't match"
		echo " expected: $hash"
		echo "      got: $outhash"
		exit 1
		;;
	esac
}

testit 512 e032bdfd6f509f3acbad2d5e8373ac7a9ec39027a1fbb319f5efe1144c5ea9d6
testit 1024 e032bdfd6f509f3acbad2d5e8373ac7a9ec39027a1fbb319f5efe1144c5ea9d6
testit 3072 e032bdfd6f509f3acbad2d5e8373ac7a9ec39027a1fbb319f5efe1144c5ea9d6
testit 12288 e032bdfd6f509f3acbad2d5e8373ac7a9ec39027a1fbb319f5efe1144c5ea9d6
testit 98304 e032bdfd6f509f3acbad2d5e8373ac7a9ec39027a1fbb319f5efe1144c5ea9d6
testit 1572864 e032bdfd6f509f3acbad2d5e8373ac7a9ec39027a1fbb319f5efe1144c5ea9d6
testit 50331648 e032bdfd6f509f3acbad2d5e8373ac7a9ec39027a1fbb319f5efe1144c5ea9d6
testit 576 e032bdfd6f509f3acbad2d5e8373ac7a9ec39027a1fbb319f5efe1144c5ea9d6
testit 1152 e032bdfd6f509f3acbad2d5e8373ac7a9ec39027a1fbb319f5efe1144c5ea9d6
testit 3456 e032bdfd6f509f3acbad2d5e8373ac7a9ec39027a1fbb319f5efe1144c5ea9d6
testit 13824 e032bdfd6f509f3acbad2d5e8373ac7a9ec39027a1fbb319f5efe1144c5ea9d6
testit 110592 e032bdfd6f509f3acbad2d5e8373ac7a9ec39027a1fbb319f5efe1144c5ea9d6
testit 1769472 e032bdfd6f509f3acbad2d5e8373ac7a9ec39027a1fbb319f5efe1144c5ea9d6
testit 56623104 e032bdfd6f509f3acbad2d5e8373ac7a9ec39027a1fbb319f5efe1144c5ea9d6
