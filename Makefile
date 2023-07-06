all: main.c
	./to64.sh 1 2 > chapterarray.h && m4 base64.c > final.c
	gcc final.c -o a -lpulse -lpulse-simple -lmad -g
