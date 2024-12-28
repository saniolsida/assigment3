#include <stdio.h>
#include <stdlib.h>
#include <netinet/in.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>

#define EPOLL_SIZE 1
#define BUF_SIZE 1024
#define FILE_SIZE 512
#define NEW_FILE_SIZE 1024

typedef struct
{
    char file_name[FILE_SIZE];
    char file_path[FILE_SIZE];
    int bytes;
} file_info_t;

void get_file_info(const char *folder_name, file_info_t (*file_info)[10], int *index, int fd);
void error_handling(char *message);
void get_parent_path(char *path);

int main(int argc, char *argv[])
{
    int serv_sock;
    int clnt_sock;
    int total_bytes = 0;
    char buf[BUF_SIZE], message[BUF_SIZE];
    int str_len;
    struct sockaddr_in serv_adr, clnt_adr;
    file_info_t file_info[10][10];
    socklen_t clnt_adr_sz;
    int index[10];

    struct epoll_event *ep_events;
    struct epoll_event event;
    int epfd, event_cnt;
    char *curr_dir = getcwd(NULL, 0);
    char *cur_path[10];

    serv_sock = socket(PF_INET, SOCK_STREAM, 0);
    if (serv_sock == -1)
        error_handling("UDP socket creation error");

    memset(&serv_adr, 0, sizeof(serv_adr));
    serv_adr.sin_family = AF_INET;
    serv_adr.sin_addr.s_addr = htonl(INADDR_ANY);
    serv_adr.sin_port = htons(atoi(argv[1]));

    if (bind(serv_sock, (struct sockaddr *)&serv_adr, sizeof(serv_adr)) == -1)
        error_handling("bind() error");

    if (listen(serv_sock, 5) == -1)
        error_handling("listen() error");

    // get_file_info(curr_dir, file_info, &index);

    epfd = epoll_create(EPOLL_SIZE);
    ep_events = malloc(sizeof(struct epoll_event) * EPOLL_SIZE);

    event.events = EPOLLIN;
    event.data.fd = serv_sock;
    epoll_ctl(epfd, EPOLL_CTL_ADD, serv_sock, &event);

    while (1)
    {
        event_cnt = epoll_wait(epfd, ep_events, EPOLL_SIZE, -1);
        if (event_cnt == -1)
        {
            puts("epoll_wait() error");
            break;
        }
        for (int i = 0; i < event_cnt; i++)
        {
            if (ep_events[i].data.fd == serv_sock) // 최초 연결
            {
                clnt_adr_sz = sizeof(clnt_adr);
                clnt_sock = accept(serv_sock, (struct sockaddr *)&clnt_adr, &clnt_adr_sz);
                event.events = EPOLLIN;
                event.data.fd = clnt_sock;
                epoll_ctl(epfd, EPOLL_CTL_ADD, clnt_sock, &event);

                printf("connected client: %d \n", clnt_sock);

                get_file_info(curr_dir, file_info, &index[i], i);

                cur_path[i] = strdup(curr_dir);

                snprintf(message, sizeof(message), "-1) Send File to server");
                write(clnt_sock, message, sizeof(message));
                for (int j = 0; j < index[i]; j++)
                {
                    if (file_info[i][j].bytes == -1)
                    {
                        snprintf(message, sizeof(message), "%d) %s/", j + 1, file_info[i][j].file_name);
                    }
                    else
                    {
                        snprintf(message, sizeof(message), "%d) %s, %d bytes", j + 1, file_info[i][j].file_name, file_info[i][j].bytes);
                    }

                    write(clnt_sock, message, sizeof(message));
                }
                write(clnt_sock, "[END]", 5);
                printf("\n");
            }
            else
            {
                str_len = read(ep_events[i].data.fd, buf, BUF_SIZE);
                if (str_len == 0)
                {
                    epoll_ctl(epfd, EPOLL_CTL_DEL, ep_events[i].data.fd, NULL);
                    close(ep_events[i].data.fd);

                    memset(file_info[i], 0, 10 * sizeof(file_info_t));
                    index[i] = 0;
                    if (cur_path[i])
                    {
                        free(cur_path[i]);
                        cur_path[i] = NULL; // 포인터 초기화
                    }

                    printf("closed client: %d \n", ep_events[i].data.fd);
                }
                else
                {
                    buf[str_len] = '\0';
                    printf("\nclient %d request file or folder number: %s\n", ep_events[i].data.fd, buf);

                    int sel_index = atoi(buf) - 1;

                    if (sel_index == -2) // receive file from client
                    {
                        write(ep_events[i].data.fd, "[SEND]", 6);
                        char response[BUF_SIZE];

                        str_len = read(ep_events[i].data.fd, response, BUF_SIZE);

                        char file_name[FILE_SIZE];
                        char new_file_name[BUF_SIZE];
                        char *name = response + 6;
                        char *end = strstr(name, "[END]");
                        if (end)
                            *end = '\0';

                        char *ptr = strtok(name, " ");
                        snprintf(file_name, sizeof(file_name), "%s", ptr);

                        ptr = strtok(NULL, " ");

                        int bytes = atoi(ptr);

                        printf("sending file %s, bytes %d\n", file_name, bytes);

                        memset(&message, 0, sizeof(message));


                        snprintf(new_file_name, sizeof(new_file_name),"%s/%s",cur_path[i],file_name);
                        FILE *fp = fopen(new_file_name, "wb");
                        if (!fp)
                            error_handling("fopen() failed");

                        while (bytes > 0)
                        {
                            str_len = read(ep_events[i].data.fd, message, BUF_SIZE);

                            bytes -= str_len;

                            fwrite((void *)message, 1, str_len, fp);
                        }

                        fclose(fp);
                        snprintf(message, sizeof(message), "파일 전송 성공: %s", file_name);

                        write(ep_events[i].data.fd, message, strlen(message));
                        
                        index[i] = 0;

                        get_file_info(cur_path[i], file_info, &index[i], i);

                        snprintf(message, sizeof(message), "-1) Send File to server");
                            write(ep_events[i].data.fd, message, sizeof(message));

                            if (strcmp(cur_path[i], curr_dir) != 0) // 현재 위치가 최상위가 아니라면
                            {
                                snprintf(message, sizeof(message), "0) Back");
                                write(ep_events[i].data.fd, message, sizeof(message));
                            }

                            for (int j = 0; j < index[i]; j++)
                            {
                                if (file_info[i][j].bytes == -1)
                                {
                                    snprintf(message, sizeof(message), "%d) %s/", j + 1, file_info[i][j].file_name);
                                }
                                else
                                {
                                    snprintf(message, sizeof(message), "%d) %s, %d bytes", j + 1, file_info[i][j].file_name, file_info[i][j].bytes);
                                }

                                write(ep_events[i].data.fd, message, sizeof(message));
                            }
                            write(ep_events[i].data.fd, "[END]", 5);
                    }
                    else if (sel_index == -1) // back to parent path
                    {
                        index[i] = 0;

                        get_parent_path(cur_path[i]); // 부모 경로 찾기

                        get_file_info(cur_path[i], file_info, &index[i], i);

                        write(ep_events[i].data.fd, "[DIR]", 5);

                        memset(&message, 0, sizeof(message));

                        while (1)
                        {
                            str_len = read(ep_events[i].data.fd, message, BUF_SIZE);
                            if (strcmp(message, "Thankyou") == 0)
                                break;
                        }

                        snprintf(message, sizeof(message), "-1) Send File to server");
                        write(ep_events[i].data.fd, message, sizeof(message));

                        if (strcmp(cur_path[i], curr_dir) != 0) // 현재 위치가 최상위가 아니라면
                        {
                            snprintf(message, sizeof(message), "0) Back");
                            write(ep_events[i].data.fd, message, sizeof(message));
                        }
                        for (int j = 0; j < index[i]; j++)
                        {
                            if (file_info[i][j].bytes == -1)
                            {
                                snprintf(message, sizeof(message), "%d) %s/", j + 1, file_info[i][j].file_name);
                            }
                            else
                            {
                                snprintf(message, sizeof(message), "%d) %s, %d bytes", j + 1, file_info[i][j].file_name, file_info[i][j].bytes);
                            }

                            write(ep_events[i].data.fd, message, sizeof(message));
                        }
                        write(ep_events[i].data.fd, "[END]", 5);
                        printf("\n");
                    }
                    else if (sel_index + 1 <= index[i] && sel_index + 1 > 0)
                    {

                        if (file_info[i][sel_index].bytes == -1) // 디렉토리
                        {
                            write(ep_events[i].data.fd, "[DIR]", 5);

                            memset(&message, 0, sizeof(message));

                            while (1)
                            {
                                str_len = read(ep_events[i].data.fd, message, BUF_SIZE);
                                if (strcmp(message, "Thankyou") == 0)
                                    break;
                            }

                            index[i] = 0;

                            cur_path[i] = strdup(file_info[i][sel_index].file_path);

                            printf("current path: %s", cur_path[i]);
                            get_file_info(file_info[i][sel_index].file_path, file_info, &index[i], i);

                            snprintf(message, sizeof(message), "-1) Send File to server");
                            write(ep_events[i].data.fd, message, sizeof(message));

                            snprintf(message, sizeof(message), "0) Back");
                            write(ep_events[i].data.fd, message, sizeof(message));
                            for (int j = 0; j < index[i]; j++)
                            {
                                if (file_info[i][j].bytes == -1)
                                {
                                    snprintf(message, sizeof(message), "%d) %s/", j + 1, file_info[i][j].file_name);
                                }
                                else
                                {
                                    snprintf(message, sizeof(message), "%d) %s, %d bytes", j + 1, file_info[i][j].file_name, file_info[i][j].bytes);
                                }

                                write(ep_events[i].data.fd, message, sizeof(message));
                            }

                            write(ep_events[i].data.fd, "[END]", 5);
                            printf("\n");
                        }
                        else
                        {
                            char tmp_file_path[NEW_FILE_SIZE];

                            snprintf(tmp_file_path, NEW_FILE_SIZE, "%s/%s", file_info[i][sel_index].file_path, file_info[i][sel_index].file_name);
                            FILE *fp = fopen(tmp_file_path, "rb");

                            if (!fp)
                                error_handling("fopen() failed");

                            snprintf(message, sizeof(message), "[FILE]%s %d[END]", file_info[i][sel_index].file_name, file_info[i][sel_index].bytes);
                            write(ep_events[i].data.fd, message, strlen(message));

                            memset(&message, 0, sizeof(message));

                            while (1)
                            {
                                str_len = read(ep_events[i].data.fd, message, BUF_SIZE);
                                if (strcmp(message, "[FILE]") == 0)
                                    break;
                            }

                            memset(&message, 0, sizeof(message));

                            while (1)
                            {
                                str_len = fread((void *)message, 1, BUF_SIZE, fp);
                                if (str_len < BUF_SIZE)
                                {
                                    write(ep_events[i].data.fd, message, str_len);
                                    break;
                                }
                                write(ep_events[i].data.fd, message, BUF_SIZE);
                            }

                            memset(&message, 0, sizeof(message));

                            printf("Send to Client %d success\n", ep_events[i].data.fd);
                            while (1)
                            {
                                str_len = read(ep_events[i].data.fd, message, BUF_SIZE);
                                if (strncmp(message, "[ENDFILE]", 9) == 0)
                                    break;
                            }

                            snprintf(message, sizeof(message), "-1) Send File to server");
                            write(ep_events[i].data.fd, message, sizeof(message));

                            if (strcmp(cur_path[i], curr_dir) != 0) // 현재 위치가 최상위가 아니라면
                            {
                                snprintf(message, sizeof(message), "0) Back");
                                write(ep_events[i].data.fd, message, sizeof(message));
                            }

                            for (int j = 0; j < index[i]; j++)
                            {
                                if (file_info[i][j].bytes == -1)
                                {
                                    snprintf(message, sizeof(message), "%d) %s/", j + 1, file_info[i][j].file_name);
                                }
                                else
                                {
                                    snprintf(message, sizeof(message), "%d) %s, %d bytes", j + 1, file_info[i][j].file_name, file_info[i][j].bytes);
                                }

                                write(ep_events[i].data.fd, message, sizeof(message));
                            }
                            write(ep_events[i].data.fd, "[END]", 5);
                        }
                    }
                }
            }
        }
    }

    close(serv_sock);
    close(epfd);
    return 0;
}
void get_parent_path(char *path)
{
    char *ptr = strrchr(path, '/');

    if (ptr)
        *ptr = '\0';

    printf("path %s", path);
}

void get_file_info(const char *folder_name, file_info_t (*file_info)[10], int *index, int fd)
{
    DIR *dir = opendir(folder_name);
    struct stat sb;
    if (!dir)
        error_handling("opendir() error");

    struct dirent *entry;
    struct stat st;

    while ((entry = readdir(dir)) != NULL)
    {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
        {
            continue;
        }

        if (*index >= BUF_SIZE)
        {
            fprintf(stderr, "index == BUF_SIZE\n");
            return;
        }

        char path[FILE_SIZE];
        snprintf(path, sizeof(path), "%s/%s", folder_name, entry->d_name);
        if (stat(path, &st) == -1)
            error_handling("stat() failed");

        if (S_ISREG(st.st_mode))
        {
            strcpy(file_info[fd][*index].file_path, folder_name); // 파일 경로 저장

            char *ptr = strtok(path, "/");
            char *file_name;
            while (ptr != NULL)
            {
                file_name = ptr;
                ptr = strtok(NULL, "/");
            }
            strcpy(file_info[fd][*index].file_name, file_name); // 파일 이름 저장

            // stat
            char tmp_file_path[NEW_FILE_SIZE];
            snprintf(tmp_file_path, NEW_FILE_SIZE, "%s/%s", file_info[fd][*index].file_path, file_info[fd][*index].file_name);
            stat(tmp_file_path, &st);

            file_info[fd][*index].bytes = st.st_size;
            (*index)++;
        }
        if (S_ISDIR(st.st_mode))
        {
            char tmp_path[FILE_SIZE];
            strcpy(file_info[fd][*index].file_path, path); // 폴더 경로 저장
            strcpy(tmp_path, path);
            char *ptr = strtok(path, "/");
            char *file_name;

            while (ptr != NULL)
            {
                file_name = ptr;
                ptr = strtok(NULL, "/");
            }
            strcpy(file_info[fd][*index].file_name, file_name); // 폴더 이름 저장

            file_info[fd][*index].bytes = -1;
            (*index)++;
            // get_file_info(tmp_path, file_info, index);
        }
    }

    closedir(dir);
}

void error_handling(char *message)
{
    fputs(message, stderr);
    fputc('\n', stderr);
    exit(1);
}
