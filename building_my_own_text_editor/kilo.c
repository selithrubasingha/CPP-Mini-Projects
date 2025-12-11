/*** includes ***/


#include <sys/ioctl.h>
#include <stdlib.h>
#include <errno.h>
#include <termios.h>
#include <unistd.h>
#include <ctype.h>
#include <stdio.h>

/*** defines ***/
#define CTRL_KEY(k) ((k) & 0x1f)

/*** data ***/


struct editorConfig {
      int screenrows;
      int screencols;
      struct termios orig_termios;
};
struct editorConfig E;

/*** terminal ***/

// Print error message and exit
void die(const char *s) {

    write(STDOUT_FILENO, "\x1b[2J", 4);
    write(STDOUT_FILENO, "\x1b[H", 3);
    perror(s);
    exit(1);
}

// Restore terminal to original settings
void disableRawMode() {
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &E.orig_termios) == -1)
        die("tcsetattr");
}

// Enable raw mode (disable echo, canonical mode, signals, etc.)
void enableRawMode() {
    if (tcgetattr(STDIN_FILENO, &E.orig_termios) == -1) die("tcgetattr");
    atexit(disableRawMode); // Restore settings on exit

    struct termios raw = E.orig_termios;

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

char editorReadKey(){
    int nread;
    char c;
    while ((nread = read(STDIN_FILENO, &c, 1)!=1)){
        if (nread == -1 && errno != EAGAIN) die("read");
    }
    return c ;

}

int getWindowSize(int *rows, int *cols) {
  struct winsize ws;
  if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
    return -1;
  } else {
    *cols = ws.ws_col;
    *rows = ws.ws_row;
    return 0;
  }
}


/*** output ***/

void editorDrawRows() {
    int y;
    for (y = 0; y < E.screenrows; y++) {
        write(STDOUT_FILENO, "~\r\n", 3);
    }
}

void editorRefreshScreen() {
    write(STDOUT_FILENO, "\x1b[2J", 4);
    write(STDOUT_FILENO, "\x1b[H", 3);
    editorDrawRows();
    write(STDOUT_FILENO, "\x1b[H", 3);
}

/*** input ***/

void editorProcessKeypress(){
    char c = editorReadKey();
    switch (c) {
        case CTRL_KEY('q'):
        write(STDOUT_FILENO, "\x1b[2J", 4);
        write(STDOUT_FILENO, "\x1b[H", 3);
        exit(0);
        break;
    }
}
/*** init ***/

void initEditor() {
  if (getWindowSize(&E.screenrows, &E.screencols) == -1) die("getWindowSize");
}

int main() {
    enableRawMode();
    initEditor();

    char c;

    while (1) {
        editorRefreshScreen();
        editorProcessKeypress();
    }
    
    return 0;
}