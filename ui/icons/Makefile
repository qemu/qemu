
# Regenerate bitmaps from the SVG using inkscape CLI export
# and ImageMagick. Don't use ImageMagick for the initial
# SVG conversion, since it merely calls inkscape, but uses
# 96 DPI res resulting in poor quality output.

regenerate:
	for s in 16 24 32 48 64 128 256 512; \
	do \
	    inkscape --without-gui --export-png=qemu_$${s}x$${s}.png \
		--export-width=$$s --export-height=$$s qemu.svg ; \
	done
	convert qemu_32x32.png qemu_32x32.bmp
