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
	tcgetattr(STDIN_FILENO, &original_termios); 		//get the origin termios & save it
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
	
	tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw); 		//sets the state of the FD to the new struct we've created
		//TCSAFLUSH - only applies changes after all pending output is written, discards unread input
}

int main() {
	enableRawMode();
	char c;
	while (read(STDIN_FILENO, &c, 1) == 1 && c != 'q') {
		if (iscntrl(c)) 		//tests whether the character is a ctrl character or not. i.e, if it is a non-printable character
			printf("%d\r\n", c);
		else
			printf("%d ('%c')\r\n",c,c);
	};
	return 0;
}
