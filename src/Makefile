C_FILES = $(wildcard *.c)
H_FILES = $(wildcard *.h)
O_FILES = $(C_FILES:.c=.o)

.PHONY: all clean
.DEFAULT: all

all: mapgisf

mapgisf: $(O_FILES)
	gcc -g -o $@ $^

%.o: %.c $(H_FILES)
	gcc -g -c $<

clean:
	-rm -f $(O_FILES)
	-rm -f mapgisf
