#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <signal.h>
#include <syslog.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>

#define PORT 9000

#define FILE_PATH "/var/tmp/aesdsocketdata"

#define BACKLOG 10

#define BUF_SIZE 1024

int server_fd = -1;
int client_fd = -1;

FILE *fp = NULL;


void cleanup_and_exit(int signo)
{
    (void)signo; 
    syslog(LOG_INFO, "signal received, exiting");

    if (client_fd != -1)
        close(client_fd);

    if (server_fd != -1)
        close(server_fd);

    if (fp)
        fclose(fp);

    remove(FILE_PATH);
    
    closelog();
    
    exit(0);
}

int main(int argc, char *argv[])
{


    int daemon_mode = 0;

    
    if (argc == 2 && strcmp(argv[1], "-d") == 0) 
    {
        daemon_mode = 1;
        
    }
    
    
    struct sockaddr_in server_addr, client_addr;
    
    socklen_t addr_len = sizeof(client_addr);
    
    char buffer[BUF_SIZE];
    ssize_t bytes_received;
    
    openlog("aesdsocket", LOG_PID, LOG_USER);
    
    
    
    signal(SIGINT, cleanup_and_exit);
    signal(SIGTERM, cleanup_and_exit);
    signal(SIGPIPE, SIG_IGN);  
    
    
    
    
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    
    if (server_fd == -1) 
    {
        syslog(LOG_ERR, "socket creation failed: %s", strerror(errno));
        
        return -1;
    }
    

     int opt = 1;
     
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    
    memset(&server_addr, 0, sizeof(server_addr));
    
    server_addr.sin_family = AF_INET;
    
    server_addr.sin_addr.s_addr = INADDR_ANY;
    
    server_addr.sin_port = htons(PORT);

    
    
    
    
     if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1)
      {
        syslog(LOG_ERR, "bind failed: %s", strerror(errno));
        
        cleanup_and_exit(0);
        
        return -1;
   }
   
   
    
    if (listen(server_fd, BACKLOG) == -1) 
    {
        syslog(LOG_ERR, "listen failed: %s", strerror(errno));
        
        cleanup_and_exit(0);
        
        return -1;
      }
    
    
    if (daemon_mode)
     {
        pid_t pid = fork();
        
        if (pid < 0) 
        {
            syslog(LOG_ERR, "fork failed: %s", strerror(errno));
            
            cleanup_and_exit(0);
            
            return -1;
        }
        
        if (pid > 0) 
        {
            
            exit(0);
        }

       
        if (setsid() == -1) 
        {
            syslog(LOG_ERR, "setsid failed: %s", strerror(errno));
            
            cleanup_and_exit(0);
            
            return -1;
        }

     
        if (chdir("/") == -1)
         {
            syslog(LOG_ERR, "chdir failed: %s", strerror(errno));
            
            cleanup_and_exit(0);
            
            return -1;
        }

     
        close(STDIN_FILENO);
        close(STDOUT_FILENO);
        close(STDERR_FILENO);
    }

    
     while (1) 
     {
        
           
        addr_len = sizeof(client_addr);
        
        client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &addr_len);
        
        
        
        if (client_fd == -1)
         {
            syslog(LOG_ERR, "Accept failed: %s", strerror(errno));
            
            continue;
        }

        syslog(LOG_INFO, "Accepted connection from %s", inet_ntoa(client_addr.sin_addr));

        fp = fopen(FILE_PATH, "a+");
        
        if (!fp)
         {
            syslog(LOG_ERR, "File did not open: %s", strerror(errno));
            
             close(client_fd);
            
            
            client_fd = -1;
            
            continue;
        }

       
        for (;;)
         {
            bytes_received = recv(client_fd, buffer, BUF_SIZE, 0);
            
            if (bytes_received < 0)
             {
                if (errno == EINTR) continue;   
                
                syslog(LOG_ERR, "recv failed: %s", strerror(errno));
                
                break;
            }
            
            if (bytes_received == 0) 
            {
                
                break;
            }

            size_t written = fwrite(buffer, 1, (size_t)bytes_received, fp);
            
            
            
            if (written != (size_t)bytes_received) 
            {
                syslog(LOG_ERR, "fwrite failed: %s", strerror(errno));
                break;
            }
            
            
            fflush(fp);
            

            if (memchr(buffer, '\n', (size_t)bytes_received)) 
            {
                break; 
            }
        }

       
        if (fseek(fp, 0, SEEK_SET) != 0)
         {
            syslog(LOG_ERR, "fseek failed: %s", strerror(errno));
        } 
        
        
        else
         {
            char sbuf[BUF_SIZE];
            
            size_t nread;
            
            while ((nread = fread(sbuf, 1, sizeof(sbuf), fp)) > 0) 
            {
              size_t off = 0;
                
                 while (off < nread)
                 {
                   ssize_t n = send(client_fd, sbuf + off, nread - off, 0); 
                          
            
                    if (n < 0) 
                    {
                        if (errno == EINTR) continue;    
                                    
                         if (errno == EPIPE || errno == ECONNRESET) break; 
                        
                        syslog(LOG_ERR, "send failed: %s", strerror(errno));
                        
                        break;
                    }
                    
                    off += (size_t)n;
                }
                
                
                if (off < nread) break; 
            }
        }

        fclose(fp);
        
        fp = NULL;

          syslog(LOG_INFO, "Closed connection from %s", inet_ntoa(client_addr.sin_addr));
        
        close(client_fd);
        client_fd = -1;

    }
    
    

      cleanup_and_exit(0);
      
    return 0;
   
}
