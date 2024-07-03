LIB += -lpthread -lm 

ifeq ($(shell uname -s), Darwin)
LIB += -framework AudioUnit -framework CoreAudio -framework CoreFoundation
endif

all: test1 exaudio

test1: test1.c
	cc -g $(INC) test1.c -o test1 $(LIB)

exaudio: exaudio.c
	cc -g $(INC) exaudio.c -o exaudio $(LIB)

clean:
	rm -f test1
	rm -f exaudio
	rm -rf *.dSYM

