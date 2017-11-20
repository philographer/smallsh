#include "smallsh.h" /* include file for example */
#include <stdlib.h>
#include <signal.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <pwd.h>
#include <errno.h>
#include <fcntl.h>


/* program buffers and work pointers */
static char inpbuf[MAXBUF], tokbuf[2*MAXBUF], *ptr = inpbuf, *tok = tokbuf;
int intr_p = 0;
char *prompt = "Command>"; /* prompt */
char curdir[MAXBUF];
const char *homedir;
int fg_pid = 0;

struct specialStruct {
    int index[2];
    int found;
};

int getLength(char **cline) {
    int n = sizeof(cline)/sizeof(cline[0]);
    printf("n is %d", n);
    return n;
}


int userin(char *p) /* print prompt and read a line */
{
    int c, count;
    /* initialization for later routines */
    ptr = inpbuf;
    tok = tokbuf;
    
    /* display prompt */
    printf("%s ", p);
    
    for(count = 0;;){
        if((c = getchar()) == EOF)
            return(EOF);
        
        if(count < MAXBUF)
            inpbuf[count++] = c;
        
        if(c == '\n' && count < MAXBUF){
            inpbuf[count] = '\0';
            return(count);
        }
        
        /* if line too long restart */
        if(c == '\n'){
            printf("smallsh: input line too long\n");
            count = 0;
            printf("%s ", p);
        }
    }
}

static char special[] = {' ', '\t', '&', ';', '\n', '\0'};

int inarg(char c) /* are we in an ordinary argument */
{
    char *wrk;
    for(wrk = special; *wrk != '\0'; wrk++)
        if(c == *wrk)
            return(0);
    
    return(1);
}

int gettok(char **outptr) /* get token and place into tokbuf */
{
    int type;
    
    *outptr = tok;
    
    /* strip white space */
    for(;*ptr == ' ' || *ptr == '\t'; ptr++)
        ;
    
    *tok++ = *ptr;
    
    switch(*ptr++){
        case '\n':
            type = EOL; break;
        case '&':
            type = AMPERSAND; break;
        case ';':
            type = SEMICOLON; break;
        default:
            type = ARG;
            while(inarg(*ptr))
                *tok++ = *ptr++;
    }
    
    *tok++ = '\0';
    return(type);
}

void handle_int(int signo) {
    if(!fg_pid) {
        switch (signo) {
            case SIGINT:
                printf("ctrl-c hit at shell prompt\n");
                break;
            case SIGQUIT:
                printf("ctrl-\\ hit at shell prompt\n");
                break;
            case SIGTSTP:
                printf("Ctrl-z hit at shell prompt\n");
                break;
            default:
                printf("signal at shell prompt\n");
                break;
        }
        
        /* ctrl-c hit at shell prompt */
        return;
    }
    if(intr_p) {
        printf("\ngot it, signalling\n");
        kill(fg_pid, SIGTERM);
        fg_pid = 0;
    } else {
        printf("\nignoring, type ^C again to interrupt\n");
        intr_p = 1;
    }
}

void getHomeDir() {
    if ((homedir = getenv("HOME")) == NULL) {
        homedir = getpwuid(getuid())->pw_dir;
    }
    // printf("home directory: %s\n", homedir);
}

void changeDir(char **cline) {
    char temp_curdir[MAXBUF];
    // copy current dir
    strcpy(temp_curdir, curdir);
    
    // case 1. cd (Only) => homedir
    if(cline[1] == NULL || cline[1] == '\0') {
        strcpy(temp_curdir, homedir);
    } else {
        // case 2. start with / ($ cd / || cd /var/ like)
        const char firstChar = *cline[1];
        
        if(firstChar == '/' || strcmp(temp_curdir, "/") == 0) { // case 2. start root
            if(strcmp(cline[1], "/") == 0) { // case 2(1). $ cd /
                strcpy(temp_curdir, "/");
            } else { // $ case 2(2). cd /bla/bla/bla
                strcpy(temp_curdir, cline[1]);
            }
        } else { // case 3. cd "blaDir"
            strcat(temp_curdir, "/");
            strcat(temp_curdir, cline[1]);
        }
    }
    
    printf("to: %s\n", temp_curdir);
    
    int nResult = chdir(temp_curdir);
    if(nResult == 0) { // 이동 성공
        getcwd(curdir, sizeof(curdir));
    } else {
        printf("No such directory: %s\n", temp_curdir);
    }
}

// 파이프가 포함되어 있는지 아닌지 구별
struct specialStruct isPipe(char **cline, int narg) {
    struct specialStruct _special = {};
    
    // 구조체 초기화
    _special.found = 0;
    _special.index[0] = -1;
    _special.index[1] = -1;
    int i;
    int idx = 0;
    
    for(i=0; i < narg; i++) {
        if(strcmp(cline[i], "|") == 0) {
            _special.found = 1;
            _special.index[idx] = i;
            idx++;
        }
    }
    
    return _special;
}

// 리다이렉션이 포함되어 있는지 아닌지 구별
struct specialStruct isRedirect(char **cline, int narg) {
    struct specialStruct _special = {};
    
    // 구조체 초기화
    _special.found = 0;
    _special.index[0] = -1;
    _special.index[1] = -1;
    int i;
    int idx = 0;
    
    for(i=0; i < narg; i++) {
        if(strcmp(cline[i], ">") == 0 || strcmp(cline[i], "<") == 0) {
            _special.found = 1;
            _special.index[idx] = i;
            idx++;
        }
    }
    
    return _special;
}



/***
 ex) ls -l /usr/lib | grep ^d
 ex) ls ~/ grep Down
 ***/
void singlePipe(char *com1[], char *com2[]) {
    int p[2], status;
    
    if(pipe(p) == -1) printf("pipe error\n");
    
    switch(fork()) {
        case -1:
            printf("Error: fork call in singlePipe\n");
        case 0: // 자식
            dup2(p[1], 1); /*표준 출력이 파이프로 가게함*/
            close(p[0]);
            close(p[1]);
            execvp(com1[0], com1);
            printf("Error: 1st execvp call in singlePipe\n");
        default: // 부모
            dup2(p[0], 0); /* 표준 입력이 파이프로부터 오게함 */
            close(p[0]);
            close(p[1]);
            execvp(com2[0], com2); /* com2: grep */
            printf("Error: 2nd execvp call in singlePipe\n");
            printf("errno: %d", errno);
    }
}

/***
 ex) ls –al > tmp.txt (right, out)
 ex) sort < ./tmp.txt (변경 전, tmp.log < dmesg) (left, in)
 ***/
void singleRedirect(char *com1[], char *com2[], int isRightHand) {
    int fd, p[2], status;
    
    switch(fork()) {
        case -1:
            printf("Error: fork call in join\n");
        case 0: // 자식
            if(isRightHand) { // ">" out
                fd = open(com2[0] , O_WRONLY | O_CREAT | O_TRUNC, 0644);
                dup2(fd, STDOUT_FILENO); // 표준 출력이 파일로 오게함
                close(fd);
                execvp(*com1, com1);
            } else { // "<" in
                fd = open(com2[0], O_RDONLY);
                dup2(fd, STDIN_FILENO); // 표준 입력이 파일로 오게 함
                close(fd);
                execvp(*com1, com1);
                // printf("Error: parent execvp call in singleRedirect\n");
            }
            // execvp(com2[0], com2);
        default: // 부모
            wait(NULL);
            
            // execvp(com1[0], com1);
            // printf("Error: 2nd execvp call in join\n");
            // printf("errno: %d\n", errno);
    }
}

// pipe가 포함된 실행구문 실행
void procPipe(char **cline, int narg, int *pipeIdx) {
    char *one[MAXBUF];
    char *two[MAXBUF];
    char *three[MAXBUF];
    
    if((pipeIdx[0] != -1) && (pipeIdx[1] == -1)) {
        // case 1. single pipe
        int i=0;
        
        // for one args
        for(i=0; i<pipeIdx[0]; i++) {
            one[i] = cline[i];
        }
        one[i] = NULL;
        
        // for two args
        int i2=0;
        for(i=pipeIdx[0]+1; i<narg; i++) {
            two[i2] = cline[i];
            i2++;
        }
        two[i2] = NULL;
        
        singlePipe(one, two);
        
    } else if(pipeIdx[0] != -1 && pipeIdx[0] != -1) {
        // case 2. double pipe
        
    
    } else {
        // printf("*cline: %s \n", *cline);
        // printf("narg: %d \n", narg);
        // printf("pipeIdx[0]: %d \n", pipeIdx[0]);
        // printf("pipeIdx[1] %d \n", pipeIdx[1]);
        printf("pipe wrong case! \n");
    }
}

// redirect가 포함된 실행구문 실행
void procRedirect(char **cline, int narg, int *redirectIdx) {
    char *one[MAXBUF];
    char *two[MAXBUF];
    

    // case 1. redirect >
    int i=0;
    
    // for one args
    for(i=0; i<redirectIdx[0]; i++) {
        one[i] = cline[i];
    }
    one[i] = NULL;
    
    // for two args
    int i2=0;
    for(i=redirectIdx[0]+1; i<narg; i++) {
        two[i2] = cline[i];
        i2++;
    }
    two[i2] = NULL;
    
    // printf("one[0]: %s \n", one[0]);
    // printf("one[1]: %s \n", one[1]);
    // printf("one[1]: %s \n", one[2]);
    // printf("two[0]: %s \n", two[0]);
    // printf("two[1]: %s \n", two[1]);
    
    
    if(strcmp(cline[redirectIdx[0]], "<") == 0) {
        // case 1. redirect <, in, left
        // printf("left side\n");
        singleRedirect(one, two, 0); // left side
    } else if(strcmp(cline[redirectIdx[0]], ">") == 0){
        // case 2. redirect >, out, right
        // printf("right side\n");
        singleRedirect(one, two, 1); // right side
    } else {
        printf("redirect wrong case! \n");
    }
    
}

/* execute a command with optional wait */
int runcommand(char **cline, int where, int narg)
{
    int pid, exitstat, ret;
    
    /* ignore signal (linux) */
    struct sigaction sa_ign, sa_conf;
    sa_ign.sa_handler = SIG_IGN; // 시그널 무시
    sigemptyset(&sa_ign.sa_mask);
    sa_ign.sa_flags = SA_RESTART; // 시그널 핸들러에 의해 중지된 시스템 호출을 자동적으로 재시작한다.
    
    sa_conf.sa_handler = handle_int; // 부모의 SIGINT, SIGQUIT 시그널이 발생할 때 까지 기다림.
    sigemptyset(&sa_conf.sa_mask);
    sa_conf.sa_flags = SA_RESTART;
    
    // IF CD Command
    if (strcmp(*cline, "cd") == 0){
        changeDir(cline);
        return 0;
    }
    
    // IF EXIT command
    if(strcmp(*cline, "exit") == 0) {
        exit(0);
    }
    
    if((pid = fork()) < 0){
        perror("smallsh fork error");
        return(-1);
    }
    
    if(pid == 0){ // child
        sigaction(SIGINT, &sa_ign, NULL); // 자식은 시그널 무시(SIG_IGN)
        sigaction(SIGQUIT, &sa_ign, NULL);
        sigaction(SIGTSTP, &sa_ign, NULL);
        
        
        struct specialStruct isPipeResult= isPipe(cline, narg);
        struct specialStruct isRedirectResult = isRedirect(cline, narg);
        
        
        if(isPipeResult.found == 1) {
            // printf("is pipe!!!!!\n");
            // Case 1. cline has pipe(|)
            
            // Case 1 (1). 단일 파이프
            
            // printf("cline is: %s\n", *cline);
            // printf("narg is: %d\n", narg);
            // printf("index[0] is: %d\n", isPipeResult.index[0]);
            // printf("index[1] is: %d\n", isPipeResult.index[1]);
            procPipe(cline, narg, isPipeResult.index);
            
            
            // Case 1 (2). 복수 파이프
            // printf("pipe founded \n");
            
        } else if(isRedirectResult.found == 1) {
            // Case 2. cline has redirect(>,<)
            procRedirect(cline, narg, isRedirectResult.index);
            // printf("redirect founded\n");
            
        } else {
            // Case 3. Normal Case
            // printf("normal founded\n");
            execvp(*cline, cline);
            perror(*cline);
        }
    
        exit(127);
    } else { // parent
        fg_pid = pid;
    }
    
    /* code for parent */
    /* if background process print pid and exit */
    if(where == BACKGROUND){
        fg_pid = 0;
        printf("[Process id %d]\n", pid);
        return(0);
    }
    
    /* wait until process pid exits */
    sigaction(SIGINT, &sa_conf, NULL); // 부모 시그널 핸들링(handle_int)
    sigaction(SIGQUIT, &sa_conf, NULL);
    sigaction(SIGTSTP, &sa_conf, NULL);
    
    while( (ret=wait(&exitstat)) != pid && ret != -1)
        ;
    
    fg_pid = 0;
    return(ret == -1 ? -1 : exitstat);
}

void procline() /* process input line */
{
    char *arg[MAXARG+1]; /* pointer array for runcommand */
    int toktype; /* type of token in command */
    int narg; /* numer of arguments so far */
    int type; /* FOREGROUND or BACKGROUND? */
    
    /* reset intr flag */
    intr_p = 0;
    
    for(narg = 0;;){ /* loop forever */
        /* take action according to token type */
        switch(toktype = gettok(&arg[narg])){
            case ARG:
                if(narg < MAXARG)
                    narg++;
                break;
                
            case EOL: // EOL OR
            case SEMICOLON: // SEMICOLON OR
            case AMPERSAND: // AMPERSAND CASE THEN
                type = (toktype == AMPERSAND) ? BACKGROUND : FOREGROUND;
                if(narg != 0){
                    arg[narg] = NULL;
                    runcommand(arg, type, narg);
                }
                
                if(toktype == EOL)
                    return;
                
                narg = 0;
                break;
        }
    }
}

void fpe(int signo){
    if(signo==SIGINT){ // SIGINT, Ctrl-C, 기본적으로 프로세스가 실행을 유예시키는 역할을 한다.
        printf("Cauht SIGINT\n");
    } else if(signo==SIGQUIT){ // SIGQUIT, Ctrl-\, 기본적으로 프로세스를 종료시킨 뒤 코어를 덤프하는 역할을 한다.
        printf("Caught SIGQUIT\n");
    } else if(signo==SIGTSTP) { // SIGTSTP, Ctrl-Z, 기본적으로 프로세스가 실행을 유예시키는 역할을 한다.
        printf("Caught SIGTSTP\n");
    }
}

int main(int argc, char *argv[])
{
    /* sigaction struct (linux) */
    struct sigaction sa;
    
    // Home Directory Initialize
    getHomeDir();
    
    // Current Working Directory Initialize
    getcwd(curdir, sizeof(curdir));
    
    // 시그널 핸들러 생성
    sa.sa_handler = fpe;
    sigfillset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    
    // 시그널 핸들러 등록
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGQUIT, &sa, NULL);
    sigaction(SIGTSTP, &sa, NULL);
    
    while(userin(prompt) != EOF)
        procline();
}


