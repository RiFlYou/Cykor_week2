#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/wait.h>
#include <limits.h>
#include <signal.h>


#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#define MAX_LINE 1024
#define MAX_ARGS 64

void print_prompt() {
    char cwd[PATH_MAX];
    getcwd(cwd, sizeof(cwd));
    printf("mysh:%s> ", cwd);
}

int parse_command(char *line, char **args) {
    int i = 0;
    args[i] = strtok(line, " \t\r\n");
    while (args[i] != NULL && i < MAX_ARGS - 1) {
        i++;
        args[++i] = strtok(NULL, " \t\r\n");
    }
    args[i] = NULL;

    if (i > 0 && strcmp(args[i - 1], "&") == 0) {
        args[i - 1] = NULL;  // & 제거
        return 1;            // 백그라운드
    }
    return 0;
}

void shell_cd(char **args) {
    if (args[1] == NULL) {
        fprintf(stderr, "cd: 경로가 없습니다\n");
    } else {
        if (chdir(args[1]) != 0) {
            perror("cd");
        }
    }
}

void shell_pwd() {
    char cwd[PATH_MAX];
    if (getcwd(cwd, sizeof(cwd)) != NULL) {
        printf("%s\n", cwd);
    } else {
        perror("pwd");
    }
}

void execute_command(char *line) {
    if (strchr(line, '|') != NULL) {
        execute_pipeline(line);
        return;
    }

    char *args[MAX_ARGS];
    int is_background = parse_command(line, args);
    if (args[0] == NULL) return;

    if (strcmp(args[0], "cd") == 0) {
        shell_cd(args);
    } 
    else if (strcmp(args[0], "pwd") == 0) {
        shell_pwd();
    } 
    else if (strcmp(args[0], "exit") == 0) {
        printf("Bye!\n");
        exit(0);
    } 
    else {
        pid_t pid = fork();
        if (pid == 0) {
            execvp(args[0], args);
            perror("exec");
            exit(1);
        } 
        else {
            if (!is_background) {
                waitpid(pid, NULL, 0);
            } 
            else {
                printf("[백그라운드 pid: %d]\n", pid);
            }
        }
    }
}

void execute_line(char *line) {
    char *command = strtok(line, ";");
    while (command != NULL) {
        // 앞뒤 공백 제거
        while (*command == ' ') command++;
        if (*command != '\0') {
            execute_command(command);  // 기존 함수 재사용
        }
        command = strtok(NULL, ";");
    }
}


void execute_pipeline(char *line) {
    char *commands[10];  // 최대 파이프 : 10개 
    int cmd_count = 0;

    // | 기준으로 명령어 분리
    commands[cmd_count] = strtok(line, "|");
    while (commands[cmd_count] != NULL && cmd_count < 9) {
        cmd_count++;
        commands[cmd_count] = strtok(NULL, "|");
    }

    int pipes[2 * (cmd_count - 1)];

    // 파이프 생성
    for (int i = 0; i < cmd_count - 1; i++) {
        pipe(pipes + i*2);
    }

    for (int i = 0; i < cmd_count; i++) {
        pid_t pid = fork();
        if (pid == 0) {
            // 입출력 연결
            if (i > 0) {
                dup2(pipes[(i - 1)*2], 0);  
            }
            if (i < cmd_count - 1) {
                dup2(pipes[i*2 + 1], 1);    
            }

            // 모든 pipe 닫기
            for (int j = 0; j < 2*(cmd_count - 1); j++) {
                close(pipes[j]);
            }

            // 명령어 
            char *args[MAX_ARGS];
            parse_command(commands[i], args);
            execvp(args[0], args);
            perror("exec");
            exit(1);
        }
    }

    // pipe 닫기
    for (int i = 0; i < 2*(cmd_count - 1); i++) {
        close(pipes[i]);
    }

    for (int i = 0; i < cmd_count; i++) {
        wait(NULL);
    }
}


int execute_conditional_line(char *line) {
    char *token;
    char *rest = line;
    int last_status = 0;
    int run_next = 1;
    char *op = NULL;

    while ((token = strsep(&rest, "&&||")) != NULL) {
        while (*token == ' ') token++;  // 앞 공백 제거
        if (*token == '\0') continue;

        // 실행 조건 확인
        if (run_next) {
            pid_t pid = fork();
            if (pid == 0) {
                char *args[MAX_ARGS];
                parse_command(token, args);
                execvp(args[0], args);
                perror("exec");
                exit(1);
            } else {
                int status;
                waitpid(pid, &status, 0);
                last_status = WIFEXITED(status) ? WEXITSTATUS(status) : 1;
            }
        }

        // 다음 연산자 추적
        if (rest != NULL) {
            op = rest - 2;  // '&&' 또는 '||' 위치 추정
            if (strncmp(op, "&&", 2) == 0)
                run_next = (last_status == 0);
            else if (strncmp(op, "||", 2) == 0)
                run_next = (last_status != 0);
        }
    }

    return last_status;
}

void handle_sigchld(int sig) {
    while (waitpid(-1, NULL, WNOHANG) > 0);
}

int main() {
    signal(SIGCHLD, handle_sigchld);
    char line[MAX_LINE];
    while (1) {
        print_prompt();
        if (fgets(line, MAX_LINE, stdin) == NULL) break;
        execute_line(line);
    }
    return 0;
}
