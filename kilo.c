#include <errno.h>
#include <stdio.h>
#include <ctype.h>
#include <stdlib.h>
#include <termios.h>
#include <unistd.h>

#define CTRL_KEY(k) ((k) & 0x1f)

struct termios orig_termios;

void editorRefreshScreen() {
    // STDOUT_FILENO represents the screen of the terminal 
    // clears the terminal screen 
    write(STDOUT_FILENO, "\x1b[2J", 4);
    // changes the cursor position
    write(STDOUT_FILENO, "\x1B[H", 3);
}

void die(const char *s) {
    editorRefreshScreen(); 
    perror(s); // looks at the errno variable and prints an error message for it <stdlib.h> 
    exit(1); // <stdlib.h>
}

void disableRawMode() {
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
    die("tcsetattr");
}
/* In the default canonical mode the terminal processes input line by line (allowing the user to edit text before 
 * sending it) In Raw mode, every single keypress is instantly delivered to the program 
 */
void enableRawMode() {
    // tcgetattr() is used the read the current attibutes into a struct <termios.h>   
    if (tcgetattr(STDIN_FILENO, &orig_termios) == -1) die("tcgetattr");
    // used to run the disableRawMode function as soon as the program exits <stdlib.h> 
    atexit(disableRawMode);


    struct termios raw = orig_termios;
    raw.c_iflag &= ~(BRKINT | INPCK | ISTRIP | IXON | ICRNL);// IXON:ctrl-s/q | ICRNL:ctrl-m | BRKIINT  
    raw.c_iflag &= ~(OPOST); // used to convert \n into \r\n 
    raw.c_iflag &= ~(CS8);
    /* ctrl s stops data from being transmitted to the terminal until the user presses ctrl q
     * when ctrl v is pressed the terminal waits for the user to type a char and sends it to the program directly 
     * ctrl m gets outputted as 10 instead of 13 since the terminal converts  13 ('\r') into 10 ('\n') 
     * '\r' used to move the cursor to the start of the line 
     */
    raw.c_lflag &= ~(ECHO | ICANON | ISIG | IEXTEN); // ICANON:canonical mode | ISIG:crtl-z/c signals | | IEXTEN:ctrl-v | 
    // VMIN sets the min no of bytes read() needs before returning, VTIME the max time to wait before read() returns 
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 1;
    
    // tcsetattr() writes the new attributes back out <termios.h> 
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) die("tcsetattr");
    
}

char editorReadKey() {
    int nread;
    char c;
    // STDIN_FILENO representes the keyboard / terminal input 
    while ((nread = read(STDIN_FILENO, &c, 1)) == -1) {
        if (nread == -1 && errno != EAGAIN) die("read");
    }
    return c;
}

void editorProcessKeypress() {
    char c = editorReadKey();

    switch(c) {
        case CTRL_KEY('q'):
            editorRefreshScreen(); 
            exit(0);
            break;
    }
}



int main() {
    enableRawMode();

    while (1) {
        editorRefreshScreen();
        editorProcessKeypress();
    }

    return 0;
}
