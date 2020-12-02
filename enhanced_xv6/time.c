#include "types.h"
#include "stat.h"
#include "user.h"



int main(int argc , char *argv[])
{
	int pid = fork();
	if(pid < 0)
		printf(1 , "Forking error!\n");

	else if(pid == 0)
	{
		if(exec(argv[1] , argv+1) < 0)
		{
			printf(1 , "Exec for %s failed\n" , argv[1]);
			exit();
		}
	}
	else if(pid > 0)
	{
		int WTIME,RTIME;
		int cpid = waitx(&WTIME,&RTIME);
		printf(1, "Process %d exited with waiting time %d and run time %d\n" ,cpid ,  WTIME,RTIME);
	}

	exit();
}