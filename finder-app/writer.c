#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

#include <stdlib.h>
#include <syslog.h>

static int write_all(int fd, const char *buf, size_t len)
 {
    size_t off = 0;
    
    while (off < len) 
    
    {
        ssize_t n = write(fd, buf + off, len - off);
        
        if (n < 0) 
        {
            if (errno == EINTR) continue;  
             
            return -1;
        }
        off += (size_t)n;
    }
    return 0;
}

int main(int argc, char *argv[])
 {
    
    setlogmask(LOG_UPTO(LOG_DEBUG));
    
    openlog("writer", LOG_PID, LOG_USER);

    if (argc != 3) 
    {
        syslog(LOG_ERR, "Invalid arguments. Usage: writer <file> <string>");
        
        closelog();
        
        return 1;
    }

    const char *filepath = argv[1];
    const char *text     = argv[2];

    
    syslog(LOG_DEBUG, "Writing %s to %s", text, filepath);

    
    int fd = open(filepath, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    
    if (fd < 0)
     {
        syslog(LOG_ERR, "open(%s) failed: %s", filepath, strerror(errno));
        
        closelog();
        return 1;
    }


    size_t len = strlen(text);
    
    if (write_all(fd, text, len) < 0) 
    {
        syslog(LOG_ERR, "write(%s) failed: %s", filepath, strerror(errno));
        
         close(fd);
        closelog();
        
        return 1;
    }

    if (close(fd) < 0)
     {
          syslog(LOG_ERR, "close(%s) failed: %s", filepath, strerror(errno));
        closelog();
        
        return 1;
    }

     closelog();
    return 0;
}
