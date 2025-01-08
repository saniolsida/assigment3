#include <stdio.h>
#include <stdlib.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <pthread.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <dirent.h>
#include <sys/stat.h>

#define BUF_SIZE 1024
#define MAX_CLNT 256
#define FILE_SIZE 64
#define WORD_SIZE 32

typedef struct
{
    int seq;
    char file_name[FILE_SIZE];
    char file_path[FILE_SIZE];
    int bytes;
} file_info_t;

int clnt_cnt = 0;
int clnt_socks[MAX_CLNT];
char database_file_name[FILE_SIZE];
pthread_mutex_t mutx;

void error_handling(char *message);
void *handle_clnt(void *arg);
void get_file_info(const char *folder_name, file_info_t file_info[], int *index);
void send_file(int clnt_sock, file_info_t *file_info, int seq, int index, int struct_size);
void get_parent_path(char *path);
void receive_file_from_client(int clnt_sock, char current_dir[FILE_SIZE]);

int main(int argc, char *argv[])
{
    int serv_sock;
    int clnt_sock;
    pthread_t t_id;
    struct sockaddr_in serv_adr, clnt_adr;
    socklen_t clnt_adr_sz;

    pthread_mutex_init(&mutx, NULL);
    serv_sock = socket(PF_INET, SOCK_STREAM, 0);

    if (serv_sock == -1)
        error_handling("UDP socket creation error");

    memset(&serv_adr, 0, sizeof(serv_adr));
    serv_adr.sin_family = AF_INET;
    serv_adr.sin_addr.s_addr = htonl(INADDR_ANY);
    serv_adr.sin_port = htons(atoi(argv[1]));

    if (argc != 2)
    {
        printf("Usage : %s <Port>\n", argv[0]);
        exit(1);
    }

    if (bind(serv_sock, (struct sockaddr *)&serv_adr, sizeof(serv_adr)) == -1)
        error_handling("bind() error");

    if (listen(serv_sock, 5) == -1)
        error_handling("listen() error");

    while (1)
    {
        clnt_adr_sz = sizeof(clnt_adr);
        clnt_sock = accept(serv_sock, (struct sockaddr *)&clnt_adr, &clnt_adr_sz);
        pthread_mutex_lock(&mutx);
        clnt_socks[clnt_cnt++] = clnt_sock;
        pthread_mutex_unlock(&mutx);

        pthread_create(&t_id, NULL, handle_clnt, (void *)&clnt_sock);
        pthread_detach(t_id);
        printf("Connected client IP: %s \n", inet_ntoa(clnt_adr.sin_addr));
    }

    close(serv_sock);
    return 0;
}

void *handle_clnt(void *arg)
{
    int clnt_sock = *((int *)arg);
    int str_len = 0, i = 0;
    char buf[WORD_SIZE];
    file_info_t file_info[FILE_SIZE];
    char *root_dir = getcwd(NULL, 0);
    char current_dir[FILE_SIZE];

    int index = 0;
    int send_index = 0;
    char char_index[10];

    strcpy(current_dir, root_dir);
    get_file_info(root_dir, file_info, &index);

    write(clnt_sock, file_info, sizeof(file_info_t) * index);

    while ((str_len = read(clnt_sock, buf, sizeof(buf))) != 0)
    {
        int command = atoi(buf);

        if (command == 0)
        {
            write(clnt_sock, "[SEND]", 6);

            receive_file_from_client(clnt_sock, current_dir);

            get_file_info(current_dir, file_info, &index);

            write(clnt_sock, file_info, sizeof(file_info_t) * index);
        }
        else if (command <= index && command > 0)
        {
            int seq = command - 1;
            if (file_info[seq].bytes == -1) // dir
            {
                char path_send[BUF_SIZE];
                char full_name[BUF_SIZE];

                snprintf(full_name, sizeof(full_name), "%s/%s", file_info[seq].file_path, file_info[seq].file_name);
                strcpy(current_dir, full_name);

                str_len = snprintf(path_send, sizeof(path_send), "[DIR]%s", full_name);

                write(clnt_sock, path_send, strlen(path_send) + 1);

                memset(file_info, 0, sizeof(file_info));

                get_file_info(full_name, file_info, &index);

                for (int i = 0; i < index; i++)
                    printf("%d) %s %d\n", file_info[i].seq, file_info[i].file_name, file_info[i].bytes);

                write(clnt_sock, file_info, sizeof(file_info_t) * index);
            }
            else // file
            {
                send_file(clnt_sock, file_info, seq, index, sizeof(file_info_t) * index);
            }
        }
        else if (command == -1) // back
        {
            char back_buf[FILE_SIZE + 6];
            get_parent_path(current_dir);
            snprintf(back_buf, sizeof(back_buf), "[BACK]%s", current_dir);

            write(clnt_sock, back_buf, strlen(back_buf));

            get_file_info(current_dir, file_info, &index);
            write(clnt_sock, file_info, sizeof(file_info_t) * index);
        }
        else
        {
            write(clnt_sock, "Wrong access", 12);
        }
    }

    puts("입력 종료");

    pthread_mutex_lock(&mutx);
    for (i = 0; i < clnt_cnt; i++) // remove disconnected client
    {
        if (clnt_sock == clnt_socks[i])
        {
            while (i++ < clnt_cnt - 1)
                clnt_socks[i] = clnt_socks[i + 1];
            break;
        }
    }

    clnt_cnt--;
    pthread_mutex_unlock(&mutx);
    close(clnt_sock);
    return NULL;
}

void send_file(int clnt_sock, file_info_t *file_info, int seq, int index, int struct_size)
{
    int str_len = 0;
    char buf[BUF_SIZE];
    char full_name[BUF_SIZE];

    snprintf(full_name, sizeof(full_name), "%s/%s", file_info[seq].file_path, file_info[seq].file_name);
    FILE *fp = fopen(full_name, "rb");
    if (!fp)
        error_handling("fopen failed");

    char file_name[BUF_SIZE];

    snprintf(file_name, sizeof(file_name), "[FILE]%s %d[END]", file_info[seq].file_name, file_info[seq].bytes);
    write(clnt_sock, file_name, strlen(file_name)); // 이름과 bytes

    str_len = read(clnt_sock, buf, BUF_SIZE);
    buf[str_len] = '\0';
    printf("%s\n", buf);
    memset(&buf, 0, sizeof(buf));

    while (1)
    {
        str_len = fread((void *)buf, 1, BUF_SIZE, fp);
        if (str_len < BUF_SIZE)
        {
            write(clnt_sock, buf, str_len);
            break;
        }
        write(clnt_sock, buf, BUF_SIZE);
    }
    fclose(fp);

    memset(&buf, 0, sizeof(buf));
    str_len = read(clnt_sock, buf, BUF_SIZE);

    printf("%s\n", buf);

    write(clnt_sock, file_info, struct_size);
}

void get_file_info(const char *folder_name, file_info_t file_info[], int *index)
{
    (*index) = 0;

    DIR *dir = opendir(folder_name);
    if (!dir)
        error_handling("opendir() error");

    struct dirent *entry;
    struct stat st;
    int seq = 1;

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
            char *last_slash = strrchr(path, '/');
            if (last_slash)
            {
                size_t dir_length = last_slash - path;
                strncpy(file_info[*index].file_path, path, dir_length);
                file_info[*index].file_path[dir_length] = '\0';

                strcpy(file_info[*index].file_name, last_slash + 1);
            }

            // stat
            stat(path, &st);

            file_info[*index].bytes = st.st_size;
            file_info[*index].seq = seq;
            (*index)++;
            seq++;
        }
        if (S_ISDIR(st.st_mode))
        {
            char *last_slash = strrchr(path, '/');
            if (last_slash)
            {
                size_t dir_length = last_slash - path;
                strncpy(file_info[*index].file_path, path, dir_length);
                file_info[*index].file_path[dir_length] = '\0';

                strcpy(file_info[*index].file_name, last_slash + 1);
            }

            file_info[*index].bytes = -1;
            file_info[*index].seq = seq;
            (*index)++;
            seq++;
        }
    }

    closedir(dir);
}

void get_parent_path(char *path)
{
    char *ptr = strrchr(path, '/');

    if (ptr)
        *ptr = '\0';

    printf("path %s", path);
}

void receive_file_from_client(int clnt_sock, char current_dir[FILE_SIZE])
{
    char buf[BUF_SIZE];
    char file_name[FILE_SIZE];
    int bytes = 0;
    int str_len = read(clnt_sock, buf, BUF_SIZE);
    if (strncmp(buf, "[FILE]", 6) == 0)
    {
        char *name = buf + 6;
        char *end = strstr(name, "[END]");
        if (end)
            *end = '\0';

        char *ptr = strtok(name, " ");

        snprintf(file_name, sizeof(file_name), "%s", ptr);

        ptr = strtok(NULL, " ");

        bytes = atoi(ptr);

        char full_path[BUF_SIZE];

        snprintf(full_path, sizeof(full_path), "%s/%s", current_dir, file_name);

        FILE *fp = fopen(full_path, "wb");

        if (!fp)
            error_handling("fopen failed");

        while (bytes > 0)
        {
            str_len = read(clnt_sock, buf, BUF_SIZE);
            fwrite((void *)buf, 1, str_len, fp);

            bytes -= str_len;
        }
        fclose(fp);
    }
    else
    {
        return;
    }
}

void error_handling(char *message)
{
    fputs(message, stderr);
    fputc('\n', stderr);
    exit(1);
}