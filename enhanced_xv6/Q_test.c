#include<stdio.h>
#include <stdlib.h>

struct q_node{
	int val;
	struct q_node* next;
};

struct q_node* q_arr[5];

void push(int q_ind , int num)
{
	struct q_node* new_node = (struct q_node*)malloc(sizeof(struct q_node));
	new_node->val = num;
	new_node->next = 0;
	if(q_arr[q_ind] == 0)
		q_arr[q_ind] = new_node;
	else
	{
		struct q_node* run;
		run = q_arr[q_ind];
		while(run->next != 0)
			run = run->next;

		run->next = new_node;
	}
}

int main()
{
	push(0 , 5);
	push(0 , 6);
	push(0 , 7);

	struct q_node* run;
	run = q_arr[0];
	while(run != 0)
	{
		printf("%d " , run->val);
		run = run->next;
	}

	printf("\n");


}