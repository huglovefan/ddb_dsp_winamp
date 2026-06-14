#
# this is a python-based testing ground for the host application.
#

import io
import os
import sys
import enum
import time
import zlib
import random
import struct
import subprocess
from typing import Final, Iterable, List, Optional, final, overload
from dataclasses import dataclass

@final
class SFMT(enum.IntEnum):
	INVALID = 0
	S8      = enum.auto()
	S16     = enum.auto()
	S24     = enum.auto()
	S32     = enum.auto()
	F32     = enum.auto()
	COUNT   = enum.auto()

	def is_valid(self) -> bool:
		return (
		    self.value > SFMT.INVALID
		    and self.value < SFMT.COUNT
		)

	def equal(self, other: 'SFMT') -> bool:
		if not self.is_valid():
			return False
		if not other.is_valid():
			return False
		return self.value == other.value

	def is_float(self) -> bool:
		return self.value == SFMT.F32

	def bytes(self) -> int:
		if self.value == SFMT.S8:
			return 1
		if self.value == SFMT.S16:
			return 2
		if self.value == SFMT.S24:
			return 3
		if self.value == SFMT.S32:
			return 4
		if self.value == SFMT.F32:
			return 4
		return 0

	def bytes_n(self, n: int) -> int:
		return n * self.bytes()

	def __str__(self) -> str:
		if self.value == SFMT.S8:
			return 's8'
		if self.value == SFMT.S16:
			return 's16'
		if self.value == SFMT.S24:
			return 's24'
		if self.value == SFMT.S32:
			return 's32'
		if self.value == SFMT.F32:
			return 'f32'
		return str(self.value)

	@staticmethod
	def from_int_bits(bits: int) -> 'SFMT':
		if bits == 8:
			return SFMT.S8
		if bits == 16:
			return SFMT.S16
		if bits == 24:
			return SFMT.S24
		if bits == 32:
			return SFMT.S32
		return SFMT.INVALID

@final
class AFMT:
	sfmt: SFMT
	ch:   int
	rate: int

	@overload
	def __init__(self) -> None: ...

	@overload
	def __init__(self, sfmt: SFMT, ch: int, rate: int) -> None: ...

	def __init__(
	    self,
	    sfmt: SFMT|None = None,
	    ch:   int|None = None,
	    rate: int|None = None,
	) -> None:
		self.sfmt = sfmt if sfmt is not None else SFMT.INVALID
		self.ch   = ch   if ch   is not None else 0
		self.rate = rate if rate is not None else 0

	def is_valid(self) -> bool:
		if not self.sfmt.is_valid():
			return False
		if not (self.ch >= 1 and self.ch <= 0x7fff_ffff):
			return False
		if not (self.rate >= 1 and self.rate <= 0x7fff_ffff):
			return False
		return True

	def equal(self, other: 'AFMT') -> bool:
		if not self.is_valid():
			return False
		if not other.is_valid():
			return False
		return (
		    self.sfmt.equal(other.sfmt)
		    and self.ch == other.ch
		    and self.rate == other.rate
		)

	def frame_bytes(self) -> int:
		sample_size = self.sfmt.bytes()
		return sample_size * self.ch

	def frame_bytes_n(self, nframes: int) -> int:
		assert(nframes >= 0)
		return self.frame_bytes() * nframes

	def buf_frames(self, buflen: int) -> int:
		assert(buflen >= 0)
		frame_size = self.frame_bytes()
		assert(buflen % frame_size == 0)
		return buflen // frame_size

	def buf_frames_round_down(self, buflen: int) -> int:
		assert(buflen >= 0)
		return buflen // self.frame_bytes()

	def __str__(self) -> str:
		return '%s %uch %uHz' % (self.sfmt, self.ch, self.rate)

@final
class ABUF:
	fmt:  AFMT
	data: bytes

	@overload
	def __init__(self) -> None: ...

	@overload
	def __init__(self, fmt: AFMT, data: bytes) -> None: ...

	def __init__(
	    self,
	    fmt:  AFMT|None = None,
	    data: bytes|None = None,
	) -> None:
		self.fmt  = fmt  if fmt  is not None else AFMT()
		self.data = data if data is not None else bytes()

@final
class SFMT_REQ(enum.IntEnum):
	ANY       = 100 # /* any format */
	ANY_32BIT = 101 # /* any 32-bit format (s32 or f32) */
	ONLY_F32  = 102 # /* f32 */
	ONLY_S32  = 103 # /* s32 */

@final
class MSG_CODE(enum.IntEnum):
	P2H_PROCESS_SAMPLES          = 1000
	H2P_PROCESS_SAMPLES_RESPONSE = 1001
	P2H_PLAYBACK_INTERRUPTED     = 2000

@final
class audio_file:
	path: Final[str]

	def __init__(self, path: str) -> None:
		self.path = path

	def get_fmt(self) -> AFMT:
		bps = 0
		ch = 0
		rate = 0
		try:
			p = subprocess.run([
			    'ffprobe',
			    '-v', 'error',
			    '-hide_banner',
			    '-show_entries',
			      'stream=channels,sample_rate,sample_fmt',
			    '-of', 'default=noprint_wrappers=1',
			    '--',
			    self.path,
			], capture_output=True, check=True)
		except subprocess.CalledProcessError as e:
			sys.stderr.buffer.write(e.stderr)
			raise
		for line in io.BytesIO(p.stdout):
			k, v = line.rstrip(b'\n').split(b'=')
			if k == b'sample_fmt':
				if v == b's16':
					bps = 16
				elif v == b's24':
					bps = 24
				elif v == b's32':
					bps = 32
				else:
					print('fixme: unsupported sample_fmt:', v)
					assert(0)
			elif k == b'channels':
				ch = int(v)
			elif k == b'sample_rate':
				rate = int(v)
			else:
				assert(0)
		return AFMT(SFMT.from_int_bits(bps), ch, rate)

	def get_reader(self, fmt: AFMT) -> subprocess.Popen[bytes]:
		p = subprocess.Popen([
		    'ffmpeg',
		    '-v', 'error',
		    '-nostdin',
		    '-i', self.path,
		    '-f', 's%dle' % (fmt.sfmt.bytes()*8,),
		    'pipe:1',
		], stdout=subprocess.PIPE)
		return p

@final
class audio_file_writer:
	p: subprocess.Popen[bytes]

	def __init__(self, fmt: AFMT, path: str) -> None:
		sf = None
		if fmt.sfmt == SFMT.S8:
			sf = 's8'
		elif fmt.sfmt == SFMT.F32:
			sf = 'f32'
		else:
			sf = 's%dle' % (fmt.sfmt.bytes()*8,)
		self.p = subprocess.Popen(
		    ['ffmpeg',
		     '-y',
		     '-v', 'error',
		     '-f', sf,
		     '-ac', str(fmt.ch),
		     '-ar', str(fmt.rate),
		     '-i', 'pipe:0',
		     '--',
		     path],
		    stdin=subprocess.PIPE)

@final
class host:
	p: subprocess.Popen[bytes]

	def __init__(self, plugins: List[str]) -> None:
		self.p = subprocess.Popen(
		    ['wine',
		     'wadsp_host.exe']+plugins,
		    stdin=subprocess.PIPE,
		    stdout=subprocess.PIPE)

	def modify_samples(
	    self,
	    buf: ABUF,
	    *,
	    sfmt_req: SFMT_REQ = SFMT_REQ.ANY,
	    response_max_bytes: int = 0xffff_ffff_ffff_ffff,
	) -> ABUF:
		datacksum = zlib.crc32(buf.data)

		req_fmt = '=IQIIIIQII'

		req = struct.pack(req_fmt,
		    MSG_CODE.P2H_PROCESS_SAMPLES,
		    len(buf.data),
		    buf.fmt.sfmt,
		    buf.fmt.ch,
		    buf.fmt.rate,
		    sfmt_req,
		    response_max_bytes,
		    datacksum,
		    0)
		req = bytearray(req)

		hdrcksum = zlib.crc32(req)
		struct.pack_into(
		    'I',
		    req,
		    struct.calcsize(req_fmt[:-1]),
		    hdrcksum)

		assert(self.p.stdin)
		self.p.stdin.write(bytes(req))
		self.p.stdin.write(buf.data)
		self.p.stdin.flush()

		res_fmt = '=IQIIIII'

		assert(self.p.stdout)
		res = self.p.stdout.read(struct.calcsize(res_fmt))
		assert(res)
		res = bytearray(res)

		(code,
		 buffer_size,
		 sampleformat,
		 channels,
		 samplerate,
		 datacksum,
		 hdrcksum) = struct.unpack(res_fmt, res)
		assert(code == MSG_CODE.H2P_PROCESS_SAMPLES_RESPONSE)

		struct.pack_into(
		    'I',
		    res,
		    struct.calcsize(res_fmt[:-1]),
		    0)
		assert(hdrcksum == zlib.crc32(bytes(res)))

		fmt = AFMT(SFMT(sampleformat), channels, samplerate)
		assert(fmt.is_valid())

		dat = self.p.stdout.read(buffer_size)
		assert(datacksum == zlib.crc32(dat))

		return ABUF(fmt, dat)

	def reset_buffers(self) -> None:

		req = struct.pack('=I',
		    MSG_CODE.P2H_PLAYBACK_INTERRUPTED)

		assert(self.p.stdin)
		self.p.stdin.write(req)
		self.p.stdin.flush()






# benchmark loop silence
if 1:
	h = host(['dsp_test'])
	fmt = AFMT(SFMT.F32, 2, 44100)
	sec_in = 0.092
	frames_in = round(sec_in * fmt.rate)
	while 1:
		buf = bytes(fmt.frame_bytes_n(frames_in))
		tm_start = time.perf_counter()
		mod = h.modify_samples(
		    ABUF(fmt, buf),
		    sfmt_req=SFMT_REQ.ANY)
		tm_end = time.perf_counter()
		tm_elapsed = tm_end-tm_start
		frames_out = mod.fmt.buf_frames(len(mod.data))
		secs_out = frames_out / mod.fmt.rate
		print('%.3f msec (%.0fx)' % (tm_elapsed*1000.0, secs_out / tm_elapsed))
		time.sleep(secs_out - min(secs_out, tm_elapsed))
