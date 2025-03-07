/* Name: Chase Bennett************************
***Course: Operating Systems 1****************
***Program: Homework 3 SMALLSH****
***Date: 11/14/2024****************************
*********************************************/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <signal.h>
#include <sys/types.h>

#define MAX_CMD_LEN 2048  // Maximum command length
#define MAX_ARGS 512      // Maximum number of arguments

// Global variables
int fg_only_mode = 0;        // Flag for foreground-only mode (1: enabled, 0: disabled)
int last_status = 0;         // Status of the last foreground process
pid_t bg_pids[MAX_ARGS];     // Array to store PIDs of background processes
int bg_count = 0;            // Counter for background processes

// Function declarations
void handle_SIGTSTP(int signo);
void handle_SIGINT(int signo);
void execute_command(char **args, int bg_process);
void expand_pid(char *input, char *output);
void check_bg_processes();

int main() {
    char *args[MAX_ARGS + 1];       // Array to hold command arguments
    char command[MAX_CMD_LEN];       // Command input from user
    char expanded_cmd[MAX_CMD_LEN];  // Command after expanding $$
    int bg_process;                  // Flag for background process

    // Set up SIGINT handler to ignore it in the shell itself
    struct sigaction SIGINT_action = {0};
    SIGINT_action.sa_handler = SIG_IGN;
    sigaction(SIGINT, &SIGINT_action, NULL);

    // Set up SIGTSTP handler to toggle foreground-only mode
    struct sigaction SIGTSTP_action = {0};
    SIGTSTP_action.sa_handler = handle_SIGTSTP;
    sigfillset(&SIGTSTP_action.sa_mask);
    SIGTSTP_action.sa_flags = SA_RESTART;
    sigaction(SIGTSTP, &SIGTSTP_action, NULL);

    while (1) {
        // Check for completed background processes
        check_bg_processes();

        // Display the prompt
        printf(": ");
        fflush(stdout);

        // Get command input from the user
        if (fgets(command, MAX_CMD_LEN, stdin) == NULL) {
            clearerr(stdin);  // Clear any error if fgets fails
            continue;
        }

        // Remove the newline character at the end of the command
        command[strcspn(command, "\n")] = '\0';

        // Skip empty lines and comments
        if (command[0] == '\0' || command[0] == '#') {
            continue;
        }

        // Expand $$ to the process ID of the shell
        expand_pid(command, expanded_cmd);

        // Parse the command and arguments
        int arg_count = 0;
        char *token = strtok(expanded_cmd, " ");
        bg_process = 0;  // Reset background process flag

        while (token != NULL && arg_count < MAX_ARGS) {
            // Check if & is at the end, indicating a background process
            if (strcmp(token, "&") == 0 && strtok(NULL, " ") == NULL) {
                bg_process = !fg_only_mode;  // Ignore & if in fg_only_mode
            } else {
                args[arg_count++] = token;
            }
            token = strtok(NULL, " ");
        }
        args[arg_count] = NULL;  // Null-terminate the argument list

        // Handle built-in commands
        if (args[0] != NULL) {
            if (strcmp(args[0], "exit") == 0) {
                exit(0);  // Exit the shell
            } else if (strcmp(args[0], "cd") == 0) {
                if (args[1] != NULL) {
                    if (chdir(args[1]) == -1) {
                        perror("cd");  // Error if directory change fails
                    }
                } else {
                    chdir(getenv("HOME"));  // Go to HOME directory if no argument
                }
            } else if (strcmp(args[0], "status") == 0) {
                // Display the exit status of the last foreground process
                if (WIFEXITED(last_status)) {
                    printf("exit value %d\n", WEXITSTATUS(last_status));
                } else if (WIFSIGNALED(last_status)) {
                    printf("terminated by signal %d\n", WTERMSIG(last_status));
                }
                fflush(stdout);
            } else {
                // For non-built-in commands
                execute_command(args, bg_process);
            }
        }
    }
    return 0;
}

// Handler for SIGTSTP (Ctrl+Z): toggles foreground-only mode
void handle_SIGTSTP(int signo) {
    fg_only_mode = !fg_only_mode;  // Toggle the mode

    char *message = fg_only_mode ? 
                    "\nEntering foreground-only mode (& is now ignored)\n" :
                    "\nExiting foreground-only mode\n";
    write(STDOUT_FILENO, message, strlen(message));  // Output message directly to stdout
    fflush(stdout);
}

// Expand $$ in the input string to the shell's PID
void expand_pid(char *input, char *output) {
    pid_t pid = getpid();
    char pid_str[20];
    sprintf(pid_str, "%d", pid);

    char *ptr = input;
    char *optr = output;

    while (*ptr) {
        if (*ptr == '$' && *(ptr + 1) == '$') {
            strcpy(optr, pid_str);  // Copy PID string in place of $$
            optr += strlen(pid_str);
            ptr += 2;
        } else {
            *optr++ = *ptr++;
        }
    }
    *optr = '\0';  // Null-terminate the output
}

// Execute non-built-in commands
void execute_command(char **args, int bg_process) {
    pid_t child_pid = fork();
    int child_status;

    if (child_pid == -1) {
        perror("fork failed");  // Error if fork fails
        exit(1);
    } else if (child_pid == 0) {
        // In the child process

        // Set up SIGINT handling for foreground processes only
        if (!bg_process) {
            struct sigaction SIGINT_action = {0};
            SIGINT_action.sa_handler = SIG_DFL;  // Default SIGINT for foreground
            sigaction(SIGINT, &SIGINT_action, NULL);
        }

        // Handle input/output redirection
        int fd;
        for (int i = 0; args[i] != NULL; i++) {
            if (strcmp(args[i], "<") == 0 && args[i + 1] != NULL) {
                fd = open(args[i + 1], O_RDONLY);  // Open input file
                if (fd == -1) {
                    fprintf(stderr, "Cannot open input file %s\n", args[i + 1]);
                    exit(1);
                }
                dup2(fd, 0);  // Redirect stdin
                close(fd);
                args[i] = NULL;
                break;
            } else if (strcmp(args[i], ">") == 0 && args[i + 1] != NULL) {
                fd = open(args[i + 1], O_WRONLY | O_CREAT | O_TRUNC, 0644);  // Open output file
                if (fd == -1) {
                    perror("Cannot open output file");
                    exit(1);
                }
                dup2(fd, 1);  // Redirect stdout
                close(fd);
                args[i] = NULL;
                break;
            }
        }

        // Redirect stdin/stdout to /dev/null for background processes
        if (bg_process) {
            int null_fd = open("/dev/null", O_RDONLY);
            dup2(null_fd, 0);
            close(null_fd);
        }

        // Execute the command
        if (execvp(args[0], args) == -1) {
            fprintf(stderr, "Command not found: %s\n", args[0]);
            exit(1);
        }
    } else {
        // In the parent process
        if (bg_process) {
            printf("background pid is %d\n", child_pid);  // Display background PID
            fflush(stdout);
            bg_pids[bg_count++] = child_pid;  // Store the background PID
        } else {
            waitpid(child_pid, &child_status, 0);  // Wait for the foreground process
            if (WIFEXITED(child_status)) {
                last_status = WEXITSTATUS(child_status);
            } else if (WIFSIGNALED(child_status)) {
                last_status = WTERMSIG(child_status);
                printf("terminated by signal %d\n", last_status);
                fflush(stdout);
            }
        }
    }
}

// Check for completed background processes
void check_bg_processes() {
    int child_status;
    for (int i = 0; i < bg_count; i++) {
        if (waitpid(bg_pids[i], &child_status, WNOHANG) > 0) {
            // Print the completion status of the background process
            printf("background pid %d is done: ", bg_pids[i]);
            if (WIFEXITED(child_status)) {
                printf("exit value %d\n", WEXITSTATUS(child_status));
            } else if (WIFSIGNALED(child_status)) {
                printf("terminated by signal %d\n", WTERMSIG(child_status));
            }
            fflush(stdout);

            // Remove the completed process from the array
            for (int j = i; j < bg_count - 1; j++) {
                bg_pids[j] = bg_pids[j + 1];
            }
            bg_count--;
            i--;
        }
    }
}

