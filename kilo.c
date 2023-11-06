/*** includes ***/
#define _DEFAULT_SOURCE
#define _GNU_SOURCE
#define _BSD_SOURCE

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>
/*** defines ***/
#define KILO_VERSION "0.0.1"
#define KILO_TAB_STOP 8
#define CTRL_KEY(k) ((k) & 0x1f)
enum editorKey {
	BACKSPACE 	= 127,
	ARROW_LEFT 	= 1000,
	ARROW_RIGHT 	,
	ARROW_UP 	,
	ARROW_DOWN 	,
	//
	DEL_KEY 	,
	//
	HOME_KEY 	,
	END_KEY 	,
	//
	PAGE_UP 	,
	PAGE_DOWN 	,
};
/*** data ***/
//an Editor ROW, dynamically stores a line of text
typedef struct erow {
	int size;
	int rsize;
	char *chars;
	char *render;
} erow;
struct editorConfig { 				//global struct that will contain our editor state
	int cx,cy; 				//cursor x & y positions, 0,0 == top-left
	int rx; 				//cursor x position in render
	int row_off; 				//row offset
	int col_off; 				//column offset
	int screenrows; 			//count of rows on screen
	int screencols; 			//count of columns on screen
	int numrows; 				//the number of rows
	erow *row; 				//array of rows
	char* filename; 			//the name of the file
	char statusmsg[80];
	time_t statusmsg_time;
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
	while ((nread = read(STDIN_FILENO, &c, 1)) != 1){
		if (nread == -1 && errno != EAGAIN) die("read");//if read == -1 it indicates a failure, on some systems it will return -1 & flag EAGAIN on timeout
	}
	if (c == '\x1b') {
		//if I create a char array for the sequence of escapes, we end up with w,a,s,d repeating their inputs infinitely until another character is pressed
		//I search arouned & I am honestly unsure why this problem occurs, but simply adding char a[3] will cause this 'hanging' or 'reptition' where the cursor
		//slowly drifts across the screen
		char seq[3];
		//get the next values in the sequence
		if(read(STDIN_FILENO, &seq[0], 1) != 1) return c;
		if(read(STDIN_FILENO, &seq[1], 1) != 1) return c;
		//handle escape sequnce
		if (seq[0] == '[') {
			if (seq[1] >= '0' && seq[1] <= '9') {
				if (read(STDIN_FILENO,&seq[2], 1) != 1) return c;
				if (seq[2] == '~') {
					switch (seq[1]) {
						case '1': return HOME_KEY;
						case '3': return DEL_KEY;
						case '4': return END_KEY;
						case '5': return PAGE_UP;
						case '6': return PAGE_DOWN;
						case '7': return HOME_KEY;
						case '8': return END_KEY;
					}
				
				}
			}
			else {
				switch (seq[1]) {
					case 'A': return ARROW_UP;
					case 'B': return ARROW_DOWN;
					case 'C': return ARROW_RIGHT;
					case 'D': return ARROW_LEFT;
					case 'H': return HOME_KEY;
					case 'F': return END_KEY;
				}
			}
		}
		else if (seq[0] == 'O') {
			switch (seq[1]) {
				case 'H': return HOME_KEY;
				case 'F': return END_KEY;
			}
		}
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
/*** row operations ***/
int editorRowCxtoRx(erow *row, int cx) {
	int rx = 0;
	int j;
	for (j = 0; j < cx; j++) {
		if (row->chars[j] == '\t')
			rx+=(KILO_TAB_STOP-1) - (rx % KILO_TAB_STOP);
		rx++;
	}
	return rx;
}

void editorUpdateRow(erow *row) {
	int tabs = 0; 					//total tabs found in row
	int j; 						//iteration variable
	for (j = 0; j < row->size; j++) 		//for-each character in the row
		if (row->chars[j] == '\t') tabs++; 	//if character = tab, tab++
	free(row->render); 				//free the render string
	row->render = malloc(row->size + tabs*(KILO_TAB_STOP-1) + 1); 	//malloc the render string with extra-space allocated for spaces, TAB_STOP-1 because \t' =1

	int idx = 0; 					//contains the number of letters copied into row->render
	for (j = 0; j < row->size; j++) {
		if (row->chars[j] == '\t') {
			row->render[idx++] = ' ';
			while (idx % 8 	!= 0)
				row->render[idx++] = ' ';
		}
		else {
			row->render[idx++] = row->chars[j];
		}
	}
	row->render[idx] = '\0';
	row->rsize = idx;
}

void editorAppendRow(char *s, size_t len) {
	E.row = realloc(E.row, sizeof(erow) * (E.numrows + 1)); //reallocate the array
	int at = E.numrows;
	E.row[at].size = len;
	E.row[at].chars = malloc(len + 1);
	memcpy(E.row[at].chars, s, len);
	E.row[at].chars[len] = '\0';
	
	E.row[at].rsize = 0;
	E.row[at].render = NULL;
	editorUpdateRow(&E.row[at]);
	E.numrows++;
}

void editorRowInsertChar(erow *row, int at, int c) {
	if (at < 0 || at > row->size) at = row->size; 				//validate at
	row->chars = realloc(row->chars, row->size + 2); 			//make space for character to insert & null byte
	memmove(&row->chars[at+1], &row->chars[at], row->size - at + 1); 	//I believe this is moving the final character, a null byte, to the new end of string
	row->size++; 								//inrement row size
	row->chars[at] = c; 							//insert the new caracter
	editorUpdateRow(row); 							//update row
}
/*** editor operations ***/
void editorInsertChar(int c) {
	if (E.cy == E.numrows) { 			//if the cursor is at the end of the file
		editorAppendRow("",0); 			//append a blank row
	}
	editorRowInsertChar(&E.row[E.cy], E.cx, c); 	//insert the character into the row, and column, marked by the cursor
	E.cx++; 					//increment the column cursor after inserting the character
}

/*** file I/O ***/
char *editorRowsToString(int *buflen) {
	int totlen = 0; 					//sum of the length of all rows + a new-line character for each
	int j; 							//iterator
	for (j = 0; j < E.numrows; j++) 			//for-each row
		totlen += E.row[j].size + 1; 			//add a row & its new-line to the total
	*buflen = totlen; 					//set the buffer length

	char *buf = malloc(totlen); 				//allocate a buffer to hold all the rows
	char *p = buf; 						//pointer to end of buffer
	for (j=0; j< E.numrows; j++) { 				//for-each row in rows
		memcpy(p, E.row[j].chars, E.row[j].size); 	//copy the current row into the end of the buffer
		p += E.row[j].size; 				//move the pointer to the end of the buffer
		*p = '\n'; 					//add a new-line character
		p++; 						//move pointer to end of buffer again
	}
	return buf; 						//expect the caller to free memory
}

void editorOpen(char* filename) {
	free(E.filename);
	E.filename = strdup(filename);
	FILE *fp = fopen(filename, "r");
	if (!fp) die("fopen");

	char* line = NULL;
	size_t linecap = 0;
	ssize_t linelen;
	while((linelen = getline(&line, &linecap, fp)) != -1) {
		while (linelen > 0 &&
				(line[linelen - 1] == '\n' || line[linelen - 1] == '\r'))
			linelen--;
		editorAppendRow(line,linelen);
	}
	free(line);
	fclose(fp);
}

void editorSave() {
	if (E.filename == NULL) return; //no file to save to

	int len; 						//the length of the buffer
	char *buf = editorRowsToString(&len); 			//pointer to the buffer

	int fd = open(E.filename, O_RDWR | O_CREAT, 0644); 	//open for RW / create, a file with chmod 0644 
	if (fd != -1) {
		if(ftruncate(fd,len) != -1) { 			//sets the file-size to a specific length. Helps with data-loss prevention?
			if (write(fd,buf,len) == len) { 	//write the buffer of length to the file
				close(fd); 			//close the file
				free(buf); 			//free the buffer
				return;
			}
		}
		close(fd);
	}
	free(buf);
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
void editorScroll() {
	E.rx = 0;
	if (E.cy < E.numrows)
		E.rx = editorRowCxtoRx(&E.row[E.cy], E.cx);

	if (E.cy < E.row_off) {
		E.row_off = E.cy;
	}
	if (E.cy >= E.row_off + E.screenrows) {
		E.row_off = E.cy - E.screenrows + 1;
	}
	if (E.rx < E.col_off) {
		E.col_off = E.rx;
	}
	if (E.rx > E.col_off + E.screencols) {
		E.col_off = E.rx - E.screencols + 1;
	}
}

void editorDrawStatusBar(struct abuf *ab) {
	abAppend(ab,"\x1b[7m", 4);
	
	char status[80], rstatus[80];
	int len = snprintf(status, sizeof(status), "%.20s - %d lines", E.filename ? E.filename : "[No Name]", E.numrows);
	int rlen = snprintf(rstatus, sizeof(rstatus), "%d/%d", E.cy + 1, E.numrows);
	if (len > E.screencols) len = E.screencols;
	abAppend(ab, status, len);
	while (len < E.screencols) {
		if (E.screencols - len == rlen) {
			abAppend(ab, rstatus, rlen);
			break;
		} else {
			abAppend(ab, " ", 1);
			len++;
		}
	}
	abAppend(ab, "\x1b[m",3);
	abAppend(ab, "\r\n",2);
}

void editorDrawMessageBar(struct abuf *ab) {
	abAppend(ab, "\x1b[K", 3);
	int msglen = strlen(E.statusmsg);
	if (msglen > E.screencols) msglen = E.screencols;
	if (msglen && time(NULL) - E.statusmsg_time < 5)
		abAppend(ab, E.statusmsg, msglen);
}

void editorDrawRows(struct abuf *ab) {
	int y;
	for (y = 0; y < E.screenrows; y++) {
		int filerow = y + E.row_off;
		if (filerow >= E.numrows) {
			if (E.numrows == 0 && y == E.screenrows / 3) {
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
			else {
				abAppend(ab, "~",1);
			}
		}
		else { //NOT (filerow>=numRows)
			int len = E.row[filerow].rsize - E.col_off;
			if (len < 0) len = 0;
			if (len > E.screencols) len = E.screencols;
			abAppend(ab, &E.row[filerow].render[E.col_off], len);
		}
		abAppend(ab, "\x1b[K",3); 	// K = Erase in line
		abAppend(ab, "\r\n", 2);		
	}
}
//refresh the screen
void editorRefreshScreen() {
	editorScroll();
	struct abuf ab = ABUF_INIT;
	abAppend(&ab,"\x1b[?25l",6);
		//l 	| reset mode
		//?25 	| not in the usually associated vt100 documents; however, it should hide the cursor
	abAppend(&ab, "\x1b[H", 3); 	//move the cursor to the 1st row & 1st column
		//H - "Cursor Position" (https://vt100.net/docs/vt100-ug/chapter3.html#CUP)

	editorDrawRows(&ab);
	editorDrawStatusBar(&ab);
	editorDrawMessageBar(&ab);

	char buf[32];
	snprintf(buf, sizeof(buf), "\x1b[%d;%dH", (E.cy - E.row_off) + 1, (E.rx - E.col_off) + 1);
	abAppend(&ab, buf, strlen(buf));

	abAppend(&ab,"\x1b[?25h",6);
		//h 	| set mode
		//?25l, see above, this should show the cursor?
	
	write(STDOUT_FILENO, ab.b, ab.len);
	abFree(&ab);
}

void editorSetStatusMessage(const char *fmt, ...) {
	va_list ap;
	va_start(ap,fmt);
	vsnprintf(E.statusmsg, sizeof(E.statusmsg), fmt, ap);
	va_end(ap);
	E.statusmsg_time = time(NULL);
}

/*** input ***/
void editorMoveCursor(int key) {
	erow *row = (E.cy >= E.numrows) ? NULL : &E.row[E.cy];

	switch(key) {
		case ARROW_LEFT:
			if(E.cx != 0)
				E.cx--;
			else if (E.cy > 0){
				E.cy--;
				E.cx = E.row[E.cy].size; 	//move left at start of line
			}
			break;
		case ARROW_RIGHT:
			if (row && E.cx < row->size)
				E.cx++;
			else if (row && E.cx == row->size) { 	//snap right at end of line
				E.cy++;
				E.cx = 0;
			}
			break;
		case ARROW_UP:
			if (E.cy != 0)
				E.cy--;
			break;
		case ARROW_DOWN:
			if (E.cy < E.numrows)
				E.cy++;
			break;
	}

	row = (E.cy >= E.numrows) ? NULL : &E.row[E.cy]; //we set this variable again because it could have changed during execution
	int rowlen = row ? row->size : 0;
	if (E.cx > rowlen) {
		E.cx = rowlen;
	}
}
void editorProcessKeypress() {
	int c = editorReadKey();

	switch(c) {
		case '\r':
			/*TODO*/
			break;
		case CTRL_KEY('q'):
			write(STDOUT_FILENO, "\x1b[2J",4); 	//clear the entire screen
			write(STDOUT_FILENO, "\x1b[H", 3); 	//move the cursor to the 1st row & 1st column
			exit(0);
			break;

		case CTRL_KEY('s'):
			editorSave();
			break;
		//cursor movement
		case HOME_KEY:
			E.cx = 0;
			break;
		case END_KEY:
			if (E.cy < E.numrows)
				E.cx = E.row[E.cy].size;
			break;
		//
		case BACKSPACE:
		case CTRL_KEY('h'):
		case DEL_KEY:
			/*TODO*/
			break;
		//
		case PAGE_UP:
		case PAGE_DOWN:
			{
				if (c == PAGE_UP) {
					E.cy = E.row_off;
				} else if (c == PAGE_DOWN) {
					E.cy = E.row_off + E.screenrows - 1;
					if (E.cy > E.numrows) E.cy = E.numrows;
				}
				int times = E.screenrows;
				while (times--)
					editorMoveCursor(c == PAGE_UP ? ARROW_UP : ARROW_DOWN);
			}
			break;
		//
		case ARROW_UP:
		case ARROW_DOWN:
		case ARROW_LEFT:
		case ARROW_RIGHT:
			editorMoveCursor(c);
			break;
		//
		case CTRL_KEY('l'):
		case '\x1b':
			break;
		//
		default:
			editorInsertChar(c);
			break;
	}
}

/*** init ***/
void initEditor() {
	E.cx = 0; 	//init cursor x position
	E.cy = 0; 	//init cursor y position
	E.rx = 0; 	//init render marker
	E.row_off = 0; 	//init row offset
	E.col_off = 0; 	//init column offset
	E.numrows = 0; 	//init number of rows
	E.row = NULL; 	//init the array of rows. 
	E.screenrows -= 1; //to make room for the status bar
	E.filename = NULL; //init filename
	E.statusmsg[0] = '\0';
	E.statusmsg_time = 0;
	if (getWindowSize(&E.screenrows, &E.screencols) == -1)
		die("getWindowSize");
	E.screenrows-=2;
}

int main(int argc, char* argv[]) {
	enableRawMode();
	initEditor();
	if (argc > 1) {
		editorOpen(argv[1]);
	}
	editorSetStatusMessage("HELP: Ctrl-Q = QUIT");
	while (1) {
		editorRefreshScreen();
		editorProcessKeypress();
	}
	return 0;
}
