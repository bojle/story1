#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <mad.h>
#include <pulse/simple.h>
#include <pulse/error.h>

/* supposed to be pre-processed by m4 */
define(PUT_CHAPTERS_HERE, include(chapterarray.h))
define(PUT_SUBCHAPS_HERE, `include(subchaps.h)')
PUT_CHAPTERS_HERE

pa_simple *device = NULL;
int ret = 1;
int error;
struct mad_stream mad_stream;
struct mad_frame mad_frame;
struct mad_synth mad_synth;


/* aaaack but it's fast and const should make it shared text page. */
static const unsigned char pr2six[256] =
{
    /* ASCII table */
    64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
    64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
    64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 62, 64, 64, 64, 63,
    52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 64, 64, 64, 64, 64, 64,
    64,  0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14,
    15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 64, 64, 64, 64, 64,
    64, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40,
    41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 64, 64, 64, 64, 64,
    64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
    64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
    64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
    64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
    64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
    64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
    64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
    64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64
};

/* base 64 decode */
int b64d(char *bufplain, const char *bufcoded)
{
    int nbytesdecoded;
    register const unsigned char *bufin;
    register unsigned char *bufout;
    register int nprbytes;

    bufin = (const unsigned char *) bufcoded;
    while (pr2six[*(bufin++)] <= 63);
    nprbytes = (bufin - (const unsigned char *) bufcoded) - 1;
    nbytesdecoded = ((nprbytes + 3) / 4) * 3;

    bufout = (unsigned char *) bufplain;
    bufin = (const unsigned char *) bufcoded;

    while (nprbytes > 4) {
    *(bufout++) =
        (unsigned char) (pr2six[*bufin] << 2 | pr2six[bufin[1]] >> 4);
    *(bufout++) =
        (unsigned char) (pr2six[bufin[1]] << 4 | pr2six[bufin[2]] >> 2);
    *(bufout++) =
        (unsigned char) (pr2six[bufin[2]] << 6 | pr2six[bufin[3]]);
    bufin += 4;
    nprbytes -= 4;
    }

    /* Note: (nprbytes == 1) would be an error, so just ingore that case */
    if (nprbytes > 1) {
    *(bufout++) =
        (unsigned char) (pr2six[*bufin] << 2 | pr2six[bufin[1]] >> 4);
    }
    if (nprbytes > 2) {
    *(bufout++) =
        (unsigned char) (pr2six[bufin[1]] << 4 | pr2six[bufin[2]] >> 2);
    }
    if (nprbytes > 3) {
    *(bufout++) =
        (unsigned char) (pr2six[bufin[2]] << 6 | pr2six[bufin[3]]);
    }

    *(bufout++) = '\0';
    nbytesdecoded -= (4 - nprbytes) & 3;
    return nbytesdecoded;
}

/* return len of the original message after decode */
int predict_length(const char *b64_encoded_str, int len) {
	int padding_bytes = 0;
	for (int i = len-1; i >= 0; --i) {
		if (b64_encoded_str[i] == '=') {
			padding_bytes++;
		}
		else {
			break;
		}
	}
	int l = ((3 * (len  / 4)) - (padding_bytes)) + 1;
	return l;
}

void stuttered_print(const char *str) {
	while (*str != '\0') {
		putc(*str, stdout);
		fflush(stdout);
		usleep(10000);
		str++;
	}
}

void display_chapter(const char *chp) {
	puts("\033[2J");
	puts("\033[H");
	int bufsz = predict_length(chp, strlen(chp));
	char *chbuf = (char *) calloc(bufsz, sizeof(*chbuf));
	int ret = b64d(chbuf, chp);
	/*
	if (ret != bufsz) {
		printf("display_chapter: ret(%d) != bufsz (%d)", ret, bufsz);
		return;
	}
	*/
	stuttered_print(chbuf);
	free(chbuf);

	struct winsize w;
	/* get terminal sizes, store em in w */
    ioctl(STDOUT_FILENO, TIOCGWINSZ, &w);
	/* move to the last line on the current terminal */
	printf("\033[%d;0H", w.ws_row);
	printf("Press Enter to Continue ");
	return;
}

/* set the terminal to non-canonical mode so unbuffered inputs can be processed */
void set_noncanon() {
	struct termios settings;
	int ret = tcgetattr(STDOUT_FILENO, &settings);
	if (ret != 0)  {
		printf("tcgetattr: could not enter non canon mode\n");
		exit(1);
	}
	//settings.c_lflag &= ~(ICANON);
	cfmakeraw(&settings);
}

void process_event() {
	int key_read;
	int ret = fgetc(stdin);
	if (key_read == '\n') {
		printf("q\n");
		return;
	}
}

void play_tone(int i) {
	// TODO: this
}

/* initializations */
void lights() {
	/* term init */
	set_noncanon();

	/* pulseaudio init */
    static const pa_sample_spec ss = { .format = PA_SAMPLE_S16LE, .rate = 44100, .channels = 2 };
    if (!(device = pa_simple_new(NULL, "MP3 player", PA_STREAM_PLAYBACK, NULL, 
					"playback", &ss, NULL, NULL, &error))) {
        printf("pa_simple_new() failed\n");
		exit(1);
    }
	/* mpeg lib init */
    mad_stream_init(&mad_stream);
    mad_synth_init(&mad_synth);
    mad_frame_init(&mad_frame);
}

void camera() {

}

/* meta programming shitz */
#define subchap(id) (chapter ## id)
#define TOTAL_CHAPTERS 2

void action() {
	char *list_of_chapters[] = {
		PUT_SUBCHAPS_HERE
	};
	for (int i = 0; i < TOTAL_CHAPTERS; ++i) {
		play_tone(i);
		display_chapter(list_of_chapters[i]);
		process_event();
	}
}



int main() {
	lights();
	camera();
	action();
}
