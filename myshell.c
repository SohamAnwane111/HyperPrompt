#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>

// constraints on number of commands & arguments.
#define MAX_COMMANDS 10
#define MAX_ARGS 10

// flags to segreggate type of command.
int exit_cond = 0;
int num_commands = 0;
int are_commands_sequential = 0;
int are_commands_parallel = 0;
int is_redirection = 0;
int is_cd = 0;
int is_pipeline = 0;
char current_working_directory[1024];
char *old_pwd = NULL;

// trap handler code for CTRL + C.
void handle_sigint(int signal)
{
	// typecasting from (int) -> (void) supresses the signal.
	(void)signal;
}

// trap handler code for CTRL + Z.
void handle_sigtstp(int signal)
{
	// typecasting from (int) -> (void) supresses the signal.
	(void)signal;
}

char ***parseInput(char *ip_command)
{
	// 2-D matrix (variable column and row length) of strings.
	char ***result = malloc(MAX_COMMANDS * sizeof(char **));
	int commandIndex = 0;

	// flipping sequential command flag.
	if (strstr(ip_command, "##") != NULL)
		are_commands_sequential = 1;

	// flipping parallel command flag.
	if (strstr(ip_command, "&&") != NULL)
		are_commands_parallel = 1;

	// flipping pipeline command flag.
	if (strstr(ip_command, "|") != NULL)
		is_pipeline = 1;

	char *command;
	char *rest = ip_command;
	while ((command = strsep(&rest, "&&##|")) != NULL)
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
					// flipping exit command flag.
					if (strcmp(arg, "exit") == 0)
						exit_cond = 1;

					// flipping redirection command flag.
					if (strcmp(arg, ">") == 0)
						is_redirection = 1;

					// flipping change directory command flag/
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
			int len = strlen(arg_list[i]);
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
		is_cd = 0;
		if (strcmp(arg_list[1], "-") != 0 && chdir(arg_list[1]) != 0)
		{
			printf("Shell: No such directory exists\n");
		}
		if (strcmp(arg_list[1], "-") == 0)
		{
			if (!old_pwd)
				printf("Shell: OLDPWD not set\n");
			else
			{
				chdir(old_pwd);
				printf("%s\n", old_pwd);		
				old_pwd = strdup(current_working_directory);
			}
		}
		
                old_pwd = strdup(current_working_directory);     
                getcwd(current_working_directory, sizeof(current_working_directory));
			
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
				// in case if execvp() fails.
				printf("Shell: Incorrect command\n");
				exit(1);
			}
		}
	}

	// these wait(NULL)s prevents the sub_commands from being orphan process.
	// ignoring these will kill the shell program before-hand.
	for (int i = 0; i < num_commands; i++)
	{
		wait(NULL);
	}
}

void executeSequentialCommands(char ***arg_list)
{
	for (int i = 0; i < num_commands; i++)
	{
		if (strcmp(arg_list[i][0], "cd") == 0)
		{
			is_cd = 1;
			executeCommand(arg_list[i]);
		}
		else
		{
			int rc = fork();
			if (rc == 0)
			{
				// Child process executes other commands.
				if (execvp(arg_list[i][0], arg_list[i]) == -1)
				{
					// If execvp() fails.
					printf("Shell: Incorrect command\n");
					exit(1);
				}
			}
			else if (rc > 0)
			{
				// Parent process waits for the child to finish.
				wait(NULL);
			}
			else
			{
				// If fork() fails.
				printf("Shell: Fork failed\n");
				exit(1);
			}
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

			execvp(arg_list[0], arg_list);
			exit(1);
		}
		else
		{
			// if input redirection command is incorrect.
			exit(1);
		}
	}
	else
	{
		// in case if fork() fails.
		wait(NULL);
	}
}

void executePipelineCommands(char ***arg_list)
{
    int num_pipes = num_commands - 1;
    int pipefds[2 * num_pipes];

    // Creating pipes.
    for (int i = 0; i < num_pipes; i++)
    {
        if (pipe(pipefds + i * 2) < 0)
        {
            perror("Shell: Pipe creation failed");
            exit(1);
        }
    }

    int commandIndex = 0;
    while (arg_list[commandIndex] != NULL)
    {
        int pid = fork();

        if (pid == 0)
        {
            // If not the first command, getting input from the previous pipe.
            if (commandIndex > 0)
            {
                if (dup2(pipefds[(commandIndex - 1) * 2], STDIN_FILENO) < 0)
                {
                    perror("Shell: dup2 failed");
                    exit(1);
                }
            }

            // If not the last command, output to the next pipe.
            if (arg_list[commandIndex + 1] != NULL)
            {
                if (dup2(pipefds[commandIndex * 2 + 1], STDOUT_FILENO) < 0)
                {
                    perror("Shell: dup2 failed");
                    exit(1);
                }
            }

            // Close all pipes in the child process.
            for (int i = 0; i < 2 * num_pipes; i++)
            {
                close(pipefds[i]);
            }

            // Execute command.
            if (execvp(arg_list[commandIndex][0], arg_list[commandIndex]) == -1)
            {
                perror("Shell: Command execution failed");
                exit(1);
            }
        }
        else if (pid < 0)
        {
            perror("Shell: Fork failed");
            exit(1);
        }

        commandIndex++;
    }

    // Parent process must close all pipes.
    for (int i = 0; i < 2 * num_pipes; i++)
    {
        close(pipefds[i]);
    }

    // Parent must wait for all child processes.
    for (int i = 0; i < num_commands; i++)
    {
        wait(NULL);
    }
}

int main()
{
	while (1)
	{
		// triggers the trap handler code defined above.
		signal(SIGINT, handle_sigint);
		signal(SIGTSTP, handle_sigtstp);

		// using cwd(), fetching the current working directory.
		if (getcwd(current_working_directory, sizeof(current_working_directory)) != NULL)
			printf("%s$", current_working_directory);

		// using getline(), fetching the raw user given command.
		char *ip_command = NULL;
		size_t len = 0;
		getline(&ip_command, &len, stdin);

		ip_command[strcspn(ip_command, "\n")] = '\0';

		// how does parseInput() processes the raw user command -->
		// 1) Breaks the command with delimiter "##", "&&", or "|"
		// 2) Now we get sub-commands from step-1, It breaks this sub-commands with delimiter " ".
		// 3) thus, we obtain a 2-D matrix of strings.
		// 4) where each rows represents a complete sub-command.
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
		else if(is_pipeline)
		{
			printf("Pipeline...\n");
			executePipelineCommands(arg_list);
		}

		// freeing the memory/resources as a single user command terminates.
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

		// flipping all the flags to 0 after single user command termination.
		num_commands = 0;
		are_commands_parallel = 0;
		are_commands_sequential = 0;
		is_redirection = 0;
		is_cd = 0;
	}

	return 0;
}
