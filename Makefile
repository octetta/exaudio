LIB += -lpthread -lm 

ifeq ($(shell uname -s), Darwin)
LIB += -framework AudioUnit -framework CoreAudio -framework CoreFoundation
endif

all: test1 crex1

test1: test1.c
	cc -g $(INC) test1.c -o test1 $(LIB)

crex1: crex1.c
	cc -g $(INC) crex1.c -o crex1 $(LIB)

clean:
	rm -f test1
	rm -f crex1
	rm -rf *.dSYM

