#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <dirent.h>
#include <sys/stat.h>

#define BUF_SIZE 1024
#define FILE_SIZE 512

typedef struct
{
    char file_name[FILE_SIZE];
    char file_path[FILE_SIZE];
    int bytes;
} file_info_t;

void error_handling(char *message);
void get_file_info(const char *folder_name, file_info_t file_info[], int *index);

int main(int argc, char *argv[])
{
    int sock;
    char message[BUF_SIZE];
    int str_len, bytes;
    struct sockaddr_in serv_adr;
    file_info_t file_info[BUF_SIZE];
    char *curr_dir = getcwd(NULL, 0);
    int index = 0;

    FILE *fp;
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

    while ((str_len = read(sock, message, BUF_SIZE)) > 0)
    {
        message[str_len] = '\0';

        if (str_len == -1)
            error_handling("read() error!");
        if (!strcmp(message, "[END]"))
            break;

        printf("%s\n", message);
    }

    while (1)
    {
        index = 0;
        get_file_info(curr_dir, file_info, &index);
        fputs("\nSelect file or folder(Q to quit): ", stdout);
        fgets(message, BUF_SIZE, stdin);
        printf("\n---------------------\n");
        if (!strcmp(message, "q\n") || !strcmp(message, "Q\n"))
            break;

        write(sock, message, strlen(message));

        str_len = read(sock, message, BUF_SIZE - 1);

        message[str_len] = 0;
        if (strncmp(message, "[FILE]", 6) == 0) // 파일을 선택
        {
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

            write(sock, "[FILE]", 6);

            fp = fopen(file_name, "wb");
            if (!fp)
                error_handling("fopen() failed");

            while (bytes > 0)
            {
                str_len = read(sock, message, BUF_SIZE);

                bytes -= str_len;

                fwrite((void *)message, 1, str_len, fp);
            }
            fclose(fp);

            printf("파일(%s) 다운로드 성공!\n---------------\n", file_name);

            write(sock, "[ENDFILE]", 9);

            char new_message[BUF_SIZE];
            while (1)
            {
                str_len = read(sock, new_message, BUF_SIZE);
                if (strncmp(new_message, "[END]", 5) == 0)
                    break;
                printf("%s\n", new_message);
            }
        }
        else if (strncmp(message, "[DIR]", 5) == 0) // dir select
        {
            write(sock, "Thankyou", 8);

            while (1)
            {
                str_len = read(sock, message, BUF_SIZE);
                if (strncmp(message, "[END]", 5) == 0)
                    break;
                printf("%s\n", message);
            }
            // printf("%s\n", message);
        }
        else if (strncmp(message, "[BACK]", 6) == 0)
        {
            write(sock, "BACK", 4);

            while ((str_len = read(sock, message, BUF_SIZE)) > 0)
            {
                message[str_len] = '\0';

                if (str_len == -1)
                    error_handling("read() error!");
                if (!strcmp(message, "[END]"))
                    break;

                printf("%s\n", message);
            }
        }
        else if (strncmp(message, "[SEND]", 6) == 0)
        {
            int input = 0;

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
            memset(&message, 0, sizeof(message));   

            str_len = read(sock, message, BUF_SIZE - 1);
            
            printf("%s\n",message);

            printf("---------------------\n");
            char new_message[BUF_SIZE];
            while (1)
            {
                str_len = read(sock, new_message, BUF_SIZE);
                if (strncmp(new_message, "[END]", 5) == 0)
                    break;
                printf("%s\n", new_message);
            }
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

void error_handling(char *message)
{
    fputs(message, stderr);
    fputc('\n', stderr);
    exit(1);
}