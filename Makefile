# CREATED BY VIM-PIO
all:
	platformio -f -c vim run -e usb

upload:
	platformio -f -c vim run --target upload -e usb

uploadusbasp:
	platformio -f -c vim run --target upload -e usbasp

clean:
	platformio -f -c vim run --target clean

# program:
# 	platformio -f -c vim run --target program
#
# uploadfs:
# 	platformio -f -c vim run --target uploadfs
compiledb:
	pio run -t compiledb -e usb
