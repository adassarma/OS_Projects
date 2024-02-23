NAME = wsh

CC = gcc
CFLAGS = -Wall -Werror -pedantic -std=gnu18

.PHONY: all
all: $(NAME)

wsh: $(NAME).c $(NAME).h
	$(CC) $(CFLAGS) -o $@ $<

.PHONY: clean
clean:
	rm -r $(NAME)
