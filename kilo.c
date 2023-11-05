#include <termios.h>
#include <unistd.h>

void enableRawMode() {
	struct termios raw; //struct that holds current attributes
	tcgetattr(STDIN_FILENO, &raw); //load the current attributes into the struct
	raw.c_lflag &= ~(ECHO); // a "bitwise AND" assignment of a "bitewise NOT" of ECHO. Should disable ECHOing of user input
	tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw); //sets the state of the FD to the new struct we've created
}

int main() {
	enableRawMode();
	char c;
	while (read(STDIN_FILENO, &c, 1) == 1 && c != 'q');
	return 0;
}
