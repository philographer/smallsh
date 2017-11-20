#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int join(char *com1[], char*com[]);

int main() {
    char *one[4] = {"ls", "-l", "/usr/lib", NULL};
    char *two[3] = {"grep", "^d", NULL};
    int ret;

    ret = join(one, two);
    printf("join returned %d\n", ret);
    return 0;
}

int join(char *com1[], char *com2[]) {
    int p[2], status;

    switch (fork()) {
        case -1: 
            printf("1st fork call in join");
        case 0: 
            break;
        default: 
            wait(&status); 
            return (status);
    }
    if(pipe(p) == -1) printf("pipe error\n");

    switch(fork()) {
        case -1: 
            printf("2st fork call in join");
        case 0:
            dup2(p[1], 1); /*표준 출력이 파이프로 가게함*/
            close(p[0]);
            close(p[1]);
            execvp(com1[0], com1);
            printf("1st execvp call in join");
        default: 
            dup2(p[0], 0);
            close(p[0]);
            close(p[1]);
            execvp(com2[0], com2); /* com2: grep */
            printf("2nd execvp call in join");            
    }
}
