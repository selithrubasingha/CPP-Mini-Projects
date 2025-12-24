/*** defines ***/
#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE
#define _GNU_SOURCE

/*** includes ***/
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <stdarg.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <termios.h>
#include <unistd.h>
#include <fcntl.h>

/*** constants ***/
#define KILO_TAB_STOP 8
#define KILO_VERSION "0.0.1"
#define CTRL_KEY(k) ((k) & 0x1f)
#define KILO_QUIT_TIMES 1


//the keys we use in the text editor
enum editorKey
{
  BACKSPACE = 127,
  ARROW_LEFT = 1000,
  ARROW_RIGHT,
  ARROW_UP,
  ARROW_DOWN,
  DEL_KEY,
  HOME_KEY,
  END_KEY,
  PAGE_UP,
  PAGE_DOWN
};

/*** data ***/

//
typedef struct erow
{
  int size;
  int rsize;
  char *chars;
  char *render;
} erow;

struct editorConfig
{ 
  //char x and char y position (just the normal one)
  int cx, cy;
  int rx;//render x position (inside the render buffer)
  int rowoff;
  int coloff;
  int screenrows;
  int screencols;
  struct termios orig_termios;
  int numrows;
  char statusmsg[80];
  time_t statusmsg_time;
  erow *row;
  int dirty;
  char* filename;
};
struct editorConfig E;
struct abuf
{
  char *b;   // A pointer to the actual data in memory
  int len;   // The current length of the string stored in 'b'
};


/***function prototypes***/
char *editorRowsToString(int *buflen);
void editorDrawMessageBar(struct abuf *ab);
void editorSetStatusMessage(const char *fmt, ...);
void editorRefreshScreen();
char *editorPrompt(char *prompt);

/*** terminal ***/

// Print error message and exit
void die(const char *s)
{
  //clear the screen
  write(STDOUT_FILENO, "\x1b[2J", 4);
  //reposition cursor to top-left
  write(STDOUT_FILENO, "\x1b[H", 3);
  perror(s);
  exit(1);
}

// Restore terminal to original settings
void disableRawMode()
{
  //terminal control set attribute --> sets the terminal attrs
  //STDIN_FILENO --> changes some in the terminal input output (that is the keyboard input)
  //TCA_FLUSH --> clears the input *buffer* not the terminal ... It prevents "leftover" keypresses from leaking into your terminal after the program quits.
  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &E.orig_termios) == -1)
    die("tcsetattr");
}

// Enable raw mode (disable echo, canonical mode, signals, etc.)
void enableRawMode()
{
  if (tcgetattr(STDIN_FILENO, &E.orig_termios) == -1)
    die("tcgetattr");
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

  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1)
    die("tcsetattr");
}

int editorReadKey()
{
  int nread;
  char c;
  //read the inputted thing in the terminal (the 1 is for ->"store it in with 1 byte")
  //if DOES NOT press a key then the loop continues !!
  while ((nread = read(STDIN_FILENO, &c, 1) != 1))
  //some error handling
  {
    if (nread == -1 && errno != EAGAIN)
      die("read");
  }

  //this part is not inside the loop aha!
  if (c == '\x1b')
  {
    char seq[3];
    if (read(STDIN_FILENO, &seq[0], 1) != 1)
      return '\x1b';
    if (read(STDIN_FILENO, &seq[1], 1) != 1)
      return '\x1b';
    //after the \x1b is read then comes the other characters
    if (seq[0] == '[')
    {
      //boom it reads the key related codes 
      if (seq[1] >= '0' && seq[1] <= '9')
      {
        if (read(STDIN_FILENO, &seq[2], 1) != 1)
          return '\x1b';
        //these key codes contain a ~
        if (seq[2] == '~')
        {
          switch (seq[1])
          {
          case '1':
            return HOME_KEY;
          case '3':
            return DEL_KEY;
          case '4':
            return END_KEY;
          case '5':
            return PAGE_UP;
          case '6':
            return PAGE_DOWN;
          case '7':
            return HOME_KEY;
          case '8':
            return END_KEY;
          }
        }
      }
      else
      {
        // if no numbers and just texts its the up down arrow keys (don't ask me why it just is that way)
        switch (seq[1])
        {
        case 'A':
          return ARROW_UP;
        case 'B':
          return ARROW_DOWN;
        case 'C':
          return ARROW_RIGHT;
        case 'D':
          return ARROW_LEFT;
        case 'H':
          return HOME_KEY;
        case 'F':
          return END_KEY;
        }
      }
    }
    else if (seq[0] == 'O')
    {
      switch (seq[1])
      {
      case 'H':
        return HOME_KEY;
      case 'F':
        return END_KEY;
      }
    }
    return '\x1b';
  }
  else
  {
    return c;
  }
}

int getCursorPosition(int *rows, int *cols)
{
  char buf[32];
  unsigned int i = 0;

  // Send request to terminal: "Report Cursor Position"
  if (write(STDOUT_FILENO, "\x1b[6n", 4) != 4)
    return -1;

  // Read the response (looks like ^[[24;80R) into buf
  while (i < sizeof(buf) - 1)
  {
    if (read(STDIN_FILENO, &buf[i], 1) != 1)
      break;
    if (buf[i] == 'R') // 'R' marks the end of the message
      break;
    i++;
  }
  buf[i] = '\0'; // Null-terminate the string

  // Verify the response starts with the correct escape sequence
  if (buf[0] != '\x1b' || buf[1] != '[')
    return -1;

  // Parse the numbers from the string (skipping the first 2 chars)
  if (sscanf(&buf[2], "%d;%d", rows, cols) != 2)
    return -1;

  return 0;
}

int getWindowSize(int *rows, int *cols)
{
  struct winsize ws;
  //TIOCGWINSZ-> terminal input output get window size
  if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0)
  {
    //large numbers like 999 are added to force the cursor to the bottom-right corner
    if (write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) != 12)
      return -1;
    editorReadKey();
    return getCursorPosition(rows, cols);
  }
  else
  {
    //using the data inside the winsize struct --> assign the cols and row variables there values 
    *cols = ws.ws_col;
    *rows = ws.ws_row;
    return 0;
  }
}

/*** row operations ***/

// Convert cursor x position to render x position
int editorRowCxToRx(erow *row, int cx) {
  //in cx the tabs are \t but we need them to be "    " in rx.actually think of tabs as a grid structure it;s not alway 4 spaces!
  int rx = 0;
  int j;
  for (j = 0; j < cx; j++) {
    // Handle tabs
    if (row->chars[j] == '\t')
    // Tabs act as a variable-width jump to the next 8-column grid line.
      rx += (KILO_TAB_STOP - 1) - (rx % KILO_TAB_STOP);
    rx++;
  }
  return rx;
}

void editorUpdateRow(erow *row) {
  int tabs = 0;
  int j;
  //row->size :: "hi\t" means the size is 3 !
  for (j = 0; j < row->size; j++)
  //calculating the number of tabs
    if (row->chars[j] == '\t') tabs++;
  
  //this render is like the actual row that is drawn on the screen
  //first we free and allocate some memory for it
  free(row->render);
  row->render = malloc(row->size + tabs*(KILO_TAB_STOP - 1) + 1);//+1 for null terminator '\0

  //this is tabs expansion uusing the grid like structure of hitting the tab key
  int idx = 0;
  for (j = 0; j < row->size; j++) {
    //notice that in both is and else we increment idx... by doing this we will be able to fill the render buffer correctly
    if (row->chars[j] == '\t') {
      row->render[idx++] = ' ';
      while (idx % KILO_TAB_STOP != 0) row->render[idx++] = ' ';
    } else {
      row->render[idx++] = row->chars[j];
    }
  }
  row->render[idx] = '\0';
  //idx captured the actual size of the render buffer
  row->rsize = idx;
}

void editorInsertRow(int at, char *s, size_t len)
{ 
  if (at<0 || at>E.numrows ) return;
  //realloc re-allocates memory -> notice we are increasing the number of rows by 1 
  //there are two arguments -> pointer to the previous memory block and the new size
  E.row = realloc(E.row, sizeof(erow) * (E.numrows + 1));
  //if we insert a row in the middle you gotta shift the rest of the rows
  memmove(&E.row[at + 1], &E.row[at], sizeof(erow) * (E.numrows - at));

  //assigning the data that is to be WRITTEN in the row
  E.row[at].size = len;
  E.row[at].chars = malloc(len + 1);
  //copying the string of line from s adress and copying with memcpy
  memcpy(E.row[at].chars, s, len);
  E.row[at].chars[len] = '\0';//don't forget the null terminator!


  //resetting the render buffer
  E.row[at].rsize = 0;
  E.row[at].render = NULL;
  //after the reset t=we use the row , clean it , and reuse it to display the next row.
  editorUpdateRow(&E.row[at]);

  E.numrows++;
  E.dirty++;
}

void editorRowInsertChar(erow *row , int at , int c ){
  //this function does not have to worry about where the cursor is 

  //if the index is incorrect we default to end of the line
  if (at<0 || at>row->size) at = row->size;

  row->chars = realloc(row->chars,row->size+2);//+2 one for the new character added and other for the null terminator?

  //memove is used for shifting the whole line to the right . memmove is more memory safe than memcpy
  memmove(&row->chars[at+1],&row->chars[at],row->size-at+1);//we only shift the chars to the right
  
  row->size++;
  row->chars[at] = c ;

  editorUpdateRow(row);
  E.dirty++;


}

void editorRowDelChar(erow *row, int at) {
  if (at < 0 || at >= row->size) return;
  memmove(&row->chars[at], &row->chars[at + 1], row->size - at);
  row->size--;
  editorUpdateRow(row);
  E.dirty++;
}


void editorFreeRow(erow *row) {
  //freeing the memory for render and chars arrays
  free(row->render);
  free(row->chars);
}

void editorDelRow(int at) {
  if (at < 0 || at >= E.numrows) return;
  editorFreeRow(&E.row[at]);
  //this is the same as del char 
  //BUT THERE IS A CHANGE ! notice we shift the whole set of rows one up!!!!
  memmove(&E.row[at], &E.row[at + 1], sizeof(erow) * (E.numrows - at - 1));
  E.numrows--;
  E.dirty++;
  }

void editorRowAppendString(erow *row , char *s , size_t len){
  row->chars = realloc(row->chars,row->size+len+1);
  memcpy(&row->chars[row->size],s,len);
  row->size += len;
  row->chars[row->size] = '\0';
  editorUpdateRow(row);
  E.dirty++;
}
/*** editor operations ***/

void editorInsertChar(int c ){
//this function does not have to worry about dealing with modifying the Erow.
//this is what is called encapsulation baby!

  //typing on a brand new, empty line at the end of the file.
  if (E.cy == E.numrows)     editorInsertRow(E.numrows, "", 0);

  //normal method calling
  editorRowInsertChar(&E.row[E.cy],E.cx,c);
  //since we add one character cx should be incrememnted . 
  E.cx++;

}

void editorInsertNewline() {
  //if current line is empty make a new blank line
  if (E.cx == 0) editorInsertRow(E.cy,"",0) ;
  else {
    //if the line your in has stuff and cursor is in the middle
    //pressing enter shifts part of the string to the next line 
    //naming the variable row for easier use
    erow *row = &E.row[E.cy];
    //inserting the new row 
    editorInsertRow(E.cy+1,&row->chars[E.cx],row->size-E.cx);
    //since the above function used realloc ... we need reinitialize the 
    //row variable
    row = &E.row[E.cy];
    //now we need to clean the previous line ... the line that the user was in 
    row->size = E.cx;
    row->chars[row->size] = '\0';
    //update row will will update that prev row with the new changes
    editorUpdateRow(row);
  } 
}

void editorDelChar() {
  //most of it is like the insert char method
  if (E.cy == E.numrows) return;
  if (E.cx == 0 && E.cy == 0) return;

  erow *row = &E.row[E.cy];
  if (E.cx > 0) {
    editorRowDelChar(row, E.cx - 1);
    E.cx--;
  }else{
    //when you backspace at the beginning of a line ... the cursor 
    //should go the last text of the previous line !!
    E.cx = E.row[E.cy].size;
    //afterwards the string in the backspaced line ... yea that line goes to the previous line!
    //line1->hello line2->world ->hit backspace line 1 -> hello world
    editorRowAppendString(&E.row[E.cy - 1], row->chars, row->size);
    editorDelRow(E.cy);
    E.cy--;

  }
}
/*** file i/o ***/
void editorOpen(char *filename)
{
  //clean up previous filename if any and allocate new memory
  free(E.filename);
  E.filename = strdup(filename);

  FILE *fp = fopen(filename, "r");
  if (!fp)
    die("fopen");

  char *line = NULL;
  size_t linecap = 0; //unsigned integer -> size_t (only positive values)
  ssize_t linelen; //signed integer -> ssize
  //when we call getline with a NULL pointer it will allocate memory for us ! default autopilot stuff
  while ((linelen = getline(&line, &linecap, fp)) != -1)
  {
    //inside this while loop we remove the \n and \r at the end of each line (actually the linelen is the one that s decreased)
    while (linelen > 0 && (line[linelen - 1] == '\n' || line[linelen - 1] == '\r'))
      linelen--;


  //now we append the line to the editor rows
    editorInsertRow(E.numrows, line, linelen);
  }
  //freeing the memory and closing the file
  free(line);
  fclose(fp);
  E.dirty = 0;
}

void editorSave(){
  // If we don't have a filename yet (e.g. new file), we can't save!
  if (E.filename == NULL) {
    E.filename = editorPrompt("Save as: %s (ESC to cancel)");

    if (E.filename == NULL){
      editorSetStatusMessage("Save Aborted");
      return;
    }
  }
  
  int len;

  //get the pointer to the super long string of data
  char *buf = editorRowsToString(&len);

  // O_RDWR | O_CREAT-> "I want the file to be Read/Write AND I want it to be created if it doesn't exist."
  int fd = open(E.filename, O_RDWR | O_CREAT, 0644);

  //we use the 3 if conditionals to ERROR HANDLE.
  if (fd!=-1){
    //if the original file is 100line and i deleted 50 lines ... to clean the other 50 we use truncate
    //even though these are inside if statemnets ... they actually trunacate and write while inside the if statement as well
    if (ftruncate(fd , len)!=-1){
      //this is where the magic happens
      if (write(fd, buf, len) == len) {
          //close the fd int and free the buf
        close(fd);
        free(buf);
        E.dirty = 0;
        editorSetStatusMessage("%d bytes written to disk", len);
        return;

    }
  }  close(fd);
}
  free(buf);
  editorSetStatusMessage("Can't save! I/O error: %s", strerror(errno));
}
/*** append buffer ***/


#define ABUF_INIT {NULL, 0}

void abAppend(struct abuf *ab, const char *s, int len)
{
  //dynamically add stuff to the string buffer
  char *new = realloc(ab->b, ab->len + len);

  //error handling
  if (new == NULL)
    return;

  //this memcpy is used often better to remember it 
  memcpy(&new[ab->len], s, len);

  //we ifrst made the allocation and here... we assign the pointers !
  ab->b = new;
  ab->len += len;
}

void abFree(struct abuf *ab)
{
  //free the memory allocated for the buffer
  free(ab->b);
}

/*** output ***/

void editorScroll()
{
  E.rx = 0;
  if (E.cy < E.numrows) {
    E.rx = editorRowCxToRx(&E.row[E.cy], E.cx);
  }
  if (E.cy < E.rowoff)
  {
    E.rowoff = E.cy;
  }
  // when the cy is larger than the screen itself (means the user has scrolled down)
  //then there is a offset and we caculate it .
  if (E.cy >= E.rowoff + E.screenrows)
  {
    E.rowoff = E.cy - E.screenrows + 1;
  }
  //E.cx changed to E.rx
  if (E.rx < E.coloff) {
    E.coloff = E.rx;
  }
  if (E.rx >= E.coloff + E.screencols) {
    E.coloff = E.rx - E.screencols + 1;
  }
}

//this is the function that draws the actual rows line by line from the editor

void editorDrawRows(struct abuf *ab)
{
  int y;
  //we draw only fixed amount of lines
  for (y = 0; y < E.screenrows; y++)
  {
    //but we should start with rowoffset in mind!
    int filerow = y + E.rowoff;
    if (filerow >= E.numrows)
    {
      //the below if statement happens if a file is not open
      if (E.numrows == 0 && y == E.screenrows / 3)
      {
        //a buffer to store the welcome string 
        char welcome[80];
        int welcomelen = snprintf(welcome, sizeof(welcome),
                                  "Kilo editor -- version %s", KILO_VERSION);
        if (welcomelen > E.screencols)
          welcomelen = E.screencols;

        int padding = (E.screencols - welcomelen) / 2;
        if (padding)
        {
          abAppend(ab, "~", 1);
          padding--;
        }
        while (padding--)
          abAppend(ab, " ", 1);

        abAppend(ab, welcome, welcomelen);
      }
      else
      {
        abAppend(ab, "~", 1);
      }
    }
    else //else means if a file is open
    {
      //notice the file row contians the offset , and here we get the file line size 
      int len = E.row[filerow].rsize - E.coloff;
      if (len < 0)
        len = 0;
      if (len > E.screencols)
        len = E.screencols;
      //render is a string ... so when we say render[x] it means start the line of string from x position! 
      // that is how we add the column offset baby!
      abAppend(ab, &E.row[filerow].render[E.coloff], len);
    }
    
    //clear the rest of the line and go to the next line
    abAppend(ab, "\x1b[K", 3);
      abAppend(ab, "\r\n", 2);
    }
  
}

void editorDrawStatusBar(struct abuf *ab) {
  //escape sequence -> invert colors!
  abAppend(ab, "\x1b[7m", 4);
  
  //fill the buffers and adAppend them
  char status[80], rstatus[80];
  int len = snprintf(status, sizeof(status), "%.20s - %d lines %s",
    E.filename ? E.filename : "[No Name]", E.numrows,
    E.dirty ? "(modified)" : "");
  int rlen = snprintf(rstatus, sizeof(rstatus), "%d/%d",
    E.cy + 1, E.numrows);
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
  //escape sequence to stop inverting colors
  abAppend(ab, "\x1b[m", 3);
  abAppend(ab, "\r\n", 2);
}

void editorRefreshScreen()
{
  editorScroll();
  //initilize the append buffer
  struct abuf ab = ABUF_INIT;

  //hide the cursor
  abAppend(&ab, "\x1b[?25l", 6);
  //move the cursor to the top left
  abAppend(&ab, "\x1b[H", 3);
  editorDrawRows(&ab);
  editorDrawStatusBar(&ab);
  editorDrawMessageBar(&ab);

  char buf[32];
  //move the to a epcific position
  snprintf(buf, sizeof(buf), "\x1b[%d;%dH", (E.cy - E.rowoff) + 1, (E.rx - E.coloff) + 1);
  abAppend(&ab, buf, strlen(buf));

  //show the cursor
  abAppend(&ab, "\x1b[?25h", 6);

  write(STDOUT_FILENO, ab.b, ab.len);
  abFree(&ab);
}

void editorSetStatusMessage(const char *fmt, ...) {
  //variadic functions -> lets you pass a flexible number of arguments
  va_list ap;
  va_start(ap, fmt);
  //setting the message  to the fmt
  vsnprintf(E.statusmsg, sizeof(E.statusmsg), fmt, ap);
  va_end(ap);

  //tells the exact time the status message is made? i think this will be used later
  E.statusmsg_time = time(NULL);
}

void editorDrawMessageBar(struct abuf *ab) {
  abAppend(ab, "\x1b[K", 3);
  int msglen = strlen(E.statusmsg);
  if (msglen > E.screencols) msglen = E.screencols;
  if (msglen && time(NULL) - E.statusmsg_time < 5)
    abAppend(ab, E.statusmsg, msglen);
}
/*** input ***/
char *editorPrompt(char *prompt){
  //simple to imput a string and return it

  //initiate a buffer size
  size_t bufsize = 128;
  char *buf = malloc(bufsize);

  size_t buflen= 0 ;
  buf[0] = '\0';

  while (1){
    editorSetStatusMessage(prompt , buf);
    editorRefreshScreen();

    int c = editorReadKey();

    if (c == DEL_KEY || c == CTRL_KEY('h') || c == BACKSPACE){
      //after we do this there are 2 null terminators but the compiler ignores one so it's no need to worry
      if (buflen != 0) buf[--buflen] = '\0';
    } else if (c == '\x1b') {
      //empty the buff and empty the string!!
      editorSetStatusMessage("");
      free(buf);
      return NULL;
    } else if (c == '\r') { //if enter key is pressed boom return the string
      if (buflen !=0){
        editorSetStatusMessage("");
        return buf;
      }
    }else if (!iscntrl(c) && c<128){
      if(buflen == bufsize-1){//if the buf size is small .double the size
        bufsize *=2;
        buf = realloc(buf,bufsize);

      }
      //character by by character add it to the buffer and sheft the null terminator!!
      buf[buflen++]=c;
      buf[buflen]='\0';
    }
  }
}

void editorMoveCursor(int key)
{
  // used to access the row->size .. to access it , we initialize row here
  erow *row = (E.cy >= E.numrows) ? NULL : &E.row[E.cy];
  switch (key)
  {
  case ARROW_LEFT:
    if (E.cx != 0)
    {
      E.cx--;
    }
    // in beginning of line if we go left we go to previous line end
    else if (E.cy > 0)
    {
      E.cy--;
      E.cx = E.row[E.cy].size;
    }
    break;
  case ARROW_RIGHT:
    if (row && E.cx < row->size)
    {
      E.cx++;
    }
    // end of a line ->right -> goes to next line beginning
    else if (row && E.cx == row->size)
    {
      E.cy++;
      E.cx = 0;
    }
    break;
  case ARROW_UP:
    if (E.cy != 0)
    {
      E.cy--;
    }
    break;
  case ARROW_DOWN:
    if (E.cy < E.numrows)
    {
      E.cy++;
    }
    break;
  }
  // since this row is after inrementing , the row has chagned ,
  // when we go right and then down .. sometimes the cx exceeds line length
  // we fixe this inside here
  row = (E.cy >= E.numrows) ? NULL : &E.row[E.cy];
  int rowlen = row ? row->size : 0;
  if (E.cx > rowlen)
  {
    E.cx = rowlen;
  }
}

void editorProcessKeypress()
{
  static int quit_times = KILO_QUIT_TIMES;
  int c = editorReadKey();
  switch (c)
  {
  case '\r'://this is actually the ENTER key!
    editorInsertNewline();
    break;
  case CTRL_KEY('q'):
      if (E.dirty && quit_times > 0) {
        editorSetStatusMessage("WARNING!!! File has unsaved changes. "
          "Press Ctrl-Q %d more times to quit.", quit_times);
        quit_times--;
        return;
      }
    write(STDOUT_FILENO, "\x1b[2J", 4);
    write(STDOUT_FILENO, "\x1b[H", 3);
    exit(0);
    break;
  case HOME_KEY:
    E.cx = 0;
    break;
  case END_KEY:
  //go to the end of a line (where the text ends not the screen)
    if (E.cy < E.numrows)
      E.cx = E.row[E.cy].size;
    break;

  case BACKSPACE:
  case CTRL_KEY('h')://xtrl h is for backspace command
  case DEL_KEY:
  //you first gotta move the cursor to the right before deleting
      if (c == DEL_KEY) editorMoveCursor(ARROW_RIGHT);
      editorDelChar();
    break;
  case CTRL_KEY('s'):
    editorSave();
    break;
  case PAGE_UP:
  case PAGE_DOWN:
  {
    int times = E.screenrows;
    while (times--)
      editorMoveCursor(c == PAGE_UP ? ARROW_UP : ARROW_DOWN);
  }
  break;
  case ARROW_UP:
  case ARROW_DOWN:
  case ARROW_LEFT:
  case ARROW_RIGHT:
    editorMoveCursor(c);
    break;

  case CTRL_KEY('l')://ctrl l is for refreshing the screen
  case '\x1b':
    break;
  default:
    editorInsertChar(c);
    break;
  }

  quit_times = KILO_QUIT_TIMES;
}

char *editorRowsToString(int *buflen){
  //this creates a super large string of the file !

  //first we need to allocate memory to store the new saved string ...
  //for that we  first caculate the totlength
  int totlen = 0;
  for (int j =0 ;j<E.numrows;j++){
    totlen+= E.row[j].size +1;

  }
  *buflen = totlen;

  char *buf = malloc(totlen);
  char *p =buf;

  //now we do the memcyping line by line
  for (int j=0;j<E.numrows;j++){
    memcpy(p, E.row[j].chars, E.row[j].size);
    p += E.row[j].size;
    *p = '\n';
    p++;

  }

  return buf;

}
/*** init ***/

void initEditor()
{
  E.cx = 0;
  E.cy = 0;
  E.rx = 0;
  E.rowoff = 0;
  E.coloff = 0;
  E.numrows = 0;
  E.row = NULL;
  E.dirty = 1;
  E.filename = NULL;
  E.statusmsg[0] = '\0';
  E.statusmsg_time = 0;
  if (getWindowSize(&E.screenrows, &E.screencols) == -1)
    die("getWindowSize");
  E.screenrows -= 2;
}

int main(int argc, char *argv[])
{
  enableRawMode();
  initEditor();
  if (argc >= 2)
  {
    editorOpen(argv[1]); //this argv[1] containes the file name! we give it in the terminal
  }

  editorSetStatusMessage("HELP: Ctrl-S = save | Ctrl-Q = quit");

  // char c;

  while (1)
  {
  
    editorRefreshScreen();
    editorProcessKeypress();
  }

  return 0;
}