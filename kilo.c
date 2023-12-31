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
#define KILO_QUIT_TIMES 3
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

enum editorHighlight {
	HL_NORMAL 	= 0,
	HL_COMMENT 	,
	HL_MLCOMMENT 	,
	HL_KEYWORD1 	,
	HL_KEYWORD2 	,
	HL_STRING 	,
	HL_NUMBER 	,
	HL_MATCH 	,
};

#define HL_HIGHLIGHT_NUMBERS (1<<0)
#define HL_HIGHLIGHT_STRINGS (1<<1)
/*** data ***/
struct editorSyntax {
	char *filetype;
	char **filematch; 		//array of strings to match the filename against
	char **keywords;
	char *singleline_comment_start;	//contains the string that a singeline comment starts with
	char *multiline_comment_start;
	char *multiline_comment_end;
	int flags; 			//bit flags to determine whether we highlight numbers or strings for the filetype
};
//an Editor ROW, dynamically stores a line of text
typedef struct erow {
	int idx;  		//index within file
	int size; 		//size of char string
	int rsize; 		//size of render string
	char *chars; 		//contains the raw file contents
	char *render; 		//contains our render version that will be displayed
	int hl_open_comment;
	unsigned char *hl; //indicates whether a character, in RENDER, is part of a string, comment, number, &c.
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
	int dirty; 				//measure of how modified a file is. 0 = unadultered, >0 indictates # of changes
	char* filename; 			//the name of the file
	char statusmsg[80]; 			//the status message to display
	time_t statusmsg_time; 			//
	struct editorSyntax *syntax; 		//pointer to the current syntax
	struct termios original_termios; 	//the original state of the user's termio
} E;
/*** prototypes ***/
void editorSetStatusMessage(const char* fmt, ...);
void editorRefreshScreen();
char *editorPrompt(char *prompt, void (*callback)(char *,int));

/*** filetypes  ***/

char *C_HL_EXTENSIONS[] = { ".c", ".h", ".cpp", NULL };
char *C_HL_KEYWORDS[] = {"switch", "if", "while", "for", "break",
			"continue", "return", "else", "struct", "union",
			"typedef", "static", "enum", "class", "case",
			//terminating with a '|', to indicate that the are secondary keywords
			"int|", "long|", "double|", "float|", "char|", "unsigned|", "signed|", "void|",
NULL};
struct editorSyntax HLDB[] = {
	{
		"c",
		C_HL_EXTENSIONS,
		C_HL_KEYWORDS,
		"//", "/*", "*/",
		HL_HIGHLIGHT_NUMBERS | HL_HIGHLIGHT_STRINGS
	},
};
#define HLDB_ENTRIES (sizeof(HLDB) / sizeof(HLDB[0]))

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

/*** syntax highlighting ***/

int is_separator(int c) {
	return isspace(c)
		|| c == '\0'
		|| strchr(",.()+-/*=~%<>[];",c) != NULL;
}

void editorUpdateSyntax(erow *row) {
	row->hl = realloc(row->hl, row->rsize); 	//because the highlights refer to every character in render, they are the same length
	memset(row->hl, HL_NORMAL, row->rsize); 	//set all the values to be of NORMAL highlight

	if (E.syntax == NULL) return; 			//if no syntax highlighting is set, exit

	char **keywords = E.syntax->keywords; 		//the array of keywords
	
	char *scs = E.syntax->singleline_comment_start;	//the singleline comment start symbol
	char *mcs = E.syntax->multiline_comment_start; 	//the mlc start symbol
	char *mce = E.syntax->multiline_comment_end; 	//the mlc end symbol

	int scs_len = scs ? strlen(scs) : 0; 		//length of the single-line comment marker
	int mcs_len = mcs ? strlen(mcs) : 0; 		//len of multi-line comment starter
	int mce_len = mce ? strlen(mce) : 0; 		//len of mlc end

	int prev_sep = 1; 				//so that numbers at the beginning of the line are highlighted
	int in_string = 0; 				//keep track if we are in a string or not. stores the value of a double/single-quote depending on how the string was declared
	int in_comment = (row->idx > 0 && E.row[row->idx - 1].hl_open_comment); //keeps track if we are in a multi-line comment

	int i=0;
	while (i < row->rsize) { 			//while the index is less than the length of render
		char c = row->render[i]; 		//the character at index i 
		unsigned char prev_hl = (i>0) ? row->hl[i-1] : HL_NORMAL;

		if (scs_len && !in_string && !in_comment) {
			if (!strncmp(&row->render[i],scs,scs_len)) {
				memset(&row->hl[i],HL_COMMENT, row->rsize-i);
				break;
			}
		}

		if (mcs_len && mce_len && !in_string) { 		//if mcs_len & mce_len are not null, and we are not in a string
			if (in_comment) { 				//if we are already in the comment
				row->hl[i] = HL_MLCOMMENT;
				if (!strncmp(&row->render[i], mce, mce_len)) { 	//if the current index is the end of the multi-line comment
					memset(&row->hl[i], HL_MLCOMMENT, mce_len); 	//highlight as a comment
					i += mce_len; 				//increment index
					in_comment = 0; 			//exit the comment
					prev_sep = 1; 				//mark as a separator
					continue; 				//skip
				} else { 					//if the current index is just part of the comment
					i++; 					//increment index
					continue; 				//skip
				}
			}
			else if (!strncmp(&row->render[i], mcs, mcs_len)) { 	//if the current index is the beginning of the comment
				memset(&row->hl[i], HL_MLCOMMENT, mcs_len); 	//highlight the symbol
				i += mcs_len; 					//increment the index
				in_comment = 1; 				//flag that we are in a comment
				continue; 					//skip
			}
		}

		if (E.syntax->flags & HL_HIGHLIGHT_STRINGS) { 	//highlight strings enabled
			if (in_string) { 				//if we are in a string
				row->hl[i] = HL_STRING; 		//highlight as string
				if(c == '\\' && i + 1 < row->rsize) {
					row->hl[i+1] = HL_STRING;
					i += 2;
					continue;
				}
				if (c== in_string) in_string = 0; 	//if the current character is the end-quote, in_string = false
				i++; 					//increment
				prev_sep = 1; 				//consider the final quote a separator
				continue; 				//skip
			} else { 					//if we are NOT in a string
				if (c == '"' || c == '\'') { 		//see if the character is a quote
					in_string = c; 			//set the in_string to the quote's value
					row->hl[i] = HL_STRING; 	//highliht as string
					i++; 				//increment
					continue; 			//skip
				}
			}
		}
		if (E.syntax->flags & HL_HIGHLIGHT_NUMBERS) { 	//if number highlighting is enabled
			if ((isdigit(c) &&  (prev_sep || prev_hl == HL_NUMBER)) //if the character is a digit and (the previous char was a SEPARATOR or highlighted as a NUMBER)
				|| (c == '.' && prev_hl == HL_NUMBER)) { 	//if the character is a decimal, and previous value is highlighted as a number
				row->hl[i] = HL_NUMBER; 	//set the highlight to a number
				i++; 				//increase index
				prev_sep = 0; 			//set prev_sep flag to 0, since this was a number
				continue;
			}
		}

		if (prev_sep) { 							//keyword must be preceded by a separator
			int j; 								//iterator
			for (j = 0; keywords[j]; j++) { 				//FOR-EACH keyword
				int klen = strlen(keywords[j]); 			//the LENGTH of the current keyword
				int kw2 = keywords[j][klen-1] == '|'; 			//FLAG, if the keyword is a secondary keyword (ends with a pipe->|) 
				if (kw2) klen--; 					//don't include the pipe
				if (!strncmp(&row->render[i], keywords[j], klen) &&  	//IF there is a keyword at the current index
						is_separator(row->render[i + klen])) { 	//AND if there is a separator following the keyword
					memset(&row->hl[i], kw2 ? HL_KEYWORD2 : HL_KEYWORD1, klen); 	//highlight the row
					i += klen; 					//increment the index
					break; 						//break
				}
			}
			if (keywords[j] == NULL) { 			//if there are no keywords left
				prev_sep = 0; 				//set sep to 0
				continue; 				//skip
			}
		}
		prev_sep = is_separator(c); 		//if the character is a separator, set the prev_sep flag
		i++; 					//increment index
	}
	int changed = (row->hl_open_comment != in_comment); //if the comment highlight of a row is going to change
	row->hl_open_comment = in_comment; 		//set highlight to whatever the comment status is
	if (changed && row->idx + 1 < E.numrows) { 	//if the highlight did change, and we're not at the end of the file
		editorUpdateSyntax(&E.row[row->idx+1]); 	//update the next row's syntax
	}
}

int editorSyntaxToColor(int hl) {
	switch(hl) { 	//does not need to handle HL_NORMAL, this is handled elsewhere
		case HL_MLCOMMENT:
		case HL_COMMENT:	return 36; 	//cyan
		case HL_KEYWORD1:	return 33; 	//yellow
		case HL_KEYWORD2:	return 32; 	//green
		case HL_STRING:		return 35; 	//magenta
		case HL_NUMBER:		return 31; 	//foreground red
		case HL_MATCH: 		return 34; 	
		default: 		return 37; 	//foreground white
	}
}

void editorSelectSyntaxHighlight() {
	E.syntax = NULL;
	if (E.filename == NULL) return;

	char *ext = strchr(E.filename, '.');
	for (unsigned int j = 0; j < HLDB_ENTRIES; j++) { 		//loop through each highlight DB entry
		struct editorSyntax *s = &HLDB[j];
		unsigned int i = 0;
		while (s->filematch[i]) { 				//loop through each HLDB's filematch entries
			int is_ext = (s->filematch[i][0] == '.');
			if ((is_ext && ext && !strcmp(ext, s->filematch[i]))
					|| (!is_ext && strstr(E.filename, s->filematch[i]))) {
				E.syntax = s;
				int filerow;
				for(filerow=0; filerow<E.numrows;filerow++){
					editorUpdateSyntax(&E.row[filerow]);
				}
				return;
			}
			i++;
		}
	}
}

/*** row operations ***/
int editorRowCxtoRx(erow *row, int cx) { 	//converts a character index into a render index
	int rx = 0; 				//the row index
	int j; 					//iterator 
	for (j = 0; j < cx; j++) { 		//for each character in the character string
		if (row->chars[j] == '\t') 	//if we come across a tab (the only thing rendered differently by our editor)
			rx+=(KILO_TAB_STOP-1) - (rx % KILO_TAB_STOP); //add characters until we hit the TAB STOP
		rx++; 				//increment rx
	}
	return rx;
}

int editorRowRxtoCx(erow *row, int rx) {
	int cur_rx = 0; 			//our internal rx value
	int cx; 				//the character index we will return later
	for (cx = 0; cx < row->size; cx++) { 	//from character index 0 to the length of chars
		if (row->chars[cx] == '\t') 	//if character is a TAB
			cur_rx+=(KILO_TAB_STOP-1) - (cur_rx % KILO_TAB_STOP); //do the tab_stop math & increment cur_rx
		cur_rx++; 			//increment cur_rx
		if (cur_rx > rx) return cx; 	//if the current render index is greater than the rx argument, return CX
	}
	return cx; 				//return the character index, I believe this just the end of the row
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

	editorUpdateSyntax(row);
}

void editorInsertRow(int at, char *s, size_t len) {
	if (at < 0 || at > E.numrows) return;
	E.row = realloc(E.row, sizeof(erow) * (E.numrows + 1)); //reallocate the array
	memmove(&E.row[at+1], &E.row[at], sizeof(erow) * (E.numrows - at));
	for (int j = at + 1; j <= E.numrows; j++) E.row[j].idx++;

	E.row[at].idx = at; //index of the row
	
	E.row[at].size = len;
	E.row[at].chars = malloc(len + 1);
	memcpy(E.row[at].chars, s, len);
	E.row[at].chars[len] = '\0';
	
	E.row[at].rsize = 0;
	E.row[at].render = NULL;
	E.row[at].hl = NULL;
	E.row[at].hl_open_comment = 0;
	editorUpdateRow(&E.row[at]);

	E.numrows++;
	E.dirty++;
}

void editorFreeRow(erow *row) {
	free(row->render);
	free(row->chars);
	free(row->hl);
}

void editorDelRow(int at) {
	if (at < 0 || at >= E.numrows) return;
	editorFreeRow(&E.row[at]);
	memmove(&E.row[at], &E.row[at+1], sizeof(erow) * (E.numrows - at - 1));
	for (int j = at; j < E.numrows - 1; j++) E.row[j].idx--;
	E.numrows--;
	E.dirty++;

}

void editorRowInsertChar(erow *row, int at, int c) {
	if (at < 0 || at > row->size) at = row->size; 				//validate at
	row->chars = realloc(row->chars, row->size + 2); 			//make space for character to insert & null byte
	memmove(&row->chars[at+1], &row->chars[at], row->size - at + 1); 	//I believe this is moving the final character, a null byte, to the new end of string
	row->size++; 								//inrement row size
	row->chars[at] = c; 							//insert the new caracter
	editorUpdateRow(row); 							//update row
	E.dirty++;
}

void editorRowAppendString(erow *row, char *s, size_t len) {
	row->chars = realloc(row->chars, row->size + len + 1);
	memcpy(&row->chars[row->size], s, len);
	row->size += len;
	row->chars[row->size] = '\0';
	editorUpdateRow(row);
	E.dirty++;
}

void editorRowDelChar(erow *row, int at) {
	if (at < 0 || at >= row->size) return;
	memmove(&row->chars[at], &row->chars[at+1], row->size - at);
	row->size--;
	editorUpdateRow(row);
	E.dirty++;
}

/*** editor operations ***/
void editorInsertChar(int c) {
	if (E.cy == E.numrows) { 			//if the cursor is at the end of the file
		editorInsertRow(E.numrows, "",0); 			//append a blank row
	}
	editorRowInsertChar(&E.row[E.cy], E.cx, c); 	//insert the character into the row, and column, marked by the cursor
	E.cx++; 					//increment the column cursor after inserting the character
}

void editorInsertNewLine() {
	if (E.cx == 0) { 							//if we are at the beginning of the line
		editorInsertRow(E.cy, "",0); 					//just insert a row
	} else { 								//if we are within a line
		erow *row = &E.row[E.cy]; 					//get the rows address
		editorInsertRow(E.cy + 1, &row->chars[E.cx], row->size - E.cx); 	//split the current line and move the string to the right of the cursor to the new-line
		row = &E.row[E.cy]; 						//row pointer could have been reassigned in editorInsertRow
		row->size = E.cx; 						//update the row-size to the cursor position
		row->chars[row->size] = '\0'; 					//add a nullbyte
		editorUpdateRow(row);
	}
	E.cy++;
	E.cx = 0;
}

void editorDelChar() {
	if (E.cy == E.numrows) return; 			//if the cursor is at the end of the file, we cannot delete anything
	erow *row = &E.row[E.cy];
	if (E.cx > 0) {
		editorRowDelChar(row, E.cx - 1); 	//delete the character in the current row at the current column
		E.cx--; 					//decrement the column cursor after deleting the character
	} else {
		E.cx = E.row[E.cy - 1].size;
		editorRowAppendString(&E.row[E.cy-1], row->chars, row->size);
		editorDelRow(E.cy);
		E.cy--;
	}
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

	editorSelectSyntaxHighlight();

	FILE *fp = fopen(filename, "r");
	if (!fp) die("fopen");

	char* line = NULL;
	size_t linecap = 0;
	ssize_t linelen;
	while((linelen = getline(&line, &linecap, fp)) != -1) {
		while (linelen > 0 &&
				(line[linelen - 1] == '\n' || line[linelen - 1] == '\r'))
			linelen--;
		editorInsertRow(E.numrows,line,linelen);
	}
	free(line);
	fclose(fp);
	E.dirty = 0;
}

void editorSave() {
	if (E.filename == NULL) { 				//no file to save to
		E.filename = editorPrompt("Save as %s", NULL); 	//prompt for a file-name
		if (E.filename == NULL) {
			editorSetStatusMessage("Save aborted");
			return;
		}
		editorSelectSyntaxHighlight();
	}

	int len; 						//the length of the buffer
	char *buf = editorRowsToString(&len); 			//pointer to the buffer

	int fd = open(E.filename, O_RDWR | O_CREAT, 0644); 	//open for RW / create, a file with chmod 0644 
	if (fd != -1) {
		if(ftruncate(fd,len) != -1) { 			//sets the file-size to a specific length. Helps with data-loss prevention?
			if (write(fd,buf,len) == len) { 	//write the buffer of length to the file
				close(fd); 			//close the file
				free(buf); 			//free the buffer
				editorSetStatusMessage("%d bytes written to disk", len);
				E.dirty = 0;
				return;
			}
		}
		close(fd);
	}
	free(buf);
	editorSetStatusMessage("Failed to save. I/O Error: %s", strerror(errno));
}

/*** find ***/

//finds a string in the file
void editorFindCallback(char* query, int key) {
	static int last_match = -1;
	static int direction = 1;

	static int saved_hl_line;
	static char *saved_hl = NULL;
	if (saved_hl) {
		memcpy(E.row[saved_hl_line].hl, saved_hl, E.row[saved_hl_line].rsize);
		free(saved_hl);
		saved_hl = NULL;
	}

	if (key == '\r' || key == '\x1b') { 			//IF ESCAPE OR RETURN
		last_match = -1; 				//reset last_match on exiting search
		direction = 1; 					//reset direction on exiting search
		return;
	} else if (key == ARROW_RIGHT || key == ARROW_DOWN) {
		direction = 1;
	} else if (key == ARROW_LEFT || key == ARROW_UP) {
		direction = -1;
	} else {
		direction = 1;
		last_match = -1;
	}
	if (last_match == -1) direction = 1;
	int current = last_match; 				//index of the current row we are searching
	int i;
	for (i = 0; i < E.numrows; i++) { 			//FOR-EACH row in the editor
		current += direction; 				//decrement if we are searching backwards, increment if we are moving forwards
		if (current == -1) current = E.numrows - 1; 	//if the current match was moved to before the file, wrap around to the end of the file
		else if (current == E.numrows) current = 0; 	//IF the current match is at the end of the file, move to the beginning of the file

		erow *row = &E.row[current]; 			//set var to current row
		char *match = strstr(row->render, query); 	//returns a pointer to the first occurence of the QUERY in the row
		if (match) { 					//if the pointer is not NULL, meaning we have a match
			last_match = current; 			//update the last_match to be the current match
			E.cy = current; 			//set the cursor to the current row
			E.cx = editorRowRxtoCx(row,match - row->render); 		//set the cursor to the beginning of the match
			E.row_off = E.numrows;  		//set row_offset to the bottom of the file so that the editorScroll will bring us to the matching line(top of screen)

			saved_hl_line = current;
			saved_hl = malloc(row->size);
			memcpy(saved_hl, row->hl, row->rsize);
			memset(&row->hl[match - row->render], HL_MATCH, strlen(query)); //highlight the match
			break; 					//break to end the search
		}
	}
}
//editor find, calls callback
void editorFind() {
	//save context information
	int saved_cx = E.cx;
	int saved_cy = E.cy;
	int saved_coloff = E.col_off;
	int saved_rowoff = E.row_off;

	char *query = editorPrompt("Search %s (ESC to cancel/ARROWS to navigate/Enter to find)", editorFindCallback);
	if (query) free(query); 				//free the query, if it exists
	else {
		//restore context
		E.cx 		= saved_cx; 
		E.cy 		= saved_cy; 
		E.col_off 	= saved_coloff; 
		E.row_off 	= saved_rowoff; 
	}
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
	int len = snprintf(status, sizeof(status), "%.20s - %d lines %s",
			E.filename ? E.filename : "[No Name]",
			E.numrows,
			E.dirty ? "(modified)" : "");
	int rlen = snprintf(rstatus, sizeof(rstatus), "%s | %d/%d",
			E.syntax ? E.syntax->filetype : "no ft",E.cy + 1, E.numrows);
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
	for (y = 0; y < E.screenrows; y++) { 				//for-each row in the screen
		int filerow = y + E.row_off; 				//the current visible line of the file
		if (filerow >= E.numrows) { 				//if the file-row is greater than the number of rows in the editor
			if (E.numrows == 0 && y == E.screenrows / 3) { 	//WELCOME MESSAGE
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
		else { //NOT (filerow>=numRows) 				//ACTUAL CONTENT
			int len = E.row[filerow].rsize - E.col_off; 		//the length of the visible line
			if (len < 0) len = 0; 					//validate length
			if (len > E.screencols) len = E.screencols; 		//if the length is greater than the currently visible columns, truncate length
			char *c = &E.row[filerow].render[E.col_off]; 		//pointer to the first visible character in a row
			unsigned char *hl = &E.row[filerow].hl[E.col_off]; 	//the current highlight
			int current_color = -1;
			int j;
			for (j = 0; j < len; j++) { 					//for each character in the visible segment of the row
				if (iscntrl(c[j])) { 					//IF character IS control character
					char sym = (c[j] < 26) ? '@' + c[j] : '?'; 	//convert the control character to a symbol from A-Z (1-26) & @ (0), else '?'
					abAppend(ab, "\x1b[7m",4); 			//invert color
					abAppend(ab, &sym,1); 				//print the symbol
					abAppend(ab, "\x1b[m",3); 			//reset the color
					if (current_color != -1) { 			//if a color is set
						char buf[16];
						int clen = snprintf(buf, sizeof(buf),"\x1b[%dm", current_color);
						abAppend(ab, buf, clen);
					}

				}
				else if (hl[j] == HL_NORMAL) { 				//if of normal highlight
					if (current_color != -1) {
						abAppend(ab, "\x1b[39m",5); 		//set color to normal
						current_color = -1;
					}
					abAppend(ab, &c[j],1); 				//print character
				}
				else {
					int color = editorSyntaxToColor(hl[j]);		//get the color for the syntax
					if (color != current_color) {
						current_color = color;
						char buf[16];
						int clen = snprintf(buf, sizeof(buf), "\x1b[%dm",color);
						abAppend(ab, buf, clen);
					}
					abAppend(ab, &c[j], 1); 			//print character
				}
			}
			abAppend(ab, "\x1b[39m",5); //set color to normal
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
char *editorPrompt(char *prompt, void (*callback)(char *, int)) { 				//displays a prompt in the status bar
	size_t bufsize = 128;
	char *buf = malloc(bufsize); 				//stores user-input

	size_t buflen = 0; 					//no text, length is 0
	buf[0] = '\0'; 						//set the null-byte to the end-of-string

	while(1) {
		editorSetStatusMessage(prompt, buf); 		//update status message as user is typing
		editorRefreshScreen(); 				//redraw the screen

		int c = editorReadKey(); 			//read a single byte from the user
		if (c == DEL_KEY || c == CTRL_KEY('h') || c == BACKSPACE) { //IF a delete key
			if (buflen != 0) buf[--buflen] = '\0';
		}
		else if (c == '\x1b') { 			//ELSE-IF ESCAPE
			editorSetStatusMessage(""); 		//clear the status message
			if (callback) callback(buf,c);
			free(buf); 				//free the buffer
			return NULL; 				//RETURN NULL
		}
		else if (c == '\r') { 				//ELSE-IF ENTER
			if (buflen != 0) { 			//IF the user has typed SOMETHING
				editorSetStatusMessage(""); 	//clear the status message
				if (callback) callback(buf,c);
				return buf; 			//RETURN user-input
			}
		}
		else if (!iscntrl(c) && c < 128) { 		//ELSE-IF ASCII CHARACTER
			if (buflen == bufsize - 1) { 		//IF the user has used all available charaters
				bufsize *= 2; 			//inrease the buffer size
				buf = realloc(buf, bufsize); 	//reallocte the buffer with the new buffer size
			}
			buf[buflen++]  = c; 			//add the input to the string
			buf[buflen] = '\0'; 			//terminate string with null-byte
		}
		if (callback) callback(buf,c);
	}
}

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
	static int quit_times = KILO_QUIT_TIMES;
	int c = editorReadKey();

	switch(c) {
		case '\r':
			editorInsertNewLine();
			break;
		case CTRL_KEY('q'):
			if (E.dirty && quit_times > 0) {
				editorSetStatusMessage("WARNING! File has UNSAVED changes. "
						"Press Ctrl-Q %d more times to quit.",quit_times);
				quit_times--;
				return;
			}
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
		case CTRL_KEY('f'):
			editorFind();
			break;
		//
		case BACKSPACE:
		case CTRL_KEY('h'):
		case DEL_KEY:
			//delete key @ end-of-line should move the line under to it
			if (c== DEL_KEY) editorMoveCursor(ARROW_RIGHT);
			editorDelChar();
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
	quit_times = KILO_QUIT_TIMES; 	//resets the amount of quit_times when a user does anything but press ctrl-q
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
	E.dirty = 0; 	//the file is clean before we edit
	E.screenrows -= 1; //to make room for the status bar
	E.filename = NULL; //init filename
	E.statusmsg[0] = '\0';
	E.statusmsg_time = 0;
	if (getWindowSize(&E.screenrows, &E.screencols) == -1)
		die("getWindowSize");
	E.screenrows-=2;
	E.syntax = NULL;
}

int main(int argc, char* argv[]) {
	enableRawMode();
	initEditor();
	if (argc > 1) {
		editorOpen(argv[1]);
	}
	editorSetStatusMessage("HELP: Ctrl-S = SAVE | Ctrl-Q = QUIT | Ctrl-F = FIND");
	while (1) {
		editorRefreshScreen();
		editorProcessKeypress();
	}
	return 0;
}
