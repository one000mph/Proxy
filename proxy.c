/*
 * proxy.c - CS:APP Web proxy
 *
 * TEAM MEMBERS:
 *     Heather Seaman
 *     Tessa Barton
 * 
 * IMPORTANT: Give a high level description of your code here. You
 * must also provide a header comment at the beginning of each
 * function that describes what that function does.
 */ 

#include "csapp.h"
#include "strmanip.h"

/* The name of the proxy's log file */
#define PROXY_LOG "proxy.log"

/* Undefine this if you don't want debugging output */
#define DEBUG

/* 
 * This struct remembers some key attributes of an HTTP request and
 * the thread that is processing it.
 */
typedef struct {
    int myid;    /* Small integer used to identify threads in debug messages */
    int connfd;                    /* Connected file descriptor */ 
    struct sockaddr_in clientaddr; /* Client IP address */
} arglist_t;

/*
 * Place global declarations here.
 */ 
pthread_mutex_t mutex;
sem_t semaphore;
/*
 * Place forward function declarations here.
 */
void *process_request(void* vargp);
ssize_t Rio_readlineb_w(rio_t *rp, void *usrbuf, size_t maxlen); 
int parse_uri(char *uri, char *target_addr, char *path, int  *port);
void format_log_entry(char *logstring, int stringsize, struct sockaddr_in *sockaddr, char *uri, int size);

// we wrote these methods below
int Getnameinfo(const struct sockaddr *sa, socklen_t salen, char *host, socklen_t hostlen,
                       char *serv, socklen_t servlen, int flags);
int Open_clientfd_ts(char *hostname, int port);

/*
 * Handy macro to compare something with a constant prefix.  For example,
 * prefixcmp(foo, "abc") returns 0 if the first three characters of foo
 * are "abc".
 */
#define prefixcmp(str, prefix) strncmp(str, prefix, sizeof(prefix) - 1)

/* 
 * main - Main routine for the proxy program 
 */
int main(int argc, char **argv)
{

    /* Check arguments */
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <port number>\n", argv[0]);
        exit(0);
    }

    int listenfd;
    int connfd;
    socklen_t clientlen;
    struct sockaddr_storage clientaddr; // enough space for any address
    char client_hostname[MAXLINE];
    char client_port[MAXLINE];
    int id = 0;
    pthread_t tid;

    /* Open listener socket */
    listenfd = Open_listenfd((int) atoi(argv[1]));

    while (1) {
        clientlen = sizeof(struct sockaddr_storage);

        // Parse the request
        connfd = Accept(listenfd, (SA*)&clientaddr, &clientlen);
        Getnameinfo((SA*) &clientaddr, clientlen, client_hostname, MAXLINE, client_port, MAXLINE, 0);
        printf("Connected to (%s, %s)\n", client_hostname, client_port);
        arglist_t* arglist = Malloc(sizeof(arglist_t));
        arglist->myid = id;
        arglist->connfd = connfd;
        arglist->clientaddr = *((struct sockaddr_in*) &clientaddr);

        // Create thread to handle request
        Sem_init(&semaphore, 0, 1);
        Pthread_create(&tid, NULL, process_request, (void*) arglist);

        id++;

    }
    exit(0);
}

/*
 * process_request - Thread routine.
 * 
 * Each thread reads an HTTP request from a client, forwards it to the
 * end server (always as a simple HTTP/1.0 request), waits for the
 * response, and then forwards it back to the client.
 */ 
void *process_request(void *vargp) 
{
    arglist_t arglist;              /* Arg list passed into thread */ 
    struct sockaddr_in clientaddr;  /* Client socket address */      
    int connfd;                     /* Socket descriptor for talking with client */
    char *request;                  /* HTTP request from client */
    int realloc_size;               /* Used to increase size of request buffer if necessary */  
    int request_len;                /* Total size of HTTP request */
    int n;                          /* General counting variable */
    rio_t rio;                      /* Rio buffer for calls to buffered rio_readlineb routine */
    char buf[MAXLINE];              /* General I/O buffer */
    
    arglist = *((arglist_t *)vargp); /* Copy the arguments onto the stack */
    connfd = arglist.connfd;         /* Put connfd and clientaddr in scalars for convenience */  
    clientaddr = arglist.clientaddr;
    /* See the man page on pthread_detach for why the following line is handy */
    Pthread_detach(pthread_self());  /* Detach the thread */
    Free(vargp);                     /* Free up the arguments */ 

    /* 
     * Read the entire HTTP request into the request buffer, one line
     * at a time.
     */
    request = (char *)Malloc(MAXLINE);
    request[0] = '\0';
    realloc_size = MAXLINE;
    request_len = 0;
    Rio_readinitb(&rio, connfd);

    while (1) {
        // handle errors
        if ((n = Rio_readlineb_w(&rio, buf, MAXLINE)) <= 0) {

            printf("Thread %d: process_request: client issued a bad request (1).\n",
              arglist.myid);
            printf("Thread %d: process_request: partial request was %s\n",
              arglist.myid, request);
            close(connfd);
            free(request);
            return NULL;
        }

        /* Don't pass "Connection:" lines; they cause long hangs */
        if (prefixcmp(buf, "Connection:") == 0)
            continue;

        /* If not enough room in request buffer, make more room */
        if (request_len + n + 1 > realloc_size) {
            /*
             * In this program the following loop always runs exactly once,
             * but if you were to use similar code elsewhere it might be
             * needed, so we'll keep it here for pedagogical purposes.
             */
            while (request_len + n + 1 > realloc_size)
                realloc_size *= 2;
            request = Realloc(request, realloc_size);
        }

        memcpy(request + request_len, buf, n);
        request_len += n;

        /* An HTTP request is always terminated by a blank line */
        if (strcmp(buf, "\r\n") == 0  ||  strcmp(buf, "\n") == 0)
            break;
    }

    /* 
     * Make sure that this is indeed a GET request
     */
    if (prefixcmp(request, "GET ") != 0) {
        printf("process_request: Received non-GET request\n");
        close(connfd);
        free(request);
        return NULL;
    }
     
     /* begin our code */
     // find len of first line
     int count = 0;
     while(request[count] != '\n') {
         count++;
     }
     //extract first line
     char* firstLine = (char*)Malloc(count);
     memcpy(firstLine, &request[0], count-1);
     firstLine[count] = '\0';

     //Change to proper protocol
     P(&semaphore);
     char* httpRequest = substitute_re(request, request_len, " HTTP\\/1\\..", " HTTP/1.0", 0, 0, NULL, NULL);
     V(&semaphore);
     printf("httpRequest: %s\n", httpRequest);

     // extract data from first line
     char* get = Malloc(3);
     char* url = Malloc(MAXLINE);
     char* protocol = Malloc(8);
     sscanf(firstLine, "%s %s %s", get, url, protocol);


     // parse info from uri
     char* hostname = (char *)Malloc(MAXLINE);
     char* pathname = (char *)Malloc(MAXLINE);
     int* port = (int *)Malloc(sizeof(int*));;
     if (parse_uri(url, hostname, pathname, port) != 0) {
         printf("Warning: parse_uri failed; error = %s\n", strerror(errno));
     };

     
     
     // forward request to the server
     int clientfd;
     if((clientfd = Open_clientfd_ts(hostname, *port)) < 0) {
        printf("%s\n", "could not open connection to client");
        return NULL;
     }

     // initialize buffer
     Rio_readinitb(&rio, clientfd);

     // write response to buffer
     Rio_writen(clientfd, httpRequest, strlen(request));
     ssize_t readData;
     int responseLen = 0;
     while ((readData = Rio_readnb(&rio, buf, MAXLINE)) > 0) {
        responseLen += readData;
        Rio_writen(connfd, buf, readData);
     }

     if (responseLen>0) {
         /* Create log entry */
         char * log_entry = Malloc(MAXLINE);
         format_log_entry(log_entry, MAXLINE, &clientaddr, url, responseLen);

          /* Logfile is a shared resource, must be protected with a mutex */
         pthread_mutex_lock(&mutex);
         FILE* file = Fopen("proxy.log", "a");
         fprintf(file, "%s\n", log_entry); //buffered
         Free(log_entry);
         Fclose(file);
         pthread_mutex_unlock(&mutex);


     }
   
     // cleanup
     Free(firstLine);
     Free(get);
     Free(url);
     Free(hostname);
     Free(pathname);
     Free(port);
     Free(request);

     Close(connfd);
     Close(clientfd);
     pthread_exit(0);
     return NULL;
}


/*
 * Rio_readlineb_w - A wrapper for rio_readlineb (csapp.c) that
 * prints a warning when a read fails instead of terminating 
 * the process.
 */
ssize_t Rio_readlineb_w(rio_t *rp, void *usrbuf, size_t maxlen) 
{
    ssize_t rc;

    if ((rc = rio_readlineb(rp, usrbuf, maxlen)) < 0) {
        printf("Warning: rio_readlineb failed; error = %s\n", strerror(errno));
        return 0;
    }
    return rc;
}

/* Thread safe open_clientfd with error handling
*/
int Open_clientfd_ts(char *hostname, int port) {
    P(&semaphore);
    int result = Open_clientfd(hostname, port);
    V(&semaphore);
    return result;
    
}

/*
 * Getnameinfo- A wrapper for getnameinfo <sys/socket.h> that
 * prints a warning when the name fetch fails instead of terminating 
 * the process.
 */
int Getnameinfo(const struct sockaddr *sa, socklen_t salen, char *host, socklen_t hostlen,
                       char *serv, socklen_t servlen, int flags) {
    ssize_t result;

    if ((result = getnameinfo(sa, salen, host, hostlen, serv, servlen, flags)) < 0) {
        printf("Warning: getnameinfo; error = %s\n", strerror(errno));
        return 0;
    }

    return result;
}

/*
 * parse_uri - URI parser
 * 
 * Given a URI from an HTTP proxy GET request (i.e., a URL), extract
 * the host name, path name, and port.  The memory for hostname and
 * pathname must already be allocated and should be at least MAXLINE
 * bytes. Return -1 if there are any problems.
 */
int parse_uri(char *uri, char *hostname, char *pathname, int *port)
{
    char *hostbegin;
    char *hostend;
    char *pathbegin;
    int len;

    if (strncasecmp(uri, "http://", 7) != 0) {
        hostname[0] = '\0';
        return -1;
    }
       
    /* Extract the host name */
    hostbegin = uri + 7;
    hostend = strpbrk(hostbegin, " :/\r\n");
    if (hostend != 0)
        len = hostend - hostbegin;
    else
        len = strlen(hostbegin);
    hostend = hostbegin + len;
    strncpy(hostname, hostbegin, len);
    hostname[len] = '\0';
    
    /* Extract the port number */
    *port = 80; /* default */
    if (*hostend == ':')   
        *port = atoi(hostend + 1);
    
    /* Extract the path */
    pathbegin = strchr(hostbegin, '/');
    if (pathbegin == NULL) {
        pathname[0] = '\0';
    }
    else {
        pathbegin++;    
        strcpy(pathname, pathbegin);
    }

    return 0;
}

/*
 * format_log_entry - Create a formatted log entry in logstring. 
 * 
 * The arguments are the socket address of the requesting client
 * (sockaddr), the URI from the request (uri), and the size in bytes
 * of the response from the server (size).
 */
void format_log_entry(char *logstring, int stringsize,
                      struct sockaddr_in *sockaddr, char *uri, int size)
{
    time_t now;
    char time_str[MAXLINE];
    unsigned long host;
    unsigned char a, b, c, d;

    /* Get a formatted time string */
    now = time(NULL);
    strftime(time_str, MAXLINE, "%a %d %b %Y %H:%M:%S %Z", localtime(&now));

    /* 
     * Convert the IP address in network byte order to dotted decimal
     * form. Note that we could have used inet_ntoa, but chose not to
     * because inet_ntoa is a Class 3 thread unsafe function that
     * returns a pointer to a static variable (Ch 13, CS:APP).
     */
    host = ntohl(sockaddr->sin_addr.s_addr);
    a = host >> 24;
    b = (host >> 16) & 0xff;
    c = (host >> 8) & 0xff;
    d = host & 0xff;


    /* Return the formatted log entry string */
    snprintf(logstring, stringsize, "%s: %d.%d.%d.%d %s %d\n", time_str,
                                    a, b, c, d, uri, size);
}
