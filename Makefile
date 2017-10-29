all:
	gcc -std=gnu99 -lm $(shell pkg-config --libs libzip) $(shell pkg-config --libs librtlsdr) -o rtl_sampler main.c
