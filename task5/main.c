#include <fcntl.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int main(){
    
    pid_t pid;
    // OPEN FILES
    int fd;
    fd = open("test.txt" , O_RDWR | O_CREAT | O_TRUNC);
    if (fd == -1)
    {
        fprintf(stderr, "fail on open %s\n", "test.txt");
		return -1;
    }

    //write 'hello fcntl!' to file
    char *data = "hello fcntl!";
    size_t len = strlen(data);
    write(fd,data,len);    

    // DUPLICATE FD
    int dup_fd = fcntl(fd,F_DUPFD,0);
    
    pid = fork();

    if(pid < 0){
        // FAILS
        printf("error in fork");
        return 1;
    }
    
    struct flock fl;

    if(pid > 0){
        // PARENT PROCESS
        //set the lock
        struct flock lock_p = {F_WRLCK, SEEK_SET,0,0};
        int ret = fcntl(fd, F_SETLKW, &lock_p);
        if(ret < 0){
            printf("error in fcntl");
            return 1;
        }

        //append 'b'
        char *data_b = "b";
        size_t len_b = strlen(data_b);
        write(fd,data_b,len_b);
        
        //unlock
        lock_p.l_type = F_UNLCK;
        fcntl(fd,F_SETLK,&lock_p);
        sleep(3);

        lseek(fd,0,SEEK_SET);
        char buf[20];
        int n = read(fd,buf,19);
        buf[n] = '\0';
        printf("%s\n",buf);
        // printf("%s", str); the feedback should be 'hello fcntl!ba'
        
        exit(0);

    } else {
        // CHILD PROCESS
        sleep(2);
        struct flock lock_s = {F_WRLCK,SEEK_SET, 0,0};
        // get the lock
        int ret = fcntl(dup_fd,F_SETLKW,&lock_s);
        if(ret < 0){
            printf("error in fcntl");
            return 1;
        }
        //append 'a'
        char *data_a = "a";
        size_t len_a = strlen(data_a);
        write(dup_fd,data_a,len_a);

        exit(0);
    }
    close(fd);
    return 0;
}