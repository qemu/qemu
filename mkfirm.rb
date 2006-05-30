#!/usr/bin/ruby -w

=begin

$Id$

mkfirm - create and change firmware and flash images

Copyright (c) 2006 Stefan Weil

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA

Files:
0xb0000000.bin	flash image
0xb0020000.bin	configuration
0xb0040000.bin	web
0xb0110000.bin	code
0xb01f0000.bin	params

Vergleich zip code und code partition:

--- 1	2006-04-22 11:52:11.000000000 +0200
+++ 2	2006-04-22 11:52:21.000000000 +0200
@@ -43176,5 +43176,8 @@
 000a8aa0  00 00 00 00 00 a4 81 00  00 00 00 73 6f 68 6f 2e  |.....¤.....soho.|
 000a8ab0  62 69 6e 55 54 05 00 03  1f 35 79 41 55 78 00 00  |binUT....5yAUx..|
 000a8ac0  50 4b 05 06 00 00 00 00  01 00 01 00 43 00 00 00  |PK..........C...|
-000a8ad0  7d 8a 0a 00 00 00                                 |}.....|
-000a8ad6
+000a8ad0  7d 8a 0a 00 00 00 ff ff  ff ff ff ff ff ff ff ff  |}.....ÿÿÿÿÿÿÿÿÿÿ|
+000a8ae0  ff ff ff ff ff ff ff ff  ff ff ff ff ff ff ff ff  |ÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿ|
+*
+000dfff0  ff ff ff ff d6 8a 0a 00  78 56 34 12 cb 3c 60 2d  |ÿÿÿÿÖ...xV4.Ë<`-|
+000e0000

	data (zipped soho.img)
	(n) 0xff fill bytes
	(4) length of data (little endian)
	(4) magic number 0x12345678 (little endian)
	(4) CRC-32 of data
	(10) signature

=end

require 'zlib'	# Zlib.crc32

KiB = 1024
MiB = (KiB * KiB)

FLASHIMAGE = 'boot/flashimage.bin'
FLASHSIZE = 2 * MiB

FILE = 'file'
START = 'start'
SIZE = 'size'
LABEL = 'label'

# Sinus 154 DSL Basic SE
# Sinus 154 DSL Basic 3
PARTITIONS = {
	'boot' => { FILE => '0xb0000000.bin', START => 0xb0000000, SIZE => 0x20000, LABEL => 'boot'},
	'configuration' => { FILE => '0xb0020000.bin', START => 0xb0020000, SIZE => 0x20000, LABEL => 'configuration'},
	'web' => { FILE => '0xb0040000.bin', START => 0xb0040000, SIZE => 0xd0000, LABEL => 'web'},
	'code' => { FILE => '0xb0110000.bin', START => 0xb0110000, SIZE => 0xe0000, LABEL => 'code'},
	'params' => { FILE => '0xb01f0000.bin', START => 0xb01f0000, SIZE => 0x10000, LABEL => 'params'}
}

# AVM Fritz!Box Fon WLAN
#~ PARTITIONS = {
	#~ 'code' 0x10000
	#~ 'filesystem' 0x9d100, 0xc0000
#~ }

class Partition
	BASEADDRESS = PARTITIONS['boot'][START]

	def initialize(name)
		@name = name
		@partition = PARTITIONS[@name]
		erase
	end
	def data
		return @data
	end
	def erase
		@data = "\xff" * size
	end
	def range
		offset = start - Partition::BASEADDRESS
		return offset ... offset + size
	end
	def size
		return @partition[SIZE]
	end
	def start
		return @partition[START]
	end
	def update(firmware)
		raise if firmware.size > size
		erase
		@data[0 ... firmware.size] = firmware
		@data[size - 3 * 4 ... size] = [firmware.size, 0x12345678, Zlib.crc32(firmware)].pack('V3')
	end
end

class Flashimage
	def initialize
		@flashimage = Flashimage.read
	end

	def update(partition)
		@flashimage[partition.range] = partition.data
		#~ puts("#{__FILE__}:#{__LINE__} #{@flashimage.size}")
	end

	def write
		Flashimage.write(@flashimage)
	end

	def self.read(filename = FLASHIMAGE)
		flashdata = nil
		File.open(filename, 'rb') { |f|
			flashdata = f.read
		}
		return flashdata
	end

	def self.write(flashdata, filename = FLASHIMAGE)
		File.open(filename, 'wb') { |f|
			f.write(flashdata)
		}
	end
end

# Create an erased flash.
def createflashimage
	flashdata = "\xff" * FLASHSIZE
	Flashimage.write(flashdata)
end

# Split flash image in partitions.
def splitflashimage
	flashdata = Flashimage.read
	size = flashdata.size
	puts("Flash image size: #{size / MiB} MiB = #{size} B.")
	raise('Flash size is unexpected.') if (size != FLASHSIZE)
	baseaddress = PARTITIONS['boot'][START]
	PARTITIONS.each { |label, partition|
		first = partition[START] - baseaddress
		last = first + partition[SIZE]
		filename = "#{File.dirname(FLASHIMAGE)}/#{partition[FILE]}"
		Flashimage.write(flashdata[first ... last], filename)
	}
end

# Make flash image from partitions.
def mergepartitions
	flashdata = "\xff" * FLASHSIZE
	baseaddress = PARTITIONS['boot'][START]
	PARTITIONS.each { |label, partition|
		first = partition[START] - baseaddress
		last = first + partition[SIZE]
		filename = "#{File.dirname(FLASHIMAGE)}/#{partition[FILE]}"
		partdata = Flashimage.read(filename)
		size = partdata.size
		puts("Partition size: #{size / KiB} KiB = #{size} B (#{label}).")
		raise('Partition size is unexpected.') if (size != partition[SIZE])
		flashdata[first ... last] = partdata
	}
	Flashimage.write(flashdata)
end

# Change partition (configuration, web or code).
def changepartition(partition, filename)
	baseaddress = PARTITIONS['boot'][START]	
	size = partition[SIZE]
	partdata = Flashimage.read(filename)
	length = partdata.size
	last = partition[SIZE]
	raise('Input file too large.') if length + 12 > last
	crc32 = Zlib.crc32(partdata)
	partdata[length ... last - 12] = "\xff" * (last - length - 12)
	partdata[last - 12 ... last] = [length, 0x12345678, crc32].pack('V3')
	filename = "#{File.dirname(FLASHIMAGE)}/#{partition[FILE]}"
	Flashimage.write(partdata, filename)
end

def getblock(text, firmware, offset)
	puts
	puts("Get #{text} part...")
	offset -= 4 * 3
	length, magic, crc32 = firmware.unpack("x#{offset}V3")
	while firmware[offset - 1] == 0xff
		offset -= 1
	end
	block_end = offset
	block_start = offset - length
	puts("offset:    #{'0x%08x' % block_start}")
	puts("length:    #{length}")
	if magic == 0x12345678
		puts("magic:     #{'0x%08x' % magic}, ok")
	else
		puts("magic:     #{'0x%08x' % magic}, bad")
	end
	if crc32 == Zlib.crc32(firmware[block_start ... block_start + length])
		puts("crc32:     #{'0x%08x' % crc32}, ok")
	else
		puts("crc32:     #{'0x%08x' % crc32}, bad")
	end
	if firmware[block_start ... block_start + 2] != 'PK'
		puts("zipdata:   missing 'PK' at offset 0")
	end
	if firmware[block_start + 2] != 3
		puts("zipdata:   missing 0x03 at offset 2")
	end
	if firmware[block_start + 3] != 4
		puts("zipdata:   missing 0x04 at offset 3")
	end
	if (firmware[block_start + 7] & 1) == 1
		puts("zipdata:   wrong bit 0 at offset 7")
	end
	if firmware[block_start + 8 .. block_start + 9] != "\x08\x00"
		puts("zipdata:   missing 0x08,0x00 at offset 8")
	end
	return length, magic, crc32, block_start
end

# Load partition file (code, web or configuration).
def loadpart(partname, filename)
	flashimage = Flashimage.new
	firmware = Flashimage.read(filename)
	size = firmware.size
	offset_signature = size - 10
	signature = firmware[offset_signature .. -1]
	puts("signature: #{signature.inspect}")
	length, magic, crc32, offset = getblock(partname, firmware, offset_signature)
	partition = Partition.new(partname)
	partition.update(firmware[offset ... offset + length])
	flashimage.update(partition)
	flashimage.write
end

# Load firmware file (code and web).
def loadfw(filename)
	flashimage = Flashimage.new
	firmware = Flashimage.read(filename)
	size = firmware.size
	offset_signature = size - 10
	signature = firmware[offset_signature .. -1]
	puts("signature: #{signature.inspect}")
	length, magic, crc32, offset_code = getblock('code', firmware, offset_signature)
	code = Partition.new('code')
	code.update(firmware[offset_code ... offset_code + length])
	puts code.size
	length, magic, crc32, offset_web = getblock('web', firmware, offset_code)
	web = Partition.new('web')
	puts web.size
	web.update(firmware[offset_web ... offset_web + length])
	flashimage.update(code)
	flashimage.update(web)
	flashimage.write
end

def test
	PARTITIONS.each { |label, partition|
		puts label.inspect
		puts partition.inspect
	}
end

command = ARGV[0]

if command == 'create'
	createflashimage
elsif command == 'split'
	# Split flash image in partitions.
	splitflashimage
elsif command == 'merge'
	# Make flash image from partitions.
	mergepartitions
elsif command == 'load-fw'
	# Load firmware file (code and web).
	loadfw(ARGV[1])
elsif command == 'load-configuration'
	# Load configuration partition from file into flash.
	loadpart('configuration', ARGV[1])
elsif command == 'load-web'
	# Load web partition from file into flash.
	loadpart('web', ARGV[1])
elsif command == 'load-code'
	# Load code partition from file into flash.
	loadpart('code', ARGV[1])
elsif command == 'configuration'
	# Modify web partition.
	changepartition(PARTITIONS['configuration'], ARGV[1])
elsif command == 'web'
	# Modify web partition.
	changepartition(PARTITIONS['web'], ARGV[1])
elsif command == 'code'
	# Modify code partition.
	changepartition(PARTITIONS['code'], ARGV[1])
elsif command == 'test'
	test
else
	puts("Unknown command #{command.inspect}.")
	puts("Supported commands: create, split, merge, load-fw, load-web, load-code,")
	puts("configuration, code, web, test.")
end	

# eof
