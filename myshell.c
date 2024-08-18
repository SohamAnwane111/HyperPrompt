#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>

#define MAX_COMMANDS 10
#define MAX_ARGS 10

int exit_cond = 0;
int num_commands = 0;
int are_commands_sequential = 0;
int are_commands_parallel = 0;
int is_redirection = 0;
int is_cd = 0;

void handle_sigint(int signal)
{
	(void)signal;
}

void handle_sigtstp(int signal)
{
	(void)signal;
}

char ***parseInput(char *ip_command)
{
	char ***result = malloc(MAX_COMMANDS * sizeof(char **));
	int commandIndex = 0;

	if (strstr(ip_command, "##") != NULL)
		are_commands_sequential = 1;

	if (strstr(ip_command, "&&") != NULL)
		are_commands_parallel = 1;

	char *command;
	char *rest = ip_command;
	while ((command = strsep(&rest, "&&##")) != NULL)
	{
		if (*command != '\0')
		{
			result[commandIndex] = malloc(MAX_ARGS * sizeof(char *));
			int argIndex = 0;

			char *arg;
			while ((arg = strsep(&command, " ")) != NULL)
			{
				if (*arg != '\0')
				{
					if (strcmp(arg, "exit") == 0)
						exit_cond = 1;
					if (strcmp(arg, ">") == 0)
						is_redirection = 1;
					if (strcmp(arg, "cd") == 0)
						is_cd = 1;
					result[commandIndex][argIndex++] = strdup(arg);
				}
			}

			result[commandIndex][argIndex] = NULL;
			commandIndex++;
			num_commands++;
		}
	}

	result[commandIndex] = NULL;

	return result;
}

void executeCommand(char **arg_list)
{
	if (is_cd == 0)
	{
		int i = 0;
		while (arg_list[i] != NULL)
		{
			size_t len = strlen(arg_list[i]);
			if (len > 0 && arg_list[i][len - 1] == '\n')
			{
				arg_list[i][len - 1] = '\0';
			}
			i++;
		}

		int rc = fork();
		if (rc == 0)
		{
			if (execvp(arg_list[0], arg_list) == -1)
			{
				printf("Shell: Incorrect command\n");
				exit(1);
			}
		}
		else if (rc > 0)
		{
			wait(NULL);
		}
		else
		{
			printf("Shell: Fork failed");
			exit(1);
		}
	}
	else
	{
		if (chdir(arg_list[1]) != 0)
		{
			printf("Shell: No such directory exists\n");
		}
	}
}

void executeParallelCommands(char ***arg_list)
{
	for (int i = 0; i < num_commands; i++)
	{
		int rc = fork();
		if (rc == 0)
		{
			if (execvp(arg_list[i][0], arg_list[i]) == -1)
			{
				printf("Shell: Incorrect command\n");
				exit(1);
			}
		}
	}
	for (int i = 0; i < num_commands; i++)
	{
		wait(NULL);
	}
}

void executeSequentialCommands(char ***arg_list)
{
	for (int i = 0; i < num_commands; i++)
	{
		int rc = fork();
		if (rc == 0)
		{
			if (execvp(arg_list[i][0], arg_list[i]) == -1)
			{
				printf("Shell: Incorrect command\n");
				exit(1);
			}
		}
		else if (rc > 0)
		{
			wait(NULL);
		}
		else
		{
			exit(1);
		}
	}
}

void executeCommandRedirection(char **arg_list)
{
	int rc = fork();

	if (rc < 0)
	{
		exit(1);
	}
	else if (rc == 0)
	{
		int i = 0;
		while (arg_list[i] != NULL && strcmp(arg_list[i], ">") != 0)
		{
			i++;
		}

		if (arg_list[i] != NULL && arg_list[i + 1] != NULL)
		{
			close(STDOUT_FILENO);
			open(arg_list[i + 1], O_CREAT | O_WRONLY | O_TRUNC, S_IRWXU);

			arg_list[i] = NULL;

			execvp(arg_list[0], arg_list) == -1;
			exit(1);
		}
		else
		{
			exit(1);
		}
	}
	else
	{
		wait(NULL);
	}
}

int main()
{
	while (1)
	{
		signal(SIGINT, handle_sigint);
		signal(SIGTSTP, handle_sigtstp);

		char cwd[1024];

		if (getcwd(cwd, sizeof(cwd)) != NULL)
			printf("%s$ ", cwd);

		char *ip_command = NULL;
		size_t len = 0;
		getline(&ip_command, &len, stdin);

		ip_command[strcspn(ip_command, "\n")] = '\0';

		char ***arg_list = parseInput(ip_command);

		if (exit_cond)
		{
			printf("Exiting shell...\n");
			break;
		}

		if (are_commands_parallel && num_commands > 1)
		{
			executeParallelCommands(arg_list);
		}
		else if (are_commands_sequential && num_commands > 1)
		{
			executeSequentialCommands(arg_list);
		}
		else if (is_redirection)
		{
			executeCommandRedirection(arg_list[0]);
		}
		else if (num_commands == 1 || is_cd)
		{
			executeCommand(arg_list[0]);
		}

		for (int i = 0; arg_list[i] != NULL; i++)
		{
			for (int j = 0; arg_list[i][j] != NULL; j++)
			{
				free(arg_list[i][j]);
			}
			free(arg_list[i]);
		}
		free(arg_list);
		free(ip_command);

		num_commands = 0;
		are_commands_parallel = 0;
		are_commands_sequential = 0;
		is_redirection = 0;
		is_cd = 0;
	}

	return 0;
}