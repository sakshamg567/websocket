#include <sys/socket.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/types.h>
#include <regex.h>
#include <string.h>

#define PORT 6969 // :)
#define BACKLOG 10
#define BUFFER_SIZE 10240 // 10mb buffer
#define MAX_HEADERS 100

const char *PLACEHOLDER_RESPONSE = "<HTML><HEAD><meta http-equiv=\"content-type\" content=\"text/html;charset=utf-8\">\r\n<TITLE>LIGMA</TITLE></HEAD><BODY>\r\n<H1>BALLZ</H1>\r\n</BODY></HTML>\r\n\r\n";

typedef struct
{
   char *key;
   char *value;
} header_t;

typedef struct
{
   char method[16];
   char target[1024];
   char version[16];
   header_t headers[MAX_HEADERS];
   int header_count;
} http_request_t;

void parse_request(const char *request, http_request_t *parsed)
{
   char *req_copy = strdup(request);
   if (!req_copy)
   {
      perror("strdup");
      return;
   }

   char *header_end = strstr(req_copy, "\r\n\r\n");
   *header_end = '\0';

   char *line = strtok(req_copy, "\r\n");
   sscanf(line, "%15s %1023s %15s", parsed->method, parsed->target, parsed->version);

   parsed->header_count = 0;

   while ((line = strtok(NULL, "\r\n")) != NULL) // Fixed parentheses
   {
      char *colon = strstr(line, ": ");
      if (colon)
      {
         *colon = '\0';
         char *key = line;
         char *value = colon + 2;
         parsed->headers[parsed->header_count].key = strdup(key);
         parsed->headers[parsed->header_count].value = strdup(value);
         parsed->header_count++;
      }
   }
   free(req_copy);
}

void *handle_client(void *arg)
{
   int client_fd = *((int *)arg); // type-cast void * to int * and dereference it
   char *buffer = (char *)malloc(BUFFER_SIZE * sizeof(char));

   ssize_t bytes_received = recv(client_fd, buffer, BUFFER_SIZE, 0);

   if (bytes_received > 0)
   {
      buffer[bytes_received] = '\0';
      http_request_t req;
      parse_request(buffer, &req);
      printf("Method: %s\n", req.method);
      printf("Target: %s\n", req.target);
      printf("Version: %s\n", req.version);

      const char *http_headers =
          "HTTP/1.1 200 OK\r\n"
          "Content-Type: text/html\r\n"
          "Content-Length: %zu\r\n"
          "\r\n";

      const char *body = PLACEHOLDER_RESPONSE;
      char response[BUFFER_SIZE];
      size_t body_len = strlen(body);
      int header_len = snprintf(response, BUFFER_SIZE, http_headers, body_len);
      strncat(response, body, BUFFER_SIZE - header_len - 1);

      send(client_fd, response, strlen(response), 0);
   }
   close(client_fd);
   free(arg);
   free(buffer);
   return NULL;
}

// void testParsing(void)
// {
//    const char *request =
//        "GET /index.html HTTP/1.1\r\n"
//        "Host: example.com\r\n"
//        "User-Agent: curl/7.64.1\r\n"
//        "Accept: */*\r\n"
//        "\r\n"
//        "body starts here";

//    http_request_t req;
//    parse_request(request, &req);

//    printf("Method: %s\n", req.method);
//    printf("Target: %s\n", req.target);
//    printf("Version: %s\n", req.version);

//    for (int i = 0; i < req.header_count; i++)
//    {
//       printf("Header: %s -> %s\n", req.headers[i].key, req.headers[i].value);
//    }
//    return;
// }

int main()
{
   int server_fd;
   struct sockaddr_in server_adress;

   if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == -1)
   {
      perror("socket");
      exit(EXIT_FAILURE);
   }

   server_adress.sin_addr.s_addr = INADDR_ANY;
   server_adress.sin_family = AF_INET;
   server_adress.sin_port = htons(PORT);

   int opt = 1;
   if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) == -1)
   {
      perror("setsocketopt");
      close(server_fd);
      exit(EXIT_FAILURE);
   }

   if (bind(server_fd, (struct sockaddr *)&server_adress, sizeof(server_adress)) == -1)
   {
      perror("binding");
      close(server_fd);
      exit(EXIT_FAILURE);
   }

   if (listen(server_fd, BACKLOG) == -1)
   {
      perror("listen");
      close(server_fd);
      exit(EXIT_FAILURE);
   }

   printf("Listening on port : %d\n", PORT);

   while (1)
   {
      struct sockaddr_in client_adress;
      size_t client_addr_len = sizeof(client_adress);
      int *client_fd = malloc(sizeof(int));

      if ((*client_fd = accept(server_fd, (struct sockaddr *)&client_adress, (socklen_t *)&client_addr_len)) == -1)
      {
         perror("accept");
         continue;
      }

      pthread_t thread_id;
      pthread_create(&thread_id, NULL, handle_client, (void *)client_fd);
      pthread_detach(thread_id);
   }
}
