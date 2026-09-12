/***    includes ***/
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

/*** defines ***/
#define CTRL_KEY(k) ((k) & 0x1f)

/*** data ***/
// struct used to store editor state
typedef struct {
    int screenrows;
    int screencols;
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

char editorReadKey() {
    int nread;
    char c;
    // STDIN_FILENO representes the keyboard / terminal input
    while ((nread = read(STDIN_FILENO, &c, 1)) != 1) {
        if (nread == -1 && errno != EAGAIN)
            die("read");
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

/*** output ***/
void editorDrawRows() {
    for (int i = 0; i < E.screenrows; i++) {
        write(STDOUT_FILENO, "~\r\n", 3);
        if (i == (E.screenrows - 1)) {
            write(STDOUT_FILENO, "~", 1);
        }
    }
}

void editorRefreshScreen() {
    // STDOUT_FILENO represents the screen of the terminal
    // clears the terminal screen and repositions the cursor \x1b (escape
    // character)
    write(STDOUT_FILENO, "\x1b[2J", 4); // [2J clears the entire screen
    write(STDOUT_FILENO, "\x1B[H",
          3); // [H reposition the cursor, by default [1;1H
              // [12;40H centers cursor in a 80x24 terminal

    editorDrawRows();

    write(STDOUT_FILENO, "\x1B[H", 3);
}

/*** input ***/

void editorProcessKeypress() {
    char c = editorReadKey();

    switch (c) {
        case CTRL_KEY('q'):
            write(STDOUT_FILENO, "\x1b[2J", 4);
            write(STDOUT_FILENO, "\x1B[H", 3);
            exit(0);
            break;
    }
}

void initEditor() {
    if (getWindowSize(&E.screenrows, &E.screencols) == -1)
        die("getWindowSize");
}

int main() {
    enableRawMode();
    initEditor();

    while (1) {
        editorRefreshScreen();
        editorProcessKeypress();
    }

    return 0;
}
