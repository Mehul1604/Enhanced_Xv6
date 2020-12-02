#include "types.h"
#include "stat.h"
#include "user.h"



int main(int argc , char *argv[])
{
	if(!(argc == 3))
	{
		printf(1 , "Invalid arguments\n");
		return 1;
	}

	int newp = atoi(argv[1]);
	int pid = atoi(argv[2]);

	if(set_priority(newp , pid) > 0)
	{
		printf(1 , "Priority of process %d changed to %d\n" , pid , newp);
	}


	exit();
}