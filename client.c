#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/msg.h>
#include <pthread.h>
//#include <semaphore.h>
#include <time.h>

#define PIPE_NAME "np_client_server"

int main()
{
  // Opens the pipe for writing
  int fd;
    if ((fd = open(PIPE_NAME, O_WRONLY)) < 0) {
    perror("Cannot open pipe for writing: ");
    exit(0);
  }
  // Do some work
  while (1) {
    char word[1200];
    fgets(word, sizeof(word), stdin);
    strtok(word,"\n");
    printf("-%s-\n",word);
    write(fd, &word, sizeof(word));
    sleep(2);
  }
    return 0;
}
