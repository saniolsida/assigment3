#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <dirent.h>
#include <sys/stat.h>

#define BUF_SIZE 1024
#define FILE_SIZE 64
#define WORD_SIZE 32

typedef struct
{
    int seq;
    char file_name[FILE_SIZE];
    char file_path[FILE_SIZE];
    int bytes;
} file_info_t;

void error_handling(char *message);
void get_file_info(const char *folder_name, file_info_t file_info[], int *index);
void get_file_from_server(int sock, char message[BUF_SIZE],
                          int bytes);
void update_receive_file_info(int sock);

file_info_t recv_file_info[FILE_SIZE];
int recv_index = 0;
char server_curr_dir[FILE_SIZE];
char server_root_dir[FILE_SIZE];

int main(int argc, char *argv[])
{
    int sock;
    char message[BUF_SIZE];
    int str_len, bytes;
    struct sockaddr_in serv_adr;
    file_info_t file_info[BUF_SIZE];

    char *curr_dir = getcwd(NULL, 0);

    int index = 0;
    int sock_num = 0;

    if (argc != 3)
    {
        printf("Usage : %s <IP> <port>\n", argv[0]);
        exit(1);
    }

    sock = socket(PF_INET, SOCK_STREAM, 0);
    if (sock == -1)
        error_handling("socket() error");

    memset(&serv_adr, 0, sizeof(serv_adr));
    serv_adr.sin_family = AF_INET;
    serv_adr.sin_addr.s_addr = inet_addr(argv[1]);
    serv_adr.sin_port = htons(atoi(argv[2]));

    if (connect(sock, (struct sockaddr *)&serv_adr, sizeof(serv_adr)) == -1)
        error_handling("connect() error!");
    else
        puts("Connected...........");

    update_receive_file_info(sock);

    strcpy(server_root_dir, recv_file_info[0].file_path);
    strcpy(server_curr_dir, recv_file_info[0].file_path);

    index = 0; // client의 파일 개수
    while (1)
    {
        fputs("\nSelect file or folder(Q to quit): ", stdout);
        fgets(message, BUF_SIZE, stdin);
        printf("\n---------------------\n");
        if (!strcmp(message, "q\n") || !strcmp(message, "Q\n"))
            break;

        char *endptr;
        long num = strtol(message, &endptr, 10);

        if (*endptr != '\n' && *endptr != '\0')
        {
            printf("Invalid input. Please enter a number or Q to quit.\n");
            continue;
        }
        else
        {
            write(sock, message, strlen(message));
        }

        str_len = read(sock, message, BUF_SIZE - 1);

        message[str_len] = 0;
        if (strncmp(message, "[FILE]", 6) == 0) // 파일을 선택
        {
            get_file_from_server(sock, message, bytes);
        }
        else if (strncmp(message, "[DIR]", 5) == 0) // dir select
        {
            char *dir_path = message + 5;

            strcpy(server_curr_dir, dir_path);

            printf("open dir: %s\n\n", server_curr_dir);
            update_receive_file_info(sock);
        }
        else if (strncmp(message, "[BACK]", 6) == 0)
        {
            char *dir_path = message + 6;
            strcpy(server_curr_dir, dir_path);

            update_receive_file_info(sock);
        }
        else if (strncmp(message, "[SEND]", 6) == 0)
        {
            int input = 0;
            get_file_info(curr_dir, file_info, &index);

            for (int i = 0; i < index; i++)
            {
                snprintf(message, sizeof(message), "%d) %s, %d bytes", i + 1, file_info[i].file_name, file_info[i].bytes);
                printf("%s\n", message);
            }
            while (1)
            {
                fputs("\nSelect file which want to send: ", stdout);
                fgets(message, BUF_SIZE, stdin);

                input = atoi(message) - 1;

                if (input + 1 <= index && input + 1 > 0)
                {
                    break;
                }
                else
                {
                    printf("Wrong input\n");
                }
            }

            snprintf(message, sizeof(message), "[FILE]%s %d[END]", file_info[input].file_name, file_info[input].bytes);
            write(sock, message, strlen(message));

            FILE *fp = fopen(file_info[input].file_path, "rb");

            if (!fp)
                error_handling("fopen() failed");

            memset(&message, 0, sizeof(message));

            while (1)
            {
                str_len = fread((void *)message, 1, BUF_SIZE, fp);
                if (str_len < BUF_SIZE)
                {
                    write(sock, message, str_len);
                    break;
                }
                write(sock, message, BUF_SIZE);
            }

            fclose(fp);

            memset(&message, 0, sizeof(message));

            printf("---------------------\n");

            update_receive_file_info(sock);
        }
        else
        {
            printf("%s", message);
        }
    }

    close(sock);
    return 0;
}

void get_file_info(const char *folder_name, file_info_t file_info[], int *index)
{
    (*index) = 0;
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

        char path[BUF_SIZE];
        snprintf(path, sizeof(path), "%s/%s", folder_name, entry->d_name);
        if (stat(path, &st) == -1)
            error_handling("stat() failed");

        if (S_ISREG(st.st_mode))
        {
            strcpy(file_info[*index].file_path, path); // 파일 경로 저장

            char *ptr = strtok(path, "/");
            char *file_name;
            while (ptr != NULL)
            {
                file_name = ptr;
                ptr = strtok(NULL, "/");
            }
            strcpy(file_info[*index].file_name, file_name); // 파일 이름 저장
            // stat
            stat(file_info[*index].file_path, &st);

            file_info[*index].bytes = st.st_size;
            (*index)++;
        }
        if (S_ISDIR(st.st_mode))
        {
            get_file_info(path, file_info, index);
        }
    }

    closedir(dir);
}

void get_file_from_server(int sock, char message[BUF_SIZE], int bytes)
{
    FILE *fp;
    int str_len;
    char buf[BUF_SIZE];
    char file_name[FILE_SIZE];
    char *name = message + 6;

    char *end = strstr(name, "[END]");
    if (end)
        *end = '\0';

    char *ptr = strtok(name, " ");

    snprintf(file_name, sizeof(file_name), "%s", ptr);

    ptr = strtok(NULL, " ");

    bytes = atoi(ptr);

    printf("file name: %s, bytes: %d\n", file_name, bytes);

    file_name[strlen(file_name)] = '\0';

    fp = fopen(file_name, "wb");
    if (!fp)
        error_handling("fopen() failed");

    write(sock, file_name, strlen(file_name));

    while (bytes > 0)
    {
        str_len = read(sock, buf, BUF_SIZE);
        fwrite((void *)buf, 1, str_len, fp);

        bytes -= str_len;
    }
    fclose(fp);

    printf("파일(%s) 다운로드 성공!\n---------------------\n", file_name);

    write(sock, "[ENDFILE]", 9);
    update_receive_file_info(sock);
}

void update_receive_file_info(int sock)
{
    memset(recv_file_info, 0, sizeof(recv_file_info));

    read(sock, recv_file_info, sizeof(recv_file_info)); // get whole file index

    if (strcmp(server_curr_dir, server_root_dir) != 0)
    {
        printf("-1) Back\n");
    }

    printf("0) Send file\n");

    for (int i = 0; i < FILE_SIZE; i++)
    {
        if (strlen(recv_file_info[i].file_name) > 0) // file_name이 비어 있지 않은 경우
        {
            printf("%d) %s : %d bytes\n", recv_file_info[i].seq,
                   recv_file_info[i].file_name, recv_file_info[i].bytes);
        }
    }
}

void error_handling(char *message)
{
    fputs(message, stderr);
    fputc('\n', stderr);
    exit(1);
}