all:
	gcc -std=gnu99 $(shell pkg-config --libs libzip) $(shell pkg-config --libs librtlsdr) -o rtl_sampler main.c
