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

ADAM2 bootloader flash layout
=============================

See http://wiki.ip-phone-forum.de/software:ds-mod:development:flash

4 MiB

flash: mtd[0] 0x900C0000 - 0x903C0000 (filesystem)
flash: mtd[1] 0x90010000 - 0x900C0000 (kernel)          720896
flash: mtd[2] 0x90000000 - 0x90010000 (urlader)
flash: mtd[3] 0x903C0000 - 0x903E0000 (tffs)
flash: mtd[4] 0x903E0000 - 0x90400000 (tffs)
flash: mtd[5] 0x900a1b00 - 0x900c0000 (hidden filesystem)

8 MiB

mtd0    0x90000000,0x90000000
mtd1    0x90010000,0x90780000
mtd2    0x90000000,0x90010000
mtd3    0x90780000,0x907C0000
mtd4    0x907C0000,0x90800000

These addresses are only examples. They differ for different flash sizes.

BRN bootloader flash layout
===========================
Files:
0xb0000000.bin	flash image
0xb0020000.bin	configuration
0xb0040000.bin	web
0xb0110000.bin	code            917504
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

# SX541
#~ PARTITIONS = {
	#~ 'boot' => { FILE => '0xb0000000.bin', START => 0xb0000000, SIZE => 0x20000, LABEL => 'boot'},
	#~ 'configuration' => { FILE => '0xb0020000.bin', START => 0xb0020000, SIZE => 0x20000, LABEL => 'configuration'},
	#~ 'web' => { FILE => '0xb0040000.bin', START => 0xb0040000, SIZE => 0x50000, LABEL => 'web'},
	#~ 'code' => { FILE => '0xb0110000.bin', START => 0xb0090000, SIZE => 0x150000, LABEL => 'code'},
	#~ 'params' => { FILE => '0xb01f0000.bin', START => 0xb01f0000, SIZE => 0x10000, LABEL => 'params'}
#~ }

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
  def initialize(filename = FLASHIMAGE)
    @filename = filename
    @flashimage = Flashimage.read(filename)
  end

  def update(partition)
    @flashimage[partition.range] = partition.data
    #~ puts("#{__FILE__}:#{__LINE__} #{@flashimage.size}")
  end

  def read(filename = @filename)
    @flashimage = Flashimage.read(filename)
    return @flashimage
  end

  def write
    Flashimage.write(@flashimage, @filename)
  end

  def data
    return @flashimage
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
def splitflashimage(filename)
        filename = FLASHIMAGE if filename.nil?
        flashdata = Flashimage.read(filename)
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
	puts("length:    #{'0x%08x' % length} = #{length}")
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

module Adam2
  #~ FLASHIMAGE = "boot/adam2-flashimage.bin"
  FLASHIMAGE = "boot/test/flashimage.bin"
  FLASHSIZE = 4 * MiB
  if FLASHSIZE == 2 * MiB
    MTD0start = 0x00000000
    MTD1start = 0x00010000
    MTD2start = 0x00000000
    MTD3start = 0x00780000
    MTD4start = 0x007c0000
    MTD0end = MTD0start
    #~ MTD1end = MTD3start
    MTD1end = 0x00800000
    MTD0size = MTD0end - MTD0start
    MTD1size = MTD1end - MTD1start
  elsif FLASHSIZE == 4 * MiB
    # 4 MiB flash
    MTD0start = 0x000c0000
    MTD1start = 0x00010000
    MTD2start = 0x00000000
    MTD3start = 0x003c0000
    MTD4start = 0x003e0000
    MTD0end = MTD3start
    MTD1end = MTD0start
    MTD2end = MTD1start
    MTD0size = MTD0end - MTD0start
    MTD1size = MTD1end - MTD1start
    MTD2size = MTD2end - MTD2start
  elsif FLASHSIZE == 8 * MiB
    raise
    # 8 MiB flash
    #~ mtd0    0x90000000,0x90000000    # filesystem
    #~ mtd1    0x90010000,0x90780000    # kernel
    #~ mtd2    0x90000000,0x90010000    # ADAM2
    #~ mtd3    0x90780000,0x907C0000
    #~ mtd4    0x907C0000,0x90800000
  else
    raise
  end

  def self.loadadam2(filename, imagename)
    imagename = FLASHIMAGE if imagename.nil?
    flashimage = Flashimage.new(imagename)
    flashimage.data[MTD2start ... MTD2end] = "\xff" * MTD2size
    data = Flashimage.read(filename)
    puts("Bootloader size: #{'0x%08x' % data.size} byte (max #{'0x%08x' % MTD2size}")
    datasize = data.size - 8
    raise if datasize > MTD2size
    flashimage.data[MTD2start ... MTD2start + datasize] = data[0 ... datasize]
    flashimage.write
  end

  def self.loadfilesystem(filename, imagename)
    imagename = FLASHIMAGE if imagename.nil?
    flashimage = Flashimage.new(imagename)
    flashimage.data[MTD0start ... MTD0end] = "\xff" * MTD0size
    data = Flashimage.read(filename)
    puts("Filesystem size: #{'0x%08x' % data.size} byte (max #{'0x%08x' % MTD0size}")
    datasize = data.size - 8
    raise if datasize > MTD0size
    flashimage.data[MTD0start ... MTD0start + datasize] = data[0 ... datasize]
    flashimage.write
  end

  def self.loadkernel(filename, imagename)
    imagename = FLASHIMAGE if imagename.nil?
    flashimage = Flashimage.new(imagename)
    flashimage.data[MTD1start ... MTD1end] = "\xff" * MTD1size
    data = Flashimage.read(filename)
    puts("Kernel size: #{'0x%08x' % data.size} byte (max #{'0x%08x' % MTD1size}")
    datasize = data.size - 8
    #~ raise if datasize > MTD1size
    flashimage.data[MTD1start ... MTD1start + datasize] = data[0 ... datasize]
    flashimage.write
  end
end # Adam2

# Extract bootloader from binary file.
def extract(filename, destination)
    data = Flashimage.read(filename)
    offset = data.index("\x00\x90\x80\x40")
    while offset
        data = data[offset .. -1]
        puts("Bootloader at offset #{offset}")
        if destination
          Flashimage.write(data[0 ... 0x10000], "#{destination}")
        end
        data = data[4 .. -1]
        offset = data.index("\x00\x90\x80\x40")
    end
end

# Extract squashfs from binary file.
def getsquashfs(filename, destination)
    data = Flashimage.read(filename)
    n = 0
    offset = data.index('hsqs')
    while offset
        data = data[offset .. -1]
        puts("SQUASH filesystem at offset #{offset}")
        if destination
          n += 1
          Flashimage.write(data, "#{destination}#{n}.squashfs")
        end
        data = data[4 .. -1]
        offset = data.index('hsqs')
    end
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
elsif command == 'extract-bootloader'
	extract(ARGV[1], ARGV[2])
elsif command == 'split'
	# Split flash image in partitions.
	splitflashimage(ARGV[1])
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
elsif command == 'load-bootloader'
	# Load kernel from file into flash (AVM).
	Adam2.loadadam2(ARGV[1], ARGV[2])
elsif command == 'load-kernel'
	# Load kernel from file into flash (AVM).
	Adam2.loadkernel(ARGV[1], ARGV[2])
elsif command == 'load-filesystem'
	# Load filesystem from file into flash (AVM).
	Adam2.loadfilesystem(ARGV[1], ARGV[2])
elsif command == 'configuration'
	# Modify web partition.
	changepartition(PARTITIONS['configuration'], ARGV[1])
elsif command == 'web'
	# Modify web partition.
	changepartition(PARTITIONS['web'], ARGV[1])
elsif command == 'code'
	# Modify code partition.
	changepartition(PARTITIONS['code'], ARGV[1])
elsif command == 'getsquashfs'
	# Extract squashfs from binary file.
	getsquashfs(ARGV[1], ARGV[2])
elsif command == 'test'
	test
else
	puts("Unknown command #{command.inspect}.")
	puts("Supported commands: create, split, merge, load-fw, load-web, load-code,")
	puts("configuration, code, web, getsquashfs, test.")
end	

# eof
