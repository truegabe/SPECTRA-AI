# SPECTRA runtime Makefile
# Builds the C runtime as a static library

CC      = gcc
CFLAGS  = -std=c99 -O2 -Wall -Wextra -Iruntime
SRCDIR  = runtime
OBJDIR  = build

SRCS    = $(SRCDIR)/specton.c $(SRCDIR)/tensor.c $(SRCDIR)/memory.c
OBJS    = $(OBJDIR)/specton.o $(OBJDIR)/tensor.o $(OBJDIR)/memory.o
LIB     = $(OBJDIR)/libspectra.a

.PHONY: all clean test

all: $(LIB)

$(OBJDIR):
	mkdir -p $(OBJDIR)

$(OBJDIR)/specton.o: $(SRCDIR)/specton.c $(SRCDIR)/specton.h | $(OBJDIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJDIR)/tensor.o: $(SRCDIR)/tensor.c $(SRCDIR)/tensor.h $(SRCDIR)/specton.h | $(OBJDIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJDIR)/memory.o: $(SRCDIR)/memory.c $(SRCDIR)/memory.h | $(OBJDIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(LIB): $(OBJS)
	ar rcs $@ $^
	@echo "Built: $@"

clean:
	rm -rf $(OBJDIR)

test: $(LIB)
	@echo "Running runtime tests..."
	$(CC) $(CFLAGS) tests/test_runtime.c $(LIB) -lm -o $(OBJDIR)/test_runtime
	./$(OBJDIR)/test_runtime
