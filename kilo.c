#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <termios.h>
#include <unistd.h>

struct termios original_termios; //the original state of the user's termio

void disableRawMode() {
	tcsetattr(STDIN_FILENO, TCSAFLUSH, &original_termios); //sets the users termio back to how it was
}

void enableRawMode() {
	tcgetattr(STDIN_FILENO, &original_termios); 	//get the origin termios & save it
	atexit(disableRawMode); 			//ensures that disableRawMode() is called on application exit()

	struct termios raw = original_termios; 		//struct that holds the termio setup for raw input
	//c_lflag is the "local flag"
	raw.c_lflag &= ~(ECHO | ICANON | ISIG); 	//disable ECHOing, canonical mode(user-input sending on enter), & signal interrupts (ctrl-Z/C)
	tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw); 	//sets the state of the FD to the new struct we've created
						  	//TCSAFLUSH - only applies changes after all pending output is written, discards unread input
}

int main() {
	enableRawMode();
	char c;
	while (read(STDIN_FILENO, &c, 1) == 1 && c != 'q') {
		if (iscntrl(c)) 		//tests whether the character is a ctrl character or not. i.e, if it is a non-printable character
			printf("%d\n", c);
		else
			printf("%d ('%c')\n",c,c);
	};
	return 0;
}
