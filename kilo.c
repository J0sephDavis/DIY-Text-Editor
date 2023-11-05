/*** includes ***/
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <termios.h>
#include <unistd.h>
/*** defines ***/
#define CTRL_KEY(k) ((k) & 0x1f)

/*** data ***/
struct termios original_termios; //the original state of the user's termio

/*** terminal  ***/
//prints error message & exits program
void die(const char *s) {
	perror(s); 	//prints out the string 's' & then outputs the global err no + description
	exit(1); 	//exit != 0 indicates failure
}

//disable raw mode & restores the termio
void disableRawMode() {
	if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &original_termios) == -1) //sets the users termio back to how it was
		die("tcsetattr");
}

//enables raw mode
void enableRawMode() {
	if (tcgetattr(STDIN_FILENO, &original_termios)) 	//get the origin termios & save it
		die("tgetattr");
	atexit(disableRawMode); 				//ensures that disableRawMode() is called on application exit()

	struct termios raw = original_termios; 			//struct that holds the termio setup for raw input

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

char editorReadKey() {
	int nread;
	char c;
	while ((nread = read(STDIN_FILENO, &c, 1)) == -1){ //if read == -1 it indicates a failure, on some systems it will return -1 & flag EAGAIN on timeout
		if (nread == -1 && errno != EAGAIN)
			die("read");
	}	
	return c;
}
/*** output ***/
void editorRefreshScreen() {
	//4-> 4 bytes written to terminal
	// 	[0] - \x1b	| the escape sequence for the terminal
	// 	[1] - [  	| part of the esc sequence
	// 	[2] - 2 	| argument of J, indicates we clear the entire screen
	// 	[3] - J 	| "Erase In Display" (https://vt100.net/docs/vt100-ug/chapter3.html#ED)
	write(STDOUT_FILENO, "\x1b[2J",4);
}

/*** input ***/
void editorProcessKeypress() {
	char c = editorReadKey();

	switch(c) {
		case CTRL_KEY('q'):
			exit(0);
			break;
	}
}

/*** init ***/
int main() {
	enableRawMode();
	while (1) {
		editorRefreshScreen();
		editorProcessKeypress();
	}
	return 0;
}
