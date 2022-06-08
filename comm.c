#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <stdlib.h>
#include <ctype.h>
#include <unistd.h>
#include <sys/types.h>
#include <stdint.h>
#include <sys/wait.h>
#include <errno.h>
#define BUF_LEN (100)
#define MAX_COMMAND (8)

uint64_t swap_uint64(uint64_t val) {
    val = ((val << 8u) & 0xFF00FF00FF00FF00ULL )
          | ((val >> 8u) & 0x00FF00FF00FF00FFULL );
    val = ((val << 16u) & 0xFFFF0000FFFF0000ULL )
          | ((val >> 16u) & 0x0000FFFF0000FFFFULL );
    return (val << 32u) | (val >> 32u);
}


/* protocol
 * message format:
    1.from parent to child
        first byte:
            'E' for error or no operation
            'O' for operation

        second byte:
            if 'O':
                'G' for get
                'T' for toggle
                'D' for destroy
                'S' for store
        3 - 6 bytes:
            the storing value
    2. from child to parent
        first 8 bytes:
            num of values n
        n * 4 bytes:
            n int32_t values
 */

struct storage {
    int32_t* data;
    uint64_t size;
    struct process_pipe* pipe_info;
};

struct process_pipe {
    int p1c0_pipe[2]; // 0 -- read | 1 -- write
    int p0c1_pipe[2];
    int id;
    pid_t pid;
};

volatile int state = 1;
int storage_id;

void signal_handler(int signum) {
    char buf[BUF_LEN];
    memset(buf, 0, BUF_LEN);
    if (state == 0) {
        state = 1;
        sprintf(buf, "Storage #%d is mutable\n", storage_id);
        write(STDOUT_FILENO, buf, strlen(buf) + 1);
    } else {
        state = 0;
        sprintf(buf, "Storage #%d is immutable\n", storage_id);
        write(STDOUT_FILENO, buf, strlen(buf) + 1);
    }
}

int main() {

    struct process_pipe** pipes = NULL;
    struct storage* storage = malloc(sizeof(struct storage));
    int global_id = 0;
    int child_num = 0;
    int is_parent = 1;
    char buf[BUF_LEN];
    char command[MAX_COMMAND];


    while (1) {

        if (is_parent) {
            memset(command, 0, MAX_COMMAND);
            int id;
            int32_t value;
            int input_num;
            fgets(buf, BUF_LEN, stdin);
            input_num = sscanf(buf, " %s %d %d", command, &id, &value);
            for (int i = 0; i < strlen(command); i++) {
                command[i] = toupper(command[i]);
            }

            if (input_num == 3) {
                if (strcmp(command, "STORE") == 0) {
                    /* execute store operation */
                    printf("command: store\n");
                    char request = 'S';
                    int find = 0;
                    for (int i = 0; i < child_num; i++) {
                        if (pipes[i]->id == id) {
                            write(pipes[i]->p1c0_pipe[1], &request, 1);
                            write(pipes[i]->p1c0_pipe[1], &value, sizeof(int32_t));
                            find = 1;
                            break;
                        }
                    }
                    if (find == 0) {
                        // no such id
                        fprintf(stderr, "Unable to process request.\n");
                        fflush(stderr);
                    }

                } else {
                    fprintf(stderr, "Unable to process request.\n");
                    fflush(stderr);
                }
            } else if (input_num == 2) {
                if (strcmp(command, "GET") == 0) {
                    /* execute get operation */
                    char request = 'G';
                    struct process_pipe* current_pipe = NULL;
                    for (int i = 0; i < child_num; i++) {
                        if (pipes[i]->id == id) {
                            current_pipe = pipes[i];
                            write(pipes[i]->p1c0_pipe[1], &request, 1);
                            break;
                        }
                    }
                    if (current_pipe == NULL) {
                        // no such id
                        fprintf(stderr, "Unable to process request.\n");
                        fflush(stderr);
                        continue;
                    }
                    // wait for the response from child
                    uint64_t num;
                    read(current_pipe->p0c1_pipe[0], &num, sizeof(uint64_t));
                    int32_t* values = malloc(sizeof(int32_t) * num);
                    read(current_pipe->p0c1_pipe[0], values, sizeof(int32_t) * num);
                    for (uint64_t i = 0; i < num; i++) {
                        fprintf(stdout, "%d\n", values[i]);
                    }
                    fflush(stdout);
                    free(values);
                } else if (strcmp(command, "DESTROY") == 0) {
                    /* execute destroy operation */
                    char request = 'D';
                    int index = -1;
                    for (int i = 0; i < child_num; i++) {
                        if (pipes[i]->id == id) {
                            index = i;
                            write(pipes[i]->p1c0_pipe[1], &request, 1);
                            break;
                        }
                    }
                    if (index == -1) {
                        // no such id
                        fprintf(stderr, "Unable to process request.\n");
                        fflush(stderr);
                        continue;
                    }
                    printf("freeing %p\n", (void*)pipes[index]);
                    close(pipes[index]->p0c1_pipe[0]);
                    close(pipes[index]->p1c0_pipe[1]);
                    free(pipes[index]);
                    for (int i = index; i < child_num - 1; i++) {
                        pipes[i] = pipes[i + 1];
                    }
                    child_num--;

                } else if (strcmp(command, "TOGGLE") == 0) {
                    /* execute toggle operation */
                    char request = 'T';
                    int find = 0;
                    for (int i = 0; i < child_num; i++) {
                        if (pipes[i]->id == id) {
                            write(pipes[i]->p1c0_pipe[1], &request, 1);
                            find = 1;
                            break;
                        }
                    }

                    if (find == 0) {
                        // no such id
                        fprintf(stderr, "Unable to process request.\n");
                        fflush(stderr);
                    }

                } else {
                    fprintf(stderr, "Unable to process request.\n");
                    fflush(stderr);


                }

            } else if (input_num == 1) {
                if (strcmp(command, "LAUNCH") == 0) {
                    fprintf(stdout, "Launced a new storage of id %d\n", global_id);
                    fflush(stdout);
                    /* execute launch operation */
                    // 0. we only modify on parent
                    // 1. add one more pipe
                    // 2. update child process number
                    child_num++;
                    pipes = realloc(pipes, sizeof(struct process_pipe *) * child_num);
                    struct process_pipe* pipe_info = malloc(sizeof(struct process_pipe));
                    pipes[child_num - 1] = pipe_info;
                    pipe_info->id = global_id++;
                    int ret = pipe(pipe_info->p1c0_pipe);
                    if (ret == -1) {
                        perror("p1c0 pipe error");
                        return 1;
                    }
                    ret = pipe(pipe_info->p0c1_pipe);
                    if (ret == -1) {
                        perror("p0c1 pipe error");
                        return 1;
                    }
                    is_parent = fork();
                    if (is_parent == 0) {
                        // new child process
                        for (int i = 0; i < child_num; i++) {
                            if (pipes[i]->id != pipe_info->id) {
                                free(pipes[i]);
                            }
                        }
                        free(pipes);
                        struct sigaction sa = {.sa_handler = signal_handler, .sa_flags = SA_RESTART};

                        sigaction(SIGUSR1, &sa, NULL);

                        storage->size = 0;
                        storage->data = NULL;

                        close(pipe_info->p0c1_pipe[0]); // close read (parent read)
                        close(pipe_info->p1c0_pipe[1]); // close write parent write)

                        storage->pipe_info = pipe_info;
                        storage->pipe_info->pid = getgid();
                        storage_id = pipe_info->id;
                    } else {
                        // parent
                        printf("pid = %d\n", is_parent);
                        close(pipe_info->p1c0_pipe[0]); // close read (child read)
                        close(pipe_info->p0c1_pipe[1]);// close write (child write)

                    }
                } else if (strcmp(command, "EXIT") == 0) {
                    /* execute exit operation */
                    char request = 'D';
                    for (int i = 0; i < child_num; i++) {
                        printf("parent freeing %d\n", pipes[i]->id);
                        write(pipes[i]->p1c0_pipe[1], &request, 1);
                    }
                    pid_t wpid;
                    int status = 0;
                    while ((wpid = wait(&status)) > 0);
                    for (int i = 0; i < child_num; i++) {
                        printf("parent freeing %p | %d\n", (void*)pipes[i], pipes[i]->id);
                        close(pipes[i]->p0c1_pipe[0]);
                        close(pipes[i]->p1c0_pipe[1]);
                        free(pipes[i]);
                    }
                    free(storage);
                    free(pipes);
                    exit(0);

                } else if (strcmp(command, "LIST") == 0) {
                    printf("There are %d storages\n", child_num);
                    for (int i = 0; i < child_num; i++) {
                        printf("storage id <%d>\n", pipes[i]->id);
                    }

                } else {
                    fprintf(stderr, "Unable to process request.\n");
                    fflush(stderr);
                }
            } else {
                fprintf(stderr, "Unable to process request.\n");
                fflush(stderr);

            }


        } else {

            char second;
            read(storage->pipe_info->p1c0_pipe[0], &second, 1);

            if (second == 'G') {
                write(storage->pipe_info->p0c1_pipe[1], &storage->size, sizeof(uint64_t));
                write(storage->pipe_info->p0c1_pipe[1], storage->data, sizeof(int32_t) * storage->size);


            } else if (second == 'T') {
                if (state == 0) {
                    state = 1;
                    fprintf(stdout, "Storage #%d is mutable\n", storage_id);
                }
                fflush(stdout);
            } else if (second == 'D') {
                // free storage
                free(storage->data);
                printf("child %d freeing %p\n", storage_id, (void*)storage->pipe_info);
                close(storage->pipe_info->p0c1_pipe[1]);
                close(storage->pipe_info->p1c0_pipe[0]);
                free(storage->pipe_info);
                free(storage);
                _exit(EXIT_SUCCESS);
            } else if (second == 'S') {
                int32_t value;
                read(storage->pipe_info->p1c0_pipe[0], &value, sizeof(int32_t));
                if (state == 1) {
                    // mutable
                    fprintf(stdout, "Store value: %d to storage of id: %d\n", value, storage_id);
                    fflush(stdout);
                    storage->size++;
                    storage->data = realloc(storage->data, storage->size * sizeof(int32_t));
                    storage->data[storage->size - 1] = value;
                }

            }
            fprintf(stdout, "(errno = %d): %s\n", errno, strerror(errno));
            fprintf(stdout, "\n");

        }


    }
    return 0;
}










//gcc -Wall -pedantic -g comm.c -o c
//valgrind --leak-check=full ./c
