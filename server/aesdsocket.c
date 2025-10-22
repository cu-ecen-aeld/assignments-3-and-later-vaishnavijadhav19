#define _GNU_SOURCE
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <syslog.h>
#include <unistd.h>


#include <time.h>

#include "queue.h"

#define PORT            9000

#define BACKLOG         10

#define BUF_SIZE        1024

//#define FILE_PATH       "/var/tmp/aesdsocketdata"

#ifndef USE_AESD_CHAR_DEVICE
#define USE_AESD_CHAR_DEVICE 1
#endif

#if USE_AESD_CHAR_DEVICE
#define FILE_PATH "/dev/aesdchar"
#else
#define FILE_PATH "/var/tmp/aesdsocketdata"
#endif


static int listen_fd = -1;

static pthread_mutex_t file_mutex = PTHREAD_MUTEX_INITIALIZER;



static volatile sig_atomic_t g_exit_requested = 0;





struct client_thread 
{
    pthread_t          tid;
    int                client_fd;
    
     struct sockaddr_in client_addr;
    volatile bool      done;              
    SLIST_ENTRY(client_thread) entries;   
};

SLIST_HEAD(thread_list, client_thread);

static struct thread_list g_threads_head;



static void handle_signal(int signo)
{
    (void)signo;
    
    
    g_exit_requested = 1;

   
    if (listen_fd != -1) 
    {
        close(listen_fd);
        
        listen_fd = -1;
    }
}



static void log_peer(const char *prefix, const struct sockaddr_in *addr)
{
     char ip[INET_ADDRSTRLEN] = {0};
    
    inet_ntop(AF_INET, &(addr->sin_addr), ip, sizeof(ip));
    
    syslog(LOG_INFO, "%s %s", prefix, ip);
}







static void *connection_thread(void *arg)
{
    struct client_thread *self = (struct client_thread *)arg;
    
    int cfd = self->client_fd;
    
    char buffer[BUF_SIZE];
    
    char *acc = NULL;
    
    size_t acc_len = 0;

   
    log_peer("Thread handling client", &self->client_addr);

    for (;;)
     {
        // receive data from the client
        ssize_t n = recv(cfd, buffer, sizeof(buffer), 0);
        
        if (n < 0) 
        {
            // retry if interrupted, else log error
            if (errno == EINTR) continue;
            
            syslog(LOG_ERR, "recv error: %s", strerror(errno));
            
            goto cleanup;
        }
        if (n == 0)
         {
           //if no data connection closed
            if (acc_len == 0) goto cleanup;
            
#if USE_AESD_CHAR_DEVICE

            //make sure command ends with new line 
            if (acc[acc_len - 1] != '\n') 
            {
                char *tmp = realloc(acc, acc_len + 1); //allocate byte sized space for newline
                
                
                if (!tmp) 
                {
                    syslog(LOG_ERR, "realloc error");
                    goto cleanup;
                }
                
                acc = tmp;
                
                acc[acc_len++] = '\n';// append new line at the end 
            }
#endif
        } 
        else 
        {
           
            char *tmp = realloc(acc, acc_len + (size_t)n);
            
            if (!tmp)
             {
                syslog(LOG_ERR, "realloc error");
                goto cleanup;
            }
            
            acc = tmp;
            
            memcpy(acc + acc_len, buffer, (size_t)n);
            
            acc_len += (size_t)n;

           
            if (memchr(acc, '\n', acc_len) == NULL) continue;
        }

       
        if (pthread_mutex_lock(&file_mutex) != 0)
         {
            syslog(LOG_ERR, "mutex lock failed");
            goto cleanup;
        }

     
        size_t line_len = acc_len;
        
        char *nlp = memchr(acc, '\n', acc_len);
        
        if (nlp) line_len = (size_t)(nlp - acc) + 1;

      
        int wfd = open(FILE_PATH, O_WRONLY); //accessed only when client sends data
        
        if (wfd < 0) 
        {
            syslog(LOG_ERR, "open write error: %s", strerror(errno));
            
            
            pthread_mutex_unlock(&file_mutex);
            goto cleanup;
        }

        
        size_t off = 0;
        
        while (off < line_len)
         {
            ssize_t w = write(wfd, acc + off, line_len - off);
            
            
            if (w < 0)
             {
                if (errno == EINTR) continue;
                
                
                syslog(LOG_ERR, "write error: %s", strerror(errno));
                
                close(wfd);
                
                pthread_mutex_unlock(&file_mutex);
                goto cleanup;
            }
            
            
            off += (size_t)w;
        }
        
        close(wfd);

        
        int rfd = open(FILE_PATH, O_RDONLY); //reads data
        if (rfd < 0)
         {
            syslog(LOG_ERR, "open read error: %s", strerror(errno));
            
            pthread_mutex_unlock(&file_mutex);
            goto cleanup;
        }
        
        
        for (;;) 
        {
            ssize_t r = read(rfd, buffer, sizeof(buffer));
            
            
            if (r < 0) 
            {
                if (errno == EINTR) continue;
                syslog(LOG_ERR, "read error: %s", strerror(errno));
                break;
            }
            
            
            if (r == 0) break; 
            
            size_t sent = 0;
            
            while (sent < (size_t)r)
             {
                ssize_t s = send(cfd, buffer + sent, (size_t)r - sent, 0);
                
                if (s < 0)
                 {
                    if (errno == EINTR) continue;
                    r = 0;
                    break;
                }
                
                
                sent += (size_t)s;
            }
            
            if (r == 0) break;
        }
        
        close(rfd);
        
        pthread_mutex_unlock(&file_mutex);

        break; 
    }

cleanup:
    free(acc);
    close(cfd);
    self->client_fd = -1;
    self->done = true;

   
    log_peer("Client disconnected", &self->client_addr);
    
    return NULL;
}


static int daemonize(void)
{
    pid_t pid = fork();
    
    if (pid < 0) return -1;
    if (pid > 0) _exit(0);

    if (setsid() == -1) return -1;

    pid = fork();
    
    if (pid < 0) return -1;
    if (pid > 0) _exit(0);

    if (chdir("/") == -1) return -1;

   
     close(STDIN_FILENO);
      close(STDOUT_FILENO);
    close(STDERR_FILENO);

   
    int fd = open("/dev/null", O_RDWR);
    
    
    
    if (fd >= 0)
     {
        dup2(fd, STDIN_FILENO);
        dup2(fd, STDOUT_FILENO);
        dup2(fd, STDERR_FILENO);
        
        
        if (fd > 2) close(fd);
    }
    return 0;
}


#if !USE_AESD_CHAR_DEVICE
static void *timestamp_thread(void *arg)
{
    (void)arg;

    while (!g_exit_requested) 
    {
        
        time_t now = time(NULL);
        
        struct tm *tm_info = localtime(&now);
        
        if (tm_info)
         {
            char tbuf[128];
           
            strftime(tbuf, sizeof(tbuf), "timestamp:%a, %d %b %Y %T %z\n", tm_info);

            pthread_mutex_lock(&file_mutex);
            
            FILE *fp = fopen(FILE_PATH, "a+");
            if (fp) 
            {
                fputs(tbuf, fp);
                fclose(fp);
            }
            
            
            pthread_mutex_unlock(&file_mutex);
        }

      
        for (int i = 0; i < 10 && !g_exit_requested; i++) 
        {
            sleep(1);
        }
        
        
        
    }
    return NULL;
}
#endif



int main(int argc, char *argv[])
{
    int daemon_mode = (argc == 2 && strcmp(argv[1], "-d") == 0);
    
    
 
    #if !USE_AESD_CHAR_DEVICE
    pthread_t ts_tid;
    bool ts_started = false;
    #endif
    
    openlog("aesdsocket", LOG_PID, LOG_USER);

    
    struct sigaction sa = {0};
    
     sa.sa_handler = handle_signal;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGINT,  &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    signal(SIGPIPE, SIG_IGN);
    

  
    SLIST_INIT(&g_threads_head);

   
    listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    
    if (listen_fd < 0)
     {
         syslog(LOG_ERR, "socket failed: %s", strerror(errno));
        closelog();
        
        
        return EXIT_FAILURE;
    }
    
    

    int opt = 1;
    (void)setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in srv = {0};
    
    srv.sin_family = AF_INET;
    
    srv.sin_addr.s_addr = htonl(INADDR_ANY);
    
    srv.sin_port = htons(PORT);
    
    

    if (bind(listen_fd, (struct sockaddr *)&srv, sizeof(srv)) < 0)
     {
         syslog(LOG_ERR, "bind failed: %s", strerror(errno));
        close(listen_fd);
        closelog();
        
        
        return EXIT_FAILURE;
    }

    if (listen(listen_fd, BACKLOG) < 0)
     {
        syslog(LOG_ERR, "listen failed: %s", strerror(errno));
        
        close(listen_fd);
        
        closelog();
        
        return EXIT_FAILURE;
    }

  
    if (daemon_mode)
     {
        if (daemonize() != 0) 
        {
            syslog(LOG_ERR, "daemonize failed: %s", strerror(errno));
            
            close(listen_fd);
            
            closelog();
            
            return EXIT_FAILURE;
        }
    }

  #if !USE_AESD_CHAR_DEVICE
    if (pthread_create(&ts_tid, NULL, timestamp_thread, NULL) != 0) 
    {
        syslog(LOG_ERR, "Failed to create timestamp thread");
        
        close(listen_fd);
        
        closelog();
        
        return EXIT_FAILURE;
    }
    
    ts_started = true;
  #endif

    syslog(LOG_INFO, "Server listening on port %d", PORT);

    
    while (!g_exit_requested)
     {
        struct sockaddr_in cli = {0};
        
        socklen_t clilen = sizeof(cli);

        int cfd = accept(listen_fd, (struct sockaddr *)&cli, &clilen);
        
        if (cfd < 0) 
        {
            if (g_exit_requested) break;   
            
            if (errno == EINTR) continue;
            
            syslog(LOG_ERR, "accept failed: %s", strerror(errno));
            
            continue;
        }

      
        struct client_thread *node = calloc(1, sizeof(*node));
        
        if (!node) 
        {
            syslog(LOG_ERR, "calloc failed for thread node");
            
            close(cfd);
            
            continue;
        }

        node->client_fd = cfd;
        node->client_addr = cli;
        node->done = false;

        SLIST_INSERT_HEAD(&g_threads_head, node, entries);

        int rc = pthread_create(&node->tid, NULL, connection_thread, node);
        if (rc != 0) 
        {
            syslog(LOG_ERR, "pthread_create failed: %s", strerror(rc));
            close(cfd);
           
            SLIST_REMOVE(&g_threads_head, node, client_thread, entries);
            free(node);
            continue;
        }

        
        struct client_thread *it, *tmp;
        
#ifdef SLIST_FOREACH_SAFE

        SLIST_FOREACH_SAFE(it, &g_threads_head, entries, tmp)
#else
       
        SLIST_FOREACH(it, &g_threads_head, entries)
#endif
        {
        
            if (it->done) 
            {
                pthread_join(it->tid, NULL);
                SLIST_REMOVE(&g_threads_head, it, client_thread, entries);
                free(it);
            }
        }
    }

  
    if (listen_fd != -1)
     {
        close(listen_fd);
        listen_fd = -1;
    }

   
    struct client_thread *it2;
    
    
    while (!SLIST_EMPTY(&g_threads_head)) 
    {
        it2 = SLIST_FIRST(&g_threads_head);
        
        pthread_join(it2->tid, NULL);
        
        SLIST_REMOVE_HEAD(&g_threads_head, entries);
        
        free(it2);
    }

    #if !USE_AESD_CHAR_DEVICE
    if (ts_started) pthread_join(ts_tid, NULL);
    #endif

    #if !USE_AESD_CHAR_DEVICE
    remove(FILE_PATH);
    #endif
    
    pthread_mutex_destroy(&file_mutex);

    syslog(LOG_INFO, "Signal received, shutting down");
    
    closelog();
    
    
    
    
    return EXIT_SUCCESS;
}

