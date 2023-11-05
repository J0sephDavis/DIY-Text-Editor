/*** includes ***/
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>
/*** defines ***/
#define KILO_VERSION "0.0.1"
#define CTRL_KEY(k) ((k) & 0x1f)
enum editorKey {
	ARROW_LEFT 	= 1000,
	ARROW_RIGHT 	,
	ARROW_DOWN 	,
	ARROW_UP 	,
};
/*** data ***/
struct editorConfig { 				//global struct that will contain our editor state
	int cx,cy; 				//cursor x & y positions, 0,0 == top-left
	int screenrows; 			//count of rows on screen
	int screencols; 			//count of columns on screen
	struct termios original_termios; 	//the original state of the user's termio
} E;

/*** terminal  ***/
//prints error message & exits program
void die(const char *s) {
	write(STDOUT_FILENO, "\x1b[2J",4); 	//clear the entire screen
	write(STDOUT_FILENO, "\x1b[H", 3); 	//move the cursor to the 1st row & 1st column

	perror(s); 	//prints out the string 's' & then outputs the global err no + description
	exit(1); 	//exit != 0 indicates failure
}

//disable raw mode & restores the termio
void disableRawMode() {

	if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &E.original_termios) == -1) //sets the users termio back to how it was
		die("tcsetattr");
}

//enables raw mode
void enableRawMode() {
	if (tcgetattr(STDIN_FILENO, &E.original_termios)) 	//get the origin termios & save it
		die("tgetattr");
	atexit(disableRawMode); 				//ensures that disableRawMode() is called on application exit()

	struct termios raw = E.original_termios; 			//struct that holds the termio setup for raw input

	raw.c_lflag &= ~(ECHO | ICANON | ISIG | IEXTEN); 	//disable the following features
		//ECHO - user-input echoing back to terminal
		//ICANON - canonical mode, all user-input is only given to the program on pressing ENTER
		//ISIG - signal interrupts such as CTRL-Z/C
		//IEXTEN - CTRL-V, which is used to print the actual values of things such as CTRL-Q (3)

	raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);	//disable the following features
		//BRKINT 	- break-conditions send CTRL-C to program
		//ICRNL 	- terminal changes all carriage-returns to new lines (CTRL-M = '/r', but with it enabled it becomes ENTER = '\n')
		//INPCK 	- parity checking
		//ISTRIP 	- sets the 8th bit of each byte to 0
		//IXON 		- software flow control, CTRL-S/Q

	raw.c_oflag &= ~(OPOST); 				//disable the following features
		//OPOST - output processing

	raw.c_cflag |= ~(CS8); 					//disable the following features
		//CS8 		- a bit mask that sets the systems character size to 8bits

	raw.c_cc[VMIN] = 0; 					//minimum number of bytes needed before read()
	raw.c_cc[VTIME] = 1; 					//maximum amount of time to wait before read() returns, in 10ths of a second. read() returns 0 after timeout
	
	if(tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw)) 		//sets the state of the FD to the new struct we've created
		die("tsetattr");
		//TCSAFLUSH - only applies changes after all pending output is written, discards unread input
}

int editorReadKey() {
	int nread;
	char c;
	while ((nread = read(STDIN_FILENO, &c, 1)) == -1){ //if read == -1 it indicates a failure, on some systems it will return -1 & flag EAGAIN on timeout
		if (nread == -1 && errno != EAGAIN)
			die("read");
	}
	if (c == '\x1b') {
		//if I create a char array for the sequence of escapes, we end up with w,a,s,d repeating their inputs infinitely until another character is pressed
		//I search arouned & I am honestly unsure why this problem occurs, but simply adding char a[3] will cause this 'hanging' or 'reptition' where the cursor
		//slowly drifts across the screen
		char seq0;
		char seq1;
		//
		if(read(STDIN_FILENO, &seq0, 1) != 1) return '\x1b';
		if(read(STDIN_FILENO, &seq1, 1) != 1) return '\x1b';
		if (seq0 == '[') {
			switch (seq1) {
				case 'A': return ARROW_UP;
				case 'B': return ARROW_DOWN;
				case 'C': return ARROW_RIGHT;
				case 'D': return ARROW_LEFT;
			}
		}
		//if the escape sequence wasn't able to be hanlded, it just returns escape (\x1b) at the end
	}
	return c;
}

int getCursorPosition(int *rows, int *cols) {
	char buf[32];
	unsigned int i = 0;
	//
	if (write(STDOUT_FILENO, "\x1b[6n",4) != 4) return -1;
	//
	while (i < sizeof(buf) - 1) {
		if (read(STDIN_FILENO, &buf[i], 1) != 1) break;
		if (buf[i] == 'R') break; 	//after calling n(6), we expect to receive a Cursor Position Report,ends with R (https://vt100.net/docs/vt100-ug/chapter3.html#CPR)
		i++;
	}
	buf[i] = '\0'; 	//terminate string
	if (buf[0] != '\x1b' || buf[1] != '[') return -1; //if the buffer doesn't contain the CPR, return a failure
	if (sscanf(&buf[2], "%d;%d", rows,cols) != 2) return -1; // if scanf doesnt read the two positions of the CPR, return a failure
	return 0; 	//return success
}

int getWindowSize(int *rows, int *cols) {
	struct winsize ws;
	if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) { //if ioctl fails, or it fails without letting us know
		//fallback for getting window size
		if (write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) != 12)
			return -1; 			//return a failure
		return getCursorPosition(rows,cols); 	//returns the fallback attempt of getting rows & cols
	}
	else {
		*cols = ws.ws_col; 	//set the columns to the pointed value
		*rows = ws.ws_row; 	//set the rows to the pointed value
		return 0; 	//return success
	}
	//
}
/*** append buffer ***/
/* To reduce the amount of write() calls we make. Thus, reducing flicker & unexpected behavior */
struct abuf { 		//the append buffer
	char *b; 	//pointer to buffer
	int len; 	//length of buffer
};
#define ABUF_INIT {NULL, 0}

//append string s to buffer
void abAppend(struct abuf *ab, const char *s, int len) {
	char *new = realloc(ab->b, ab->len + len);

	if (new == NULL) return;
	memcpy(&new[ab->len],s, len);
	ab->b  = new;
	ab->len += len;
}

void abFree(struct abuf *ab) {
	free(ab->b);
}

/*** output ***/
//draw a tilde at the beginning of each line
void editorDrawRows(struct abuf *ab) {
	int y;
	for (y = 0; y < E.screenrows; y++) {
		if (y == E.screenrows / 3) {
			char welcome[80];
			int welcomelen = snprintf(welcome, sizeof(welcome),
					"Kilo editor -- version %s", KILO_VERSION);
			if (welcomelen > E.screencols) welcomelen = E.screencols;
			int padding = (E.screencols - welcomelen) / 2;
			if (padding) {
				abAppend(ab, "~", 1);
				padding--;
			}
			while (padding--)
				abAppend(ab, " ", 1);
			abAppend(ab, welcome, welcomelen);
		}
		else
			abAppend(ab, "~",1);

		abAppend(ab, "\x1b[K",3); 	// K = Erase in line
		if (y < E.screenrows - 1)
			abAppend(ab, "\r\n", 2);
	}
}
//refresh the screen
void editorRefreshScreen() {
	struct abuf ab = ABUF_INIT;
	abAppend(&ab,"\x1b[?25l",6);
		//l 	| reset mode
		//?25 	| not in the usually associated vt100 documents; however, it should hide the cursor
	abAppend(&ab, "\x1b[H", 3); 	//move the cursor to the 1st row & 1st column
		//H - "Cursor Position" (https://vt100.net/docs/vt100-ug/chapter3.html#CUP)
	editorDrawRows(&ab);

	char buf[32];
	snprintf(buf, sizeof(buf), "\x1b[%d;%dH", E.cy + 1, E.cx + 1);
	abAppend(&ab, buf, strlen(buf));

	abAppend(&ab,"\x1b[?25h",6);
		//h 	| set mode
		//?25l, see above, this should show the cursor?
	
	write(STDOUT_FILENO, ab.b, ab.len);
	abFree(&ab);
}

/*** input ***/
void editorMoveCursor(int key) {
	switch(key) {
		case ARROW_LEFT:
			if(E.cx != 0)
				E.cx--;
			break;
		case ARROW_RIGHT:
			if (E.cx != E.screencols - 1)
				E.cx++;
			break;
		case ARROW_UP:
			if (E.cy != 0)
				E.cy--;
			break;
		case ARROW_DOWN:
			if (E.cy != E.screenrows - 1)
				E.cy++;
			break;
	}
}
void editorProcessKeypress() {
	int c = editorReadKey();

	switch(c) {
		case CTRL_KEY('q'):
			write(STDOUT_FILENO, "\x1b[2J",4); 	//clear the entire screen
			write(STDOUT_FILENO, "\x1b[H", 3); 	//move the cursor to the 1st row & 1st column
			exit(0);
			break;
		//cursor movement
		case ARROW_UP:
		case ARROW_DOWN:
		case ARROW_LEFT:
		case ARROW_RIGHT:
			editorMoveCursor(c);
			break;
	}
}

/*** init ***/
void initEditor() {
	E.cx = 0; 	//init cursor x position
	E.cy = 0; 	//init cursor y position
	if (getWindowSize(&E.screenrows, &E.screencols) == -1)
		die("getWindowSize");
}

int main() {
	enableRawMode();
	initEditor();
	while (1) {
		editorRefreshScreen();
		editorProcessKeypress();
	}
	return 0;
}
