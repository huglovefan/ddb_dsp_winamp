#!/bin/sh

err=
for exe in ffmpeg ffprobe bc pv; do
	command -v "$exe" >/dev/null || "$exe" || err=1
done
if [ -z "$HOST_CMD" ]; then
	command -v wine >/dev/null || wine || err=1
	[ -x ./host/ddw_host.exe ] || ./host/ddw_host.exe || err=1
fi

Barg=
Sarg=
iarg=
oarg=
sarg=
wflag=
while getopts 'B:S:i:o:s:w' o; do
	case $o in
	[BSios]) eval "${o}arg=\$OPTARG";;
	w) wflag=1;;
	*) exit 1;;
	esac
done
shift $(( OPTIND - 1 ))

if [ -z "$iarg" -o -z "$oarg" -o $# -eq 0 ]; then
	if [ -n "$err" ]; then
		>&2 echo "warning: some required programs are missing"
	fi
	cat <<- EOF >&2
	usage: ${0##*/} <options> [--] <dll> ...
	options:
	    -B <bps>   maximum bit depth for the dsp
	    -i <file>  input file (required)
	    -o <file>  output file (required)
	    -S <sec>   feed the dsp this many seconds of silence before the file
	    -s <sec>   feed the dsp this many seconds of silence after the file
	    -w         wait for a keypress before processing
	environment variables:
	    HOST_CMD   command to run ddw_host.exe (default: "wine ./host/ddw_host.exe")
	EOF
	exit 1
fi

if [ -n "$err" ]; then
	>&2 echo "error: missing required programs, exiting"
	exit 1
fi

get_file_info() {
	info=$(Ffprobe -show_entries stream=channels,sample_rate,bits_per_sample,duration \
	               -of default=noprint_wrappers=1 \
	               -- "$iarg")
	if [ $? -ne 0 ]; then
		return 1
	fi

	sample_rate=
	channels=
	bits_per_sample=
	duration=
	eval "$info"
	if [ "$bits_per_sample" = "0" ]; then # .ogg has a zero here
		bits_per_sample=16
	fi
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
	if [ -n "$wflag" ]; then
		>&2 echo '>> Press enter to begin.'
		read -r _ 0<>/dev/tty 1>&0 2>&0
	fi
	if [ -n "$Sarg" ]; then
		Ffmpeg -f lavfi -i anullsrc -t "$Sarg" -f "s${BPS}le" -ac "$CH" -ar "$SR" pipe:1
	fi
	cat
	if [ -n "$sarg" ]; then
		Ffmpeg -f lavfi -i anullsrc -t "$sarg" -f "s${BPS}le" -ac "$CH" -ar "$SR" pipe:1
	fi
}

# wait until there's data on stdin, then run the command
ondata() {
	b=$(dd bs=1 count=1 2>/dev/null | od -A n -t x1)
	b=${b# }; b=${b#0}; b=${b%% *}
	if [ -z "$b" ]; then
		return 1
	fi
	{
		# needs a printf with hex escape support
		"$(which printf)" "\\x${b}"
		cat
	} | "$@"
}

Ffprobe() {
	ffprobe -v error "$@"
}
Ffmpeg() {
	ffmpeg -nostdin -v error -y "$@"
	# alias doesn't work if you run it through "$@"
}

get_file_info || exit

export DDW_HOST_FIXED=1
export SR="$sample_rate"
export BPS="$bits_per_sample"
export CH="$channels"

# this is absolutely essential
bytes=$(echo "(${Sarg:-0}+${duration}+${sarg:-0})*${sample_rate}*(${bits_per_sample}/8)*${channels}" | bc)
bytes=${bytes%%.*}

Ffmpeg -i "$iarg" -f "s${BPS}le" pipe:1 | \
    stuff | \
    ${HOST_CMD:-wine host/ddw_host.exe} "$@" | \
    ondata pv -s "$bytes" | \
    ondata Ffmpeg -f "s${BPS}le" -ac "$CH" -ar "$SR" -i pipe:0 -- "$oarg"
