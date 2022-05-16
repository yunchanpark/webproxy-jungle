#include "csapp.h"

/*  트랜잭션 처리함수, GET Request이나 HEAD Request 들어오면 정적인지 동적인지 파악하여 각각의 함수를 실행 */
void doit(int fd);

/* 요청 헤더를 읽고 무시 */
void read_requesthdrs(rio_t *rp, int fd);

/* filename(파일 경로)과 cgiargs(쿼리 스트링)값 세팅 */
int parse_uri(char *uri, char *filename, char *cgiargs);

/* 찾은 정적 컨텐츠 값 fd에 쓰기 */
void serve_static(int fd, char *filename, int filesize, char *method);

/* filetype을 찾아서 MIME타입으로 바꿔주기 */
void get_filetype(char *filename, char *filetype);

/* 찾은 동적 컨텐츠 값 fd자에 쓰기 */
void serve_dynamic(int fd, char *filename, char *cgiargs, char *method);

/* 클라이언트에 보내줄 에러 페이지 fd에 쓰기 */
void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg);

/* 요청 헤더 출력해주는 함수 */
void echo(int fd);

/*
* argc: 메인 함수에 전달 되는 데이터의 수
* argv: 메인 함수에 전달 되는 실질적인 정보
*/
int main(int argc, char **argv) {
    /* 프로그램 실행 시 port를 안썼으면 */
    if (argc != 2) {
        fprintf(stderr, "usage: %s <port>\n", argv[0]);
        exit(1);
    }
    
    int listenfd, connfd;                  /* listen 식별자, connfd 식별자 */
    char hostname[MAXLINE], port[MAXLINE]; /* hostname: 접속한 클라이언트 ip, port: 접속한 클라이언트 port */
    socklen_t clientlen;                   /* socklen_t 는 소켓 관련 매개 변수에 사용되는 것으로 길이 및 크기 값에 대한 정의를 내려준다 */
    struct sockaddr_storage clientaddr;    /* 어떤 타입의 소켓 구조체가 들어올지 모르기 때문에 충분히 큰 소켓 구조체로 선언 */

    /* listenfd: 이 포트에 대한 듣기 소켓 오픈 */
    listenfd = Open_listenfd(argv[1]);
    
    while (1) {
        clientlen = sizeof(clientaddr);                                                 /* 소켓 구조체 크기 */
        connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen);                       /* 연결 요청 큐에 아무것도 없을 경우 기본적으로 연결이 생길때까지 호출자를 막아둠, 즉 대기 상태 */
        
        Getnameinfo((SA *)&clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE, 0); /* clientaddr: SA 구조체로 형변환, 소켓 정보를 가져옴 */
        printf("Accepted connection from (%s, %s)\n", hostname, port);                  /* 어떤 주소와 포트 번호를 가진 client가 들어왔는지 print */
        // echo(connfd);
        doit(connfd);                                                                   /* 트랜잭션 수행 */
        
        /* 연결이 끝났다고 print 하고 식별자(파일 디스크립트)를 닫아줌 */
        printf("endoffile\n");
        Close(connfd);
    }
}

/* 요청해더 출력해주는 함수 */
void echo(int connfd) {
    size_t n;           /* 한 라인의 수 담을 배열 */
    char buf[MAXLINE];  /* MAXLINE: 8192만큼 배열을 만듬(사용자 버퍼) */
    rio_t rio;          /* rio_t 구조체 만듬(내부 버퍼를 사용하기 위함) */

    /* rio 초기화 */
    Rio_readinitb(&rio, connfd);

    /* 한 라인의 수가 0이 아닐 때 반복, 즉, client가 더이상 요청을 하지 않을 때 멈춤 */
    while ((n = Rio_readlineb(&rio, buf, MAXLINE)) != 0) {
        if (strcmp(buf, "\r\n") == 0) {
            break;
        }
        Rio_writen(connfd, buf, n);
    }
}


/*
* 파일 정보를 저장하는 구조체
* struct stat { 
*   dev_t     st_dev;     장치 파일의 위치 및 여부를 기술
*   ino_t     st_ino;     파일의 inode 번호
*   mode_t    st_mode;    파일의 모드를 다룸
*   nlink_t   st_nlink;   파일의 하드링크 수
*   uid_t     st_uid;     user ID
*   gid_t     st_gid;     group ID
*   dev_t     st_rdev;    장치 파일 (inode)를 기술
*   off_t     st_size;    파일의 사이즈
*   blksize_t st_blksize; 효율적인 I/O 파일 시스템을 위한 블록 사이즈
*   blkcnt_t  st_blocks;  파일에 할당한 블록 수
*   time_t    st_atime;   파일의 마지막 접속 시간
*   time_t    st_mtime;   파일의 마지막 수정시간
*   time_t    st_xtime;   파일의 마지막 상태 변경 시간
* }
*/

/*
* 파일 디스크립터 정보는 물론 내부적인 임시 버퍼와 관련된 정보들도 포함
* struct rio_t {
*   int rio_fd                   기존 파일 디스크립터 정보
*   int rio_cnt                  내부 버퍼의 읽은 바이트 수
*   char *rio_bufptr             내부 버퍼에서 읽고 쓰는 위치를 지정하기 위한 포인터
*   char rio_buf[RIO_BUFSIZE]    내부 버퍼
* } 
*/
void doit(int fd) {
    int is_static;                                                      /* 정적 요청인지 동적 요청인지 판단할 변수 */
    struct stat sbuf;                                                   /* 파일 정보 담을 변수 */
    char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE]; /* buf: request 헤더 정보 담을 공간*/
    char filename[MAXLINE], cgiargs[MAXLINE];                           /* filename: 파일이름, cgiargs: 쿼리 스트링 담을 공간 */
    rio_t rio;

    Rio_readinitb(&rio, fd);                                            /* rio 구조체 초기화 */
    Rio_readlineb(&rio, buf, MAXLINE);                                  /* buf에 fd에서 읽을 값이 담김 */
                                       
    printf("\n Request headers: \n");
    printf("%s", buf);
    sscanf(buf, "%s %s %s", method, uri, version);                      /* sscanf는 첫 번째 매개 변수가 우리가 입력한 문자열, 두 번째 매개 변수 포맷, 나머지 매개 변수에 포맷에 맞게 데이터를 읽어서 인수들에 저장 */

    /* GET이나 HEAD가 아닐 때 error 메시지 출력 */
    if (strcasecmp(method, "GET") && strcasecmp(method, "HEAD")) {
        clienterror(fd, method, "501", "Not implemented", "Tiny does not implement this method");
        return;
    }

    /* write를 할 때, 헤더에 쓸 수 없기 때문에 파일 포인터를 데이터를 쓸 수 있는 데로 옮기기 위해 읽으면서 포인터 이동, 즉, 요청 헤더는 무시 */
    read_requesthdrs(&rio, fd);
    /* parse_uri은 filename(파일 경로)과 cgiargs(쿼리 스트링)값 세팅하는데 정적 컨텐츠이면 return 1, 동적 컨텐츠이면 return 0 */
    is_static = parse_uri(uri, filename, cgiargs);  
    if (stat(filename, &sbuf) < 0) {
        clienterror(fd, filename, "404", "Not found", "Tiny couldn't find this file");
        return;
    }

    /* 정적 컨텐츠를 요구하는 경우 */
    if (is_static) {
        /* 파일이 일반 파일인지, 읽기 권한이 있는 파일인지 검사 */
        if (!(S_ISREG(sbuf.st_mode)) || !(S_IRUSR & sbuf.st_mode)) {
            clienterror(fd, filename, "403", "Forbidden", "Tiny couldn't read the file");
        }
        /* 위 검사를 통과 했다면, client에게 파일 제공 */
        serve_static(fd, filename, sbuf.st_size, method);
    } 
    /* 동적 컨텐츠를 요구하는 경우 */
    else {
        /* 파일이 일반 파일인지, 실행 권한이 있는 파일인지 검사 */
        if (!(S_ISREG(sbuf.st_mode)) || !(S_IXUSR & sbuf.st_mode)) {
            clienterror(fd, filename, "403", "Forbidden", "Tiny couldn't run the CGI program");
            return;
        }
        /* 위 검사를 통과 했다면, client에게 파일 제공 */
        serve_dynamic(fd, filename, cgiargs, method);
    }
}

/* client에게 오류 보고하기 */
void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg) {
    char buf[MAXLINE], body[MAXBUF];

    /* resoponse할 body build */
    sprintf(body, "<html><title>Tiny Error</title>");
    sprintf(body, "%s<body bgcolor=" "ffffff"">\r\n", body);
    sprintf(body, "%s%s: %s\r\n", body, errnum, shortmsg);
    sprintf(body, "%s<p>%s: %s\r\n", body, longmsg, cause);
    sprintf(body, "%s<hr><em>The Tiny Web server</em>\r\n", body);

    /* response header */
    sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Content-type: text/html\r\n");
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Content-length: %d\r\n\r\n", (int)strlen(body));
    Rio_writen(fd, buf, strlen(buf));
    /* response body */
    Rio_writen(fd, body, strlen(body));
}

/* 요청헤더 읽기 */
void read_requesthdrs(rio_t *rp, int fd) {
    char buf[MAXLINE];

    Rio_readlineb(rp, buf, MAXLINE); /* MAXLINE 까지 읽기 */
    printf("%s", buf);

    /* \r\n 나올때까지 계속 읽기, 즉, 헤더의 끝 */
    while (strcmp(buf, "\r\n")) {
        Rio_readlineb(rp, buf, MAXLINE);
        printf("%s", buf);
    }
    return;
}

int parse_uri(char *uri, char *filename, char *cgiargs)
{
    char *ptr;
    //cgi-bin가 없다면
    if (!strstr(uri, "cgi-bin")) /* Static content */
    {
        strcpy(cgiargs, "");
        strcpy(filename, "."); // ./uri/home.html 가 된다.
        strcat(filename, uri);
        if (uri[strlen(uri) - 1] == '/')
            strcat(filename, "home.html");
        return 1;
    }
    else /* Dynamic content */
    {
        ptr = index(uri, '?'); // /cgi-bin/adder?a=1&b=1
        //CGI 인자 추출
        if (ptr)
        {
            //물음표 뒤에 인자 다 같다 붙인다.
            strcpy(cgiargs, ptr + 1);
            // 포인터는 문자열 마지막으로 바꾼다.
            *ptr = '\0'; //uri 물음표 뒤 다 없애기
        }
        else
            strcpy(cgiargs, ""); // 물음표 뒤 녀석들 전부 넣어주기

        // 나머지 부분 상대 URI로 바꿈, 나중에 이 서버의 uri가 뭔지 확실히 알아보자
        strcpy(filename, "."); // ./cgi-bin/adder
        strcat(filename, uri); // ./uri 가 된다.
        return 0;
    }
}

// fd 응답받는 소켓(연결식별자), 파일 이름, 파일 사이즈
void serve_static(int fd, char *filename, int filesize, char *method) {
    rio_t file_rio;                                 /* 찾은 파일 디스크립트(식별가)와 정보를 담을 rio 구조체 */
    int srcfd;                                      /* 찾은 파일 디스크립트(식별자) 담을 변수 */
    char filetype[MAXLINE], buf[MAXBUF];            /* filetype: MIME 타입을 담을 변수, buf: response될 내용을 담고 있는 임시 버퍼 */
    char *file;                                     /* 파일의 내용을 임시로 담을 포인터 변수 */

    /* 파일 접미어 검사해서 파일 이름에서 타입 가지고 오기 */
    get_filetype(filename, filetype);
    printf("Response header:\n");

    /* response header */
    sprintf(buf, "HTTP/1.0 200 OK\r\n");
    sprintf(buf, "%sServer: Tiny Web Server\r\n", buf);
    sprintf(buf, "%sConnection: close\r\n", buf);
    sprintf(buf, "%sContent-length: %d\r\n", buf, filesize);
    sprintf(buf, "%sContent-type: %s\r\n\r\n", buf, filetype);
    Rio_writen(fd, buf, strlen(buf));

    printf("%s", buf);  /* 서버에 출력 */

    /* GET함수 일 때, HEAD는 파일의 내용을 담으면 안됨 */
    if (!strcmp(method, "GET")) {
        srcfd = Open(filename, O_RDONLY, 0);    /* filename(경로)에서 읽을 수만 있는 파일로 열기, return 파일 디스크립트 */
        file = (char *)malloc(filesize);        /* file 내용을 임시로 저장할 메모리 할당 */

        Rio_readinitb(&file_rio, srcfd);        /* rio_t 구조체 srcfd 식별자로 초기화 */
        Rio_readnb(&file_rio, file, filesize);  /* file에 file크기 만큼 다 저장 */
        Rio_writen(fd, file, filesize);         /* fd식별자에 file의 내용을 file의 크기만큼 쓰기 */

        Close(srcfd);                           /* srcfd 닫기 */
        free(file);                             /* 할당한 file 메모리 할당 해제 */
    }
}

/* get_filetype - 파일 MIME 타입 결정하기 */
void get_filetype(char *filename, char *filetype) {
    if (strstr(filename, ".html")) {
        strcpy(filetype, "text/html");
    }
    else if (strstr(filename, ".gif")) {
        strcpy(filetype, "image/gif");
    }
    else if (strstr(filename, ".png")) {
        strcpy(filetype, "image/png");
    }
    else if (strstr(filename, ".jpg")) {
        strcpy(filetype, "image/jpeg");
    }
    else if (strstr(filename, ".mp4")) {
        strcpy(filetype, "video/mp4");
    }
    else if (strstr(filename, ".mpeg")) {
        strcpy(filetype, "video/mpeg");
    }
    else
        strcpy(filetype, "text/plain");
}

void serve_dynamic(int fd, char *filename, char *cgiargs, char *method) {
    char buf[MAXLINE], *emptylist[] = {NULL};   /* buf: response header에 담을 정보를 담고 있는 임시 버퍼, emptylist: agrv의 역할을 할 배열 */
    /* response header */
    sprintf(buf, "HTTP/1.0 200 OK\r\n");
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Server: Tiny Web Server yunchan yunchan\r\n");
    Rio_writen(fd, buf, strlen(buf));
    
    /* 자식 생성 이 아래 내용은 각각 실행 자식만 조건 문 안에서 실행 */
    if (Fork() == 0) {
        /* 환경변수 세팅, 즉, QUERY_STRING에 cgiargs(쿼리스트링) 세팅, 마지막 매개변수가 0이면 덮어쓰기 불가능 1이면 덮어쓰기 가능 */
        setenv("QUERY_STRING", cgiargs, 1); // 1&1
        setenv("REQUEST_METHOD", method, 1);
        /* 자식의 표준 출력에 fd를 덮어 씌움, STDOUT_FILENO: 1(즉,표준 파일 식별자) */
        Dup2(fd, STDOUT_FILENO);
        /* 
        *   첫 번째 매개 변수는 실행할 파일 경로 
        *   두 번째 매개 변수는 agrv의 역할 처음이 NULL인 이유는 여기서 실행 할 때는 실행하기 위해 작성하는 처음 기본 실행문이 담기지 않기 때문
        *   세 번째 매개 변수는 agrv에 담을 값을 저장하고 있는 환경 변수
        */
        Execve(filename, emptylist, environ);
    }
    /* 자식 끝날때까지 기다림 */
    Wait(NULL);
}