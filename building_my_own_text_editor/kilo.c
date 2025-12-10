/*** includes ***/

#include <stdlib.h>
#include <errno.h>
#include <termios.h>
#include <unistd.h>
#include <ctype.h>
#include <stdio.h>

/*** data ***/

struct termios orig_termios;

/*** terminal ***/

// Print error message and exit
void die(const char *s) {
  perror(s);
  exit(1);
}

// Restore terminal to original settings
void disableRawMode() {
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios) == -1)
        die("tcsetattr");
}

// Enable raw mode (disable echo, canonical mode, signals, etc.)
void enableRawMode() {
    if (tcgetattr(STDIN_FILENO, &orig_termios) == -1) die("tcgetattr");
    atexit(disableRawMode); // Restore settings on exit

    struct termios raw = orig_termios;

    // Disable special input processing (flow control, CR translation, parity)
    raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    
    // Disable output processing (post-processing)
    raw.c_oflag &= ~(OPOST);
    
    // Set character size to 8 bits per byte
    raw.c_cflag |= (CS8);
    
    // Disable local flags (echo, canonical mode, signals, extended input)
    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);

    // Set read timeout (VMIN=0, VTIME=1 => 100ms timeout)
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 1;

    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) die("tcsetattr");
}

/*** init ***/

int main() {
    enableRawMode();

    char c;

    while (1) {
        c = '\0';
        
        // Read 1 byte with error handling (ignore timeout error EAGAIN)
        if (read(STDIN_FILENO, &c, 1) == -1 && errno != EAGAIN) die("read");
        
        // Note: Logic allows second read which may overwrite previous byte
        read(STDIN_FILENO, &c, 1);

        if (iscntrl(c)) {
            printf("%d\r\n", c);
        } else {
            printf("%d ('%c')\r\n", c, c);
        }

        if (c == 'q') break;
    }
    
    return 0;
}