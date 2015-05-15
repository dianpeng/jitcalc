SRCDIR=sljit_src
SLJIT_LIR_FILES = $(SRCDIR)/sljitLir.c 
all:
	g++ -O2 $(SLJIT_LIR_FILES) jitcalc.cc -o calc

.PHONY: clean

clean:
	rm -rf calc

