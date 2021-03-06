#include <sys/types.h>
#include <pthread.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <iostream>
#include <unistd.h>
#include <sys/stat.h>

#define BLACKLOG 5
#define IsSpace(x) isspace((int)(x))
#define SERVER_STRING "Server: ribin_httpd/0.1.0\r\n"
#define STDIN 0
#define STDOUT 1
#define STRERR 2

using namespace std;

int init_srv(u_short *);
void* handle_request(void *);
size_t get_line(int, char*, int);
void unimplemented(int);
void not_found(int);
void server_file(int, const char*);
void headers(int, const char*);
void cat(int, FILE*);
void execute_cgi(int, const char*, char*, char);
void bad_request(int);
void cannot_execute(int);

void Err(const char* s)
{
    cerr << s << endl;
    exit(0);
}
void cannot_execute(int sock)
{
    char buf[1024];

    sprintf(buf, "HTTP/1.0 500 Internal Server Error\r\n");
    send(sock, buf, strlen(buf), 0);
    sprintf(buf, "Content-type: text/html\r\n");
    send(sock, buf, strlen(buf), 0);
    sprintf(buf, "\r\n");
    send(sock, buf, strlen(buf), 0);
    sprintf(buf, "<P>Error prohibited CGI execution.\r\n");
    send(sock, buf, strlen(buf), 0);
}

void bad_request(int sock)
{
    char buf[1024];

    sprintf(buf, "HTTP/1.0 400 BAD REQUEST\r\n");
    send(sock, buf, sizeof(buf), 0);
    sprintf(buf, "Content-type: text/html\r\n");
    send(sock, buf, sizeof(buf), 0);
    sprintf(buf, "\r\n");
    send(sock, buf, sizeof(buf), 0);
    sprintf(buf, "<P>Your browser sent a bad request, ");
    send(sock, buf, sizeof(buf), 0);
    sprintf(buf, "such as a POST without a Content-Length.\r\n");
    send(sock, buf, sizeof(buf), 0);
}

void execute_cgi(int sock, const char* path, char* method, char* s)
{
    pid_t pid;

    char buffer[1024];
    char c;

    int cgi_output[2];
    int cgi_input[2];
    int status;
    int i;
    int numchars = 1;
    int content_len = -1;

    buffer[0] = 'A';buffer[1] = '\0';
    if(strcasecmp(method, "GET") == 0) {
        while ((numchars > 0) && strcmp("\n", buffer))
            numchars = get_line(sock, buffer, sizeof(buffer));
    } else if(strcasecmp(method, "POST") == 0) {
        numchars = get_line(sock, buffer, sizeof(buffer));
        while((numchars > 0) && strcmp("\n", buffer)) {
            buffer[15] = '\0';
            if(strcasecmp(buffer, "Content-Length:") == 0)
                content_len = atoi(&(buffer[16]));
            numchars = get_line(sock, buffer, sizeof(buffer));
        }
        if(content_len == -1) {
            bad_request(sock);
            return;
        }
    } else {
        numchars = get_line(sock, buffer, sizeof(buffer));
        while((numchars > 0) && strcmp("\n", buffer)) {
            buffer[15] = '\0';
            if(strcasecmp(buffer, "Content-Length:") == 0)
                content_len = atoi(&(buffer[16]));
            numchars = get_line(sock, buffer, sizeof(buffer));
        }
        if(content_len == -1) {
            bad_request(sock);
            return;
        }
    }

    if(pipe(cgi_output) < 0) {
        cannot_execute(sock);
        return;
    }
    if(pipe(cgi_input) < 0) {
        cannot_execute(sock);
        return;
    }
    if((pid = fork()) < 0) {
        cannot_execute(sock);
        return;
    }

    sprintf(buffer, "HTTP/1.0 200 OK\r\n");
    send(sock, buffer, strlen(buffer), 0);
    //  Child Process
    if(pid == 0) {
        char meth_env[255];
        char query_env[255];
        char length_env[255];

        dup2(cgi_output[1], STDOUT);
        dup2(cgi_input[0], STDIN);
        close(cgi_input[1]);
        close(cgi_output[0]);

        sprintf(meth_env, "REQUEST_METHOD = %s", method);

        putenv(meth_env);
        if(strcasecmp(method, "GET") == 0) {
            sprintf(query_env, "QUERY STRING = %s", s);
            putenv(query_env);
        } else {
            sprintf(length_env, "CONTENT_LENGTH = %d", content_len);
            putenv(length_env);
        }
        execl(path, NULL);
        exit(0);
    } else {
        close(cgi_input[1]);
        close(cgi_output[0]);
        if(strcasecmp(method, "POST") == 0) {
            for(i = 0;i < content_len;i++) {
                recv(sock, &c, 1, 0);
                write(cgi_input[1], &c, 1);
            }
        }
        while(read(cgi_output[0], &c, 1) > 0) {
            send(sock, &c, 1, 0);
        }

        close(cgi_output[0]);
        close(cgi_input[1]);
        waitpid(pid, &status, 0);
    }
}

void cat(int sock, FILE* resource)
{
    char buf[1024];

    fgets(buf, sizeof(buf), resource);
    while (!feof(resource))
    {
        send(sock, buf, strlen(buf), 0);
        fgets(buf, sizeof(buf), resource);
    }
}

void headers(int sock, const char* filename)
{
    char buf[1024];
    (void)filename;  /* could use filename to determine file type */

    strcpy(buf, "HTTP/1.0 200 OK\r\n");
    send(sock, buf, strlen(buf), 0);
    strcpy(buf, SERVER_STRING);
    send(sock, buf, strlen(buf), 0);
    sprintf(buf, "Content-Type: text/html\r\n");
    send(sock, buf, strlen(buf), 0);
    strcpy(buf, "\r\n");
    send(sock, buf, strlen(buf), 0);
}
void server_file(int sock, const char* filename)
{
    FILE *resource = NULL;
    int numchars = 1;
    char buffer[1024];

    buffer[0] = 'A';buffer[1] = '\0';
    while((numchars > 0) && strcmp("\n", buffer))
        numchars = get_line(sock, buffer, sizeof(buffer));

    resource = fopen(filename, "r");
    if(resource == NULL) {
        not_found(sock);
    } else {
        headers(sock, filename);
        cat(sock, resource);
    }
    fclose(resource);
}

void not_found(int sock)
{
    char buf[1024];

    sprintf(buf, "HTTP/1.0 404 NOT FOUND\r\n");
    send(sock, buf, strlen(buf), 0);
    sprintf(buf, SERVER_STRING);
    send(sock, buf, strlen(buf), 0);
    sprintf(buf, "Content-Type: text/html\r\n");
    send(sock, buf, strlen(buf), 0);
    sprintf(buf, "\r\n");
    send(sock, buf, strlen(buf), 0);
    sprintf(buf, "<HTML><TITLE>Not Found</TITLE>\r\n");
    send(sock, buf, strlen(buf), 0);
    sprintf(buf, "<BODY><P>The server could not fulfill\r\n");
    send(sock, buf, strlen(buf), 0);
    sprintf(buf, "your request because the resource specified\r\n");
    send(sock, buf, strlen(buf), 0);
    sprintf(buf, "is unavailable or nonexistent.\r\n");
    send(sock, buf, strlen(buf), 0);
    sprintf(buf, "</BODY></HTML>\r\n");
    send(sock, buf, strlen(buf), 0);
}

void unimplemented(int sock)
{
    char buffer[1024];

    sprintf(buffer, "HTTP/1.0 501 Method Not Implemented\r\n");
    send(sock, buffer, strlen(buffer), 0);

    sprintf(buffer, SERVER_STRING);
    send(sock, buffer, strlen(buffer), 0);

    sprintf(buffer, "Content-Type: text/html\r\n");
    send(sock, buffer, strlen(buffer), 0);

    sprintf(buffer, "\r\n");
    send(sock, buffer, strlen(buffer), 0);

    sprintf(buffer, "<HTML><HEAD><TITLE>Method Not IMplemented\r\n");
    send(sock, buffer, strlen(buffer), 0);
    
    sprintf(buffer, "</TITLE></HEAD>\r\n");
    send(sock, buffer, strlen(buffer), 0);

    sprintf(buffer, "<BODY><P>HTTP request method not supported.\r\n");
    send(sock, buffer, strlen(buffer), 0);

    sprintf(buffer, "</BODY></HTML>\r\n");
    send(sock, buffer, strlen(buffer), 0);
}

size_t get_line(int sock, char* buf, int size)
{
    int i = 0;
    int n;

    char s = '\0';

    while(i < size - 1 && s != '\n') {
        n = recv(sock, &s, 1, 0);
        if(n > 0) {
            if(s == '\r') {
                n = recv(sock, &s, 1, MSG_PEEK);
                if(n > 0 && s == '\n')
                    recv(sock, &s, 1, 0);
                else
                    s = '\n';
            }
            buf[i] = s;
            i++;
        } else {
            s = '\n';
        }
    }
    buf[i] = '\0';
    // cout << buf << endl;
    return i;
}

void* handle_request(void *arg)
{
    int client = (intptr_t)arg;
    int cgi = 0;

    char buffer[1024];
    char method[255];
    char url[255];
    char path[255];

    size_t numchars;
    size_t i, j;

    struct stat st;

    char *s = NULL;
    
    numchars = get_line(client, buffer, sizeof(buffer));
    i = 0;j = 0;

    //  get method
    while(!IsSpace(buffer[i]) && (i < sizeof(method) - 1)) {
        method[i] = buffer[i];
        i++;
    }
    j = i;
    method[i] = '\0';

    //  check method
    if(strcasecmp(method, "GET") && strcasecmp(method, "POST")) {
        unimplemented(client);
        exit(0);
    }

    //  POST
    if(strcasecmp(method, "POST") == 0)
        cgi = 1;
    
    //  get url
    while (IsSpace(buffer[j]) && (j < numchars))
        j++;
    i = 0;
    while(!IsSpace(buffer[j]) && (i < sizeof(url) - 1) && (j < numchars)) {
        url[i++] = buffer[j++];
    }
    url[i] = '\0';

    //  get path
    if(strcasecmp(method, "GET") == 0) {
        s = url;
        while ((*s != '?') && (*s != '\0'))
            s++;
        if(*s == '?') {
            cgi = 1;
            *s = '\0';
            s++;
        }
    }
    sprintf(path, "cgi%s", url);
    if(path[strlen(path) - 1] == '/')
        strcat(path, "index.html");
    if(stat(path, &st) == -1) {
        //  read and discard headers
        while((numchars > 0) && strcmp("\n", buffer))
            numchars = get_line(client, buffer, sizeof(buffer));
        not_found(client);
    } else {
        if((st.st_mode & S_IFMT) == S_IFDIR)
            strcat(path, "/index.html");
        if((st.st_mode & S_IXUSR) || (st.st_mode & S_IXGRP) || (st.st_mode & S_IXOTH))
            cgi = 0;
        if(!cgi)
            server_file(client, path);
        else
            execute_cgi(client, path, method, s);
    }
    close(client);
    pthread_exit(NULL);
}

int init_srv(u_short *port)
{
    int httpd = 0;
    int on = 1;
    struct sockaddr_in name;

    httpd = socket(AF_INET, SOCK_STREAM, 0);
    if(httpd == -1)
        cerr << "socket";

    bzero(&name, sizeof(name));
    name.sin_family = AF_INET;
    name.sin_port = htons(*port);
    name.sin_addr.s_addr = htonl(INADDR_ANY);

    if((setsockopt(httpd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on))) < 0) {
        Err("setsockopt failed");
    }
    if(bind(httpd, (struct sockaddr *)&name, sizeof(name)) < 0) {
        Err("bind");
    }
    if(*port == 0) {
        socklen_t name_len = sizeof(name);
        if(getsockname(httpd, (struct sockaddr *)&name, &name_len) == -1)
            Err("getsockname");
        *port = ntohs(name.sin_port);
    }
    if(listen(httpd, BLACKLOG) < 0)
        Err("listen");

    return httpd;
}
int main()
{
    int server_sock = -1;
    u_short port = 4000;

    int client_sock = -1;
    struct sockaddr_in client;
    socklen_t client_len = sizeof(client);

    pthread_t newthread;

    server_sock = init_srv(&port);
    cout << "httpd running on port " << port << endl;

    for(;;) {
        client_sock = accept(server_sock, (struct sockaddr *)&client, &client_len);
        if(client_sock == -1)
            Err("accept");
        if(pthread_create(&newthread, NULL, handle_request, (void *)(intptr_t)client_sock) != 0)
            Err("pthread_create");
    }
    close(server_sock);

}