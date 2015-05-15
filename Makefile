SRCDIR=sljit_src
SLJIT_LIR_FILES = $(SRCDIR)/sljitLir.c 
all:
	g++ -g $(SLJIT_LIR_FILES) jitcalc.cc -o calc

