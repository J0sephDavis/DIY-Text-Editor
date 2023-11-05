#include <stdlib.h>
#include <termios.h>
#include <unistd.h>

struct termios original_termios; //the original state of the user's termio

void disableRawMode() {
	tcsetattr(STDIN_FILENO, TCSAFLUSH, &original_termios); //sets the users termio back to how it was
}

void enableRawMode() {
	tcgetattr(STDIN_FILENO, &original_termios); //get the origin termios & save it
	atexit(disableRawMode); //ensures that disableRawMode() is called on application exit()

	struct termios raw = original_termios; //struct that holds the termio setup for raw input
	raw.c_lflag &= ~(ECHO); // a "bitwise AND" assignment of a "bitewise NOT" of ECHO. Should disable ECHOing of user input
	tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw); //sets the state of the FD to the new struct we've created
						  //TCSAFLUSH - only applies changes after all pending output is written, discards unread input
}

int main() {
	enableRawMode();
	char c;
	while (read(STDIN_FILENO, &c, 1) == 1 && c != 'q');
	return 0;
}
