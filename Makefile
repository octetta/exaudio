LIB += -lpthread -lm 

ifeq ($(shell uname -s), Darwin)
LIB += -framework AudioUnit -framework CoreAudio -framework CoreFoundation
endif

all: test1 simple_playback_sine two three four five six seven eight nine \
  ten eleven tweleve thirteen fourteen fifteen sixteen \
  a.out crex1 cz101-1 cz101-2 cz101-3 cz101-4 last-mods \
  mods

test1: test1.c
	cc -g $(INC) test1.c -o test1 $(LIB)

simple_playback_sine: simple_playback_sine.c
	cc -g $(INC) simple_playback_sine.c -o simple_playback_sine $(LIB)

two: two.c
	cc -g $(INC) two.c -o two $(LIB)

three: three.c
	cc -g $(INC) three.c -o three $(LIB)

four: four.c
	cc -g $(INC) four.c -o four $(LIB)

five: five.c
	cc -g $(INC) five.c -o five $(LIB)

six: six.c
	cc -g $(INC) six.c -o six $(LIB)

seven: seven.c
	cc -g $(INC) seven.c -o seven $(LIB)

eight: eight.c
	cc -g $(INC) eight.c -o eight $(LIB)

nine: nine.c
	cc -g $(INC) nine.c -o nine $(LIB)

ten: ten.c
	cc -g $(INC) ten.c -o ten $(LIB)

eleven: eleven.c
	cc -g $(INC) eleven.c -o eleven $(LIB)

tweleve: tweleve.c
	cc -g $(INC) tweleve.c -o tweleve $(LIB)

thirteen: thirteen.c
	cc -g $(INC) thirteen.c -o thirteen $(LIB)

fourteen: fourteen.c
	cc -g $(INC) fourteen.c -o fourteen $(LIB)

fifteen: fifteen.c
	cc -g $(INC) fifteen.c -o fifteen $(LIB)

sixteen: sixteen.c
	cc -g $(INC) sixteen.c -o sixteen $(LIB)

clean:
	rm -f test1
	rm -f simple_playback_sine
	rm -f two three four five six seven eight nine \
    ten eleven tweleve thirteen fourteen fifteen \
	sixteen a.out crex1 cz101-1 cz101-2 cz101-3 cz101-4 \
	last-mods mods sixteen sid1 sid2 sid3 termaudio
	rm -f test1.o
	rm -rf *.dSYM

