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
/*hello 
everybody
 my name is
  selith */

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

enum editorHighlight {
  HL_NORMAL = 0,
  HL_NUMBER,
  HL_KEYWORD1,
  HL_KEYWORD2,
  HL_MLCOMMENT,
  HL_STRING,
  HL_COMMENT,
  HL_MATCH
};

#define HL_HIGHLIGHT_NUMBERS (1<<0)
#define HL_HIGHLIGHT_STRINGS (1<<1)
//the above is same for #define HL_HIGHLIGHT_NUMBERS 1
//bithsifting is used for "aesthetics"
/*** data ***/
struct editorSyntax {
  char *filetype;
  char **filematch;
  char **keywords;
  char *singleline_comment_start;//cause different files have different ways of commenting !
  char *multiline_comment_start;
  char *multiline_comment_end;
  int flags;
};

typedef struct erow
{
  int idx;
  int size;
  int rsize;
  char *chars;
  char *render;
  int hl_open_comment; // for multi line comment logic
  unsigned char *hl;//hl->highlight 
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
  struct editorSyntax *syntax;
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

/*** filetypes ***/

char *C_HL_extensions[] = {".c",".h",".cpp",NULL};
char *C_HL_keywords[] = {
  "switch", "if", "while", "for", "break", "continue", "return", "else",
  "struct", "union", "typedef", "static", "enum", "class", "case",
  "int|", "long|", "double|", "float|", "char|", "unsigned|", "signed|",
  "void|", NULL
};

struct editorSyntax HLDB[] = {
  {
    "c",
    C_HL_extensions,
    C_HL_keywords,
    "//", "/*", "*/",
    HL_HIGHLIGHT_NUMBERS | HL_HIGHLIGHT_STRINGS

  },
};


#define HLDB_ENTRIES (sizeof(HLDB) / sizeof(HLDB[0]))
//the above calculate the len of the array

/***function prototypes***/
char *editorRowsToString(int *buflen);
void editorDrawMessageBar(struct abuf *ab);
void editorSetStatusMessage(const char *fmt, ...);
void editorRefreshScreen();
char *editorPrompt(char *prompt, void (*callback)(char *, int));

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

/*** syntax highlighting ***/

int is_separator(int c) {
  return isspace(c) || c == '\0' || strchr(",.()+-/*=~%<>[];", c) != NULL;
}

void editorUpdateSyntax(erow *row){//we color the syntax row by row || line by line

  row->hl= realloc(row->hl,row->rsize);
  memset(row->hl,HL_NORMAL,row->rsize);

  if (E.syntax == NULL) return;

  char **keywords = E.syntax->keywords;

  //for the commenting stuffs
  char *scs = E.syntax->singleline_comment_start;
  char *mcs = E.syntax->multiline_comment_start;
  char *mce = E.syntax->multiline_comment_end;


  int scs_len = scs ? strlen(scs) : 0;
  int mcs_len = mcs ? strlen(mcs) : 0;// "*/" so len is 2
  int mce_len = mce ? strlen(mce) : 0;//also len is 2

  //we store the previous highlihgted? varibale for more efficiency
  //although it kind of spagettifies the code base
  int prev_sep = 1;
  int in_string = 0;
  //checking is the prev line started a multiline line comment
  int in_comment = (row->idx > 0 && E.row[row->idx - 1].hl_open_comment);

  int i = 0;
  while (i < row->rsize) {
    char c = row->render[i];
    unsigned char prev_hl = (i > 0) ? row->hl[i - 1] : HL_NORMAL;

    if (scs_len && !in_string && !in_comment) {
      if (!strncmp(&row->render[i],scs,scs_len)){
      memset(&row->hl[i],HL_COMMENT,row->rsize-i);
      break;
      }
    }

    //if multiline comment is ending 
    if (mcs_len && mce_len && !in_string) {
      if (in_comment) {
        row->hl[i] = HL_MLCOMMENT;
        if (!strncmp(&row->render[i], mce, mce_len)) {
          memset(&row->hl[i], HL_MLCOMMENT, mce_len);
          i += mce_len;
          in_comment = 0;
          prev_sep = 1;
          continue;
        } else {
          i++;
          continue;
        }
      } else if (!strncmp(&row->render[i], mcs, mcs_len)) {
        memset(&row->hl[i], HL_MLCOMMENT, mcs_len);
        i += mcs_len;
        in_comment = 1;
        continue;
      }
    }
    //string checkng if statement .. mostly string are stuff inside " "
    if (E.syntax->flags & HL_HIGHLIGHT_STRINGS){
      if (in_string){
        //hl[i] is the mapping the color for every char in the string
        row->hl[i] = HL_STRING;

        // --> \\ means just \ remember!
        if (c == '\\' && i + 1 < row->rsize) {
          row->hl[i + 1] = HL_STRING;
          i += 2;
          continue;
        }
        //removing of the instring is done here...
        if (c== in_string) in_string = 0 ;
        i++; prev_sep = 1;
        continue;
      } else {
          if (c == '"' || c == '\'') {//-> \' means the char ' !!! \ is just the escape character
          in_string = c;
          row->hl[i] = HL_STRING;
          i++;
          continue;
        }

      }
    }
    
    if (E.syntax->flags & HL_HIGHLIGHT_NUMBERS){
    //seperator thing is used bscuase we dont want term1 or "123456" numbers to be higlighted right?
      if ((isdigit(c) && (prev_sep || prev_hl == HL_NUMBER))  ||
      //the .is for highlighting the . in decimals
          (c == '.' && prev_hl == HL_NUMBER)) {
        row->hl[i] = HL_NUMBER;
        i++;
        prev_sep = 0;
        continue;
      }
  }

  //prevsep is mostly for spaces.. if the prev is a space we KNOW IT'S THE START OF A WORD!!
  if (prev_sep) {
      int j;
      for (j = 0; keywords[j]; j++) {
        int klen = strlen(keywords[j]);
        //if kwd2 then last char is |
        int kw2 = keywords[j][klen - 1] == '|';
        //then we just remove the | length
        if (kw2) klen--;

        //if the word matches and the char after the last letter is a seperator (mostly a space)
        if (!strncmp(&row->render[i], keywords[j], klen) &&
            is_separator(row->render[i + klen])) {

          //boom memset!
          memset(&row->hl[i], kw2 ? HL_KEYWORD2 : HL_KEYWORD1, klen);
          i += klen;
          break;
        }
      }
      if (keywords[j] != NULL) {
        prev_sep = 0;
        continue;
      }
    }

    prev_sep = is_separator(c);
    i++;
  }

  //after the loop ,we set the in_comment to whatever is left ... so the editor know 
  //if we were in a multiline comment at the end
  int changed = (row->hl_open_comment != in_comment);
  row->hl_open_comment = in_comment;
  if (changed && row->idx + 1 < E.numrows)
    editorUpdateSyntax(&E.row[row->idx + 1]);

}

int editorSyntaxToColor(int hl) {
  switch (hl) {
    case HL_COMMENT:
    case HL_MLCOMMENT: return 36;
    case HL_STRING: return 35;
    case HL_KEYWORD1: return 33;
    case HL_KEYWORD2: return 32;
    case HL_NUMBER: return 31;
    case HL_MATCH: return 34;
    default: return 37;
     
  }
}

void editorSelectSyntaxHighlight() {
  //this method checks what the filetype is from the files array

  E.syntax = NULL;
  if (E.filename == NULL) return ; 

  //ext contains the string after the dot .
  char *ext = strrchr(E.filename,'.');

  for (unsigned int j = 0; j < HLDB_ENTRIES; j++) {
    struct editorSyntax *s = &HLDB[j];
    unsigned int i = 0 ;

    while (s->filematch[i]){
      //is_ext  is used to differentiate between Makefile without . s and . file like python s++ files
      int is_ext = (s->filematch[i][0]=='.');

      //string match? logic
      if ((is_ext && ext && !strcmp(ext, s->filematch[i])) ||
          (!is_ext && strstr(E.filename, s->filematch[i]))) {
        E.syntax = s;

        //after we save the file as .c the highlights don't work afterwards
        //we fix that with thsi block right below
        int filerow;
        for (filerow = 0; filerow < E.numrows; filerow++) {
          //updating line by line
          editorUpdateSyntax(&E.row[filerow]);
        }


        return;
      }
      i++;
    }
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

int editorRowRxToCx(erow *row, int rx) {
  int cur_rx = 0;
  int cx;
  for (cx = 0; cx < row->size; cx++) {
    if (row->chars[cx] == '\t')
      cur_rx += (KILO_TAB_STOP - 1) - (cur_rx % KILO_TAB_STOP);
    cur_rx++;
    if (cur_rx > rx) return cx;
  }
  return cx;
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

  //syntax coloring done here.
  editorUpdateSyntax(row);
}

void editorInsertRow(int at, char *s, size_t len)
{ 
  if (at<0 || at>E.numrows ) return;
  //realloc re-allocates memory -> notice we are increasing the number of rows by 1 
  //there are two arguments -> pointer to the previous memory block and the new size
  E.row = realloc(E.row, sizeof(erow) * (E.numrows + 1));
  //if we insert a row in the middle you gotta shift the rest of the rows
  memmove(&E.row[at + 1], &E.row[at], sizeof(erow) * (E.numrows - at));

  //if you insert a row in the middle the whole indexes should be shifted!! that is why we use a for loop!
  for (int j = at + 1; j <= E.numrows; j++) E.row[j].idx++;

  //storing the index at variable inside the struct for later use
  E.row[at].idx = at;

  //assigning the data that is to be WRITTEN in the row
  E.row[at].size = len;
  E.row[at].chars = malloc(len + 1);
  //copying the string of line from s adress and copying with memcpy
  memcpy(E.row[at].chars, s, len);
  E.row[at].chars[len] = '\0';//don't forget the null terminator!


  //resetting the render buffer
  E.row[at].rsize = 0;
  E.row[at].render = NULL;
  E.row[at].hl = NULL; //highlight set to null
  //after the reset t=we use the row , clean it , and reuse it to display the next row.
  //default multicomment value
  E.row[at].hl_open_comment = 0;
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
  free(row->hl);
}

void editorDelRow(int at) {
  if (at < 0 || at >= E.numrows) return;
  editorFreeRow(&E.row[at]);
  //this is the same as del char 
  //BUT THERE IS A CHANGE ! notice we shift the whole set of rows one up!!!!
  memmove(&E.row[at], &E.row[at + 1], sizeof(erow) * (E.numrows - at - 1));
  for (int j = at; j < E.numrows - 1; j++) E.row[j].idx--;
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

  editorSelectSyntaxHighlight();

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
    E.filename = editorPrompt("Save as: %s (ESC to cancel)",NULL);

    if (E.filename == NULL){
      editorSetStatusMessage("Save Aborted");
      return;
    }
    editorSelectSyntaxHighlight();
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


  void editorFindCallback(char *query, int key) {
  static int last_match = -1;
  static int direction = 1;

  //non highlightin after the word is found ! to do that
  //if the line is highlighted(saved_hl) then we REMOVE THE HIGHTLIGHT
  static int saved_hl_line;
  static char *saved_hl = NULL;
  if (saved_hl) {
    memcpy(E.row[saved_hl_line].hl, saved_hl, E.row[saved_hl_line].rsize);
    free(saved_hl);
    saved_hl = NULL;
  }

  if (key == '\r' || key == '\x1b') {
    last_match = -1;
    direction = 1;
    return;
  } else if (key == ARROW_RIGHT || key == ARROW_DOWN) {
    direction = 1;
  } else if (key == ARROW_LEFT || key == ARROW_UP) {
    direction = -1;
  } else {
    last_match = -1;
    direction = 1;
  }

  if (last_match==-1) direction = 1;
  int current = last_match;

  int i ;
  //looping through every line
  for (i=0;i<E.numrows;i++){
    current+=direction;
    if (current==-1) current= E.numrows - 1;
    else if (current==E.numrows) current = 0 ;

    erow *row = &E.row[current];

    //super cool str str function that finds the substring or sth
    //this finds the memory address of where the substring starts.
    char *match = strstr(row->render, query);

    if (match){
      last_match = current;
      E.cy = current;

      E.cx = editorRowRxToCx(row, match - row->render);//match-row.render gives us the render rx!! the one with "    ".. but we need cx
      //a.k.a the one with \t s

      //this is wierd ... we set the rowoffset waaay down , and after , the scroll refresh fixes everything??
      E.rowoff = E.numrows;

      //here we save the colored highlight so it can be removed later.this method is called multiple time cause of incremented search
      saved_hl_line = current;
      saved_hl = malloc(row->rsize);
      memcpy(saved_hl, row->hl, row->rsize);

      //syntax highlighting
      memset(&row->hl[match - row->render], HL_MATCH, strlen(query));
      break; 
    }
  }
  
}

void editorFind(){
  int saved_cx = E.cx;
  int saved_cy = E.cy;
  int saved_coloff = E.coloff;
  int saved_rowoff = E.rowoff;
    //getting the find string
    //we use the call back function for incremental search !! (ecrytime you type a character it finds)
  char *query = editorPrompt("Search: %s (Use ESC/Arrows/Enter)",
                             editorFindCallback);

  if (query) free(query);
  else {
    E.cx = saved_cx;
    E.cy = saved_cy;
    E.coloff = saved_coloff;
    E.rowoff = saved_rowoff;

  }
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
      //...
      //we take the string LINE BY LINE
     char *c = &E.row[filerow].render[E.coloff];

     //an 8 bit datatype 0-255
     unsigned char *hl = &E.row[filerow].hl[E.coloff];
     int current_color = -1 ;
     int j ;

     //we check for digits ... the digits need to be colored!!
     //in each line we should add har by char
     for (j=0;j<len;j++){
      if (hl[j] == HL_NORMAL){
        if (current_color !=-1){
        abAppend(ab, "\x1b[39m", 5);
        current_color = -1 ;
      }
        abAppend(ab, &c[j], 1);

      }else {
        int color = editorSyntaxToColor(hl[j]);
        if (color != current_color) {
          current_color = color;
        char buf[16];
        int clen = snprintf(buf, sizeof(buf), "\x1b[%dm", color);
        abAppend(ab, buf, clen);
      
      }
        abAppend(ab, &c[j], 1);

      }
     }
     abAppend(ab, "\x1b[39m", 5);

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
  int rlen = snprintf(rstatus, sizeof(rstatus), "%s | %d/%d",
    E.syntax ? E.syntax->filetype : "no ft", E.cy + 1, E.numrows);
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
char *editorPrompt(char *prompt , void (*callback) (char* ,int)){
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
      if (callback) callback(buf, c);
      free(buf);
      return NULL;
    } else if (c == '\r') { //if enter key is pressed boom return the string
      if (buflen !=0){
        editorSetStatusMessage("");
        if (callback) callback(buf, c);
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

    if (callback) callback(buf, c);
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

    case CTRL_KEY('f'):
      editorFind();
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
  E.syntax = NULL;

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

  editorSetStatusMessage(
    "HELP: Ctrl-S = save | Ctrl-Q = quit | Ctrl-F = find");
  // char c;

  while (1)
  {
  
    editorRefreshScreen();
    editorProcessKeypress();
  }

  return 0;
}