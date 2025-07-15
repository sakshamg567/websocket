#include <sys/socket.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/types.h>
#include <regex.h>
#include <string.h>
#include <stdbool.h>
#include <openssl/sha.h>
#include <openssl/evp.h>
#include <openssl/bio.h>
#include <openssl/buffer.h>

#define PORT 6969 // :)
#define BACKLOG 10
#define BUFFER_SIZE 10240 // 10mb buffer
#define MAX_HEADERS 100
#define GUID "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"

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

bool header_exists(header_t headers[], int num_headers, const char *key, const char *target)
{
   for (int i = 0; i < num_headers; i++)
   {
      if (strcasecmp(headers[i].key, key) == 0)
      {
         if (target == NULL || strcasecmp(target, headers[i].value) == 0)
         {
            return true;
         }
      }
   }
   return false;
}

char *get_sec_websocket_key(header_t *headers, int num_headers)
{
   for (int i = 0; i < num_headers; i++)
   {
      if (strcasecmp(headers[i].key, "sec-websocket-key") == 0)
      {
         return headers[i].value;
      }
   }
   return NULL;
}

bool is_valid_ws_handshake(http_request_t request)
{
   // check method
   bool is_get = strcmp(request.method, "GET") == 0;

   char *version_dup;
   if ((version_dup = strdup(request.version)) == NULL)
   {
      perror("strdup version_dup");
      return false;
   }

   // check http version

   char *protocol = strtok(version_dup, "/");
   char *version_number = strtok(NULL, "\0");
   free(version_dup);

   double version_num = atof(version_number);

   bool version_compat = version_num >= 1.1;

   // check headers
   bool upgrade_header = header_exists(request.headers, request.header_count, "Upgrade", "websocket");
   bool connection_header = header_exists(request.headers, request.header_count, "connection", "Upgrade");
   bool sec_websocket_key_header = header_exists(request.headers, request.header_count, "sec-websocket-key", NULL);

   bool headers_valid = upgrade_header && connection_header && sec_websocket_key_header;

   return is_get && headers_valid;
}

char *b64_encode(const unsigned char *input)
{
   BIO *b64 = BIO_new(BIO_f_base64());
   BIO_set_flags(b64, BIO_FLAGS_BASE64_NO_NL);
   BIO *bmem = BIO_new(BIO_s_mem());
   b64 = BIO_push(b64, bmem);

   BIO_write(b64, input, SHA_DIGEST_LENGTH);
   BIO_flush(b64);

   BUF_MEM *bptr;
   BIO_get_mem_ptr(b64, &bptr);

   char *encoded = malloc(bptr->length + 1);
   if (encoded)
   {
      memcpy(encoded, bptr->data, bptr->length);
      encoded[bptr->length] = '\0';
   }

   BIO_free_all(b64);
   return encoded;
}

char *generate_sec_websocket_accept_key(char *sec_websocket_key)
{
   size_t key_len = strlen(sec_websocket_key);
   size_t GUID_len = strlen(GUID);
   size_t combined_len = key_len + GUID_len;

   char *combined = malloc(combined_len + 1);
   if (!combined)
   {
      perror("combined malloc");
      return NULL;
   }
   strcpy(combined, sec_websocket_key);
   strcat(combined, GUID);

   unsigned char hashed[SHA_DIGEST_LENGTH];
   size_t len = strlen(combined);
   SHA1(combined, len, hashed);

   char *encoded = b64_encode(hashed);

   return encoded;
}

void handle_ws_handshake(http_request_t req, int client_fd)
{
   const char *ws_headers =
       "HTTP/1.1 101 Switching Protocols\r\n"
       "Upgrade: websocket\r\n"
       "Connection: Upgrade\r\n"
       "Sec-WebSocket-Accept: %s\r\n"
       "\r\n";

   char *client_key = get_sec_websocket_key(req.headers, req.header_count);
   char *transformed_key = generate_sec_websocket_accept_key(client_key);

   char response[BUFFER_SIZE];
   int header_len = snprintf(response, BUFFER_SIZE, ws_headers, transformed_key);

   send(client_fd, response, strlen(response), 0);
}

void handle_ws_request(int client_fd)
{
   // here we will handle the ws frames
   while (1)
   {
      unsigned char meta[2]; // for reading frame metadata
      ssize_t n = recv(client_fd, meta, 2, 0);
      if (n <= 0)
         break;
      unsigned char fin = (meta[0] & 0x80) >> 7;
      unsigned char opcode = meta[0] & 0x0F;
      unsigned char masked = (meta[1] & 0x80) >> 7;
      uint64_t payload_len = meta[1] & 0x7F;

      if (payload_len < 126)
      {
      }
      else if (payload_len == 126)
      {
         char real_payload_len[2];
         recv(client_fd, real_payload_len, 2, 0);
         payload_len = (real_payload_len[0] << 8) | real_payload_len[1];
      }
      else if (payload_len == 127)
      {
         char real_payload_len[8];
         recv(client_fd, real_payload_len, 8, 0);
         uint64_t real_len = 0;
         for (int i = 0; i < 8; i++)
         {
            real_len = (real_len << 8) | real_payload_len[i];
         }
         payload_len = real_len;
      }

      char *pl_buffer = malloc(payload_len);

      if (masked)
      {
         unsigned char masking_key[4];
         ssize_t key_size = recv(client_fd, masking_key, 4, 0);

         recv(client_fd, pl_buffer, payload_len, 0);
         for (int i = 0; i < payload_len; i++)
         {
            pl_buffer[i] = pl_buffer[i] ^ masking_key[i % 4];
         }

         printf("Payload: %.*s\n", (int)payload_len, pl_buffer);
      }
      else
      {
         recv(client_fd, pl_buffer, payload_len, 0);
      }
   }
   return;
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

      for (int i = 0; i < req.header_count; i++)
      {
         printf("%s : %s\n", req.headers[i].key, req.headers[i].value);
      }

      const char *http_headers =
          "HTTP/1.1 200 OK\r\n"
          "Content-Type: text/html\r\n"
          "Content-Length: %zu\r\n"
          "\r\n";

      if (strcmp(req.target, "/websocket") == 0)
      {
         if (is_valid_ws_handshake(req))
         {
            handle_ws_handshake(req, client_fd);
            handle_ws_request(client_fd);
         }
      }
      else
      {
         const char *body = PLACEHOLDER_RESPONSE;
         char response[BUFFER_SIZE];
         size_t body_len = strlen(body);
         int header_len = snprintf(response, BUFFER_SIZE, http_headers, body_len);
         strncat(response, body, BUFFER_SIZE - header_len - 1);

         send(client_fd, response, strlen(response), 0);
      }
   }
   close(client_fd);
   free(arg);
   free(buffer);
   return NULL;
}

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

// TODO: complete the handle_ws_handshake function
// TODO: implement websocket frame functionality
