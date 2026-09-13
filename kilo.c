/***    includes ***/
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <termios.h>
#include <unistd.h>

/*** defines ***/
#define KILO_VERSION "0.0.1"

#define CTRL_KEY(k) ((k) & 0x1f)

enum editorKey {
    ARROW_LEFT = 1000, // the rest get enummerated from 1000
    ARROW_RIGHT,       // 1001, 1002, 1003...
    ARROW_UP,
    ARROW_DOWN,
    DEL_KEY,
    HOME_KEY,
    END_KEY,
    PAGE_UP,
    PAGE_DOWN
};

/*** data ***/
// struct used to store editor state

typedef struct {
    int size;
    char *chars;
} erow;

typedef struct {
    int cx, cy;
    int screenrows;
    int screencols;
    int numrows;
    erow row;
    struct termios orig_termios;
} editorConfig;

editorConfig E;


/*** terminal ***/
void die(const char *s) {
    write(STDOUT_FILENO, "\x1b[2J", 4);
    write(STDOUT_FILENO, "\x1B[H", 3);

    perror(s); // <stdlib.h> looks at the errno variable and prints an error
               // message for it
    exit(1);   // <stdlib.h>
}

void disableRawMode() {
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &E.orig_termios) == -1)
        die("tcsetattr");
}
/* In the default canonical mode the terminal processes input line by line
 * (allowing the user to edit text before sending it) In Raw mode, every single
 * keypress is instantly delivered to the program
 */
void enableRawMode() {
    // tcgetattr() is used the read the current attibutes into a struct
    // <termios.h>
    if (tcgetattr(STDIN_FILENO, &E.orig_termios) == -1)
        die("tcgetattr");
    // used to run the disableRawMode function as soon as the program exits
    // <stdlib.h>
    atexit(disableRawMode);

    struct termios raw = E.orig_termios;
    raw.c_iflag &= ~(BRKINT | INPCK | ISTRIP | IXON |
                     ICRNL); // IXON:ctrl-s/q | ICRNL:ctrl-m | BRKIINT
    raw.c_iflag &= ~(OPOST); // used to convert \n into \r\n
    raw.c_iflag &= ~(CS8);
    /* ctrl s stops data from being transmitted to the terminal until the user
     * presses ctrl q when ctrl v is pressed the terminal waits for the user to
     * type a char and sends it to the program directly ctrl m gets outputted as
     * 10 instead of 13 since the terminal converts  13 ('\r') into 10 ('\n')
     * '\r' used to move the cursor to the start of the line
     */
    raw.c_lflag &= ~(ECHO | ICANON | ISIG |
                     IEXTEN); // ICANON:canonical mode | ISIG:crtl-z/c signals |
                              // | IEXTEN:ctrl-v |
    // VMIN sets the min no of bytes read() needs before returning, VTIME the
    // max time to wait before read() returns
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 1;

    // tcsetattr() writes the new attributes back out <termios.h>
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1)
        die("tcsetattr");
}

int editorReadKey() {
    int nread;
    char c;
    // STDIN_FILENO representes the keyboard / terminal input
    while ((nread = read(STDIN_FILENO, &c, 1)) != 1) {
        if (nread == -1 && errno != EAGAIN)
            die("read");
    }
    if (c == '\x1b') {
        char seq[3];
        if (read(STDIN_FILENO, &seq[0], 1) != 1)
            return '\x1b';
        if (read(STDIN_FILENO, &seq[1], 1) != 1)
            return '\x1b';

        if (seq[0] == '[') {
            // if the char after [ is a number
            if (seq[1] >= '0' && seq[1] <= '9') {
                if (read(STDIN_FILENO, &seq[2], 1) != 1)
                    return '\x1b';
                // if the char after the number is a ~
                if (seq[2] == '~') {
                    switch (seq[1]) {
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
            } else {
                switch (seq[1]) {
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
        } else if (seq[0] == 'O') {
            switch (seq[1]) {
            case 'H':
                return HOME_KEY;
            case 'F':
                return END_KEY;
            }
        }

        return '\x1b';
    }
    return c;
}

int getCursorPosition(int *rows, int *cols) {
    char buf[32];

    if (write(STDOUT_FILENO, "\x1b[6n", 4) != 4)
        return -1;

    unsigned int i;
    for (i = 0; i < (sizeof(buf) - 1); i++) {
        if (read(STDOUT_FILENO, &buf[i], 1) != 1)
            break;
        if (buf[i] == 'R')
            break;
    }

    buf[i + 1] = '\0';

    if (buf[0] != '\x1b' || buf[1] != '[')
        return -1;
    char *p = &buf[2]; // string p skips the return and [ char
    char *next;        // stores the address / char where strol stopped
                       //
    *rows = (int)strtol(p, &next, 10); // strol stops when it reaches a non int
    if (*next != ';')
        return -1;

    p = next + 1; // update p's address

    *cols = (int)strtol(p, &next, 10);
    if (*next != 'R')
        return -1;

    return 0;
}

int getWindowSize(int *rows, int *cols) {
    struct winsize ws;

    // ioctl places the num of rows and columns into the given winsize struct
    // <sys/ioctl.h>
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
        // [C moves cursor left [B down, documentation prevents it from going
        // past the edge of terminal unlike [H
        if (write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) != 12)
            return -1;
        return getCursorPosition(rows, cols);
    } else {
        *cols = ws.ws_col;
        *rows = ws.ws_row;
        return 0;
    }
}
/*** file i/o ***/

void editorOpen() {
    char *line = "Hello World";
    ssize_t linelen = strlen(line) + 1;

    E.row.size = linelen;
    E.row.chars = malloc(linelen);
    memcpy(E.row.chars, line, linelen );
    E.numrows = 1;
}

/*** append buffer ***/

typedef struct {
    char *b;
    int len;
} abuf;

#define ABUF_INIT {NULL, 0}

void abAppend(abuf *ab, const char *s, int len) {
    // realloc takes in a pointer to the previous memory block and the size of
    // the new block
    char *new = realloc(ab->b, ab->len + len);

    if (new == NULL)
        return;
    //  takes in an address space, a pointer and n. It appends n btyes to the
    //  address

    memcpy(&new[ab->len], s, len);
    ab->b = new;
    ab->len += len;
}

void abFree(abuf *ab) { free(ab->b); }

/*** output ***/
void editorDrawRows(abuf *ab) {
    for (int i = 0; i < E.screenrows; i++) {
        if (i >= E.numrows) {
            if (i == E.screenrows / 3) {
                char welcome[80];
                // snprintf writes formated data into a buffer
                // pointer to buffer, max bytes to write, format, variables
                int welcomelen =
                    snprintf(welcome, sizeof(welcome), "Kilo Editor -- version %s",
                             KILO_VERSION);

                if (welcomelen > E.screencols)
                    welcomelen = E.screencols;
                int padding = (E.screencols - welcomelen) / 2;
                if (padding) {
                    abAppend(ab, "~", 1);
                    padding--;
                }
                while (padding--)
                    abAppend(ab, " ", 1);

                abAppend(ab, welcome, welcomelen);
            } else {
                abAppend(ab, "~", 1);
            }
        } else {
            int len = E.row.size;
            // if text exceeds the width of the terminal
            if (len > E.screencols) len = E.screencols; 
            abAppend(ab,E.row.chars, len);
        }
        abAppend(ab, "\x1b[K", 3);
        if (i < (E.screenrows - 1)) {
            abAppend(ab, "\r\n", 2);
        }
    }
}

void editorRefreshScreen() {
    abuf ab = ABUF_INIT;
    // clears the terminal screen and repositions the cursor \x1b (escape
    // character)
    abAppend(&ab, "\x1b[?25l", 6); // hides cursor
    abAppend(&ab, "\x1b[H", 3);    // [H reposition the cursor, by default [1;1H
                                   // [12;40H centers cursor in a 80x24 terminal

    editorDrawRows(&ab);

    char buf[32];
    snprintf(buf, sizeof(buf), "\x1b[%d;%dH", E.cy + 1, E.cx + 1);
    abAppend(&ab, buf, strlen(buf));

    abAppend(&ab, "\x1b[?25h", 6); // shows cursor

    write(STDOUT_FILENO, ab.b, ab.len);
    abFree(&ab);
}

/*** input ***/

void editorMoveCursor(int key) {
    switch (key) {
    case ARROW_LEFT:
        if (E.cx > 0) {
            E.cx--;
        }
        break;
    case ARROW_RIGHT:
        if (E.cx < (E.screencols - 1)) {
            E.cx++;
        }
        break;
    case ARROW_UP:
        if (E.cy > 0) {
            E.cy--;
        }
        break;
    case ARROW_DOWN:
        if (E.cy < (E.screenrows - 1)) {
            E.cy++;
        }
        break;
    }
}

void editorProcessKeypress() {
    int c = editorReadKey();

    switch (c) {
    case CTRL_KEY('q'):
        write(STDOUT_FILENO, "\x1b[2J", 4);
        write(STDOUT_FILENO, "\x1B[H", 3);
        exit(0);
        break;
    
    case HOME_KEY:
        E.cx = 0;
        break;

    case END_KEY:
        E.cx = (E.screencols - 1); 
        break;

    case PAGE_UP:
    case PAGE_DOWN:
        // need cruly brakets after switch inorder to declare a variable
        {
            int times = E.screenrows;
            while (times--)
                editorMoveCursor(c == PAGE_UP ? ARROW_UP : ARROW_DOWN);
        }
        break;

    case ARROW_LEFT:
    case ARROW_RIGHT:
    case ARROW_UP:
    case ARROW_DOWN:
        editorMoveCursor(c);
        break;
    }
}

void initEditor() {
    E.cx = 0;
    E.cy = 0;
    E.numrows = 0;

    if (getWindowSize(&E.screenrows, &E.screencols) == -1)
        die("getWindowSize");
}

int main() {
    enableRawMode();
    initEditor();
    editorOpen();

    while (1) {
        editorRefreshScreen();
        editorProcessKeypress();
    }

    return 0;
}
