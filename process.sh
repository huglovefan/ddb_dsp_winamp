#!/bin/sh

if ! command -v ddw_host.exe >/dev/null && [ -x host/ddw_host.exe ]; then
	PATH=$PATH:$PWD/host
fi

err=
for exe in ffmpeg ffprobe ddw_host.exe; do
	command -v "$exe" >/dev/null || "$exe" || err=1
done
[ -z "$err" ] || exit

Barg=
farg=
oarg=
sarg=
wflag=
while getopts 'B:f:o:s:w' o; do
	case $o in
	[Bfos]) eval "${o}arg=\$OPTARG";;
	w) wflag=1;;
	*) exit 1;;
	esac
done
shift $(( OPTIND - 1 ))

if [ -z "$farg" -o -z "$oarg" -o $# -eq 0 ]; then
	cat <<- EOF >&2
	usage: ${0##*/} <options> [--] <dll> ...
	options:
	    -B <bps>   maximum bit depth for the dsp
	    -f <file>  input file (required)
	    -o <file>  output file (required)
	    -s <sec>   feed the dsp this many seconds of silence after the file
	    -w         wait for a keypress before processing
	EOF
	exit 1
fi

get_file_info() {
	info=$(ffprobe -v error \
	               -hide_banner \
	               -show_entries stream=channels,sample_rate,bits_per_sample \
	               -of default=noprint_wrappers=1 \
	               -- "$farg")
	if [ $? -ne 0 ]; then
		return 1
	fi

	sample_rate=
	channels=
	bits_per_sample=
	eval "$info"
	if [ -z "$sample_rate" -o -z "$channels" -o -z "$bits_per_sample" ]; then
		return 1
	fi
	if [ -n "$Barg" ] && [ "$bits_per_sample" -gt "$Barg" ]; then
		>&2 echo ">> Limiting bit depth to ${Barg}."
		bits_per_sample=$Barg
	fi

	return 0
}

stuff() {
	# need to wait?
	if [ -n "$wflag" ]; then
		>&2 echo '>> Press enter to begin.'
		read -r _ 0<>/dev/tty 1>&0 2>&0
	fi
	cat
	# need to insert silence?
	if [ -n "$sarg" ]; then
		>&2 echo ">> Writing ${sarg} second(s) of silence..."
		ffmpeg -v error -f lavfi -i anullsrc -t "$sarg" -f "s${BPS}le" -ac "$CH" -ar "$SR" pipe:1
	fi
}

# wait until there's data on stdin, then run the command
whenready() {
	b=$(dd bs=1 count=1 2>/dev/null | od -A n -t x1)
	b=${b# }; b=${b#0}; b=${b%% *}
	if [ -z "$b" ]; then
		return 1
	fi
	(
		/usr/bin/printf "\\x${b}" # fixme: unportable
		cat
	) | "$@"
}

get_file_info || exit

export DDW_HOST_FIXED=1
export SR="$sample_rate"
export BPS="$bits_per_sample"
export CH="$channels"

ffmpeg -v error -stats -nostdin -i "$farg" -f "s${BPS}le" pipe:1 <&- | \
    stuff | \
    ddw_host.exe "$@" | \
    whenready ffmpeg -y -v error -f "s${BPS}le" -ac "$CH" -ar "$SR" -i pipe:0 -- "$oarg"
