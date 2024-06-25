LIB += -lpthread -lm 

ifeq ($(shell uname -s), Darwin)
LIB += -framework AudioUnit -framework CoreAudio -framework CoreFoundation
endif

all: test1 simple_playback_sine two

test1: test1.c
	cc -g $(INC) test1.c -o test1 $(LIB)

simple_playback_sine: simple_playback_sine.c
	cc -g $(INC) simple_playback_sine.c -o simple_playback_sine $(LIB)

two: two.c
	cc -g $(INC) two.c -o two $(LIB)

clean:
	rm -f test1
	rm -f simple_playback_sine
	rm -f two
	rm -f test1.o
	rm -rf *.dSYM

