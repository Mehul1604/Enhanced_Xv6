
#include "types.h"
#include "user.h"

int number_of_processes = 15;

int main(int argc, char *argv[])
{
  int j;
  for (j = 0; j < number_of_processes; j++)
  {
    int pid = fork();
    if (pid < 0)
    {
      printf(1, "Fork failed\n");
      continue;
    }
    if (pid == 0)
    {
      volatile int i;
      //printf(1 , "Process %d with priority started\n" , getpid());
      for (volatile int k = 0; k < number_of_processes; k++)
      {
        if (k <= j)
        {
          //printf(1 , "Process %d going to sleep\n" , getpid());
          sleep(200); //io time
          //printf(1 , "Process %d is awake\n" , getpid());
        }
        else
        {
          //printf(1 , "Process %d starting cpu\n" , getpid());
          for (i = 0; i < 100000000; i++)
          {
            ; //cpu time
          }
          //printf(1 , "Process %d is done with cpu yo\n" , getpid());
        }
      }
      //printf(1, "Process: %d Finished\n", j);
      //printf(1 , "\n");
      //ps();
      exit();
    }
    else{
        ;
       set_priority(100-(20+(j*5)),pid); // will only matter for PBS, comment it out if not implemented yet (better priorty for more IO intensive jobs)
    }
  }
  for (j = 0; j < number_of_processes+5; j++)
  {
    wait();
  }
  exit();
}
