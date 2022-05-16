/*
 * adder.c - a minimal CGI program that adds two numbers together
 */
/* $begin adder */
#include "../csapp.h"

int main(void) {
    char *buf, *p, *method;
    char content[MAXLINE];
    int n1=0, n2=0;

    if((buf = getenv("QUERY_STRING")) != NULL) {
        p = strchr(buf, '&');
        *p = '\0';
        sscanf(buf, "first=%d", &n1);
        sscanf(p+1, "second=%d", &n2);
    }

    method = getenv("REQUEST_METHOD");

    sprintf(content, "QUERY_STRING=%s", buf);
    sprintf(content, "Welcome to add.com: ");
    sprintf(content, "%sTHE Internet addition portal. \r\n<p>", content);
    sprintf(content, "%sThe answer is: %d + %d = %d\r\n<p>", content, n1, n2, n1 + n2);
    sprintf(content, "%sThanks for visiting\r\n", content);

    printf("Connection: close\r\n");
    printf("Content-length: %d\r\n", (int)strlen(content));
    printf("Content-type: text/html\r\n\r\n");

    if (strcasecmp(method, "HEAD") != 0){
        printf("%s", content);
    }

    fflush(stdout);

    exit(0);
}
/* $end adder */
